// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
namespace prospero_runtime {
// Written by backend load/release paths, independent of native or HTTP selection.
inline std::atomic<bool> model_resident{false};
inline void set_model_resident(bool loaded) { model_resident.store(loaded, std::memory_order_release); }
}
