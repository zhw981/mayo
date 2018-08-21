/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include "application_item.h"
#include "property.h"
#include "result.h"
#include "span.h"

#include <QtCore/QString>
#include <vector>

namespace qttask { class Progress; }

namespace Mayo {

class Document;
class DocumentItem;
class XdeDocumentItem;

class IoBase {
public:
    using Result = Result<void>;
    using Options = Span<Property*>;

    Options optionsRead() const;
    Options optionsWrite() const;

    virtual Result readFile(
            Document* doc,
            const QString& filepath,
            qttask::Progress* progress) = 0;
    virtual Result writeFile(
            Span<const ApplicationItem> spanAppItem,
            const QString& filepath,
            qttask::Progress* progress) = 0;

protected:
    IoBase();
    virtual ~IoBase();

    void addOptionRead(Property* prop);
    void addOptionWrite(Property* prop);

    static std::vector<ApplicationItem> xdeApplicationItems(
            Span<const ApplicationItem> spanAppItem);
    static void init(XdeDocumentItem* xdeDocItem, const QString& filepath);

private:
    struct Private;
    Private* const d;
};

} // namespace Mayo
