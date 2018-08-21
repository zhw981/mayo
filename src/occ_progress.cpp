/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "occ_progress.h"
#include <fougtools/qttools/task/progress.h>
#include "math_utils.h"

namespace Mayo {

OccProgress::OccProgress(qttask::Progress* progress)
    : m_progress(progress)
{
    this->SetScale(0, 100, 1);
}

bool OccProgress::Show(const bool /*force*/)
{
    if (m_progress) {
        const Handle_TCollection_HAsciiString name = this->GetScope(1).GetName();
        if (!name.IsNull())
            m_progress->setStep(QLatin1String(name->ToCString()));
        const double pc = this->GetPosition(); // Always within [0,1]
        m_progress->setValue(MathUtils::mappedValue(pc, 0, 1, 0, 100));
    }
    return true;
}

bool OccProgress::UserBreak()
{
    return m_progress ? m_progress->isAbortRequested() : false;
}

} // namespace Mayo
