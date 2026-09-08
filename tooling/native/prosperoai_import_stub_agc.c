/* Host-link declaration for system libSceAgc; never packaged or executed. */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <stdint.h>

int32_t sceAgcInit(uint32_t version)
{
    (void)version;
    return -1;
}
int32_t sceAgcSuspendPoint(void)
{
    return -1;
}
int32_t sceAgcCreateShader(void **shader, void *header, void *code)
{
    (void)shader;
    (void)header;
    (void)code;
    return -1;
}
uint32_t *sceAgcDcbSetShRegistersIndirect(void *command, const void *registers, uint32_t count)
{
    (void)command;
    (void)registers;
    (void)count;
    return 0;
}
uint32_t *sceAgcCbSetShRegisterRangeDirect(void *command, uint32_t offset, const uint32_t *values,
                                           uint32_t count)
{
    (void)command;
    (void)offset;
    (void)values;
    (void)count;
    return 0;
}
uint32_t *sceAgcDcbAcquireMem(void *command, uint8_t engine, uint32_t flags, uint32_t gcr,
                              uint64_t address, uint64_t size, uint32_t poll)
{
    (void)command;
    (void)engine;
    (void)flags;
    (void)gcr;
    (void)address;
    (void)size;
    (void)poll;
    return 0;
}
uint32_t *sceAgcDcbWaitRegMem(void *command, int mode, uint8_t compare, uint8_t control,
                              uint8_t policy, uint64_t address, uint64_t reference, uint64_t mask,
                              uint32_t poll)
{
    (void)command;
    (void)mode;
    (void)compare;
    (void)control;
    (void)policy;
    (void)address;
    (void)reference;
    (void)mask;
    (void)poll;
    return 0;
}
uint32_t *sceAgcCbDispatch(void *command, uint32_t x, uint32_t y, uint32_t z, uint32_t modifier)
{
    (void)command;
    (void)x;
    (void)y;
    (void)z;
    (void)modifier;
    return 0;
}
uint32_t *sceAgcCbReleaseMem(void *command, uint8_t action, int16_t gcr, uint64_t source,
                             int8_t destination, void *address, uint32_t data_select, uint64_t data,
                             uint16_t interrupt, uint16_t cache_policy, int8_t execute,
                             int32_t reserved)
{
    (void)command;
    (void)action;
    (void)gcr;
    (void)source;
    (void)destination;
    (void)address;
    (void)data_select;
    (void)data;
    (void)interrupt;
    (void)cache_policy;
    (void)execute;
    (void)reserved;
    return 0;
}
