/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include "io_handler.h"
#include <QtCore/QCoreApplication>

namespace Mayo {

class IoOccBRep : public IoHandler {
    Q_DECLARE_TR_FUNCTIONS(Mayo::IoOccBRep)
public:
    Result readFile(
            Document* doc,
            const QString& filepath,
            qttask::Progress* progress) override;
    Result writeFile(
            Span<const ApplicationItem> spanAppItem,
            const QString& filepath,
            qttask::Progress* progress) override;
};

} // namespace Mayo
