/****************************************************************************
** Copyright (c) 2016, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
**     1. Redistributions of source code must retain the above copyright
**        notice, this list of conditions and the following disclaimer.
**
**     2. Redistributions in binary form must reproduce the above
**        copyright notice, this list of conditions and the following
**        disclaimer in the documentation and/or other materials provided
**        with the distribution.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
****************************************************************************/

#include "application.h"

#include "document.h"
#include "document_item.h"
#include "caf_utils.h"
#include "xde_document_item.h"
#include "mesh_item.h"
#include "options.h"
#include "mesh_utils.h"
#include "fougtools/qttools/task/progress.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <IGESControl_Controller.hxx>
#include <Interface_Static.hxx>
#include <Message_ProgressIndicator.hxx>
#include <OSD_Path.hxx>
#include <RWStl.hxx>
#include <StlAPI_Writer.hxx>
#include <Transfer_FinderProcess.hxx>
#include <Transfer_TransientProcess.hxx>
#include <XSControl_TransferWriter.hxx>
#include <XSControl_WorkSession.hxx>

#include <IGESCAFControl_Reader.hxx>
#include <IGESCAFControl_Writer.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

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

#include <algorithm>
#include <array>
#include <fstream>

namespace Mayo {

namespace Internal {

static QMutex* globalMutex()
{
    static QMutex mutex;
    return &mutex;
}

#ifdef HAVE_GMIO
class GmioQtTaskProgress : public gmio::Task {
public:
    GmioQtTaskProgress(qttask::Progress* progress)
        : m_progress(progress)
    {}

    bool isStopRequested() const override {
        return m_progress->isAbortRequested();
    }

    void handleProgress(uint64_t value, uint64_t maxValue) override {
        if (maxValue > 0) {
            const auto pctNorm = value / static_cast<double>(maxValue);
            const auto pct = qRound(pctNorm * 100);
            if (pct >= (m_progress->value() + 5))
                m_progress->setValue(pct);
        }
    }

private:
    qttask::Progress* m_progress;
};

static QString gmioErrorToQString(int error)
{
    switch (error) {
    // Core
    case gmio::Error_OK:
        return QString();
    case gmio::Error_Unknown:
        return Application::tr("gmio::Error_Unknown");
    case gmio::Error_Stream:
        return Application::tr("gmio::Error_Stream");
    case gmio::Error_TaskStopped:
        return Application::tr("gmio::Error_TaskStopped");
    case gmio::Error_Stdio:
        return Application::tr("gmio::Error_Stdio");
    case gmio::Error_BadLcNumeric:
        return Application::tr("gmio::Error_BadLcNumeric");
    // TODO: complete other core enum values
    // STL
    case gmio::STL_Error_UnknownFormat:
        return Application::tr("gmio::STL_Error_UnknownFormat");
    case gmio::STL_Error_Parsing:
        return Application::tr("gmio::STL_Error_Parsing");
    case gmio::STL_Error_InvalidFloat32Prec:
        return Application::tr("gmio::STL_Error_InvalidFloat32Prec");
    case gmio::STL_Error_UnsupportedByteOrder:
        return Application::tr("gmio::STL_Error_UnsupportedByteOrder");
    case gmio::STL_Error_HeaderWrongSize:
        return Application::tr("gmio::STL_Error_HeaderWrongSize");
    case gmio::STL_Error_FacetCount:
        return Application::tr("gmio::STL_Error_FacetCount");
    }
    return Application::tr("gmio::Error_Unknown");
}
#endif

class OccImportProgress : public Message_ProgressIndicator
{
public:
    OccImportProgress(qttask::Progress* progress)
        : m_progress(progress)
    {
        this->SetScale(0., 100., 1.);
    }

    Standard_Boolean Show(const Standard_Boolean /*force*/) override
    {
        const Handle_TCollection_HAsciiString name = this->GetScope(1).GetName();
        if (!name.IsNull() && m_progress != nullptr)
            m_progress->setStep(QString(name->ToCString()));
        const Standard_Real pc = this->GetPosition(); // Always within [0,1]
        const int minVal = 0;
        const int maxVal = 100;
        const int val = minVal + pc * (maxVal - minVal);
        if (m_progress != nullptr)
            m_progress->setValue(val);
        return Standard_True;
    }

