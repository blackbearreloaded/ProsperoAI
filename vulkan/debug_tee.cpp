// ProsperoAI - What the app tells the console's log also goes to the debug log.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The model runtimes and the interface kit report through sceKernelDebugOutText, which
// only a PC attached to the console can read. The call is wrapped at link time
// (tools/build-vulkan-native.sh): the console still gets every line, and the debug log,
// when it is switched on, gets it too.
#ifdef PROSPERO_UI_VULKAN
#include "debug_log.hpp"

extern "C" int __real_sceKernelDebugOutText(int, const char *);

extern "C" int __wrap_sceKernelDebugOutText(int channel, const char *text)
{
    const int result = __real_sceKernelDebugOutText(channel, text);
    prospero::debug::system_line(text);
    return result;
}
#endif
