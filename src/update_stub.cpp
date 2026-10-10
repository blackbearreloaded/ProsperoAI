// ProsperoAI - Updates in a build without the update kit: nothing is asked or offered.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "update.hpp"

namespace prospero::update
{
void check()
{
}
bool take_offer(Offer *)
{
    return false;
}
bool begin()
{
    return false;
}
void poll(Progress *progress)
{
    *progress = Progress{};
}
void cancel()
{
}
bool apply()
{
    return false;
}
void finish()
{
}
} // namespace prospero::update
