#include "ps5_agc_backend.h"

#ifndef PS5_AGC_DIRECT_ONLY
#include "ggml-backend-impl.h"
#include "ggml-cpu.h"
#include "ggml.h"
#include "ggml-impl.h"
#endif

#include <dlfcn.h>
#include <ps5/kernel.h>
#include <sys/mman.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "media_gemm_agc.inc"
#include "media_im2col_agc.inc"
#include "stable_audio_gemm_f16_agc.inc"

#if defined(PS5_SANDBOX_APP) && !defined(PS5_APP_HAS_DSO_HANDLE)
extern "C"
{
    void *__dso_handle = nullptr;
}
#endif

namespace
{

constexpr uint32_t kRawBufferWord3 = 0x31016fac;
constexpr uint32_t kCompletionMarker = 0x50533543;
constexpr uint32_t kAcquireGcr = 0x4380;
constexpr uint32_t kAcquirePoll = 0xa0;
constexpr uint32_t kDispatchModifier = 0x8000;
constexpr uint32_t kUserDataRegister = 0x240;
constexpr size_t kAlignment = 0x4000;
constexpr size_t kMinScratch = 0x1000000;
constexpr size_t kDescriptorOffset = 0x4000;
constexpr size_t kControlOffset = 0x5000;
constexpr size_t kMarkerOffset = 0x6000;
constexpr size_t kCommandOffset = 0x8000;
constexpr size_t kCommandBytes = 0x4000;
constexpr size_t kDataOffset = 0x10000;
constexpr uintptr_t kAddress32Hint = 0x20480c000ULL;
#ifdef PS5_SD_GENERATE
constexpr size_t kInitialScratch = 1ULL * 1024 * 1024 * 1024;
#else
constexpr size_t kInitialScratch = kMinScratch;
#endif

extern "C"
{
    int64_t sceKernelGetDirectMemorySize(void);
    int32_t sceKernelAllocateDirectMemory(int64_t, int64_t, size_t, size_t, int, int64_t *);
    int32_t sceKernelMapDirectMemory(void **, size_t, int, int, int64_t, size_t);
    int32_t sceKernelReleaseDirectMemory(int64_t, size_t);
    int32_t sceKernelUsleep(uint32_t);
#ifdef PS5_SANDBOX_APP
    int sceKernelDebugOutText(int, const char *);
#endif
}

struct AgcRegister
{
    uint16_t offset;
    uint16_t padding;
    uint32_t value;
};

struct AgcCommandBuffer
{
    uint32_t *bottom;
    uint32_t *top;
    uint32_t *up;
    uint32_t *down;
    uintptr_t callback;
    void *user_data;
    uint32_t reserved_dwords;
    uint32_t padding;
};

struct AgcSubmitDescription
{
    void *words;
    uint32_t word_count;
    uint8_t flag;
    uint8_t padding[3];
};

using SetSh = uint32_t *(*)(void *, const void *, uint32_t);
using SetShDirect = uint32_t *(*)(void *, uint32_t, const uint32_t *, uint32_t);
using AcquireMem = uint32_t *(*)(void *, uint8_t, uint32_t, uint32_t, uint64_t, uint64_t, uint32_t);
using Dispatch = uint32_t *(*)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
using ReleaseMem = uint32_t *(*)(void *, uint8_t, int16_t, uint64_t, int8_t, void *, uint32_t,
                                 uint64_t, uint16_t, uint16_t, int8_t, int32_t);

struct AgcApi
{
    int (*init)(uint32_t);
    int (*create_shader)(void **, void *, void *);
    SetSh set_sh;
    SetShDirect set_sh_direct;
    AcquireMem acquire_mem;
    Dispatch dispatch;
    ReleaseMem release_mem;
    int (*suspend_point)(void);
    int (*submit)(const AgcSubmitDescription *);
};

#ifdef PS5_AGC_LINKED
extern "C"
{
    int sceAgcInit(uint32_t);
    int sceAgcCreateShader(void **, void *, void *);
    uint32_t *sceAgcDcbSetShRegistersIndirect(void *, const void *, uint32_t);
    uint32_t *sceAgcCbSetShRegisterRangeDirect(void *, uint32_t, const uint32_t *, uint32_t);
    uint32_t *sceAgcDcbAcquireMem(void *, uint8_t, uint32_t, uint32_t, uint64_t, uint64_t,
                                  uint32_t);
    uint32_t *sceAgcCbDispatch(void *, uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t *sceAgcCbReleaseMem(void *, uint8_t, int16_t, uint64_t, int8_t, void *, uint32_t,
                                 uint64_t, uint16_t, uint16_t, int8_t, int32_t);
    int sceAgcSuspendPoint(void);
    int sceAgcDriverSubmitDcb(const AgcSubmitDescription *);
}
#endif

struct Program
{
    AgcRegister *registers = nullptr;
    uint8_t count = 0;
};

struct Runtime
{
    void *agc_module = nullptr;
    void *driver_module = nullptr;
    AgcApi api = {};
    int64_t direct_start = -1;
    uint8_t *memory = nullptr;
    size_t memory_size = 0;
    Program gemm;
    Program im2col;
    Program stable_audio_gemm;
    uint64_t gemm_calls = 0;
    uint64_t im2col_calls = 0;
    bool initialized = false;
};

Runtime runtime;
volatile unsigned command_overflow;
uintptr_t released_scratch_address;

#ifndef PS5_AGC_DIRECT_ONLY
struct DirectBuffer
{
    int64_t physical;
    void *address;
    size_t mapped_size;
};

size_t direct_buffer_bytes;
#endif

template <typename T> bool load_symbol(void *module, const char *name, T *target)
{
    *reinterpret_cast<void **>(target) = dlsym(module, name);
    return *target != nullptr;
}

size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

uint8_t command_out_of_space(AgcCommandBuffer *, uint32_t, void *)
{
    command_overflow = 1;
    return 0;
}

void set_descriptor(uint32_t *descriptor, const void *buffer, size_t bytes)
{
    descriptor[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer));
    descriptor[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer) >> 32);
    descriptor[2] = static_cast<uint32_t>(bytes);
    descriptor[3] = kRawBufferWord3;
}

