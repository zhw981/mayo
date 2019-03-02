/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include "io_handler.h"
#include "property_enumeration.h"
#include <QtCore/QCoreApplication>

namespace Mayo {

class IoIges {
    Q_DECLARE_TR_FUNCTIONS(Mayo::IoIges)
public:
    struct OptionsRead : public PropertyOwner {
        OptionsRead();
        PropertyEnumeration propertyBSplineContinuity;
        //PropertyEnumeration propertyPrecisionMode;
        //PropertyDouble propertyUserPrecision;
        PropertyEnumeration propertyMaxPrecisionMode;
        PropertyLength propertyMaxPrecisionValue;
        PropertyBool propertyUseStdSameParameter;
        PropertyEnumeration propertySurfaceCurveMode;
        PropertyAngle propertyEncodeRegularityAngle;
        //PropertyBool propertyUseBSplineApproxD1;
        //PropertyQString propertyResourceName;
        //PropertyQString propertySequence;
        //PropertyEnumeration lengthUnit;
    };
    static OptionsRead* optionsRead();
    static PropertyOwner* optionsWrite() { return nullptr; }

    static void init();
    static Io::Result readFile(
            Document* doc,
            const QString& filepath,
            qttask::Progress* progress);
    static Io::Result writeFile(
            Span<const ApplicationItem> spanAppItem,
            const QString& filepath,
            qttask::Progress* progress);

private:
    enum BSplineContinuity {
        BSplineContinuity_NoChange = 0,
        BSplineContinuity_BreakIntoC1Pieces = 1,
        BSplineContinuity_BreakIntoC2Pieces = 2
    };
    enum MaxPrecisionMode {
        MaxPrecisionMode_Preferred = 0,
        MaxPrecisionMode_Forced = 1
    };
    enum SurfaceCurveMode {
        SurfaceCurveMode_Default = 0,
        SurfaceCurveMode_2DUsePreferred = 2,
        SurfaceCurveMode_2DUseForced = -2,
        SurfaceCurveMode_3DUsePreferred = 3,
        SurfaceCurveMode_3DUseForced = -3
    };
    static const Enumeration& enum_BSplineContinuity();
    static const Enumeration& enum_MaxPrecisionMode();
    static const Enumeration& enum_SurfaceCurveMode();
};

} // namespace Mayo
