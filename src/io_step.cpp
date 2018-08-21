/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "io_step.h"
#include "caf_utils.h"
#include "document.h"
#include "occ_progress.h"
#include "scope_guard.h"
#include "string_utils.h"
#include "xde_document_item.h"

#include <NCollection_Vector.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <Transfer_TransientProcess.hxx>
#include <XSControl_WorkSession.hxx>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace Mayo {

namespace { std::mutex globalMutex; }

IoBase::Result IoStep::readFile(
        Document* doc, const QString& filepath, qttask::Progress* progress)
{
    std::lock_guard<std::mutex> lock(globalMutex); Q_UNUSED(lock);

    Handle_Message_ProgressIndicator indicator = new OccProgress(progress);
    Handle_TDocStd_Document cafDoc = CafUtils::createXdeDocument();
    indicator->NewScope(30, "Loading file");
    STEPCAFControl_Reader reader;
    reader.SetColorMode(true);
    reader.SetNameMode(true);
    reader.SetLayerMode(true);
    reader.SetPropsMode(true);
    IFSelect_ReturnStatus err = reader.ReadFile(filepath.toLocal8Bit().constData());
    indicator->EndScope();
    if (err == IFSelect_RetDone) {
        Handle_XSControl_WorkSession ws = reader.Reader().WS();
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

IoBase::Result IoStep::writeFile(
        Span<const ApplicationItem> spanAppItem,
        const QString& filepath,
        qttask::Progress* progress)
{
    std::lock_guard<std::mutex> lock(globalMutex); Q_UNUSED(lock);
    Handle_Message_ProgressIndicator indicator = new OccProgress(progress);
    STEPCAFControl_Writer writer;
    const Handle_Transfer_FinderProcess& finderProcess =
            writer.ChangeWriter().WS()->TransferWriter()->FinderProcess();
    auto guard = Mayo::makeScopeGuard([=]{ finderProcess->SetProgress(nullptr); });
    finderProcess->SetProgress(indicator);
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
    const IFSelect_ReturnStatus err =
            writer.Write(filepath.toLocal8Bit().constData());
    return err == IFSelect_RetDone ?
                Result::ok() :
                Result::error(StringUtils::rawText(err));
}

} // namespace Mayo