void flush_cache(const void *address, size_t bytes)
{
    const auto *at = static_cast<const uint8_t *>(address);
    const auto *end = at + bytes;
    for (; at < end; at += 64)
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}

void trace(const char *format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
#ifdef PS5_SANDBOX_APP
    sceKernelDebugOutText(0, line);
#else
    std::fputs(line, stdout);
    std::fflush(stdout);
#endif
}

bool shader_sections(const uint8_t *elf, size_t elf_size, const uint8_t **header,
                     size_t *header_size, const uint8_t **code, size_t *code_size)
{
    uint64_t section_offset;
    uint16_t entry_size, count, names_index;
    if (elf_size < 64 || std::memcmp(elf,
                                     "\x7f"
                                     "ELF",
                                     4) != 0)
        return false;
    std::memcpy(&section_offset, elf + 40, sizeof(section_offset));
    std::memcpy(&entry_size, elf + 58, sizeof(entry_size));
    std::memcpy(&count, elf + 60, sizeof(count));
    std::memcpy(&names_index, elf + 62, sizeof(names_index));
    if (!section_offset || entry_size < 64 || !count || names_index >= count ||
        section_offset > elf_size ||
        count > (elf_size - static_cast<size_t>(section_offset)) / entry_size)
        return false;

    const uint8_t *names_record = elf + section_offset + names_index * entry_size;
    uint64_t names_offset, names_size64;
    std::memcpy(&names_offset, names_record + 24, sizeof(names_offset));
    std::memcpy(&names_size64, names_record + 32, sizeof(names_size64));
    if (names_offset > elf_size || names_size64 > elf_size - names_offset)
        return false;
    const uint8_t *names = elf + names_offset;
    const size_t names_size = static_cast<size_t>(names_size64);

    *header = nullptr;
    *code = nullptr;
    *header_size = 0;
    *code_size = 0;
    for (uint16_t index = 0; index < count; ++index)
    {
        const uint8_t *record = elf + section_offset + index * entry_size;
        uint32_t name_offset;
        uint64_t offset, size;
        std::memcpy(&name_offset, record, sizeof(name_offset));
        std::memcpy(&offset, record + 24, sizeof(offset));
        std::memcpy(&size, record + 32, sizeof(size));
        if (name_offset >= names_size || offset > elf_size || size > elf_size - offset ||
            !std::memchr(names + name_offset, 0, names_size - name_offset))
            return false;
        const char *name = reinterpret_cast<const char *>(names + name_offset);
        if (std::strcmp(name, ".shader_header") == 0)
        {
            *header = elf + offset;
            *header_size = static_cast<size_t>(size);
        }
        else if (std::strcmp(name, ".shader_text") == 0)
        {
            *code = elf + offset;
            *code_size = static_cast<size_t>(size);
        }
    }
    return *header && *code && *header_size >= 96 && *code_size;
}

bool create_program(const uint8_t *package, size_t package_size, uint8_t *header_target,
                    uint8_t *code_target, Program *program)
{
    const uint8_t *header, *code;
    size_t header_size, code_size;
    if (!shader_sections(package, package_size, &header, &header_size, &code, &code_size) ||
        header_size > 0x1000 || code_size > 0x1000)
        return false;
    std::memcpy(header_target, header, header_size);
    std::memcpy(code_target, code, code_size);
    void *shader = nullptr;
    if (runtime.api.create_shader(&shader, header_target, code_target) != 0 ||
        shader != header_target || header_target[0x5a] != 0)
        return false;
    program->registers = *reinterpret_cast<AgcRegister **>(header_target + 32);
    program->count = header_target[92];
    return program->count == 8;
}