    Standard_Boolean UserBreak() override
    {
        return m_progress != nullptr ? m_progress->isAbortRequested() : false;
    }

private:
    qttask::Progress* m_progress = nullptr;
};

template<typename READER> // Either IGESControl_Reader or STEPControl_Reader
TopoDS_Shape loadShapeFromFile(
        const QString& filepath,
        IFSelect_ReturnStatus* error,
        qttask::Progress* progress)
{
    QMutexLocker locker(globalMutex()); Q_UNUSED(locker);
    Handle_Message_ProgressIndicator indicator = new OccImportProgress(progress);
    TopoDS_Shape result;

    if (!indicator.IsNull())
        indicator->NewScope(30, "Loading file");
    READER reader;
    *error = reader.ReadFile(filepath.toLocal8Bit().constData());
    if (!indicator.IsNull())
        indicator->EndScope();
    if (*error == IFSelect_RetDone) {
        if (!indicator.IsNull()) {
            reader.WS()->MapReader()->SetProgress(indicator);
            indicator->NewScope(70, "Translating file");
        }
        reader.NbRootsForTransfer();
        reader.TransferRoots();
        result = reader.OneShape();
        if (!indicator.IsNull()) {
            indicator->EndScope();
            reader.WS()->MapReader()->SetProgress(nullptr);
        }
    }
    return result;
}

template<typename CAF_READER> struct CafReaderTraits {};

template<> struct CafReaderTraits<IGESCAFControl_Reader> {
    typedef IGESCAFControl_Reader ReaderType;
    static Handle_XSControl_WorkSession workSession(const ReaderType& reader) {
        return reader.WS();
    }
};

template<> struct CafReaderTraits<STEPCAFControl_Reader> {
    typedef STEPCAFControl_Reader ReaderType;
    static Handle_XSControl_WorkSession workSession(const ReaderType& reader) {
        return reader.Reader().WS();
    }
};

template<typename CAF_READER> // Either IGESCAFControl_Reader or STEPCAFControl_Reader
void loadCafDocumentFromFile(
        const QString& filepath,
        Handle_TDocStd_Document& doc,
        IFSelect_ReturnStatus* error,
        qttask::Progress* progress)
{
    QMutexLocker locker(globalMutex()); Q_UNUSED(locker);
    Handle_Message_ProgressIndicator indicator = new OccImportProgress(progress);

    if (!indicator.IsNull())
        indicator->NewScope(30, "Loading file");
    CAF_READER reader;
    reader.SetColorMode(Standard_True);
    reader.SetNameMode(Standard_True);
    reader.SetLayerMode(Standard_True);
    *error = reader.ReadFile(filepath.toLocal8Bit().constData());
    if (!indicator.IsNull())
        indicator->EndScope();
    if (*error == IFSelect_RetDone) {
        Handle_XSControl_WorkSession ws =
                CafReaderTraits<CAF_READER>::workSession(reader);
        if (!indicator.IsNull()) {
            ws->MapReader()->SetProgress(indicator);
            indicator->NewScope(70, "Translating file");
        }
        if (reader.Transfer(doc) == Standard_False)
            *error = IFSelect_RetFail;
        if (!indicator.IsNull()) {
            indicator->EndScope();
            ws->MapReader()->SetProgress(nullptr);
        }
    }
}

static TopoDS_Shape xdeDocumentWholeShape(const XdeDocumentItem* xdeDocItem)
{
    TopoDS_Shape shape;
    const TDF_LabelSequence seqTopLevelFreeShapeLabel =
            xdeDocItem->topLevelFreeShapeLabels();
    if (seqTopLevelFreeShapeLabel.Size() > 1) {
        TopoDS_Compound cmpd;
        BRep_Builder builder;
        builder.MakeCompound(cmpd);
        for (const TDF_Label& label : seqTopLevelFreeShapeLabel)
            builder.Add(cmpd, xdeDocItem->shape(label));
        shape = cmpd;
    }
    else if (seqTopLevelFreeShapeLabel.Size() == 1) {
        shape = xdeDocItem->shape(seqTopLevelFreeShapeLabel.First());
    }
    return shape;
}

static MeshItem* createMeshItem(
        const QString& filepath, const Handle_Poly_Triangulation& mesh)
{
    auto partItem = new MeshItem;
    partItem->propertyLabel.setValue(QFileInfo(filepath).baseName());
    partItem->propertyNodeCount.setValue(mesh->NbNodes());
    partItem->propertyTriangleCount.setValue(mesh->NbTriangles());
    partItem->propertyVolume.setValue(occ::MeshUtils::triangulationVolume(mesh));
    partItem->propertyArea.setValue(occ::MeshUtils::triangulationArea(mesh));
    partItem->setTriangulation(mesh);
    return partItem;
}

static XdeDocumentItem* createXdeDocumentItem(
        const QString& filepath, const Handle_TDocStd_Document& cafDoc)
{
    auto xdeDocItem = new XdeDocumentItem(cafDoc);
    xdeDocItem->propertyLabel.setValue(QFileInfo(filepath).baseName());

    const Handle_XCAFDoc_ShapeTool& shapeTool = xdeDocItem->shapeTool();
    const TDF_LabelSequence seqTopLevelFreeShapeLabel =
            xdeDocItem->topLevelFreeShapeLabels();
    if (seqTopLevelFreeShapeLabel.Size() > 1) {
        const TDF_Label asmLabel = shapeTool->NewShape();
        for (const TDF_Label& shapeLabel : seqTopLevelFreeShapeLabel)
            shapeTool->AddComponent(asmLabel, shapeLabel, TopLoc_Location());
        shapeTool->UpdateAssemblies();
    }

    const TopoDS_Shape shape = xdeDocumentWholeShape(xdeDocItem);
    GProp_GProps system;
    BRepGProp::VolumeProperties(shape, system);
    xdeDocItem->propertyVolume.setValue(std::max(system.Mass(), 0.));
    BRepGProp::SurfaceProperties(shape, system);
    xdeDocItem->propertyArea.setValue(std::max(system.Mass(), 0.));

    return xdeDocItem;
}

static QString occReturnStatusToQString(IFSelect_ReturnStatus status)
{
    switch (status) {
    case IFSelect_RetVoid: return Application::tr("IFSelect_RetVoid");
    case IFSelect_RetDone: return QString();
    case IFSelect_RetError: return Application::tr("IFSelect_RetError");
    case IFSelect_RetFail: return Application::tr("IFSelect_RetFail");
    case IFSelect_RetStop: return Application::tr("IFSelect_RetStop");
    }
    return Application::tr("IFSelect Unknown");
}

static const char* skipWhiteSpaces(const char* str, size_t len)
{
    size_t pos = 0;
    while (std::isspace(str[pos]) && pos < len)
        ++pos;
    return str + pos;
}

template<size_t N>
bool matchToken(const char* buffer, const char (&token)[N])
{
    return std::strncmp(buffer, token, N - 1) == 0;
}

Application::PartFormat findPartFormatFromContents(
        const char *contentsBegin,
        size_t contentsBeginSize,
        uint64_t fullContentsSizeHint)
{
    // -- Binary STL ?
    static const size_t binaryStlHeaderSize = 80 + sizeof(uint32_t);
    if (contentsBeginSize >= binaryStlHeaderSize) {
        const uint32_t offset = 80; // Skip header
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(contentsBegin);
        const uint32_t facetsCount =
                bytes[offset]
                | (bytes[offset+1] << 8)
                | (bytes[offset+2] << 16)
                | (bytes[offset+3] << 24);
        const unsigned facetSize = (sizeof(float) * 12) + sizeof(uint16_t);
        if ((facetSize * facetsCount + binaryStlHeaderSize)
                == fullContentsSizeHint)
        {
            return Application::PartFormat::Stl;
        }
    }

    // -- IGES ?
    {
        // regex : ^.{72}S\s*[0-9]+\s*[\n\r\f]
        bool isIges = true;
        if (contentsBeginSize >= 80 && contentsBegin[72] == 'S') {
            for (int i = 73; i < 80 && isIges; ++i) {
                if (contentsBegin[i] != ' ' && !std::isdigit(contentsBegin[i]))
                    isIges = false;
            }
            if (isIges && (contentsBegin[80] == '\n'
                           || contentsBegin[80] == '\r'
                           || contentsBegin[80] == '\f'))
            {
                const int sVal = std::atoi(contentsBegin + 73);
                if (sVal == 1)
                    return Application::PartFormat::Iges;
            }
        }
    } // IGES

    contentsBegin = skipWhiteSpaces(contentsBegin, contentsBeginSize);

    // -- STEP ?
    {
        // regex : ^\s*ISO-10303-21\s*;\s*HEADER
        static const char stepIsoId[] = "ISO-10303-21";
        static const size_t stepIsoIdLen = sizeof(stepIsoId) - 1;
        static const char stepHeaderToken[] = "HEADER";
        static const size_t stepHeaderTokenLen = sizeof(stepHeaderToken) - 1;
        if (std::strncmp(contentsBegin, stepIsoId, stepIsoIdLen) == 0) {
            auto charIt = skipWhiteSpaces(
                        contentsBegin + stepIsoIdLen,
                        contentsBeginSize - stepIsoIdLen);
            if (*charIt == ';'
                    && (charIt - contentsBegin) < contentsBeginSize)
            {
                charIt = skipWhiteSpaces(
                            charIt + 1,
                            contentsBeginSize - (charIt - contentsBegin));
                if (std::strncmp(charIt, stepHeaderToken, stepHeaderTokenLen)
                        == 0)
                {
                    return Application::PartFormat::Step;
                }
            }
        }
    } // STEP

    // -- OpenCascade BREP ?
    {
        // regex : ^\s*DBRep_DrawableShape
        static const char occBRepToken[] = "DBRep_DrawableShape";
        if (matchToken(contentsBegin, occBRepToken))
            return Application::PartFormat::OccBrep;
    }

    // -- ASCII STL ?
    {
        // regex : ^\s*solid
        const char asciiStlToken[] = "solid";
        if (matchToken(contentsBegin, asciiStlToken))
            return Application::PartFormat::Stl;
    }

    // Fallback case
    return Application::PartFormat::Unknown;
}

} // namespace Internal

Application::Application(QObject *parent)
    : QObject(parent)
{
}

Application *Application::instance()
{
    static Application app;
    return &app;
}

const std::vector<Document *> &Application::documents() const
{
    return m_documents;
}

Document *Application::addDocument(const QString &label)
{
    static unsigned docSequenceId = 0;
    auto doc = new Document(this);
    if (label.isEmpty()) {
        doc->setLabel(tr("Anonymous%1")
                      .arg(docSequenceId > 0 ?
                               QString::number(docSequenceId) :
                               QString()));
    }
    else {
        doc->setLabel(label);
    }
    QObject::connect(
                doc, &Document::itemAdded,
                this, &Application::documentItemAdded);
    QObject::connect(
                doc, &Document::itemPropertyChanged,
                this, &Application::documentItemPropertyChanged);
    m_documents.emplace_back(doc);
    ++docSequenceId;
    emit documentAdded(doc);
    return doc;
}

bool Application::eraseDocument(Document *doc)
{
    auto itFound = std::find(m_documents.cbegin(), m_documents.cend(), doc);
    if (itFound != m_documents.cend()) {
        m_documents.erase(itFound);
        delete doc;
        emit documentErased(doc);
        return true;
    }
    return false;
}

Application::IoResult Application::importInDocument(
        Document* doc,
        PartFormat format,
        const QString &filepath,
        qttask::Progress* progress)
{
    progress->setStep(QFileInfo(filepath).fileName());
    switch (format) {
    case PartFormat::Iges: return this->importIges(doc, filepath, progress);
    case PartFormat::Step: return this->importStep(doc, filepath, progress);
    case PartFormat::OccBrep: return this->importOccBRep(doc, filepath, progress);
    case PartFormat::Stl: return this->importStl(doc, filepath, progress);
    case PartFormat::Unknown: break;
    }
    return { false, tr("Unknown error") };
}

Application::IoResult Application::exportDocumentItems(
        const std::vector<DocumentItem*>& docItems,
        PartFormat format,
        const ExportOptions &options,
        const QString &filepath,
        qttask::Progress *progress)
{
    progress->setStep(QFileInfo(filepath).fileName());
    switch (format) {
    case PartFormat::Iges:
        return this->exportIges(docItems, options, filepath, progress);
    case PartFormat::Step:
        return this->exportStep(docItems, options, filepath, progress);
    case PartFormat::OccBrep:
        return this->exportOccBRep(docItems, options, filepath, progress);
    case PartFormat::Stl:
        return this->exportStl(docItems, options, filepath, progress);
    case PartFormat::Unknown:
        break;
    }
    return { false, tr("Unknown error") };
}

bool Application::hasExportOptionsForFormat(Application::PartFormat format)
{
    return format == PartFormat::Stl;
}

const std::vector<Application::PartFormat> &Application::partFormats()
{
    const static std::vector<PartFormat> vecFormat = {
        PartFormat::Iges,
        PartFormat::Step,
        PartFormat::OccBrep,
        PartFormat::Stl
    };
    return vecFormat;
}

QString Application::partFormatFilter(Application::PartFormat format)
{
    switch (format) {
    case PartFormat::Iges: return tr("IGES files(*.iges *.igs)");
    case PartFormat::Step: return tr("STEP files(*.step *.stp)");
    case PartFormat::OccBrep: return tr("OpenCascade BREP files(*.brep *.occ)");
    case PartFormat::Stl: return tr("STL files(*.stl *.stla)");
    case PartFormat::Unknown: break;
    }
    return QString();
}

QStringList Application::partFormatFilters()
{
    QStringList filters;
    filters << Application::partFormatFilter(PartFormat::Iges)
            << Application::partFormatFilter(PartFormat::Step)
            << Application::partFormatFilter(PartFormat::OccBrep)
            << Application::partFormatFilter(PartFormat::Stl);
    return filters;
}

Application::PartFormat Application::findPartFormat(const QString &filepath)
{
    QFile file(filepath);
    if (file.open(QIODevice::ReadOnly)) {
#ifdef HAVE_GMIO
        const gmio::STL_Format stlFormat =
                gmio::STL_probeFormat(filepath.toLocal8Bit().constData());
        if (stlFormat != gmio::STL_Format_Unknown)
            return Application::PartFormat::Stl;
#endif
        std::array<char, 2048> contentsBegin;
        contentsBegin.fill(0);
        file.read(contentsBegin.data(), contentsBegin.size());
        return Internal::findPartFormatFromContents(
                    contentsBegin.data(), contentsBegin.size(), file.size());
    }
    return PartFormat::Unknown;
}

Application::IoResult Application::importIges(
        Document* doc, const QString &filepath, qttask::Progress* progress)
{
    IGESControl_Controller::Init();
    Handle_TDocStd_Document cafDoc = occ::CafUtils::createXdeDocument();
    IFSelect_ReturnStatus err;
    Internal::loadCafDocumentFromFile<IGESCAFControl_Reader>(
                filepath, cafDoc, &err, progress);
    if (err == IFSelect_RetDone)
        doc->addRootItem(Internal::createXdeDocumentItem(filepath, cafDoc));
    return { err == IFSelect_RetDone, Internal::occReturnStatusToQString(err) };
}

Application::IoResult Application::importStep(
        Document* doc, const QString &filepath, qttask::Progress* progress)
{
    Handle_TDocStd_Document cafDoc = occ::CafUtils::createXdeDocument();
    IFSelect_ReturnStatus err;
    Internal::loadCafDocumentFromFile<STEPCAFControl_Reader>(
                filepath, cafDoc, &err, progress);
    if (err == IFSelect_RetDone)
        doc->addRootItem(Internal::createXdeDocumentItem(filepath, cafDoc));
    return { err == IFSelect_RetDone, Internal::occReturnStatusToQString(err) };
}

Application::IoResult Application::importOccBRep(
        Document* doc, const QString &filepath, qttask::Progress* progress)
{
    TopoDS_Shape shape;
    BRep_Builder brepBuilder;
    Handle_Message_ProgressIndicator indicator =
            new Internal::OccImportProgress(progress);
    const bool ok = BRepTools::Read(
            shape, filepath.toLocal8Bit().constData(), brepBuilder, indicator);
    if (ok) {
        Handle_TDocStd_Document cafDoc = occ::CafUtils::createXdeDocument();
        Handle_XCAFDoc_ShapeTool shapeTool =
                XCAFDoc_DocumentTool::ShapeTool(cafDoc->Main());
        const TDF_Label labelShape = shapeTool->NewShape();
        shapeTool->SetShape(labelShape, shape);
        doc->addRootItem(Internal::createXdeDocumentItem(filepath, cafDoc));
    }
    return { ok, ok ? QString() : tr("Unknown Error") };
}

Application::IoResult Application::importStl(
        Document* doc, const QString &filepath, qttask::Progress* progress)
{
    Application::IoResult result = { false, QString() };
    const Options::StlIoLibrary lib =
            Options::instance()->stlIoLibrary();
    if (lib == Options::StlIoLibrary::Gmio) {
#ifdef HAVE_GMIO
        QFile file(filepath);
        if (file.open(QIODevice::ReadOnly)) {
            Internal::GmioQtTaskProgress qttaskProgress(progress);
            gmio::STL_ReadOptions options = {};
            options.task = &qttaskProgress;
            int err = gmio::Error_OK;
            const auto funcRead = gmio::QIODevice_funcReadData(&file);
            while (err == gmio::Error_OK && !file.atEnd()) {
                const qint64 filepos = file.pos();
                options.ascii_solid_size = gmio::STL_probeAsciiSolidSize(funcRead);
                file.seek(filepos);
                gmio::STL_MeshCreatorOccPolyTriangulation meshcreator;
                err = gmio::STL_read(funcRead, &meshcreator, options);
                if (err == gmio::Error_OK) {
                    const Handle_Poly_Triangulation& mesh = meshcreator.polytri();
                    doc->addRootItem(Internal::createMeshItem(filepath, mesh));
                }
            }
            result.ok = (err == gmio::Error_OK);
            if (!result.ok)
                result.errorText = Internal::gmioErrorToQString(err);
        }
#endif // HAVE_GMIO
    }
    else if (lib == Options::StlIoLibrary::OpenCascade) {
        Handle_Message_ProgressIndicator indicator =
                new Internal::OccImportProgress(progress);
        const Handle_Poly_Triangulation mesh = RWStl::ReadFile(
                    OSD_Path(filepath.toLocal8Bit().constData()), indicator);
        if (!mesh.IsNull())
            doc->addRootItem(Internal::createMeshItem(filepath, mesh));
        result.ok = !mesh.IsNull();
        if (!result.ok)
            result.errorText = tr("Imported STL mesh is null");
    }
    return result;
}

Application::IoResult Application::exportIges(
        const std::vector<DocumentItem *> &docItems,
        const ExportOptions& /*options*/,
        const QString &filepath,
        qttask::Progress *progress)
{
    QMutexLocker locker(Internal::globalMutex()); Q_UNUSED(locker);
    Handle_Message_ProgressIndicator indicator =
            new Internal::OccImportProgress(progress);

    IGESControl_Controller::Init();
    IGESCAFControl_Writer writer;
    writer.SetColorMode(Standard_True);
    writer.SetNameMode(Standard_True);
    writer.SetLayerMode(Standard_True);
    if (!indicator.IsNull())
        writer.TransferProcess()->SetProgress(indicator);
    for (const DocumentItem* item : docItems) {
        if (sameType<XdeDocumentItem>(item)) {
            auto xdeDocItem = static_cast<const XdeDocumentItem*>(item);
            writer.Transfer(xdeDocItem->cafDoc());
        }
    }
    writer.ComputeModel();
    const Standard_Boolean ok = writer.Write(filepath.toLocal8Bit().constData());
    writer.TransferProcess()->SetProgress(nullptr);
    return { ok == Standard_True, QString() };
}

Application::IoResult Application::exportStep(
        const std::vector<DocumentItem *> &docItems,
        const ExportOptions& /*options*/,
        const QString &filepath,
        qttask::Progress *progress)
{
    QMutexLocker locker(Internal::globalMutex()); Q_UNUSED(locker);
    Handle_Message_ProgressIndicator indicator =
            new Internal::OccImportProgress(progress);
    STEPCAFControl_Writer writer;
    if (!indicator.IsNull())
        writer.ChangeWriter().WS()->TransferWriter()->FinderProcess()->SetProgress(indicator);
    for (const DocumentItem* item : docItems) {
        if (sameType<XdeDocumentItem>(item)) {
            auto xdeDocItem = static_cast<const XdeDocumentItem*>(item);
            writer.Transfer(xdeDocItem->cafDoc());
        }
    }
    const IFSelect_ReturnStatus err =
            writer.Write(filepath.toLocal8Bit().constData());
    writer.ChangeWriter().WS()->TransferWriter()->FinderProcess()->SetProgress(nullptr);
    return { err == IFSelect_RetDone, Internal::occReturnStatusToQString(err) };
}

Application::IoResult Application::exportOccBRep(
        const std::vector<DocumentItem *> &docItems,
        const ExportOptions& /*options*/,
        const QString &filepath,
        qttask::Progress *progress)
{
    std::vector<TopoDS_Shape> vecShape;
    vecShape.reserve(docItems.size());
    for (const DocumentItem* item : docItems) {
        if (sameType<XdeDocumentItem>(item)) {
            const auto xdeDocItem = static_cast<const XdeDocumentItem*>(item);
            const TDF_LabelSequence seqTopLevelFreeShapeLabel =
                    xdeDocItem->topLevelFreeShapeLabels();
            for (const TDF_Label& label : seqTopLevelFreeShapeLabel)
                vecShape.push_back(xdeDocItem->shape(label));
        }
    }
    TopoDS_Shape shape;
    if (vecShape.size() > 1) {
        TopoDS_Compound cmpd;
        BRep_Builder builder;
        builder.MakeCompound(cmpd);
        for (const TopoDS_Shape& subShape : vecShape)
            builder.Add(cmpd, subShape);
        shape = cmpd;
    }
    else if (vecShape.size() == 1) {
        shape = vecShape.front();
    }

    Handle_Message_ProgressIndicator indicator =
            new Internal::OccImportProgress(progress);
    const Standard_Boolean ok =
            BRepTools::Write(shape, filepath.toLocal8Bit().constData(), indicator);
    if (ok == Standard_True)
        return { true, QString() };
    return { false, tr("Unknown Error") };
}

Application::IoResult Application::exportStl(
        const std::vector<DocumentItem *> &docItems,
        const ExportOptions& options,
        const QString &filepath,
        qttask::Progress *progress)
{
    const Options::StlIoLibrary lib = Options::instance()->stlIoLibrary();
    if (lib == Options::StlIoLibrary::Gmio)
        return this->exportStl_gmio(docItems, options, filepath, progress);
    else if (lib == Options::StlIoLibrary::OpenCascade)
        return this->exportStl_OCC(docItems, options, filepath, progress);
    return { false, tr("Unknown Error") };
}

Application::IoResult Application::exportStl_gmio(
        const std::vector<DocumentItem *> &docItems,
        const Application::ExportOptions &options,
        const QString &filepath,
        qttask::Progress *progress)
{
    QFile file(filepath);
#ifdef HAVE_GMIO
    if (file.open(QIODevice::WriteOnly)) {
        const auto funcWrite = gmio::QIODevice_funcWriteData(&file);
        Internal::GmioQtTaskProgress qttaskProgress(progress);
        gmio::STL_WriteOptions gmioOptions = {};
        gmioOptions.ascii_float32_format = options.stlAsciiFloat32Format;
        gmioOptions.ascii_float32_prec = options.stlAsciiFloat32Precision;
        gmioOptions.ascii_solid_name = options.stlAsciiSolidName;
        gmioOptions.task = &qttaskProgress;
        for (const DocumentItem* item : docItems) {
            if (progress != nullptr) {
                progress->setStep(
                            tr("Writting item %1")
                            .arg(item->propertyLabel.value()));
            }
            int error = gmio::Error_OK;
            if (sameType<XdeDocumentItem>(item)) {
                auto xdeDocItem = static_cast<const XdeDocumentItem*>(item);
                const TopoDS_Shape shape = Internal::xdeDocumentWholeShape(xdeDocItem);
                const gmio::STL_MeshOccShape gmioMesh(shape);
                error = gmio::STL_write(
                            options.stlFormat, funcWrite, gmioMesh, gmioOptions);
            }
            else if (sameType<MeshItem>(item)) {
                auto meshItem = static_cast<const MeshItem*>(item);
                const gmio::STL_MeshOccPolyTriangulation gmioMesh(meshItem->triangulation());
                error = gmio::STL_write(
                            options.stlFormat, funcWrite, gmioMesh, gmioOptions);
            }
            if (error != gmio::Error_OK)
                return { false, Internal::gmioErrorToQString(error) };
        }
        return { true, QString() };
    }
#endif // HAVE_GMIO
    return { false, file.errorString() };
}

Application::IoResult Application::exportStl_OCC(
        const std::vector<DocumentItem *> &docItems,
        const Application::ExportOptions &options,
        const QString &filepath,
        qttask::Progress *progress)
{
#ifdef HAVE_GMIO
    const bool isAsciiFormat = options.stlFormat == gmio::STL_Format_Ascii;
    if (options.stlFormat != gmio::STL_Format_Ascii
            && options.stlFormat != gmio::STL_Format_BinaryLittleEndian)
    {
        return { false, tr("Format not supported") };
    }
#else
    const bool isAsciiFormat = options.stlFormat == ExportOptions::StlFormat::Ascii;
#endif
    if (docItems.size() > 1)
        return { false,  tr("OpenCascade RWStl does not support multi-solids") };

    if (docItems.size() > 0) {
        const DocumentItem* item = docItems.front();
        if (sameType<XdeDocumentItem>(item)) {
            auto xdeDocItem = static_cast<const XdeDocumentItem*>(item);
            StlAPI_Writer writer;
            writer.ASCIIMode() = isAsciiFormat;
            const TopoDS_Shape shape = Internal::xdeDocumentWholeShape(xdeDocItem);
            const Standard_Boolean ok = writer.Write(
                        shape, filepath.toLocal8Bit().constData());
            return { ok, ok ? QString() : tr("Unknown StlAPI_Writer failure") };
        }
        else if (sameType<MeshItem>(item)) {
            Handle_Message_ProgressIndicator indicator =
                    new Internal::OccImportProgress(progress);
            Standard_Boolean occOk = Standard_False;
            auto meshItem = static_cast<const MeshItem*>(item);
            const QByteArray filepathLocal8b = filepath.toLocal8Bit();
            const OSD_Path osdFilepath(filepathLocal8b.constData());
            const Handle_Poly_Triangulation& mesh = meshItem->triangulation();
            if (isAsciiFormat)
                occOk = RWStl::WriteAscii(mesh, osdFilepath, indicator);
            else
                occOk = RWStl::WriteBinary(mesh, osdFilepath, indicator);
            const bool ok = occOk == Standard_True;
            return { ok, ok ? QString() : tr("Unknown error") };
        }
    }
    return { true, QString() };
}

} // namespace Mayo
