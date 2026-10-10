// ProsperoAI - The AGC text runtime inside the Vulkan build.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The same source as the AGC build's runtime, for its two text architectures only:
// image, sound and voice stay with vulkan/gpt_runtime_hybrid.cpp, which also releases
// the media scratch area before a model.ps5lm bundle is loaded in its place.
#ifdef PROSPERO_HYBRID_MEDIA
#undef PS5_MEDIA_AUDIO
#undef PS5_MEDIA_IMAGE
#ifndef PS5_DUAL_BACKEND
#define PS5_DUAL_BACKEND 1
#endif
#define PROSPERO_RUNTIME_NAMESPACE prospero_agc
#include "runtime_agc.hpp"

#include "../src/gpt_runtime.cpp"
#endif
