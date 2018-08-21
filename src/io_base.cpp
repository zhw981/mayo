/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "io_base.h"
#include "brep_utils.h"
#include "document.h"
#include "xde_document_item.h"

#include <QtCore/QFileInfo>
#include <unordered_set>

namespace Mayo {

struct IoBase::Private {
    std::vector<Property*> m_vecOptionRead;
    std::vector<Property*> m_vecOptionWrite;
};

IoBase::IoBase()
    : d(new Private)
{
}

IoBase::~IoBase()
{
    delete d;
}

IoBase::Options IoBase::optionsRead() const
{
    return d->m_vecOptionRead;
}

IoBase::Options IoBase::optionsWrite() const
{
    return d->m_vecOptionWrite;
}

void IoBase::addOptionRead(Property* prop)
{
    d->m_vecOptionRead.push_back(prop);
}

void IoBase::addOptionWrite(Property* prop)
{
    d->m_vecOptionWrite.push_back(prop);
}

std::vector<ApplicationItem> IoBase::xdeApplicationItems(
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

void IoBase::init(XdeDocumentItem* xdeDocItem, const QString& filepath)
{
    xdeDocItem->propertyLabel.setValue(QFileInfo(filepath).baseName());
    const TDF_Label labelRoot = xdeDocItem->createRootAssembly();
    const TopoDS_Shape shapeRoot = xdeDocItem->shape(labelRoot);
    xdeDocItem->propertyArea.setQuantity(BRepUtils::area(shapeRoot));
    xdeDocItem->propertyVolume.setQuantity(BRepUtils::volume(shapeRoot));
}

} // namespace Mayo
