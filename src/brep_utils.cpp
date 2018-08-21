/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "brep_utils.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

namespace Mayo {

bool BRepUtils::moreComplex(TopAbs_ShapeEnum lhs, TopAbs_ShapeEnum rhs)
{
    return lhs < rhs;
}

QuantityVolume BRepUtils::volume(const TopoDS_Shape &shape, VolumeOptions opt)
{
    const bool flagOnlyClosed = opt.testFlag(VolumeOnlyClosed);
    const bool flagSkipShared = opt.testFlag(VolumeSkipShared);
    GProp_GProps system;
    BRepGProp::VolumeProperties(shape, system, flagOnlyClosed, flagSkipShared);
    return std::max(system.Mass(), 0.) * Quantity_CubicMillimeter;
}

QuantityArea BRepUtils::area(const TopoDS_Shape &shape, AreaOptions opt)
{
    const bool flagSkipShared = opt.testFlag(AreaSkipShared);
    GProp_GProps system;
    BRepGProp::SurfaceProperties(shape, system, flagSkipShared);
    return  std::max(system.Mass(), 0.) * Quantity_SquaredMillimeter;
}

} // namespace Mayo
