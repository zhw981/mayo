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

#include <fougtools/qttools/task/progress.h>
#include <QtCore/QString>

namespace Mayo {

class Document;
class DocumentItem;
class XdeDocumentItem;

struct Io {
    using Result = Mayo::Result<void>;
};

struct XdeUtils {
    static std::vector<ApplicationItem> xdeApplicationItems(
            Span<const ApplicationItem> spanAppItem);
    static void initProperties(
            XdeDocumentItem* xdeDocItem, const QString& filepath);
};

} // namespace Mayo
