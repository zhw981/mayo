/****************************************************************************
** Copyright (c) 2018, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#include <scope_guard/scope_guard.hpp>

namespace Mayo {

template<typename CALLBACK>
auto makeScopeGuard(CALLBACK&& callback)
{
    return sg::make_scope_guard(std::forward<CALLBACK>(callback));
}

} // namespace Mayo