bool initialize_api()
{
    if (runtime.initialized)
        return true;
#ifdef PS5_AGC_LINKED
    runtime.api = {
        sceAgcInit,
        sceAgcCreateShader,
        sceAgcDcbSetShRegistersIndirect,
        sceAgcCbSetShRegisterRangeDirect,
        sceAgcDcbAcquireMem,
        sceAgcCbDispatch,
        sceAgcCbReleaseMem,
        sceAgcSuspendPoint,
        sceAgcDriverSubmitDcb,
    };
    const int init = runtime.api.init(8);
    if (init != 0)
    {
        std::printf("[ps5_agc] init failed rc=%08x\n", static_cast<unsigned>(init));
        return false;
    }
#else
    runtime.agc_module = dlopen("libSceAgc.sprx", RTLD_NOW | RTLD_LOCAL);
    runtime.driver_module = dlopen("libSceAgcDriver.sprx", RTLD_NOW | RTLD_LOCAL);
    if (!runtime.agc_module || !runtime.driver_module ||
        !load_symbol(runtime.agc_module, "sceAgcInit", &runtime.api.init) ||
        !load_symbol(runtime.agc_module, "sceAgcCreateShader", &runtime.api.create_shader) ||
        !load_symbol(runtime.agc_module, "sceAgcDcbSetShRegistersIndirect", &runtime.api.set_sh) ||
        !load_symbol(runtime.agc_module, "sceAgcCbSetShRegisterRangeDirect",
                     &runtime.api.set_sh_direct) ||
        !load_symbol(runtime.agc_module, "sceAgcDcbAcquireMem", &runtime.api.acquire_mem) ||
        !load_symbol(runtime.agc_module, "sceAgcCbDispatch", &runtime.api.dispatch) ||
        !load_symbol(runtime.agc_module, "sceAgcCbReleaseMem", &runtime.api.release_mem) ||
        !load_symbol(runtime.agc_module, "sceAgcSuspendPoint", &runtime.api.suspend_point) ||
        !load_symbol(runtime.driver_module, "sceAgcDriverSubmitDcb", &runtime.api.submit) ||
        runtime.api.init(8) != 0)
        return false;
#endif
    runtime.initialized = true;
    return true;
}

bool allocate_scratch(size_t required)
{
    if (runtime.memory && runtime.memory_size >= required)
        return true;
    if (runtime.memory)
    {
        munmap(runtime.memory, runtime.memory_size);
        sceKernelReleaseDirectMemory(runtime.direct_start, runtime.memory_size);
        runtime.memory = nullptr;
        runtime.memory_size = 0;
        runtime.direct_start = -1;
    }
    const size_t size = align_up(std::max(required, kMinScratch), kAlignment);
    if (sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), size, kAlignment, 12,
                                      &runtime.direct_start) != 0)
        return false;
    runtime.memory = reinterpret_cast<uint8_t *>(kAddress32Hint);
    const int map_result =
        sceKernelMapDirectMemory(reinterpret_cast<void **>(&runtime.memory), size, 0x33, 0,
                                 runtime.direct_start, kAlignment);
    const unsigned mapped_high =
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(runtime.memory) >> 32);
    if (map_result != 0 || mapped_high != 2u)
    {
        if (map_result == 0)
            munmap(runtime.memory, size);
        sceKernelReleaseDirectMemory(runtime.direct_start, size);
        runtime.memory = nullptr;
        runtime.direct_start = -1;
        trace("[ps5_agc] rejected scratch mapping rc=%d high=%u\n", map_result, mapped_high);
        return false;
    }
    released_scratch_address = 0;
    runtime.memory_size = size;
    std::memset(runtime.memory, 0, kDataOffset);
    if (!create_program(media_gemm_agc_package, media_gemm_agc_package_len, runtime.memory,
                        runtime.memory + 0x1000, &runtime.gemm) ||
        !create_program(media_im2col_agc_package, media_im2col_agc_package_len,
                        runtime.memory + 0x2000, runtime.memory + 0x3000, &runtime.im2col) ||
        !create_program(stable_audio_gemm_f16_agc_package, stable_audio_gemm_f16_agc_package_len,
                        runtime.memory + 0xc000, runtime.memory + 0xd000,
                        &runtime.stable_audio_gemm))
        return false;
    trace("[ps5_agc] scratch required=%zu mapped=%zu address=%p physical=%llx\n", required,
          runtime.memory_size, runtime.memory,
          static_cast<unsigned long long>(runtime.direct_start));
    return true;
}

bool submit(Program program, uint32_t groups_x, uint32_t groups_y, uint32_t *descriptors,
            const void *acquire_base, size_t acquire_bytes, volatile uint32_t *marker,
            size_t used_bytes)
{
    auto *words = reinterpret_cast<uint32_t *>(runtime.memory + kCommandOffset);
    AgcCommandBuffer command = {words,
                                words + kCommandBytes / sizeof(*words),
                                words,
                                words + kCommandBytes / sizeof(*words),
                                reinterpret_cast<uintptr_t>(command_out_of_space),
                                nullptr,
                                0,
                                0};
    uint32_t user_data[3] = {0, 0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(descriptors))};
    command_overflow = 0;
    *marker = 0;
    if (!runtime.api.set_sh(&command, program.registers, program.count) ||
        !runtime.api.set_sh_direct(&command, kUserDataRegister, user_data, 3) ||
        !runtime.api.acquire_mem(&command, 0, 0, kAcquireGcr,
                                 reinterpret_cast<uint64_t>(acquire_base), acquire_bytes,
                                 kAcquirePoll) ||
        !runtime.api.dispatch(&command, groups_x, groups_y, 1, kDispatchModifier) ||
        !runtime.api.release_mem(&command, 40, 0x30c, 0, 0, const_cast<uint32_t *>(marker), 1,
                                 kCompletionMarker, 0, 0, 0, 0) ||
        command_overflow)
        return false;

    const uint32_t word_count = static_cast<uint32_t>(command.up - words);
    AgcSubmitDescription description = {words, word_count, 0, {0, 0, 0}};
    flush_cache(runtime.memory, used_bytes);
    if (runtime.api.submit(&description) != 0 || runtime.api.suspend_point() != 0)
        return false;
    for (unsigned poll = 0; poll < 120000; ++poll)
    {
        flush_cache(const_cast<const uint32_t *>(marker), sizeof(*marker));
        if (*marker == kCompletionMarker)
            return true;
        sceKernelUsleep(1000);
    }
    return false;
}

