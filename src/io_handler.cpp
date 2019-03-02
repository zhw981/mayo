/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "io_handler.h"
#include "brep_utils.h"
#include "document.h"
#include "xde_document_item.h"

#include <QtCore/QFileInfo>
#include <unordered_set>

namespace Mayo {

std::vector<ApplicationItem> XdeUtils::xdeApplicationItems(
        Span<const ApplicationItem> spanAppItem)
{
    std::vector<ApplicationItem> vecAppItem;
    std::unordered_set<XdeDocumentItem*> setXdeDocItem;
    std::vector<XdeAssemblyNode> vecXdeNode;

    for (const ApplicationItem& appItem : spanAppItem) {
        if (appItem.isDocument()) {
            for (DocumentItem* docItem : appItem.document()->rootItems()) {
                if (sameType<XdeDocumentItem>(docItem))
                    setXdeDocItem.insert(static_cast<XdeDocumentItem*>(docItem));
            }
        }
        else if (appItem.isDocumentItem()
                 && sameType<XdeDocumentItem>(appItem.documentItem()))
        {
            auto xdeDocItem = static_cast<XdeDocumentItem*>(appItem.documentItem());
            setXdeDocItem.insert(xdeDocItem);
        }
        else if (appItem.isXdeAssemblyNode())
        {
            vecXdeNode.push_back(appItem.xdeAssemblyNode());
        }
    }

    for (const XdeAssemblyNode& xdeNode : vecXdeNode) {
        if (setXdeDocItem.find(xdeNode.ownerDocItem) == setXdeDocItem.cend())
            vecAppItem.emplace_back(xdeNode);
    }

    for (XdeDocumentItem* xdeDocItem : setXdeDocItem)
        vecAppItem.emplace_back(xdeDocItem);

    return vecAppItem;
}

void XdeUtils::initProperties(XdeDocumentItem* xdeDocItem, const QString& filepath)
{
    xdeDocItem->propertyLabel.setValue(QFileInfo(filepath).baseName());
    QuantityArea area = {};
    QuantityVolume volume = {};
    for (const TDF_Label& label : xdeDocItem->topLevelFreeShapes()) {
        const TopoDS_Shape shape = xdeDocItem->shape(label);
        area += BRepUtils::area(shape);
        volume += BRepUtils::volume(shape);
    }

    xdeDocItem->propertyArea.setQuantity(area);
    xdeDocItem->propertyVolume.setQuantity(volume);
}

} // namespace Mayo
