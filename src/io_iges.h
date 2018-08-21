/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include "io_base.h"
#include <QtCore/QCoreApplication>

namespace Mayo {

class IoIges : public IoBase {
    Q_DECLARE_TR_FUNCTIONS(Mayo::IoIges)
public:
    IoIges();

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