bool fits_u32(size_t value)
{
    return value <= std::numeric_limits<uint32_t>::max();
}

#ifndef PS5_AGC_DIRECT_ONLY

#ifdef PS5_SD_GENERATE
extern "C" bool ps5SdIsDirectArenaRange(const void *, size_t);
#endif

void direct_buffer_free(ggml_backend_buffer_t buffer)
{
    auto *memory = static_cast<DirectBuffer *>(buffer->context);
    munmap(memory->address, memory->mapped_size);
    sceKernelReleaseDirectMemory(memory->physical, memory->mapped_size);
    direct_buffer_bytes -= memory->mapped_size;
    delete memory;
}

void *direct_buffer_base(ggml_backend_buffer_t buffer)
{
    return static_cast<DirectBuffer *>(buffer->context)->address;
}

void direct_buffer_memset(ggml_backend_buffer_t, ggml_tensor *tensor, uint8_t value, size_t offset,
                          size_t size)
{
    std::memset(static_cast<uint8_t *>(tensor->data) + offset, value, size);
}

void direct_buffer_set(ggml_backend_buffer_t, ggml_tensor *tensor, const void *data, size_t offset,
                       size_t size)
{
    std::memcpy(static_cast<uint8_t *>(tensor->data) + offset, data, size);
}

void direct_buffer_get(ggml_backend_buffer_t, const ggml_tensor *tensor, void *data, size_t offset,
                       size_t size)
{
    std::memcpy(data, static_cast<const uint8_t *>(tensor->data) + offset, size);
}

bool direct_buffer_copy(ggml_backend_buffer_t, const ggml_tensor *source, ggml_tensor *destination)
{
    if (!ggml_backend_buffer_is_host(source->buffer))
        return false;
    std::memcpy(destination->data, source->data, ggml_nbytes(source));
    return true;
}

void direct_buffer_clear(ggml_backend_buffer_t buffer, uint8_t value)
{
    std::memset(direct_buffer_base(buffer), value, buffer->size);
}

ggml_backend_buffer_i direct_buffer_interface = {
    direct_buffer_free, direct_buffer_base,  nullptr, direct_buffer_memset,
    direct_buffer_set,  direct_buffer_get,   nullptr, nullptr,
    direct_buffer_copy, direct_buffer_clear, nullptr};

const char *direct_buffer_name(ggml_backend_buffer_type_t)
{
    return "PS5AGC_Direct";
}

ggml_backend_buffer_t direct_buffer_allocate(ggml_backend_buffer_type_t type, size_t size)
{
    const size_t mapped_size = align_up(size, kAlignment);
    auto *memory = new DirectBuffer{-1, nullptr, mapped_size};
    const int64_t total = sceKernelGetDirectMemorySize();
    if (total <= 0 ||
        sceKernelAllocateDirectMemory(0, total, mapped_size, kAlignment, 12, &memory->physical) !=
            0 ||
        sceKernelMapDirectMemory(&memory->address, mapped_size, 0x33, 0, memory->physical,
                                 kAlignment) != 0)
    {
        if (memory->physical >= 0)
            sceKernelReleaseDirectMemory(memory->physical, mapped_size);
        delete memory;
        return nullptr;
    }
    direct_buffer_bytes += mapped_size;
    trace("[ps5_agc] direct_buffer bytes=%zu mapped=%zu address=%p physical=%llx\n", size,
          mapped_size, memory->address, static_cast<unsigned long long>(memory->physical));
    return ggml_backend_buffer_init(type, direct_buffer_interface, memory, size);
}

size_t direct_buffer_alignment(ggml_backend_buffer_type_t)
{
    return TENSOR_ALIGNMENT;
}

bool direct_buffer_is_host(ggml_backend_buffer_type_t)
{
    return true;
}

ggml_backend_buffer_type_t direct_buffer_type()
{
    static ggml_backend_buffer_type type = {{direct_buffer_name, direct_buffer_allocate,
                                             direct_buffer_alignment, nullptr, nullptr,
                                             direct_buffer_is_host},
                                            nullptr,
                                            nullptr};
    return &type;
}

bool is_gpu_addressable_tensor(const ggml_tensor *tensor)
{
    if (!tensor || !tensor->data)
        return false;
    if (tensor->buffer && ggml_backend_buffer_get_type(tensor->buffer) == direct_buffer_type())
        return true;
#ifdef PS5_SD_GENERATE
    return ps5SdIsDirectArenaRange(tensor->data, ggml_nbytes(tensor));
#else
    return false;
#endif
}

