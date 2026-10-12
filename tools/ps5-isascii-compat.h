#pragma once

// The PS5 Payload SDK's libc++ locale headers call isascii(), while the
// sysroot declaration is hidden under the C++ feature macros used here.
#include <ctype.h>

#ifndef isascii
#define isascii(value) ((unsigned int)(unsigned char)(value) <= 0x7fU)
#endif
