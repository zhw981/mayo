/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include "quantity.h"

#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <QtCore/QFlags>

namespace Mayo {

struct BRepUtils {
    template<typename FUNC>
    static void forEachSubShape(TopExp_Explorer& explorer, FUNC fn);

    template<typename FUNC>
    static void forEachSubShape(
            const TopoDS_Shape& shape, TopAbs_ShapeEnum shapeType, FUNC fn);

    template<typename FUNC>
    static void forEachSubFace(const TopoDS_Shape& shape, FUNC fn);

    static bool moreComplex(TopAbs_ShapeEnum lhs, TopAbs_ShapeEnum rhs);

    enum VolumeOption {
        VolumeNone = 0x0,
        VolumeOnlyClosed = 0x01,
        VolumeSkipShared = 0x02
    };
    using VolumeOptions = QFlags<VolumeOption>;
    enum AreaOption {
        AreaNone = 0x0,
        AreaSkipShared = 0x01
    };
    using AreaOptions = QFlags<AreaOption>;

    static QuantityVolume volume(
            const TopoDS_Shape& shape, VolumeOptions opt = VolumeNone);
    static QuantityArea area(
            const TopoDS_Shape& shape, AreaOptions opt = AreaNone);
};



// --
// -- Implementation
// --

template<typename FUNC>
void BRepUtils::forEachSubShape(TopExp_Explorer& explorer, FUNC fn)
{
    while (explorer.More()) {
        fn(explorer.Current());
        explorer.Next();
    }
}

template<typename FUNC>
void BRepUtils::forEachSubShape(
        const TopoDS_Shape& shape, TopAbs_ShapeEnum shapeType, FUNC fn)
{
    BRepUtils::forEachSubShape(TopExp_Explorer(shape, shapeType), std::move(fn));
}

template<typename FUNC>
void BRepUtils::forEachSubFace(const TopoDS_Shape& shape, FUNC fn)
{
    for (TopExp_Explorer expl(shape, TopAbs_FACE); expl.More(); expl.Next())
        fn(TopoDS::Face(expl.Current()));
}

} // namespace Mayo
