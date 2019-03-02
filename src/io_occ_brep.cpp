/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "io_occ_brep.h"
#include "caf_utils.h"
#include "document.h"
#include "occ_progress.h"

#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <XCAFDoc_DocumentTool.hxx>

namespace Mayo {

IoHandler::Result IoOccBRep::readFile(
        Document* doc, const QString& filepath, qttask::Progress* progress)
{
    TopoDS_Shape shape;
    BRep_Builder brepBuilder;
    Handle_Message_ProgressIndicator indicator = new OccProgress(progress);
    const bool ok = BRepTools::Read(
            shape, filepath.toLocal8Bit().constData(), brepBuilder, indicator);
    if (!ok)
        return Result::error(tr("Unknown Error"));
    Handle_TDocStd_Document cafDoc = CafUtils::createXdeDocument();
    Handle_XCAFDoc_ShapeTool shapeTool =
            XCAFDoc_DocumentTool::ShapeTool(cafDoc->Main());
    const TDF_Label labelShape = shapeTool->NewShape();
    shapeTool->SetShape(labelShape, shape);
    auto xdeDocItem = new XdeDocumentItem(cafDoc);
    IoBase::init(xdeDocItem, filepath);
    doc->addRootItem(xdeDocItem);
    return Result::ok();
}

IoHandler::Result IoOccBRep::writeFile(
        Span<const ApplicationItem> spanAppItem,
        const QString& filepath,
        qttask::Progress* progress)
{
    std::vector<TopoDS_Shape> vecShape;
    for (const ApplicationItem& xdeAppItem : IoHandler::xdeApplicationItems(spanAppItem)) {
        if (xdeAppItem.isDocumentItem()) {
            auto xdeDocItem = static_cast<const XdeDocumentItem*>(xdeAppItem.documentItem());
            for (const TDF_Label& label : xdeDocItem->topLevelFreeShapes())
                vecShape.push_back(xdeDocItem->shape(label));

        }
        if (xdeAppItem.isXdeAssemblyNode()) {
            auto xdeDocItem = static_cast<const XdeDocumentItem*>(xdeAppItem.documentItem());
            vecShape.push_back(xdeDocItem->shape(xdeAppItem.xdeAssemblyNode().label()));
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

    Handle_Message_ProgressIndicator indicator = new OccProgress(progress);
    if (!BRepTools::Write(shape, filepath.toLocal8Bit().constData(), indicator))
        return Result::error(tr("Unknown Error"));
    return Result::ok();
}

} // namespace Mayo
