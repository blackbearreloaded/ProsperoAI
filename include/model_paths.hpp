// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifndef PROSPERO_MODEL_ROOT
#define PROSPERO_MODEL_ROOT "/data/prosperoai/models"
#endif
#ifdef __cplusplus
namespace prospero
{
// The folder models are kept in. vulkan/storage.cpp settles it at start: with
// filesystem access it is PROSPERO_MODEL_ROOT, without it a folder in the app's sandbox.
inline const char *&model_root_slot()
{
    static const char *root = PROSPERO_MODEL_ROOT;
    return root;
}
inline const char *model_root()
{
    return model_root_slot();
}
} // namespace prospero
#endif
