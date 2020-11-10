/****************************************************************************
** Copyright (c) 2020, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include "occ_progress_indicator.h"
#include "task_progress.h"
#include <fougtools/occtools/qt_utils.h>

namespace Mayo {

OccProgressIndicator::OccProgressIndicator(TaskProgress* progress)
    : m_progress(progress)
{
#if OCC_VERSION_HEX < OCC_VERSION_CHECK(7, 5, 0)
    this->SetScale(0., 100., 1.);
#endif
}

#if OCC_VERSION_HEX >= OCC_VERSION_CHECK(7, 5, 0)
void OccProgressIndicator::Show(const Message_ProgressScope& scope, const bool /*isForce*/)
{
    if (m_progress) {
        if (scope.Name() && scope.Name() != m_lastStepName) {
            m_progress->setStep(QString::fromUtf8(scope.Name()));
            m_lastStepName = scope.Name();
        }

        const double pc = scope.GetPortion(); // Always within [0,1]
        const int minVal = 0;
        const int maxVal = 100;
        const int val = minVal + pc * (maxVal - minVal);
        if (m_lastProgress != val) {
            m_progress->setValue(val);
            m_lastProgress = val;
        }
    }
}
#else
bool OccProgressIndicator::Show(const bool /*force*/)
{
    if (m_progress) {
        const Handle_TCollection_HAsciiString name = this->GetScope(1).GetName();
        if (!name.IsNull())
            m_progress->setStep(occ::QtUtils::fromUtf8ToQString(name->String()));

        const double pc = this->GetPosition(); // Always within [0,1]
        const int minVal = 0;
        const int maxVal = 100;
        const int val = minVal + pc * (maxVal - minVal);
        m_progress->setValue(val);
    }

    return true;
}
#endif

bool OccProgressIndicator::UserBreak()
{
    return TaskProgress::isAbortRequested(m_progress);
}

} // namespace Mayo
