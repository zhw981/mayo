/****************************************************************************
** Copyright (c) 2020, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include <Standard_Handle.hxx>
#include <Standard_Version.hxx>
#if OCC_VERSION_HEX >= 0x070500
#  include <Message_ProgressRange.hxx>
#else
#  include <Standard_Handle.hxx>
class Message_ProgressIndicator;
#endif

#ifndef OCC_VERSION_CHECK
// Turns the major, minor and patch numbers of a version into an integer
// 0xMMNNPP (MM = major, NN = minor, PP = patch).
// This can be compared with OCC_VERSION_HEX
#  define OCC_VERSION_CHECK(major, minor, patch) ((major<<16)|(minor<<8)|(patch))
#endif

namespace Mayo {

class TKernelUtils {
public:
    template<typename STD_TRANSIENT>
    static opencascade::handle<STD_TRANSIENT> makeHandle(const STD_TRANSIENT* ptr) { return ptr; }

    using ReturnType_StartProgressIndicator =
#if OCC_VERSION_HEX >= OCC_VERSION_CHECK(7, 5, 0)
                Message_ProgressRange;
#else
                const opencascade::handle<Message_ProgressIndicator>&;
#endif
    static ReturnType_StartProgressIndicator start(const opencascade::handle<Message_ProgressIndicator>& progress);

};

} // namespace Mayo
