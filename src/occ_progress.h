/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include <Message_ProgressIndicator.hxx>
namespace qttask { class Progress; }

namespace Mayo {

class OccProgress : public Message_ProgressIndicator {
public:
    OccProgress(qttask::Progress* progress);

    bool Show(const bool /*force*/) override;
    bool UserBreak() override;

private:
    qttask::Progress* m_progress = nullptr;
};

} // namespace Mayo