void pointer_span(const void *first, size_t first_bytes, const void *second, size_t second_bytes,
                  const void **base, size_t *bytes)
{
    const uintptr_t first_begin = reinterpret_cast<uintptr_t>(first);
    const uintptr_t second_begin = reinterpret_cast<uintptr_t>(second);
    const uintptr_t begin = std::min(first_begin, second_begin);
    const uintptr_t end = std::max(first_begin + first_bytes, second_begin + second_bytes);
    *base = reinterpret_cast<const void *>(begin);
    *bytes = end - begin;
}

bool run_gemm(ggml_tensor *dst)
{
    const ggml_tensor *weights = dst->src[0];
    const ggml_tensor *input = dst->src[1];
    const size_t rows = static_cast<size_t>(weights->ne[1]);
    const size_t columns = static_cast<size_t>(weights->ne[0]);
    const size_t batch = static_cast<size_t>(input->ne[1] * input->ne[2] * input->ne[3]);
    const size_t input_bytes = ggml_nbytes(input);
    const size_t weight_bytes = ggml_nbytes(weights);
    const size_t output_bytes = ggml_nbytes(dst);
    const bool direct = is_gpu_addressable_tensor(weights) && is_gpu_addressable_tensor(input) &&
                        is_gpu_addressable_tensor(dst);
    size_t input_offset = kDataOffset;
    size_t weight_offset = align_up(input_offset + input_bytes, 256);
    size_t output_offset = align_up(weight_offset + weight_bytes, 256);
    size_t required = output_offset + output_bytes;
    if (!fits_u32(input_bytes) || !fits_u32(weight_bytes) || !fits_u32(output_bytes) ||
        !allocate_scratch(direct ? kDataOffset : required))
        return false;

    void *gpu_input = direct ? input->data : runtime.memory + input_offset;
    void *gpu_weights = direct ? weights->data : runtime.memory + weight_offset;
    void *gpu_output = direct ? dst->data : runtime.memory + output_offset;
    if (direct)
    {
        flush_cache(gpu_input, input_bytes);
        flush_cache(gpu_weights, weight_bytes);
    }
    else
    {
        std::memcpy(gpu_input, input->data, input_bytes);
        std::memcpy(gpu_weights, weights->data, weight_bytes);
        std::memset(gpu_output, 0, output_bytes);
    }

    auto *descriptors = reinterpret_cast<uint32_t *>(runtime.memory + kDescriptorOffset);
    auto *control = reinterpret_cast<uint32_t *>(runtime.memory + kControlOffset);
    auto *marker = reinterpret_cast<volatile uint32_t *>(runtime.memory + kMarkerOffset);
    set_descriptor(descriptors, gpu_input, input_bytes);
    set_descriptor(descriptors + 4, gpu_weights, weight_bytes);
    set_descriptor(descriptors + 8, gpu_output, output_bytes);
    set_descriptor(descriptors + 12, control, 7 * sizeof(uint32_t));
    const uint32_t values[7] = {
        static_cast<uint32_t>(rows),           static_cast<uint32_t>(columns),
        static_cast<uint32_t>(batch),          static_cast<uint32_t>(columns),
        static_cast<uint32_t>(columns),        static_cast<uint32_t>(rows),
        input->type == GGML_TYPE_F16 ? 1u : 0u};
    std::memcpy(control, values, sizeof(values));
    trace("[ps5_agc] gemm direct=%d tensor=%s weight=%s rows=%zu columns=%zu batch=%zu input=%zu "
          "weights=%zu output=%zu gpu=%p,%p,%p\n",
          direct, dst->name, weights->name, rows, columns, batch, input_bytes, weight_bytes,
          output_bytes, gpu_input, gpu_weights, gpu_output);
    const void *acquire_base = control;
    size_t acquire_bytes = weight_offset + weight_bytes - kControlOffset;
    size_t used_bytes = required;
    if (direct)
    {
        pointer_span(gpu_input, input_bytes, gpu_weights, weight_bytes, &acquire_base,
                     &acquire_bytes);
        used_bytes = kDataOffset;
    }
    if (!submit(runtime.gemm, static_cast<uint32_t>(rows), static_cast<uint32_t>(batch),
                descriptors, acquire_base, acquire_bytes, marker, used_bytes))
        return false;
    flush_cache(gpu_output, output_bytes);
    if (!direct)
        std::memcpy(dst->data, gpu_output, output_bytes);
    ++runtime.gemm_calls;
    return true;
}

