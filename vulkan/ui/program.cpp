// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx/gl_program.hpp"
#include "backend.hpp"
namespace hui::gfx
{
void set_glsl_prefix(const char *)
{
}
GLuint build_program(const char *name, const char *, const char *)
{
    return prospero::vkui::program(name);
}
} // namespace hui::gfx
#ifndef PROSPERO_HOST
// Mihawk's foundation has no deferred splash gate; system.cpp calls the real
// sceSystemServiceHideSplashScreen immediately after this optional hook.
extern "C" void hui_release_splash()
{
}
#endif
