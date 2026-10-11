// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Upstream defines these as nullptr/false in core/parallel.cpp. Declaring constant
// initialization avoids unresolved optional TLS init hooks in the PS5 ELF loader;
// actual per-thread storage and executor behavior remain in the upstream source.
namespace sd {
class ParallelExecutor;
namespace parallel_detail {
extern constinit thread_local ParallelExecutor *executor;
extern constinit thread_local bool active;
}
}