bool run_im2col(ggml_tensor *dst)
{
    const ggml_tensor *kernel = dst->src[0];
    const ggml_tensor *input = dst->src[1];
    const int32_t *params = reinterpret_cast<const int32_t *>(dst->op_params);
    const size_t input_bytes = ggml_nbytes(input);
    const size_t output_bytes = ggml_nbytes(dst);
    const bool direct = is_gpu_addressable_tensor(input) && is_gpu_addressable_tensor(dst);
    const size_t input_offset = kDataOffset;
    const size_t output_offset = align_up(input_offset + input_bytes, 256);
    const size_t required = output_offset + output_bytes;
    if (!fits_u32(input_bytes) || !fits_u32(output_bytes) ||
        !allocate_scratch(direct ? kDataOffset : required))
        return false;

    void *gpu_input = direct ? input->data : runtime.memory + input_offset;
    void *gpu_output = direct ? dst->data : runtime.memory + output_offset;
    if (direct)
        flush_cache(gpu_input, input_bytes);
    else
    {
        std::memcpy(gpu_input, input->data, input_bytes);
        std::memset(gpu_output, 0, output_bytes);
    }
    auto *descriptors = reinterpret_cast<uint32_t *>(runtime.memory + kDescriptorOffset);
    auto *control = reinterpret_cast<uint32_t *>(runtime.memory + kControlOffset);
    auto *marker = reinterpret_cast<volatile uint32_t *>(runtime.memory + kMarkerOffset);
    set_descriptor(descriptors, gpu_input, input_bytes);
    set_descriptor(descriptors + 4, gpu_output, output_bytes);
    set_descriptor(descriptors + 8, control, 16 * sizeof(uint32_t));
    const uint32_t values[16] = {static_cast<uint32_t>(input->ne[0]),
                                 static_cast<uint32_t>(input->ne[1]),
                                 static_cast<uint32_t>(input->ne[2]),
                                 static_cast<uint32_t>(dst->ne[1]),
                                 static_cast<uint32_t>(dst->ne[2]),
                                 static_cast<uint32_t>(kernel->ne[0]),
                                 static_cast<uint32_t>(kernel->ne[1]),
                                 static_cast<uint32_t>(input->ne[3]),
                                 static_cast<uint32_t>(params[0]),
                                 static_cast<uint32_t>(params[1]),
                                 static_cast<uint32_t>(params[2]),
                                 static_cast<uint32_t>(params[3]),
                                 static_cast<uint32_t>(params[4]),
                                 static_cast<uint32_t>(params[5]),
                                 static_cast<uint32_t>(input->nb[2] / sizeof(float)),
                                 static_cast<uint32_t>(input->nb[3] / sizeof(float))};
    std::memcpy(control, values, sizeof(values));
    const uint32_t groups = static_cast<uint32_t>((output_bytes / 4 + 63) / 64);
    trace(
        "[ps5_agc] im2col direct=%d tensor=%s kernel=%s groups=%u input=%zu output=%zu gpu=%p,%p\n",
        direct, dst->name, kernel->name, groups, input_bytes, output_bytes, gpu_input, gpu_output);
    if (!submit(runtime.im2col, groups, 1, descriptors, direct ? gpu_input : control,
                direct ? input_bytes : input_offset + input_bytes - kControlOffset, marker,
                direct ? kDataOffset : required))
        return false;
    flush_cache(gpu_output, output_bytes);
    if (!direct)
        std::memcpy(dst->data, gpu_output, output_bytes);
    ++runtime.im2col_calls;
    return true;
}

const char *backend_name(ggml_backend_t)
{
    return "PS5AGC";
}

void backend_free(ggml_backend_t backend)
{
    delete backend;
}

ggml_status graph_compute(ggml_backend_t, ggml_cgraph *graph)
{
    if (!initialize_api())
        return GGML_STATUS_FAILED;
    for (int index = 0; index < graph->n_nodes; ++index)
    {
        ggml_tensor *node = graph->nodes[index];
        if ((node->flags & GGML_TENSOR_FLAG_COMPUTE) == 0)
            continue;
        switch (node->op)
        {
        case GGML_OP_NONE:
        case GGML_OP_RESHAPE:
        case GGML_OP_VIEW:
        case GGML_OP_PERMUTE:
        case GGML_OP_TRANSPOSE:
            break;
        case GGML_OP_MUL_MAT:
            if (!run_gemm(node))
                return GGML_STATUS_FAILED;
            break;
        case GGML_OP_IM2COL:
            if (!run_im2col(node))
                return GGML_STATUS_FAILED;
            break;
        default:
            return GGML_STATUS_FAILED;
        }
    }
    return GGML_STATUS_SUCCESS;
}

ggml_backend_i backend_interface = {backend_name,  backend_free, nullptr, nullptr, nullptr, nullptr,
                                    nullptr,       nullptr,      nullptr, nullptr, nullptr, nullptr,
                                    graph_compute, nullptr,      nullptr, nullptr};

ggml_guid_t backend_guid()
{
    static ggml_guid guid = {0x50, 0x53, 0x35, 0x41, 0x47, 0x43, 0x20, 0x47,
                             0x50, 0x55, 0x20, 0x42, 0x41, 0x43, 0x4b, 0x31};
    return &guid;
}

const char *device_name(ggml_backend_dev_t)
{
    return "PS5AGC0";
}
const char *device_description(ggml_backend_dev_t)
{
    return "PlayStation 5 AGC GPU";
}
void device_memory(ggml_backend_dev_t, size_t *free, size_t *total)
{
    const int64_t direct_total = sceKernelGetDirectMemorySize();
    *total = direct_total > 0 ? static_cast<size_t>(direct_total) : 0;
    const size_t used = direct_buffer_bytes + runtime.memory_size;
    *free = used < *total ? *total - used : 0;
}
enum ggml_backend_dev_type device_type(ggml_backend_dev_t)
{
    return GGML_BACKEND_DEVICE_TYPE_GPU;
}
void device_props(ggml_backend_dev_t dev, ggml_backend_dev_props *props)
{
    props->name = device_name(dev);
    props->description = device_description(dev);
    props->type = device_type(dev);
    props->device_id = nullptr;
    device_memory(dev, &props->memory_free, &props->memory_total);
    props->caps = {false, false, true, false};
}

