// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifndef PROSPERO_MODEL_ROOT
#define PROSPERO_MODEL_ROOT "/data/homebrew/prosperoai/models"
#endif
#ifdef __cplusplus
namespace prospero
{
inline constexpr char kModelRoot[] = PROSPERO_MODEL_ROOT;
}
#endif
