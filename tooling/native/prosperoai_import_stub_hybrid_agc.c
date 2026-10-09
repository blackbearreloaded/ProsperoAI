// Link-only imports; calls resolve to the console's libSceAgc.prx.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdint.h>
uint32_t *sceAgcDcbAcquireMem(void *command, uint8_t engine, uint32_t coherency, uint32_t policy,
                              uint64_t size, uint64_t address, uint32_t interval)
{
    (void)command;
    (void)engine;
    (void)coherency;
    (void)policy;
    (void)size;
    (void)address;
    (void)interval;
    return 0;
}
uint32_t *sceAgcCbDispatch(void *command, uint32_t x, uint32_t y, uint32_t z, uint32_t flags)
{
    (void)command;
    (void)x;
    (void)y;
    (void)z;
    (void)flags;
    return 0;
}