ggml_backend_t device_init(ggml_backend_dev_t dev, const char *)
{
    if (!initialize_api() || !allocate_scratch(kInitialScratch))
        return nullptr;
    return new ggml_backend{backend_guid(), backend_interface, dev, nullptr};
}

ggml_backend_buffer_type_t device_buffer_type(ggml_backend_dev_t)
{
    return direct_buffer_type();
}

ggml_backend_buffer_t buffer_from_host(ggml_backend_dev_t, void *ptr, size_t size, size_t)
{
    return ggml_backend_cpu_buffer_from_ptr(ptr, size);
}

bool supports_op(ggml_backend_dev_t, const ggml_tensor *op)
{
    switch (op->op)
    {
    case GGML_OP_NONE:
    case GGML_OP_RESHAPE:
    case GGML_OP_VIEW:
    case GGML_OP_PERMUTE:
    case GGML_OP_TRANSPOSE:
        return true;
    case GGML_OP_MUL_MAT:
    {
        const ggml_tensor *weights = op->src[0];
        const ggml_tensor *input = op->src[1];
        return weights && input && weights->type == GGML_TYPE_F16 &&
               (input->type == GGML_TYPE_F32 || input->type == GGML_TYPE_F16) &&
               op->type == GGML_TYPE_F32 && weights->ne[2] == 1 && weights->ne[3] == 1 &&
               input->ne[0] == weights->ne[0] && ggml_is_contiguous(weights) &&
               ggml_is_contiguous(input) && ggml_is_contiguous(op);
    }
    case GGML_OP_IM2COL:
    {
        const ggml_tensor *input = op->src[1];
        const int32_t *params = reinterpret_cast<const int32_t *>(op->op_params);
        return input && input->type == GGML_TYPE_F32 && op->type == GGML_TYPE_F16 &&
               params[6] == 1 && ggml_is_contiguous(input) && ggml_is_contiguous(op);
    }
    default:
        return false;
    }
}

bool supports_buffer(ggml_backend_dev_t, ggml_backend_buffer_type_t buffer)
{
    return buffer == direct_buffer_type() || ggml_backend_buft_is_host(buffer);
}

ggml_backend_device_i device_interface = {device_name,
                                          device_description,
                                          device_memory,
                                          device_type,
                                          device_props,
                                          device_init,
                                          device_buffer_type,
                                          nullptr,
                                          buffer_from_host,
                                          supports_op,
                                          supports_buffer,
                                          nullptr,
                                          nullptr,
                                          nullptr,
                                          nullptr};

const char *registry_name(ggml_backend_reg_t)
{
    return "PS5AGC";
}
size_t registry_device_count(ggml_backend_reg_t)
{
    return 1;
}

ggml_backend_dev_t registry_device(ggml_backend_reg_t reg, size_t index)
{
    GGML_ASSERT(index == 0);
    static ggml_backend_device device = {device_interface, reg, nullptr};
    return &device;
}

void *registry_proc(ggml_backend_reg_t, const char *)
{
    return nullptr;
}

ggml_backend_reg_i registry_interface = {registry_name, registry_device_count, registry_device,
                                         registry_proc};

} // namespace

extern "C" ggml_backend_reg_t ggml_backend_ps5agc_reg(void)
{
    static ggml_backend_reg registry = {GGML_BACKEND_API_VERSION, registry_interface, nullptr};
    return &registry;
}

extern "C" ggml_backend_t ggml_backend_ps5agc_init(void)
{
    return device_init(registry_device(ggml_backend_ps5agc_reg(), 0), nullptr);
}
#endif

#ifdef PS5_AGC_DIRECT_ONLY
} // namespace
#endif

extern "C" int ps5_agc_backend_reserve(void)
{
#ifdef PS5_AGC_STABLE_AUDIO
    // Stable Audio keeps OpenCL buffers inside this arena, so it must never
    // move while a generation is active.
    constexpr size_t audio_reservation = 256ULL * 1024 * 1024;
    constexpr size_t reservation =
        kInitialScratch > audio_reservation ? kInitialScratch : audio_reservation;
#else
    constexpr size_t reservation = kMinScratch;
#endif
    return initialize_api() && allocate_scratch(reservation) ? 0 : -1;
}

extern "C" int ps5_agc_backend_release_scratch(void)
{
    released_scratch_address = 0;
#ifndef PS5_AGC_DIRECT_ONLY
    if (direct_buffer_bytes != 0)
        return -1;
#endif
    if (!runtime.memory)
        return 0;
    const uintptr_t address = reinterpret_cast<uintptr_t>(runtime.memory);
    const int unmap_result = munmap(runtime.memory, runtime.memory_size);
    const int release_result =
        sceKernelReleaseDirectMemory(runtime.direct_start, runtime.memory_size);
    runtime.direct_start = -1;
    runtime.memory = nullptr;
    runtime.memory_size = 0;
    runtime.gemm = {};
    runtime.im2col = {};
    runtime.stable_audio_gemm = {};
    if (unmap_result != 0 || release_result != 0)
        return -1;
    released_scratch_address = address;
    return 0;
}

