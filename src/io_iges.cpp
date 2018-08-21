/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "io_iges.h"
#include "caf_utils.h"
#include "document.h"
#include "occ_progress.h"
#include "scope_guard.h"
#include "string_utils.h"
#include "xde_document_item.h"

#include <IGESControl_Controller.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <IGESCAFControl_Writer.hxx>
#include <Transfer_TransientProcess.hxx>
#include <XSControl_WorkSession.hxx>
#include <mutex>
#include <vector>

namespace Mayo {

namespace { std::mutex globalMutex; }

IoIges::IoIges()
{
    IGESControl_Controller::Init();
}

IoBase::Result IoIges::readFile(
        Document* doc, const QString& filepath, qttask::Progress* progress)
{
    std::lock_guard<std::mutex> lock(globalMutex); Q_UNUSED(lock);

    Handle_Message_ProgressIndicator indicator = new OccProgress(progress);
    Handle_TDocStd_Document cafDoc = CafUtils::createXdeDocument();
    indicator->NewScope(30, "Loading file");
    IGESCAFControl_Reader reader;
    reader.SetColorMode(true);
    reader.SetNameMode(true);
    reader.SetLayerMode(true);
    IFSelect_ReturnStatus err = reader.ReadFile(filepath.toLocal8Bit().constData());
    indicator->EndScope();
    if (err == IFSelect_RetDone) {
        Handle_XSControl_WorkSession ws = reader.WS();
        ws->MapReader()->SetProgress(indicator);
        indicator->NewScope(70, "Translating file");
        if (!reader.Transfer(cafDoc))
            err = IFSelect_RetFail;
        indicator->EndScope();
        ws->MapReader()->SetProgress(nullptr);
    }

    if (err != IFSelect_RetDone)
        return Result::error(StringUtils::rawText(err));

    auto xdeDocItem = new XdeDocumentItem(cafDoc);
    IoBase::init(xdeDocItem, filepath);
    doc->addRootItem(xdeDocItem);
    return Result::ok();
}

IoBase::Result IoIges::writeFile(
        Span<const ApplicationItem> spanAppItem,
        const QString& filepath,
        qttask::Progress* progress)
{
    std::lock_guard<std::mutex> lock(globalMutex); Q_UNUSED(lock);
    Handle_Message_ProgressIndicator indicator = new OccProgress(progress);
    IGESCAFControl_Writer writer;
    const Handle_Transfer_FinderProcess& trsfProcess = writer.TransferProcess();
    trsfProcess->SetProgress(indicator);
    writer.SetColorMode(true);
    writer.SetNameMode(true);
    writer.SetLayerMode(true);
    auto guard = Mayo::makeScopeGuard([=]{ trsfProcess->SetProgress(nullptr); });
    for (const ApplicationItem& xdeAppItem : IoBase::xdeApplicationItems(spanAppItem)) {
        if (xdeAppItem.isDocumentItem()) {
            auto xdeDocItem = static_cast<const XdeDocumentItem*>(xdeAppItem.documentItem());
            if (!writer.Transfer(xdeDocItem->cafDoc()))
                return Result::error(tr("Transfer error"));
        }
        if (xdeAppItem.isXdeAssemblyNode()) {
            if (!writer.Transfer(xdeAppItem.xdeAssemblyNode().label()))
                return Result::error(tr("Transfer error"));
        }
    }
    writer.ComputeModel();
    const bool ok = writer.Write(filepath.toLocal8Bit().constData());
    return ok ? Result::ok() : Result::error(tr("Write error"));
}

} // namespace Mayo
