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

#include <Interface_Static.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <IGESCAFControl_Writer.hxx>
#include <Transfer_TransientProcess.hxx>
#include <XSControl_WorkSession.hxx>
#include <mutex>
#include <vector>

namespace Mayo {

namespace { std::mutex globalMutex; }

IoIges::OptionsRead* IoIges::optionsRead()
{
    static OptionsRead options;
    return &options;
}

void IoIges::init()
{
    IGESControl_Controller::Init();
    // Read options
    OptionsRead* optRead = IoIges::optionsRead();
    optRead->propertyBSplineContinuity.setValue(
                Interface_Static::IVal("read.iges.bspline.continuity"));
    optRead->propertyMaxPrecisionMode.setValue(
                Interface_Static::IVal("read.maxprecision.mode"));
    const double maxPrecision = Interface_Static::RVal("read.maxprecision.val");
    const double safeMaxPrecision = !qFuzzyIsNull(maxPrecision) ? maxPrecision : 1.;
    optRead->propertyMaxPrecisionValue.setQuantity(
                safeMaxPrecision * Quantity_Millimeter);
    optRead->propertyUseStdSameParameter.setValue(
                Interface_Static::IVal("read.stdsameparameter.mode") == 1);
    optRead->propertySurfaceCurveMode.setValue(
                Interface_Static::IVal("read.surfacecurve.mode"));
    const double regularityAngle = Interface_Static::RVal("read.encoderegularity.angle");
    const double safeRegularityAngle = !qFuzzyIsNull(regularityAngle) ? regularityAngle : 0.01;
    optRead->propertyEncodeRegularityAngle.setQuantity(
                safeRegularityAngle * Quantity_Radian);
    // Write options
    // ...
}

Io::Result IoIges::readFile(
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
        return Io::Result::error(StringUtils::rawText(err));

    auto xdeDocItem = new XdeDocumentItem(cafDoc);
    XdeUtils::initProperties(xdeDocItem, filepath);
    doc->addRootItem(xdeDocItem);
    return Io::Result::ok();
}

Io::Result IoIges::writeFile(
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
    for (const ApplicationItem& xdeAppItem : XdeUtils::xdeApplicationItems(spanAppItem)) {
        if (xdeAppItem.isDocumentItem()) {
            auto xdeDocItem = static_cast<const XdeDocumentItem*>(xdeAppItem.documentItem());
            if (!writer.Transfer(xdeDocItem->cafDoc()))
                return Io::Result::error(tr("Transfer error"));
        }
        if (xdeAppItem.isXdeAssemblyNode()) {
            if (!writer.Transfer(xdeAppItem.xdeAssemblyNode().label()))
                return Io::Result::error(tr("Transfer error"));
        }
    }
    writer.ComputeModel();
    const bool ok = writer.Write(filepath.toLocal8Bit().constData());
    return ok ? Io::Result::ok() : Io::Result::error(tr("Write error"));
}

const Enumeration& IoIges::enum_BSplineContinuity()
{
    static Enumeration enumeration;
    if (enumeration.size() == 0) {
        // no change; the curves are taken as they are in the IGES file. C0 entities of Open CASCADE Technology
        // may be produced.
        enumeration.map(BSplineContinuity_NoChange, tr("No change"));
        // if an IGES BSpline, Spline or CopiousData curve is C0 continuous, it is broken down into pieces of C1
        // continuous Geom_BSplineCurve.
        enumeration.map(BSplineContinuity_BreakIntoC1Pieces, tr("Break into C1 pieces"));
        // This option concerns IGES Spline curves only. IGES Spline curves are broken down into pieces of C2
        // continuity. If C2 cannot be ensured, the Spline curves will be broken down into pieces of C1 continuity.
        enumeration.map(BSplineContinuity_BreakIntoC2Pieces, tr("Break into C2 pieces"));
    }
    return enumeration;
}

const Enumeration &IoIges::enum_MaxPrecisionMode()
{
    static Enumeration enumeration;
    if (enumeration.size() == 0) {
        // maximum tolerance is used as a limit but sometimes it can be exceeded (currently, only for
        // deviation of a 3D curve of an edge from its pcurves and from vertices of such edge) to ensure shape validity
        enumeration.map(MaxPrecisionMode_Preferred, tr("Preferred"));
        // maximum tolerance is used as a rigid limit, i.e. it can not be exceeded and, if this happens,
        // tolerance is trimmed to suit the maximum-allowable value
        enumeration.map(MaxPrecisionMode_Forced, tr("Forced"));
    }
    return enumeration;
}

const Enumeration &IoIges::enum_SurfaceCurveMode()
{
    static Enumeration enumeration;
    if (enumeration.size() == 0) {
        // Use the preference flag value in the entity's Parameter Data section
        enumeration.map(SurfaceCurveMode_Default, tr("Default"));
        // the 2D is used to rebuild the 3D in case of their inconsistency
        enumeration.map(SurfaceCurveMode_2DUsePreferred, tr("2D Use Preferred"));
        // the 2D is always used to rebuild the 3D (even if 3D is present in the file)
        enumeration.map(SurfaceCurveMode_2DUseForced, tr("2D Use Forced"));
        // the 3D is used to rebuild the 2D in case of their inconsistency
        enumeration.map(SurfaceCurveMode_3DUsePreferred, tr("3D Use Preferred"));
        // the 3D is always used to rebuild the 2D (even if 2D is present in the file)
        enumeration.map(SurfaceCurveMode_3DUseForced, tr("3D Use Forced"));
    }
    return enumeration;
}

IoIges::OptionsRead::OptionsRead()
    : propertyBSplineContinuity(this, IoIges::tr("BSpline Continuity"), &enum_BSplineContinuity()),
      propertyMaxPrecisionMode(this, IoIges::tr("Max Precision Mode"), &enum_MaxPrecisionMode()),
      propertyMaxPrecisionValue(this, IoIges::tr("Max Precision Value")),
      propertyUseStdSameParameter(this, IoIges::tr("Use pcurves Lowest Tolerance")),
      propertySurfaceCurveMode(this, IoIges::tr("Surface Curve Mode"), &enum_SurfaceCurveMode()),
      propertyEncodeRegularityAngle(this, IoIges::tr("Regularity Angle"))
{
}

} // namespace Mayo