extern "C" void *ps5_agc_backend_take_released_scratch_address(void)
{
    void *address = reinterpret_cast<void *>(released_scratch_address);
    released_scratch_address = 0;
    return address;
}

extern "C" void ps5_agc_backend_print_stats(void)
{
    std::printf("[ps5_agc] gemm_calls=%llu im2col_calls=%llu scratch_bytes=%zu\n",
                static_cast<unsigned long long>(runtime.gemm_calls),
                static_cast<unsigned long long>(runtime.im2col_calls), runtime.memory_size);
}

extern "C" int ps5_agc_gemm_f16_f32(const uint16_t *a, size_t a_elements, const uint16_t *b,
                                    size_t b_elements, float *c, uint32_t rows_a,
                                    uint32_t columns_b, uint32_t reduction, uint32_t stride_a,
                                    uint32_t stride_b, uint32_t transpose_b)
{
    if (!a || !b || !c || !rows_a || !columns_b || !reduction || stride_a < reduction ||
        (transpose_b ? stride_b < reduction : stride_b < columns_b) || !initialize_api())
    {
        trace("[ps5_agc] gemm rejected rows=%u columns=%u reduction=%u stride=%u,%u transpose=%u\n",
              rows_a, columns_b, reduction, stride_a, stride_b, transpose_b);
        return -1;
    }
    const size_t a_needed = static_cast<size_t>(rows_a - 1) * stride_a + reduction;
    const size_t b_needed = transpose_b ? static_cast<size_t>(columns_b - 1) * stride_b + reduction
                                        : static_cast<size_t>(reduction - 1) * stride_b + columns_b;
    if (a_elements < a_needed || b_elements < b_needed)
    {
        trace("[ps5_agc] gemm bounds a=%zu/%zu b=%zu/%zu\n", a_elements, a_needed, b_elements,
              b_needed);
        return -1;
    }
    const size_t a_bytes = a_elements * sizeof(*a);
    std::vector<uint16_t> packed_b;
    if (!transpose_b)
    {
        packed_b.resize(static_cast<size_t>(columns_b) * reduction);
        for (uint32_t column = 0; column < columns_b; ++column)
            for (uint32_t inner = 0; inner < reduction; ++inner)
                packed_b[static_cast<size_t>(column) * reduction + inner] =
                    b[static_cast<size_t>(inner) * stride_b + column];
    }
    const uint16_t *b_upload = transpose_b ? b : packed_b.data();
    const size_t b_upload_elements = transpose_b ? b_elements : packed_b.size();
    const size_t b_bytes = b_upload_elements * sizeof(*b_upload);
    const size_t c_elements = static_cast<size_t>(rows_a) * columns_b;
    const size_t c_bytes = c_elements * sizeof(*c);
    const size_t a_offset = kDataOffset;
    const size_t b_offset = align_up(a_offset + a_bytes, 256);
    const size_t c_offset = align_up(b_offset + b_bytes, 256);
    const size_t required = c_offset + c_bytes;
    if (!fits_u32(a_bytes) || !fits_u32(b_bytes) || !fits_u32(c_bytes) ||
        !allocate_scratch(required))
    {
        trace("[ps5_agc] gemm allocation failed required=%zu\n", required);
        return -1;
    }

    void *gpu_a = runtime.memory + a_offset;
    void *gpu_b = runtime.memory + b_offset;
    void *gpu_c = runtime.memory + c_offset;
    std::memcpy(gpu_a, a, a_bytes);
    std::memcpy(gpu_b, b_upload, b_bytes);
    std::memset(gpu_c, 0, c_bytes);
    auto *descriptors = reinterpret_cast<uint32_t *>(runtime.memory + kDescriptorOffset);
    auto *control = reinterpret_cast<uint32_t *>(runtime.memory + kControlOffset);
    auto *marker = reinterpret_cast<volatile uint32_t *>(runtime.memory + kMarkerOffset);
    set_descriptor(descriptors, gpu_a, a_bytes);
    set_descriptor(descriptors + 4, gpu_b, b_bytes);
    set_descriptor(descriptors + 8, gpu_c, c_bytes);
    set_descriptor(descriptors + 12, control, 7 * sizeof(uint32_t));
    const uint32_t values[7] = {
        columns_b, reduction, rows_a, stride_a, transpose_b ? stride_b : reduction, columns_b, 1};
    std::memcpy(control, values, sizeof(values));
    if (!submit(runtime.gemm, columns_b, rows_a, descriptors, control,
                b_offset + b_bytes - kControlOffset, marker, required))
    {
        trace("[ps5_agc] gemm submit failed rows=%u columns=%u reduction=%u stride=%u,%u "
              "transpose=%u\n",
              rows_a, columns_b, reduction, stride_a, stride_b, transpose_b);
        return -1;
    }
    flush_cache(gpu_c, c_bytes);
    std::memcpy(c, gpu_c, c_bytes);
    ++runtime.gemm_calls;
    return 0;
}
