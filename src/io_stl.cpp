/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "io_stl.h"
#include "document.h"
#include "document_item.h"
#include "mesh_item.h"
#include "occ_progress.h"
#include "xde_document_item.h"

#include <BRep_Builder.hxx>
#include <Poly_Triangulation.hxx>
#include <RWStl.hxx>
#include <StlAPI_Writer.hxx>
#include <TopoDS_Compound.hxx>

#include <QtCore/QFileInfo>

namespace Mayo {

static MeshItem* createMeshItem(
        const QString& filepath, const Handle_Poly_Triangulation& mesh)
{
    auto partItem = new MeshItem;
    partItem->propertyLabel.setValue(QFileInfo(filepath).baseName());
    partItem->setTriangulation(mesh);
    return partItem;
}

static TopoDS_Shape xdeDocumentWholeShape(const XdeDocumentItem* xdeDocItem)
{
    TopoDS_Shape shape;
    const std::vector<TDF_Label> vecFreeShape = xdeDocItem->topLevelFreeShapes();
    if (vecFreeShape.size() > 1) {
        TopoDS_Compound cmpd;
        BRep_Builder builder;
        builder.MakeCompound(cmpd);
        for (const TDF_Label& label : vecFreeShape)
            builder.Add(cmpd, xdeDocItem->shape(label));
        shape = cmpd;
    }
    else if (vecFreeShape.size() == 1) {
        shape = xdeDocItem->shape(vecFreeShape.front());
    }
    return shape;
}

IoStl_OpenCascade::IoStl_OpenCascade()
    : m_outputStlFormat(nullptr, tr("Format"), &enum_StlFormat())
{
    m_outputStlFormat.setValue(static_cast<int>(StlFormat::Binary)); // Default
    this->addOptionWrite(&m_outputStlFormat);
}

IoBase::Result IoStl_OpenCascade::readFile(
        Document* doc, const QString& filepath, qttask::Progress* progress)
{
    Handle_Message_ProgressIndicator indicator = new OccProgress(progress);
    const Handle_Poly_Triangulation mesh =
            RWStl::ReadFile(filepath.toLocal8Bit().constData(), indicator);
    if (!mesh.IsNull()) {
        auto item = new MeshItem;
        item->propertyLabel.setValue(QFileInfo(filepath).baseName());
        item->setTriangulation(mesh);
        doc->addRootItem(item);
        return Result::ok();
    }
    // TODO: handle case where the operation is aborted
    return Result::error(tr("Imported STL mesh is null"));
}

IoBase::Result IoStl_OpenCascade::writeFile(
        Span<const ApplicationItem> spanAppItem,
        const QString& filepath,
        qttask::Progress* progress)
{
    Q_ASSERT(!spanAppItem.empty());

    const bool isAsciiFormat = m_outputStlFormat.valueAs<StlFormat>() == StlFormat::Ascii;
    const ApplicationItem& appItem = spanAppItem.at(0);
    const DocumentItem* docItem = appItem.documentItem();

    TopoDS_Shape shape;
    const MeshItem* meshItem = nullptr;
    if (appItem.isDocumentItem() && sameType<XdeDocumentItem>(docItem)) {
        shape = xdeDocumentWholeShape(static_cast<const XdeDocumentItem*>(docItem));
    }
    else if (appItem.isXdeAssemblyNode()) {
        const XdeAssemblyNode& asmNode = appItem.xdeAssemblyNode();
        shape = asmNode.ownerDocItem->shape(asmNode.label());
    }
    else if (appItem.isDocumentItem() && sameType<MeshItem>(docItem)) {
        meshItem = static_cast<const MeshItem*>(docItem);
    }

    if (!shape.IsNull()) {
        StlAPI_Writer writer;
        writer.ASCIIMode() = isAsciiFormat;
        const bool ok = writer.Write(shape, filepath.toLocal8Bit().constData());
        if (!ok)
            return Result::error(tr("Unknown StlAPI_Writer failure"));
    }
    else if (meshItem != nullptr) {
        Handle_Message_ProgressIndicator indicator = new OccProgress(progress);
        bool occOk = false;
        const QByteArray filepathLocal8b = filepath.toLocal8Bit();
        const OSD_Path osdFilepath(filepathLocal8b.constData());
        const Handle_Poly_Triangulation& mesh = meshItem->triangulation();
        if (isAsciiFormat)
            occOk = RWStl::WriteAscii(mesh, osdFilepath, indicator);
        else
            occOk = RWStl::WriteBinary(mesh, osdFilepath, indicator);
        if (!occOk)
            return Result::error(tr("Unknown error"));
    }
    else {
        return Result::error(tr("No input item"));
    }
    return Result::ok();
}

const Enumeration& IoStl_OpenCascade::enum_StlFormat()
{
    static Enumeration enumeration;
    if (enumeration.size() == 0) {
        enumeration.map(static_cast<int>(StlFormat::Ascii), tr("Ascii"));
        enumeration.map(static_cast<int>(StlFormat::Binary), tr("Binary"));
    }
    return enumeration;
}

#ifdef HAVE_GMIO
#  include <gmio_core/error.h>
#  include <gmio_stl/stl_error.h>
#  include <gmio_stl/stl_format.h>
#  include <gmio_stl/stl_infos.h>
#  include <gmio_stl/stl_io.h>
#  include <gmio_support/stream_qt.h>
#  include <gmio_support/stl_occ_brep.h>
#  include <gmio_support/stl_occ_polytri.h>
#endif

#ifdef HAVE_GMIO
static bool gmio_qttask_is_stop_requested(void* cookie)
{
    auto progress = static_cast<const qttask::Progress*>(cookie);
    return progress != nullptr ? progress->isAbortRequested() : false;
}

static void gmio_qttask_handle_progress(
        void* cookie, intmax_t value, intmax_t maxValue)
{
    auto progress = static_cast<qttask::Progress*>(cookie);
    if (progress != nullptr && maxValue > 0) {
        const auto pctNorm = value / static_cast<double>(maxValue);
        const auto pct = qRound(pctNorm * 100);
        if (pct >= (progress->value() + 5))
            progress->setValue(pct);
    }
}

static gmio_task_iface gmio_qttask_create_task_iface(qttask::Progress* progress)
{
    gmio_task_iface task = {};
    task.cookie = progress;
    task.func_is_stop_requested = gmio_qttask_is_stop_requested;
    task.func_handle_progress = gmio_qttask_handle_progress;
    return task;
}

static QString gmioErrorToQString(int error)
{
    switch (error) {
    // Core
    case GMIO_ERROR_OK:
        return QString();
    case GMIO_ERROR_UNKNOWN:
        return Application::tr("GMIO_ERROR_UNKNOWN");
    case GMIO_ERROR_NULL_MEMBLOCK:
        return Application::tr("GMIO_ERROR_NULL_MEMBLOCK");
    case GMIO_ERROR_INVALID_MEMBLOCK_SIZE:
        return Application::tr("GMIO_ERROR_INVALID_MEMBLOCK_SIZE");
    case GMIO_ERROR_STREAM:
        return Application::tr("GMIO_ERROR_STREAM");
    case GMIO_ERROR_TASK_STOPPED:
        return Application::tr("GMIO_ERROR_TASK_STOPPED");
    case GMIO_ERROR_STDIO:
        return Application::tr("GMIO_ERROR_STDIO");
    case GMIO_ERROR_BAD_LC_NUMERIC:
        return Application::tr("GMIO_ERROR_BAD_LC_NUMERIC");
    // TODO: complete other core enum values
    // STL
    case GMIO_STL_ERROR_UNKNOWN_FORMAT:
        return Application::tr("GMIO_STL_ERROR_UNKNOWN_FORMAT");
    case GMIO_STL_ERROR_NULL_FUNC_GET_TRIANGLE:
        return Application::tr("GMIO_STL_ERROR_NULL_FUNC_GET_TRIANGLE");
    case GMIO_STL_ERROR_PARSING:
        return Application::tr("GMIO_STL_ERROR_PARSING");
    case GMIO_STL_ERROR_INVALID_FLOAT32_PREC:
        return Application::tr("GMIO_STL_ERROR_INVALID_FLOAT32_PREC");
    case GMIO_STL_ERROR_UNSUPPORTED_BYTE_ORDER:
        return Application::tr("GMIO_STL_ERROR_UNSUPPORTED_BYTE_ORDER");
    case GMIO_STL_ERROR_HEADER_WRONG_SIZE:
        return Application::tr("GMIO_STL_ERROR_HEADER_WRONG_SIZE");
    case GMIO_STL_ERROR_FACET_COUNT:
        return Application::tr("GMIO_STL_ERROR_FACET_COUNT");
    }
    return Application::tr("GMIO_ERROR_UNKNOWN");
}
#endif

#ifdef HAVE_GMIO
Application::IoResult Application::importStl(
        Document* doc, const QString &filepath, qttask::Progress* progress)
{
        QFile file(filepath);
        if (file.open(QIODevice::ReadOnly)) {
            gmio_stream stream = gmio_stream_qiodevice(&file);
            gmio_stl_read_options options = {};
            options.func_stla_get_streamsize = &gmio_stla_infos_probe_streamsize;
            options.task_iface = Internal::gmio_qttask_create_task_iface(progress);
            int err = GMIO_ERROR_OK;
            while (gmio_no_error(err) && !file.atEnd()) {
                gmio_stl_mesh_creator_occpolytri meshcreator;
                err = gmio_stl_read(&stream, &meshcreator, &options);
                if (gmio_no_error(err)) {
                    const Handle_Poly_Triangulation& mesh = meshcreator.polytri();
                    doc->addRootItem(Internal::createMeshItem(filepath, mesh));
                }
            }
            if (err != GMIO_ERROR_OK)
                return IoResult::error(Internal::gmioErrorToQString(err));
        }
}
#endif // HAVE_GMIO

#ifdef HAVE_GMIO
Application::IoResult Application::exportStl_gmio(
        const std::vector<DocumentItem *> &docItems,
        const Application::ExportOptions &options,
        const QString &filepath,
        qttask::Progress *progress)
{
    QFile file(filepath);
    if (file.open(QIODevice::WriteOnly)) {
        gmio_stream stream = gmio_stream_qiodevice(&file);
        gmio_stl_write_options gmioOptions = {};
        gmioOptions.stla_float32_format = options.stlaFloat32Format;
        gmioOptions.stla_float32_prec = options.stlaFloat32Precision;
        gmioOptions.stla_solid_name = options.stlaSolidName.c_str();
        gmioOptions.task_iface =
                Internal::gmio_qttask_create_task_iface(progress);
        for (const DocumentItem* item : docItems) {
            if (progress != nullptr) {
                progress->setStep(
                            tr("Writting item %1")
                            .arg(item->propertyLabel.value()));
            }
            int error = GMIO_ERROR_OK;
            if (sameType<XdeDocumentItem>(item)) {
                auto xdeDocItem = static_cast<const XdeDocumentItem*>(item);
                const TopoDS_Shape shape = Internal::xdeDocumentWholeShape(xdeDocItem);
                const gmio_stl_mesh_occshape gmioMesh(shape);
                error = gmio_stl_write(
                            options.stlFormat, &stream, &gmioMesh, &gmioOptions);
            }
            else if (sameType<MeshItem>(item)) {
                auto meshItem = static_cast<const MeshItem*>(item);
                const gmio_stl_mesh_occpolytri gmioMesh(meshItem->triangulation());
                error = gmio_stl_write(
                            options.stlFormat, &stream, &gmioMesh, &gmioOptions);
            }
            if (error != GMIO_ERROR_OK)
                return IoResult::error(Internal::gmioErrorToQString(error));
        }
        return IoResult::ok();
    }
    return IoResult::error(file.errorString());
}
#endif // HAVE_GMIO

} // namespace Mayo
