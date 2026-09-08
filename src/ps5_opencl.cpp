#include <CL/cl.h>

#include "ps5_agc_gemm.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

struct _cl_platform_id
{
};
struct _cl_device_id
{
};
struct _cl_context
{
    int refs = 1;
};
struct _cl_command_queue
{
    cl_context context;
    int refs = 1;
};
struct _cl_program
{
    std::string source;
    int refs = 1;
};
struct _cl_event
{
    uint64_t start_ns = 0;
    uint64_t end_ns = 0;
    int refs = 1;
};
struct _cl_mem
{
    uint8_t *data = nullptr;
    size_t size = 0;
    size_t image_width = 0;
    size_t image_height = 0;
    size_t image_depth = 0;
    size_t pixel_bytes = 0;
    cl_mem parent = nullptr;
    int refs = 1;
    bool owned = false;
};
struct KernelArg
{
    std::vector<uint8_t> bytes;
    size_t local_bytes = 0;
};
struct _cl_kernel
{
    std::string name;
    std::vector<KernelArg> args;
    int refs = 1;
};

namespace
{

_cl_platform_id platform_object;
_cl_device_id device_object;
const char *active_operation = "startup";
std::atomic<bool> reported_nonfinite{false};

uint64_t now_ns()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

template <typename T> cl_int write_info(const T &value, size_t size, void *out, size_t *written)
{
    if (written)
        *written = sizeof(T);
    if (out)
    {
        if (size < sizeof(T))
            return CL_INVALID_VALUE;
        std::memcpy(out, &value, sizeof(T));
    }
    return CL_SUCCESS;
}

cl_int write_string(const char *value, size_t size, void *out, size_t *written)
{
    const size_t needed = std::strlen(value) + 1;
    if (written)
        *written = needed;
    if (out)
    {
        if (size < needed)
            return CL_INVALID_VALUE;
        std::memcpy(out, value, needed);
    }
    return CL_SUCCESS;
}

float half_to_float(uint16_t h)
{
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    uint32_t exponent = (h >> 10) & 0x1fu;
    uint32_t mantissa = h & 0x3ffu;
    uint32_t bits;
    if (exponent == 0)
    {
        if (mantissa == 0)
            bits = sign;
        else
        {
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x400u) == 0)
            {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3ffu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    }
    else if (exponent == 31)
    {
        bits = sign | 0x7f800000u | (mantissa << 13);
    }
    else
    {
        bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    }
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint16_t float_to_half(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const uint32_t mantissa = bits & 0x7fffffu;
    int exponent = static_cast<int>((bits >> 23) & 0xffu) - 127 + 15;
    if (exponent <= 0)
    {
        if (exponent < -10)
            return static_cast<uint16_t>(sign);
        uint32_t sub = (mantissa | 0x800000u) >> (1 - exponent);
        return static_cast<uint16_t>(sign | ((sub + 0x1000u) >> 13));
    }
    if (exponent >= 31)
        return static_cast<uint16_t>(sign | (mantissa ? 0x7e00u : 0x7c00u));
    uint32_t rounded = mantissa + 0x1000u;
    if (rounded & 0x800000u)
    {
        rounded = 0;
        if (++exponent >= 31)
            return static_cast<uint16_t>(sign | 0x7c00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | (rounded >> 13));
}

float load_value(const cl_mem memory, size_t index)
{
    return half_to_float(reinterpret_cast<const uint16_t *>(memory->data)[index]);
}

void store_value(cl_mem memory, size_t index, float value)
{
    if ((!std::isfinite(value) || std::fabs(value) > 65504.0f) &&
        !reported_nonfinite.exchange(true))
        std::fprintf(stderr, "[ps5cl] first invalid FP16 value operation=%s index=%zu value=%g\n",
                     active_operation, index, value);
    reinterpret_cast<uint16_t *>(memory->data)[index] = float_to_half(value);
}

float load_float(const cl_mem memory, size_t index)
{
    return reinterpret_cast<const float *>(memory->data)[index];
}

void store_float(cl_mem memory, size_t index, float value)
{
    reinterpret_cast<float *>(memory->data)[index] = value;
}

template <typename T> T kernel_arg(cl_kernel kernel, size_t index)
{
    T value{};
    if (index < kernel->args.size() && kernel->args[index].bytes.size() >= sizeof(T))
        std::memcpy(&value, kernel->args[index].bytes.data(), sizeof(T));
    return value;
}

cl_mem mem_arg(cl_kernel kernel, size_t index)
{
    return kernel_arg<cl_mem>(kernel, index);
}

bool span_ok(cl_mem memory, size_t offset, size_t bytes)
{
    return memory && offset <= memory->size && bytes <= memory->size - offset;
}

void retain_mem(cl_mem memory)
{
    if (memory)
        ++memory->refs;
}

void release_mem(cl_mem memory)
{
    if (!memory || --memory->refs != 0)
        return;
    if (memory->parent)
        release_mem(memory->parent);
    if (memory->owned)
        std::free(memory->data);
    delete memory;
}

void set_event(cl_event *event, uint64_t start)
{
    if (!event)
        return;
    *event = new _cl_event;
    (*event)->start_ns = start;
    (*event)->end_ns = now_ns();
}

cl_int run_kernel(cl_kernel kernel);

} // namespace

extern "C" CL_API_ENTRY cl_int CL_API_CALL clGetPlatformIDs(cl_uint count,
                                                            cl_platform_id *platforms,
                                                            cl_uint *found)
{
    if (found)
        *found = 1;
    if (platforms)
    {
        if (count == 0)
            return CL_INVALID_VALUE;
        platforms[0] = &platform_object;
    }
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clGetDeviceIDs(cl_platform_id, cl_device_type,
                                                          cl_uint count, cl_device_id *devices,
                                                          cl_uint *found)
{
    if (found)
        *found = 1;
    if (devices)
    {
        if (count == 0)
            return CL_INVALID_VALUE;
        devices[0] = &device_object;
    }
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clGetPlatformInfo(cl_platform_id, cl_platform_info name,
                                                             size_t size, void *out,
                                                             size_t *written)
{
    switch (name)
    {
    case CL_PLATFORM_NAME:
        return write_string("ProsperoAI PS5 OpenCL compatibility", size, out, written);
    case CL_PLATFORM_VENDOR:
        return write_string("BlackBearReloaded", size, out, written);
    case CL_PLATFORM_VERSION:
        return write_string("OpenCL 1.2 ProsperoAI", size, out, written);
    case CL_PLATFORM_PROFILE:
        return write_string("FULL_PROFILE", size, out, written);
    case CL_PLATFORM_EXTENSIONS:
        return write_string("", size, out, written);
    default:
        return CL_INVALID_VALUE;
    }
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clGetDeviceInfo(cl_device_id, cl_device_info name,
                                                           size_t size, void *out, size_t *written)
{
    switch (name)
    {
    case CL_DEVICE_NAME:
        return write_string("PlayStation 5 AGC compatibility", size, out, written);
    case CL_DRIVER_VERSION:
        return write_string("ProsperoAI-AGC-1", size, out, written);
    case CL_DEVICE_EXTENSIONS:
        return write_string("cl_khr_fp16", size, out, written);
    case CL_DEVICE_MAX_WORK_GROUP_SIZE:
    {
        const size_t value = 64;
        return write_info(value, size, out, written);
    }
    case CL_DEVICE_LOCAL_MEM_SIZE:
    {
        const cl_ulong value = 64 * 1024;
        return write_info(value, size, out, written);
    }
    case CL_DEVICE_TYPE:
    {
        const cl_device_type value = CL_DEVICE_TYPE_GPU;
        return write_info(value, size, out, written);
    }
    default:
        return CL_INVALID_VALUE;
    }
}

extern "C" CL_API_ENTRY cl_context CL_API_CALL clCreateContext(
    const cl_context_properties *, cl_uint, const cl_device_id *,
    void(CL_CALLBACK *)(const char *, const void *, size_t, void *), void *, cl_int *error)
{
    if (error)
        *error = CL_SUCCESS;
    return new _cl_context;
}

extern "C" CL_API_ENTRY cl_command_queue CL_API_CALL
clCreateCommandQueue(cl_context context, cl_device_id, cl_command_queue_properties, cl_int *error)
{
    if (error)
        *error = context ? CL_SUCCESS : CL_INVALID_CONTEXT;
    return context ? new _cl_command_queue{context, 1} : nullptr;
}

extern "C" CL_API_ENTRY cl_mem CL_API_CALL clCreateBuffer(cl_context, cl_mem_flags flags,
                                                          size_t size, void *host, cl_int *error)
{
    if (!size || ((flags & (CL_MEM_COPY_HOST_PTR | CL_MEM_USE_HOST_PTR)) && !host))
    {
        if (error)
            *error = CL_INVALID_VALUE;
        return nullptr;
    }
    auto *memory = new _cl_mem;
    memory->size = size;
    const bool borrowed_weight =
        host && (flags & CL_MEM_COPY_HOST_PTR) && (flags & CL_MEM_READ_ONLY);
    if (borrowed_weight)
    {
        memory->data = static_cast<uint8_t *>(host);
    }
    else
    {
        memory->data = static_cast<uint8_t *>(std::calloc(1, size));
        memory->owned = true;
        if (!memory->data)
        {
            delete memory;
            if (error)
                *error = CL_OUT_OF_HOST_MEMORY;
            return nullptr;
        }
        if (host && (flags & (CL_MEM_COPY_HOST_PTR | CL_MEM_USE_HOST_PTR)))
            std::memcpy(memory->data, host, size);
    }
    if (error)
        *error = CL_SUCCESS;
    return memory;
}

extern "C" CL_API_ENTRY cl_mem CL_API_CALL clCreateImage(cl_context context, cl_mem_flags flags,
                                                         const cl_image_format *format,
                                                         const cl_image_desc *desc, void *host,
                                                         cl_int *error)
{
    if (!format || !desc || desc->image_width == 0)
    {
        if (error)
            *error = CL_INVALID_IMAGE_DESCRIPTOR;
        return nullptr;
    }
    size_t channels = format->image_channel_order == CL_RGBA ? 4
                      : format->image_channel_order == CL_RG ? 2
                                                             : 1;
    size_t component = format->image_channel_data_type == CL_FLOAT ? 4 : 2;
    size_t height = std::max<size_t>(desc->image_height, 1);
    size_t depth = std::max<size_t>(desc->image_depth, 1);
    size_t pixel_bytes = channels * component;
    cl_mem memory = clCreateBuffer(context, flags, desc->image_width * height * depth * pixel_bytes,
                                   host, error);
    if (memory)
    {
        memory->image_width = desc->image_width;
        memory->image_height = height;
        memory->image_depth = depth;
        memory->pixel_bytes = pixel_bytes;
    }
    return memory;
}

extern "C" CL_API_ENTRY cl_mem CL_API_CALL clCreateSubBuffer(cl_mem parent, cl_mem_flags,
                                                             cl_buffer_create_type type,
                                                             const void *info, cl_int *error)
{
    if (!parent || type != CL_BUFFER_CREATE_TYPE_REGION || !info)
    {
        if (error)
            *error = CL_INVALID_VALUE;
        return nullptr;
    }
    const auto *region = static_cast<const cl_buffer_region *>(info);
    if (!span_ok(parent, region->origin, region->size))
    {
        if (error)
            *error = CL_INVALID_VALUE;
        return nullptr;
    }
    retain_mem(parent);
    auto *memory = new _cl_mem;
    memory->data = parent->data + region->origin;
    memory->size = region->size;
    memory->parent = parent;
    if (error)
        *error = CL_SUCCESS;
    return memory;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clGetMemObjectInfo(cl_mem memory, cl_mem_info name,
                                                              size_t size, void *out,
                                                              size_t *written)
{
    if (!memory)
        return CL_INVALID_MEM_OBJECT;
    if (name == CL_MEM_SIZE)
        return write_info(memory->size, size, out, written);
    return CL_INVALID_VALUE;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clReleaseMemObject(cl_mem memory)
{
    if (!memory)
        return CL_INVALID_MEM_OBJECT;
    release_mem(memory);
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_program CL_API_CALL clCreateProgramWithSource(cl_context, cl_uint count,
                                                                         const char **strings,
                                                                         const size_t *lengths,
                                                                         cl_int *error)
{
    auto *program = new _cl_program;
    for (cl_uint i = 0; i < count; ++i)
        program->source.append(strings[i],
                               lengths && lengths[i] ? lengths[i] : std::strlen(strings[i]));
    if (error)
        *error = CL_SUCCESS;
    return program;
}

extern "C" CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithBinary(cl_context, cl_uint, const cl_device_id *, const size_t *,
                          const unsigned char **, cl_int *binary_status, cl_int *error)
{
    if (binary_status)
        *binary_status = CL_SUCCESS;
    if (error)
        *error = CL_SUCCESS;
    return new _cl_program;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clBuildProgram(cl_program, cl_uint, const cl_device_id *,
                                                          const char *,
                                                          void(CL_CALLBACK *)(cl_program, void *),
                                                          void *)
{
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clGetProgramBuildInfo(cl_program, cl_device_id,
                                                                 cl_program_build_info name,
                                                                 size_t size, void *out,
                                                                 size_t *written)
{
    if (name == CL_PROGRAM_BUILD_LOG)
        return write_string("", size, out, written);
    return CL_INVALID_VALUE;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clGetProgramInfo(cl_program, cl_program_info name,
                                                            size_t size, void *out, size_t *written)
{
    if (name == CL_PROGRAM_BINARY_SIZES)
    {
        const size_t zero = 0;
        return write_info(zero, size, out, written);
    }
    return CL_INVALID_VALUE;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clReleaseProgram(cl_program program)
{
    if (!program)
        return CL_INVALID_PROGRAM;
    if (--program->refs == 0)
        delete program;
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_kernel CL_API_CALL clCreateKernel(cl_program, const char *name,
                                                             cl_int *error)
{
    if (!name)
    {
        if (error)
            *error = CL_INVALID_VALUE;
        return nullptr;
    }
    if (error)
        *error = CL_SUCCESS;
    auto *kernel = new _cl_kernel;
    kernel->name = name;
    return kernel;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clSetKernelArg(cl_kernel kernel, cl_uint index,
                                                          size_t size, const void *value)
{
    if (!kernel)
        return CL_INVALID_KERNEL;
    if (kernel->args.size() <= index)
        kernel->args.resize(index + 1);
    auto &arg = kernel->args[index];
    arg.bytes.clear();
    arg.local_bytes = value ? 0 : size;
    if (value)
    {
        arg.bytes.resize(size);
        std::memcpy(arg.bytes.data(), value, size);
    }
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clGetKernelInfo(cl_kernel kernel, cl_kernel_info name,
                                                           size_t size, void *out, size_t *written)
{
    if (!kernel)
        return CL_INVALID_KERNEL;
    if (name == CL_KERNEL_FUNCTION_NAME)
        return write_string(kernel->name.c_str(), size, out, written);
    return CL_INVALID_VALUE;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clEnqueueNDRangeKernel(cl_command_queue,
                                                                  cl_kernel kernel, cl_uint,
                                                                  const size_t *, const size_t *,
                                                                  const size_t *, cl_uint,
                                                                  const cl_event *, cl_event *event)
{
    const uint64_t start = now_ns();
    active_operation = kernel ? kernel->name.c_str() : "null-kernel";
    const cl_int result = run_kernel(kernel);
    set_event(event, start);
    return result;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clReleaseKernel(cl_kernel kernel)
{
    if (!kernel)
        return CL_INVALID_KERNEL;
    if (--kernel->refs == 0)
        delete kernel;
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadBuffer(cl_command_queue, cl_mem memory,
                                                               cl_bool, size_t offset, size_t size,
                                                               void *out, cl_uint, const cl_event *,
                                                               cl_event *event)
{
    const uint64_t start = now_ns();
    if (!out || !span_ok(memory, offset, size))
        return CL_INVALID_VALUE;
    std::memcpy(out, memory->data + offset, size);
    set_event(event, start);
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteBuffer(cl_command_queue, cl_mem memory,
                                                                cl_bool, size_t offset, size_t size,
                                                                const void *input, cl_uint,
                                                                const cl_event *, cl_event *event)
{
    const uint64_t start = now_ns();
    if (!input || !span_ok(memory, offset, size))
        return CL_INVALID_VALUE;
    std::memcpy(memory->data + offset, input, size);
    set_event(event, start);
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBuffer(
    cl_command_queue, cl_mem source, cl_mem destination, size_t source_offset,
    size_t destination_offset, size_t size, cl_uint, const cl_event *, cl_event *event)
{
    const uint64_t start = now_ns();
    if (!span_ok(source, source_offset, size) || !span_ok(destination, destination_offset, size))
        return CL_INVALID_VALUE;
    std::memmove(destination->data + destination_offset, source->data + source_offset, size);
    set_event(event, start);
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBufferToImage(
    cl_command_queue, cl_mem source, cl_mem destination, size_t source_offset,
    const size_t origin[3], const size_t region[3], cl_uint, const cl_event *, cl_event *event)
{
    const uint64_t start = now_ns();
    if (!source || !destination || !origin || !region || !destination->pixel_bytes)
        return CL_INVALID_VALUE;
    const size_t row_bytes = region[0] * destination->pixel_bytes;
    size_t read_at = source_offset;
    for (size_t z = 0; z < region[2]; ++z)
        for (size_t y = 0; y < region[1]; ++y)
        {
            size_t write_at = (((origin[2] + z) * destination->image_height + origin[1] + y) *
                                   destination->image_width +
                               origin[0]) *
                              destination->pixel_bytes;
            if (!span_ok(source, read_at, row_bytes) || !span_ok(destination, write_at, row_bytes))
                return CL_INVALID_VALUE;
            std::memcpy(destination->data + write_at, source->data + read_at, row_bytes);
            read_at += row_bytes;
        }
    set_event(event, start);
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clEnqueueFillBuffer(cl_command_queue, cl_mem memory,
                                                               const void *pattern,
                                                               size_t pattern_size, size_t offset,
                                                               size_t size, cl_uint,
                                                               const cl_event *, cl_event *event)
{
    const uint64_t start = now_ns();
    if (!pattern || !pattern_size || size % pattern_size || !span_ok(memory, offset, size))
        return CL_INVALID_VALUE;
    for (size_t at = 0; at < size; at += pattern_size)
        std::memcpy(memory->data + offset + at, pattern, pattern_size);
    set_event(event, start);
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clFinish(cl_command_queue)
{
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clGetCommandQueueInfo(cl_command_queue queue,
                                                                 cl_command_queue_info name,
                                                                 size_t size, void *out,
                                                                 size_t *written)
{
    if (!queue)
        return CL_INVALID_COMMAND_QUEUE;
    if (name == CL_QUEUE_CONTEXT)
        return write_info(queue->context, size, out, written);
    return CL_INVALID_VALUE;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clGetContextInfo(cl_context context,
                                                            cl_context_info name, size_t size,
                                                            void *out, size_t *written)
{
    if (!context)
        return CL_INVALID_CONTEXT;
    if (name == CL_CONTEXT_DEVICES)
    {
        cl_device_id device = &device_object;
        return write_info(device, size, out, written);
    }
    return CL_INVALID_VALUE;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clReleaseCommandQueue(cl_command_queue queue)
{
    if (!queue)
        return CL_INVALID_COMMAND_QUEUE;
    if (--queue->refs == 0)
        delete queue;
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clReleaseContext(cl_context context)
{
    if (!context)
        return CL_INVALID_CONTEXT;
    if (--context->refs == 0)
        delete context;
    return CL_SUCCESS;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clGetEventProfilingInfo(cl_event event,
                                                                   cl_profiling_info name,
                                                                   size_t size, void *out,
                                                                   size_t *written)
{
    if (!event)
        return CL_INVALID_EVENT;
    if (name == CL_PROFILING_COMMAND_START)
        return write_info(static_cast<cl_ulong>(event->start_ns), size, out, written);
    if (name == CL_PROFILING_COMMAND_END)
        return write_info(static_cast<cl_ulong>(event->end_ns), size, out, written);
    return CL_INVALID_VALUE;
}

extern "C" CL_API_ENTRY cl_int CL_API_CALL clReleaseEvent(cl_event event)
{
    if (!event)
        return CL_INVALID_EVENT;
    if (--event->refs == 0)
        delete event;
    return CL_SUCCESS;
}

extern "C" int ps5cl_gemm(int half_storage, int transpose_a, int transpose_b, size_t m, size_t n,
                          size_t k, const void *alpha_ptr, cl_mem a, size_t a_offset, size_t lda,
                          cl_mem b, size_t b_offset, size_t ldb, const void *beta_ptr, cl_mem c,
                          size_t c_offset, size_t ldc)
{
    if (!half_storage || transpose_a || !a || !b || !c)
        return -1;
    active_operation = transpose_b ? "gemm_nt" : "gemm_nn";
    const float alpha = half_to_float(*static_cast<const uint16_t *>(alpha_ptr));
    const float beta = half_to_float(*static_cast<const uint16_t *>(beta_ptr));
    const size_t a_need = a_offset + (m - 1) * lda + k;
    const size_t b_need = b_offset + (transpose_b ? (n - 1) * ldb + k : (k - 1) * ldb + n);
    const size_t c_need = c_offset + (m - 1) * ldc + n;
    if (!span_ok(a, 0, a_need * 2) || !span_ok(b, 0, b_need * 2) || !span_ok(c, 0, c_need * 2))
        return -1;
    std::vector<float> product(m * n);
    bool host_product = n == 1;
#ifdef PS5_AGC_STABLE_AUDIO
    if (!host_product)
    {
        const auto *ap = reinterpret_cast<const uint16_t *>(a->data) + a_offset;
        const auto *bp = reinterpret_cast<const uint16_t *>(b->data) + b_offset;
        const size_t ae = (m - 1) * lda + k;
        const size_t be = transpose_b ? (n - 1) * ldb + k : (k - 1) * ldb + n;
        if (ps5_agc_gemm_f16_f32(ap, ae, bp, be, product.data(), static_cast<uint32_t>(m),
                                 static_cast<uint32_t>(n), static_cast<uint32_t>(k),
                                 static_cast<uint32_t>(lda), static_cast<uint32_t>(ldb),
                                 transpose_b) != 0)
            return -1;
    }
#else
    host_product = true;
#endif
    // ponytail: the current AGC shader drops single-column output; this tiny
    // projection stays on CPU until the shader gains an N=1 lane.
    if (host_product)
    {
#ifdef PS5CL_HOST_OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (size_t row = 0; row < m; ++row)
            for (size_t column = 0; column < n; ++column)
            {
                float sum = 0.0f;
                for (size_t inner = 0; inner < k; ++inner)
                {
                    const size_t ai = a_offset + row * lda + inner;
                    const size_t bi =
                        b_offset + (transpose_b ? column * ldb + inner : inner * ldb + column);
                    sum += load_value(a, ai) * load_value(b, bi);
                }
                product[row * n + column] = sum;
            }
    }
    for (size_t row = 0; row < m; ++row)
        for (size_t column = 0; column < n; ++column)
        {
            const size_t ci = c_offset + row * ldc + column;
            float value = alpha * product[row * n + column];
            if (beta != 0.0f)
                value += beta * load_value(c, ci);
            store_value(c, ci, value);
        }
    return 0;
}

extern "C" int ps5cl_gemv(int half_storage, int transpose_a, size_t m, size_t n,
                          const void *alpha_ptr, cl_mem a, size_t a_offset, size_t lda, cl_mem x,
                          size_t x_offset, size_t x_inc, const void *beta_ptr, cl_mem y,
                          size_t y_offset, size_t y_inc)
{
    if (!half_storage || !a || !x || !y)
        return -1;
    active_operation = transpose_a ? "gemv_t" : "gemv_n";
    const float alpha = half_to_float(*static_cast<const uint16_t *>(alpha_ptr));
    const float beta = half_to_float(*static_cast<const uint16_t *>(beta_ptr));
    const size_t rows = transpose_a ? n : m;
    const size_t inner_count = transpose_a ? m : n;
    for (size_t row = 0; row < rows; ++row)
    {
        float sum = 0.0f;
        for (size_t inner = 0; inner < inner_count; ++inner)
        {
            const size_t ai = a_offset + (transpose_a ? inner * lda + row : row * lda + inner);
            sum += load_value(a, ai) * load_value(x, x_offset + inner * x_inc);
        }
        const size_t yi = y_offset + row * y_inc;
        float value = alpha * sum;
        if (beta != 0.0f)
            value += beta * load_value(y, yi);
        store_value(y, yi, value);
    }
    return 0;
}

namespace
{

cl_int run_kernel(cl_kernel kernel)
{
    if (!kernel)
        return CL_INVALID_KERNEL;
    const std::string &name = kernel->name;
    if (name == "embedding_gather_nlc")
    {
        cl_mem ids = mem_arg(kernel, 0), embedding = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        const int rows = kernel_arg<int>(kernel, 3), columns = kernel_arg<int>(kernel, 4),
                  vocab = kernel_arg<int>(kernel, 5);
        const auto *indices = reinterpret_cast<const int32_t *>(ids->data);
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
                store_value(out, static_cast<size_t>(row) * columns + column,
                            indices[row] >= 0 && indices[row] < vocab
                                ? load_value(embedding,
                                             static_cast<size_t>(indices[row]) * columns + column)
                                : 0.0f);
        return CL_SUCCESS;
    }
    if (name == "k_make_pos_ids")
    {
        auto *positions = reinterpret_cast<int32_t *>(mem_arg(kernel, 0)->data);
        auto *types = reinterpret_cast<int32_t *>(mem_arg(kernel, 1)->data);
        const int rows = kernel_arg<int>(kernel, 2);
        for (int row = 0; row < rows; ++row)
        {
            positions[row] = row;
            types[row] = 0;
        }
        return CL_SUCCESS;
    }
    if (name == "k_add3")
    {
        cl_mem a = mem_arg(kernel, 0), b = mem_arg(kernel, 1), c = mem_arg(kernel, 2),
               out = mem_arg(kernel, 3);
        const int count = kernel_arg<int>(kernel, 4);
        for (int i = 0; i < count; ++i)
            store_value(out, i, load_value(a, i) + load_value(b, i) + load_value(c, i));
        return CL_SUCCESS;
    }
    if (name == "silu_inplace")
    {
        cl_mem x = mem_arg(kernel, 0);
        const int n = kernel_arg<int>(kernel, 1);
        for (int i = 0; i < n; ++i)
        {
            const float v = load_value(x, i);
            store_value(x, i, v / (1.0f + std::exp(-v)));
        }
        return CL_SUCCESS;
    }
    if (name == "add_bias")
    {
        cl_mem x = mem_arg(kernel, 0), bias = mem_arg(kernel, 1);
        const int rows = kernel_arg<int>(kernel, 2), cols = kernel_arg<int>(kernel, 3);
        for (int i = 0; i < rows * cols; ++i)
            store_value(x, i, load_value(x, i) + load_value(bias, i % cols));
        return CL_SUCCESS;
    }
    if (name == "layernorm" || name == "layernorm_wg" || name == "layernorm_rowwise")
    {
        cl_mem x = mem_arg(kernel, 0), gamma = mem_arg(kernel, 1), beta = mem_arg(kernel, 2),
               out = mem_arg(kernel, 3);
        const int rows = kernel_arg<int>(kernel, 4), d = kernel_arg<int>(kernel, 5);
        const float eps = kernel_arg<float>(kernel, 6);
#ifdef PS5CL_HOST_OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int row = 0; row < rows; ++row)
        {
            const size_t base = static_cast<size_t>(row) * d;
            float mean = 0.0f;
            for (int i = 0; i < d; ++i)
                mean += load_value(x, base + i);
            mean /= d;
            float variance = 0.0f;
            for (int i = 0; i < d; ++i)
            {
                const float v = load_value(x, base + i) - mean;
                variance += v * v;
            }
            const float inv = 1.0f / std::sqrt(variance / d + eps);
            for (int i = 0; i < d; ++i)
                store_value(out, base + i,
                            (load_value(x, base + i) - mean) * inv * load_value(gamma, i) +
                                load_value(beta, i));
        }
        return CL_SUCCESS;
    }
    if (name == "k_bias_add_2d" || name == "bias_add_2d")
    {
        cl_mem out = mem_arg(kernel, 0), bias = mem_arg(kernel, 1);
        const int rows = kernel_arg<int>(kernel, 2), columns = kernel_arg<int>(kernel, 3);
        for (int i = 0; i < rows * columns; ++i)
            store_value(out, i, load_value(out, i) + load_value(bias, i % columns));
        return CL_SUCCESS;
    }
    if (name == "k_gelu")
    {
        cl_mem out = mem_arg(kernel, 0);
        const int count = kernel_arg<int>(kernel, 1);
        for (int i = 0; i < count; ++i)
        {
            const float x = load_value(out, i);
            store_value(out, i,
                        0.5f * x * (1.0f + std::tanh(0.7978845608f * (x + 0.044715f * x * x * x))));
        }
        return CL_SUCCESS;
    }
    if (name == "k_scaled_attn_scores")
    {
        cl_mem q = mem_arg(kernel, 0), k = mem_arg(kernel, 1), scores = mem_arg(kernel, 2);
        const int tokens = kernel_arg<int>(kernel, 3), heads = kernel_arg<int>(kernel, 4),
                  width = kernel_arg<int>(kernel, 5);
        const float scale = 1.0f / std::sqrt(static_cast<float>(width));
        for (int head = 0; head < heads; ++head)
            for (int row = 0; row < tokens; ++row)
                for (int column = 0; column < tokens; ++column)
                {
                    float sum = 0.0f;
                    const size_t qb = static_cast<size_t>(row) * heads * width + head * width;
                    const size_t kb = static_cast<size_t>(column) * heads * width + head * width;
                    for (int d = 0; d < width; ++d)
                        sum += load_value(q, qb + d) * load_value(k, kb + d);
                    store_value(scores,
                                (static_cast<size_t>(head) * tokens + row) * tokens + column,
                                sum * scale);
                }
        return CL_SUCCESS;
    }
    if (name == "k_scaled_attn_ctx")
    {
        cl_mem scores = mem_arg(kernel, 0), values = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        const int tokens = kernel_arg<int>(kernel, 3), heads = kernel_arg<int>(kernel, 4),
                  width = kernel_arg<int>(kernel, 5);
        for (int row = 0; row < tokens; ++row)
            for (int head = 0; head < heads; ++head)
                for (int d = 0; d < width; ++d)
                {
                    float sum = 0.0f;
                    for (int column = 0; column < tokens; ++column)
                        sum +=
                            load_value(scores, (static_cast<size_t>(head) * tokens + row) * tokens +
                                                   column) *
                            load_value(values, static_cast<size_t>(column) * heads * width +
                                                   head * width + d);
                    store_value(out, static_cast<size_t>(row) * heads * width + head * width + d,
                                sum);
                }
        return CL_SUCCESS;
    }
    if (name == "zero_buf")
    {
        cl_mem out = mem_arg(kernel, 0);
        const int count = kernel_arg<int>(kernel, 1);
        std::memset(out->data, 0, static_cast<size_t>(count) * sizeof(uint16_t));
        return CL_SUCCESS;
    }
    if (name == "concat_bi")
    {
        cl_mem forward = mem_arg(kernel, 0), backward = mem_arg(kernel, 1),
               out = mem_arg(kernel, 2);
        const int rows = kernel_arg<int>(kernel, 3), width = kernel_arg<int>(kernel, 4);
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < width; ++column)
            {
                store_value(out, static_cast<size_t>(row) * 2 * width + column,
                            load_value(forward, static_cast<size_t>(row) * width + column));
                store_value(out, static_cast<size_t>(row) * 2 * width + width + column,
                            load_value(backward, static_cast<size_t>(row) * width + column));
            }
        return CL_SUCCESS;
    }
    if (name == "aln_gather_NCL")
    {
        cl_mem input = mem_arg(kernel, 0), indices_mem = mem_arg(kernel, 1),
               out = mem_arg(kernel, 2);
        const int input_rows = kernel_arg<int>(kernel, 3), output_rows = kernel_arg<int>(kernel, 4),
                  channels = kernel_arg<int>(kernel, 5);
        const auto *indices = reinterpret_cast<const int32_t *>(indices_mem->data);
        for (int channel = 0; channel < channels; ++channel)
            for (int row = 0; row < output_rows; ++row)
            {
                const int source = std::max(0, std::min(indices[row], input_rows - 1));
                store_value(out, static_cast<size_t>(channel) * output_rows + row,
                            load_value(input, static_cast<size_t>(source) * channels + channel));
            }
        return CL_SUCCESS;
    }
    if (name == "lstm_seq")
    {
        cl_mem wx = mem_arg(kernel, 0), recurrent = mem_arg(kernel, 1), bias = mem_arg(kernel, 2),
               out = mem_arg(kernel, 3);
        const int rows = kernel_arg<int>(kernel, 4), width = kernel_arg<int>(kernel, 5),
                  reverse = kernel_arg<int>(kernel, 6);
        std::vector<float> hidden(width, 0.0f), next(width), cells(width, 0.0f);
        for (int step = 0; step < rows; ++step)
        {
            const int row = reverse ? rows - 1 - step : step;
#ifdef PS5CL_HOST_OPENMP
#pragma omp parallel for schedule(static)
#endif
            for (int column = 0; column < width; ++column)
            {
                float gates[4];
                for (int gate = 0; gate < 4; ++gate)
                {
                    const int weight_row = gate * width + column;
                    float sum = load_value(wx, static_cast<size_t>(row) * 4 * width + weight_row) +
                                load_value(bias, weight_row);
                    for (int inner = 0; inner < width; ++inner)
                        sum +=
                            hidden[inner] *
                            load_value(recurrent, static_cast<size_t>(weight_row) * width + inner);
                    gates[gate] = sum;
                }
                const float input_gate = 1.0f / (1.0f + std::exp(-gates[0]));
                const float forget_gate = 1.0f / (1.0f + std::exp(-gates[1]));
                const float output_gate = 1.0f / (1.0f + std::exp(-gates[3]));
                cells[column] = forget_gate * cells[column] + input_gate * std::tanh(gates[2]);
                next[column] = output_gate * std::tanh(cells[column]);
                store_value(out, static_cast<size_t>(row) * width + column, next[column]);
            }
            hidden.swap(next);
        }
        return CL_SUCCESS;
    }
    if (name == "transpose_NCL_to_NLC" || name == "te_transpose_NCL_to_NLC" ||
        name == "transpose_NLC_to_NCL" || name == "te_transpose_NLC_to_NCL")
    {
        cl_mem input = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int channels = kernel_arg<int>(kernel, 2), rows = kernel_arg<int>(kernel, 3);
        const bool to_nlc = name.find("NCL_to_NLC") != std::string::npos;
        for (int row = 0; row < rows; ++row)
            for (int channel = 0; channel < channels; ++channel)
            {
                const size_t nlc = static_cast<size_t>(row) * channels + channel;
                const size_t ncl = static_cast<size_t>(channel) * rows + row;
                store_value(out, to_nlc ? nlc : ncl, load_value(input, to_nlc ? ncl : nlc));
            }
        return CL_SUCCESS;
    }
    if (name == "broadcast_style")
    {
        cl_mem style = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int channels = kernel_arg<int>(kernel, 2), rows = kernel_arg<int>(kernel, 3);
        for (int channel = 0; channel < channels; ++channel)
            for (int row = 0; row < rows; ++row)
                store_value(out, static_cast<size_t>(channel) * rows + row,
                            load_value(style, channel));
        return CL_SUCCESS;
    }
    if (name == "concat_at_C")
    {
        cl_mem first = mem_arg(kernel, 0), second = mem_arg(kernel, 2), out = mem_arg(kernel, 4);
        const int first_channels = kernel_arg<int>(kernel, 1);
        const int second_channels = kernel_arg<int>(kernel, 3);
        const int rows = kernel_arg<int>(kernel, 5);
        for (int channel = 0; channel < first_channels + second_channels; ++channel)
            for (int row = 0; row < rows; ++row)
                store_value(
                    out, static_cast<size_t>(channel) * rows + row,
                    channel < first_channels
                        ? load_value(first, static_cast<size_t>(channel) * rows + row)
                        : load_value(second,
                                     static_cast<size_t>(channel - first_channels) * rows + row));
        return CL_SUCCESS;
    }
    if (name == "cvt_f16_to_f32" || name == "cvt_f32_to_f16")
    {
        cl_mem input = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int count = kernel_arg<int>(kernel, 2);
        const bool to_float = name == "cvt_f16_to_f32";
        for (int i = 0; i < count; ++i)
        {
            const float value = to_float ? load_value(input, i) : load_float(input, i);
            if (to_float)
                store_float(out, i, value);
            else
                store_value(out, i, value);
        }
        return CL_SUCCESS;
    }
    if (name == "linear_apply_f32")
    {
        cl_mem style = mem_arg(kernel, 0), weights = mem_arg(kernel, 1);
        cl_mem bias = mem_arg(kernel, 2), out = mem_arg(kernel, 3);
        const int input_width = kernel_arg<int>(kernel, 4);
        const int output_width = kernel_arg<int>(kernel, 5);
        const int has_bias = kernel_arg<int>(kernel, 6);
        for (int row = 0; row < output_width; ++row)
        {
            float sum = has_bias ? load_value(bias, row) : 0.0f;
            for (int column = 0; column < input_width; ++column)
                sum += load_value(style, column) *
                       load_value(weights, static_cast<size_t>(row) * input_width + column);
            store_float(out, row, sum);
        }
        return CL_SUCCESS;
    }
    if (name == "split2_f32")
    {
        cl_mem input = mem_arg(kernel, 0), first = mem_arg(kernel, 1), second = mem_arg(kernel, 2);
        const int channels = kernel_arg<int>(kernel, 3);
        for (int channel = 0; channel < channels; ++channel)
        {
            store_float(first, channel, load_float(input, channel));
            store_float(second, channel, load_float(input, channels + channel));
        }
        return CL_SUCCESS;
    }
    if (name == "weightnorm_f16w" || name == "weightnorm_f32")
    {
        cl_mem values = mem_arg(kernel, 0), scales = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        const int rows = kernel_arg<int>(kernel, 3), columns = kernel_arg<int>(kernel, 4);
        const bool float_output = name == "weightnorm_f32";
        for (int row = 0; row < rows; ++row)
        {
            const size_t base = static_cast<size_t>(row) * columns;
            float square_sum = 0.0f;
            for (int column = 0; column < columns; ++column)
            {
                const float value = load_value(values, base + column);
                square_sum += value * value;
            }
            const float scale = load_value(scales, row) / (std::sqrt(square_sum) + 1e-12f);
            for (int column = 0; column < columns; ++column)
            {
                const float value = load_value(values, base + column) * scale;
                if (float_output)
                    store_float(out, base + column, value);
                else
                    store_value(out, base + column, value);
            }
        }
        return CL_SUCCESS;
    }
    if (name == "instnorm_adain_snake_f16out" || name == "instnorm_adain_snake_h2h" ||
        name == "instnorm_adain_snake_f32")
    {
        cl_mem input = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        cl_mem gamma = mem_arg(kernel, 2), beta = mem_arg(kernel, 3), alpha = mem_arg(kernel, 4);
        const int channels = kernel_arg<int>(kernel, 5), rows = kernel_arg<int>(kernel, 6);
        const float epsilon = kernel_arg<float>(kernel, 7);
        const bool half_input = name == "instnorm_adain_snake_h2h";
        const bool float_output = name == "instnorm_adain_snake_f32";
        for (int channel = 0; channel < channels; ++channel)
        {
            const size_t base = static_cast<size_t>(channel) * rows;
            float mean = 0.0f;
            for (int row = 0; row < rows; ++row)
                mean += half_input ? load_value(input, base + row) : load_float(input, base + row);
            mean /= rows;
            float variance = 0.0f;
            for (int row = 0; row < rows; ++row)
            {
                const float value =
                    (half_input ? load_value(input, base + row) : load_float(input, base + row)) -
                    mean;
                variance += value * value;
            }
            const float inverse = 1.0f / std::sqrt(variance / rows + epsilon);
            const float g = load_float(gamma, channel), b = load_float(beta, channel);
            float a = load_value(alpha, channel);
            if (std::fabs(a) < 1e-6f)
                a = a >= 0.0f ? 1e-6f : -1e-6f;
            for (int row = 0; row < rows; ++row)
            {
                float value =
                    half_input ? load_value(input, base + row) : load_float(input, base + row);
                value = (1.0f + g) * ((value - mean) * inverse) + b;
                const float sine = std::sin(a * value);
                value += sine * sine / a;
                if (float_output)
                    store_float(out, base + row, value);
                else
                    store_value(out, base + row, value);
            }
        }
        return CL_SUCCESS;
    }
    if (name == "add_f32_from_h" || name == "add_f32")
    {
        cl_mem out = mem_arg(kernel, 0), input = mem_arg(kernel, 1);
        const int count = kernel_arg<int>(kernel, 2);
        const bool half_input = name == "add_f32_from_h";
        for (int i = 0; i < count; ++i)
            store_float(out, i,
                        load_float(out, i) +
                            (half_input ? load_value(input, i) : load_float(input, i)));
        return CL_SUCCESS;
    }
    if (name == "linear_apply")
    {
        cl_mem style = mem_arg(kernel, 0), weights = mem_arg(kernel, 1), bias = mem_arg(kernel, 2),
               out = mem_arg(kernel, 3);
        const int input_width = kernel_arg<int>(kernel, 4),
                  output_width = kernel_arg<int>(kernel, 5), has_bias = kernel_arg<int>(kernel, 6);
        for (int row = 0; row < output_width; ++row)
        {
            float sum = has_bias ? load_value(bias, row) : 0.0f;
            for (int column = 0; column < input_width; ++column)
                sum += load_value(style, column) *
                       load_value(weights, static_cast<size_t>(row) * input_width + column);
            store_value(out, row, sum);
        }
        return CL_SUCCESS;
    }
    if (name == "split_chunk2")
    {
        cl_mem input = mem_arg(kernel, 0), first = mem_arg(kernel, 1), second = mem_arg(kernel, 2);
        const int channels = kernel_arg<int>(kernel, 3), rows = kernel_arg<int>(kernel, 4);
        for (int channel = 0; channel < channels; ++channel)
            for (int row = 0; row < rows; ++row)
            {
                store_value(first, static_cast<size_t>(channel) * rows + row,
                            load_value(input, static_cast<size_t>(channel) * rows + row));
                store_value(
                    second, static_cast<size_t>(channel) * rows + row,
                    load_value(input, static_cast<size_t>(channels + channel) * rows + row));
            }
        return CL_SUCCESS;
    }
    if (name == "instnorm_NCL")
    {
        cl_mem input = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int channels = kernel_arg<int>(kernel, 2), rows = kernel_arg<int>(kernel, 3);
        const float epsilon = kernel_arg<float>(kernel, 4);
        for (int channel = 0; channel < channels; ++channel)
        {
            const size_t base = static_cast<size_t>(channel) * rows;
            float mean = 0.0f;
            for (int row = 0; row < rows; ++row)
                mean += load_value(input, base + row);
            mean /= rows;
            float variance = 0.0f;
            for (int row = 0; row < rows; ++row)
            {
                const float d = load_value(input, base + row) - mean;
                variance += d * d;
            }
            const float inverse = 1.0f / std::sqrt(variance / rows + epsilon);
            for (int row = 0; row < rows; ++row)
                store_value(out, base + row, (load_value(input, base + row) - mean) * inverse);
        }
        return CL_SUCCESS;
    }
    if (name == "adain_combine")
    {
        cl_mem input = mem_arg(kernel, 0), gamma = mem_arg(kernel, 1), beta = mem_arg(kernel, 2),
               out = mem_arg(kernel, 3);
        const int channels = kernel_arg<int>(kernel, 4), rows = kernel_arg<int>(kernel, 5);
        for (int channel = 0; channel < channels; ++channel)
            for (int row = 0; row < rows; ++row)
            {
                const size_t at = static_cast<size_t>(channel) * rows + row;
                store_value(out, at,
                            (1.0f + load_value(gamma, channel)) * load_value(input, at) +
                                load_value(beta, channel));
            }
        return CL_SUCCESS;
    }
    if (name == "leaky_relu" || name == "te_leaky")
    {
        cl_mem out = mem_arg(kernel, 0);
        const int count = kernel_arg<int>(kernel, 1);
        const float slope = kernel_arg<float>(kernel, 2);
        for (int i = 0; i < count; ++i)
        {
            const float value = load_value(out, i);
            if (value < 0.0f)
                store_value(out, i, value * slope);
        }
        return CL_SUCCESS;
    }
    if (name == "te_layernorm_NCL")
    {
        cl_mem input = mem_arg(kernel, 0), gamma = mem_arg(kernel, 1), beta = mem_arg(kernel, 2),
               out = mem_arg(kernel, 3);
        const int channels = kernel_arg<int>(kernel, 4), rows = kernel_arg<int>(kernel, 5);
        const float epsilon = kernel_arg<float>(kernel, 6);
        for (int row = 0; row < rows; ++row)
        {
            float mean = 0.0f;
            for (int channel = 0; channel < channels; ++channel)
                mean += load_value(input, static_cast<size_t>(channel) * rows + row);
            mean /= channels;
            float variance = 0.0f;
            for (int channel = 0; channel < channels; ++channel)
            {
                const float d = load_value(input, static_cast<size_t>(channel) * rows + row) - mean;
                variance += d * d;
            }
            const float inverse = 1.0f / std::sqrt(variance / channels + epsilon);
            for (int channel = 0; channel < channels; ++channel)
            {
                const size_t at = static_cast<size_t>(channel) * rows + row;
                store_value(out, at,
                            (load_value(input, at) - mean) * inverse * load_value(gamma, channel) +
                                load_value(beta, channel));
            }
        }
        return CL_SUCCESS;
    }
    if (name == "weightnorm_recon")
    {
        cl_mem values = mem_arg(kernel, 0), scales = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        const int rows = kernel_arg<int>(kernel, 3), columns = kernel_arg<int>(kernel, 4);
        for (int row = 0; row < rows; ++row)
        {
            const size_t base = static_cast<size_t>(row) * columns;
            float square_sum = 0.0f;
            for (int column = 0; column < columns; ++column)
            {
                const float value = load_value(values, base + column);
                square_sum += value * value;
            }
            const float scale = load_value(scales, row) / (std::sqrt(square_sum) + 1e-12f);
            for (int column = 0; column < columns; ++column)
                store_value(out, base + column, load_value(values, base + column) * scale);
        }
        return CL_SUCCESS;
    }
    if (name == "im2col_ncl")
    {
        cl_mem input = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int channels = kernel_arg<int>(kernel, 2), input_rows = kernel_arg<int>(kernel, 3);
        const int output_rows = kernel_arg<int>(kernel, 4), width = kernel_arg<int>(kernel, 5);
        const int padding = kernel_arg<int>(kernel, 6), dilation = kernel_arg<int>(kernel, 7);
        const int row_stride = kernel_arg<int>(kernel, 8);
        for (int row = 0; row < output_rows; ++row)
            for (int channel = 0; channel < channels; ++channel)
                for (int tap = 0; tap < width; ++tap)
                {
                    const int source = row - padding + tap * dilation;
                    store_value(
                        out, static_cast<size_t>(row) * row_stride + channel * width + tap,
                        source >= 0 && source < input_rows
                            ? load_value(input, static_cast<size_t>(channel) * input_rows + source)
                            : 0.0f);
                }
        return CL_SUCCESS;
    }
    if (name == "pad_rows")
    {
        cl_mem input = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int rows = kernel_arg<int>(kernel, 2), input_width = kernel_arg<int>(kernel, 3);
        const int output_width = kernel_arg<int>(kernel, 4);
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < output_width; ++column)
                store_value(out, static_cast<size_t>(row) * output_width + column,
                            column < input_width
                                ? load_value(input, static_cast<size_t>(row) * input_width + column)
                                : 0.0f);
        return CL_SUCCESS;
    }
    if (name == "transpose_TN")
    {
        cl_mem input = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int rows = kernel_arg<int>(kernel, 2), columns = kernel_arg<int>(kernel, 3);
        for (int column = 0; column < columns; ++column)
            for (int row = 0; row < rows; ++row)
                store_value(out, static_cast<size_t>(column) * rows + row,
                            load_value(input, static_cast<size_t>(row) * columns + column));
        return CL_SUCCESS;
    }
    if (name == "conv1d_hh_t8x4" || name == "conv1d_hh_lds4x4")
    {
        cl_mem input = mem_arg(kernel, 0), weights = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        cl_mem bias = mem_arg(kernel, 3);
        const int has_bias = kernel_arg<int>(kernel, 4);
        const int input_channels = kernel_arg<int>(kernel, 5),
                  output_channels = kernel_arg<int>(kernel, 6);
        const int input_rows = kernel_arg<int>(kernel, 7), output_rows = kernel_arg<int>(kernel, 8);
        const int width = kernel_arg<int>(kernel, 9), padding = kernel_arg<int>(kernel, 10);
        const int dilation = kernel_arg<int>(kernel, 11);
        const size_t inner = static_cast<size_t>(input_channels) * width;
        std::vector<uint16_t> columns(static_cast<size_t>(output_rows) * inner);
        for (int row = 0; row < output_rows; ++row)
            for (int channel = 0; channel < input_channels; ++channel)
                for (int tap = 0; tap < width; ++tap)
                {
                    const int source = row - padding + tap * dilation;
                    columns[static_cast<size_t>(row) * inner + channel * width + tap] =
                        source >= 0 && source < input_rows
                            ? reinterpret_cast<const uint16_t *>(
                                  input->data)[static_cast<size_t>(channel) * input_rows + source]
                            : 0;
                }
        const uint16_t one = float_to_half(1.0f), zero = 0;
        _cl_mem weight_memory{};
        weight_memory.data = weights->data;
        weight_memory.size = static_cast<size_t>(output_channels) * inner * sizeof(uint16_t);
        constexpr int row_tile = 4096;
        for (int first_row = 0; first_row < output_rows; first_row += row_tile)
        {
            const int rows = std::min(row_tile, output_rows - first_row);
            std::vector<uint16_t> product(static_cast<size_t>(rows) * output_channels);
            _cl_mem column_memory{}, product_memory{};
            column_memory.data = reinterpret_cast<uint8_t *>(
                columns.data() + static_cast<size_t>(first_row) * inner);
            column_memory.size = static_cast<size_t>(rows) * inner * sizeof(uint16_t);
            product_memory.data = reinterpret_cast<uint8_t *>(product.data());
            product_memory.size = product.size() * sizeof(uint16_t);
            if (ps5cl_gemm(1, 0, 1, rows, output_channels, inner, &one, &column_memory, 0, inner,
                           &weight_memory, 0, inner, &zero, &product_memory, 0,
                           output_channels) != 0)
                return CL_INVALID_OPERATION;
            for (int channel = 0; channel < output_channels; ++channel)
                for (int row = 0; row < rows; ++row)
                {
                    float value = half_to_float(
                        product[static_cast<size_t>(row) * output_channels + channel]);
                    if (has_bias)
                        value += load_value(bias, channel);
                    store_value(out, static_cast<size_t>(channel) * output_rows + first_row + row,
                                value);
                }
        }
        return CL_SUCCESS;
    }
    if (name == "conv1d_dilated" || name == "conv1d_dilated_fast" ||
        name == "conv1d_dilated_fast_c4" || name == "conv1d_dilated_c4x4" ||
        name == "conv1d_dilated_rt4")
    {
        cl_mem input = mem_arg(kernel, 0), weights = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        const int input_channels = kernel_arg<int>(kernel, 3),
                  output_channels = kernel_arg<int>(kernel, 4);
        const int input_rows = kernel_arg<int>(kernel, 5), output_rows = kernel_arg<int>(kernel, 6),
                  width = kernel_arg<int>(kernel, 7);
        const bool tiled = name == "conv1d_dilated_c4x4" || name == "conv1d_dilated_rt4";
        const int stride = tiled ? 1 : kernel_arg<int>(kernel, 8);
        const int padding = tiled ? kernel_arg<int>(kernel, 8) : kernel_arg<int>(kernel, 9);
        const int dilation = tiled ? kernel_arg<int>(kernel, 9) : kernel_arg<int>(kernel, 10);
        const int groups = tiled ? 1 : kernel_arg<int>(kernel, 11);
        if (groups == 1)
        {
            const size_t inner = static_cast<size_t>(input_channels) * width;
            std::vector<uint16_t> columns(static_cast<size_t>(output_rows) * inner);
            for (int row = 0; row < output_rows; ++row)
                for (int channel = 0; channel < input_channels; ++channel)
                    for (int tap = 0; tap < width; ++tap)
                    {
                        const int source = row * stride - padding + tap * dilation;
                        columns[static_cast<size_t>(row) * inner + channel * width + tap] =
                            source >= 0 && source < input_rows
                                ? reinterpret_cast<const uint16_t *>(
                                      input->data)[static_cast<size_t>(channel) * input_rows +
                                                   source]
                                : 0;
                    }
            std::vector<uint16_t> product(static_cast<size_t>(output_rows) * output_channels);
            _cl_mem column_memory;
            column_memory.data = reinterpret_cast<uint8_t *>(columns.data());
            column_memory.size = columns.size() * sizeof(uint16_t);
            _cl_mem product_memory;
            product_memory.data = reinterpret_cast<uint8_t *>(product.data());
            product_memory.size = product.size() * sizeof(uint16_t);
            const uint16_t one = float_to_half(1.0f), zero = 0;
            if (ps5cl_gemm(1, 0, 1, output_rows, output_channels, inner, &one, &column_memory, 0,
                           inner, weights, 0, inner, &zero, &product_memory, 0,
                           output_channels) == 0)
            {
                for (int channel = 0; channel < output_channels; ++channel)
                    for (int row = 0; row < output_rows; ++row)
                        reinterpret_cast<uint16_t *>(
                            out->data)[static_cast<size_t>(channel) * output_rows + row] =
                            product[static_cast<size_t>(row) * output_channels + channel];
                return CL_SUCCESS;
            }
        }
        const int inputs_per_group = input_channels / groups,
                  outputs_per_group = output_channels / groups;
#ifdef PS5CL_HOST_OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int output_channel = 0; output_channel < output_channels; ++output_channel)
            for (int output_row = 0; output_row < output_rows; ++output_row)
            {
                float sum = 0.0f;
                const int group = output_channel / outputs_per_group;
                for (int local_input = 0; local_input < inputs_per_group; ++local_input)
                    for (int tap = 0; tap < width; ++tap)
                    {
                        const int input_row = output_row * stride - padding + tap * dilation;
                        if (input_row >= 0 && input_row < input_rows)
                        {
                            const int input_channel = group * inputs_per_group + local_input;
                            sum +=
                                load_value(input, static_cast<size_t>(input_channel) * input_rows +
                                                      input_row) *
                                load_value(weights,
                                           (static_cast<size_t>(output_channel) * inputs_per_group +
                                            local_input) *
                                                   width +
                                               tap);
                        }
                    }
                store_value(out, static_cast<size_t>(output_channel) * output_rows + output_row,
                            sum);
            }
        return CL_SUCCESS;
    }
    if (name == "upsample_nn_NCL")
    {
        cl_mem input = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int channels = kernel_arg<int>(kernel, 2), input_rows = kernel_arg<int>(kernel, 3),
                  scale = kernel_arg<int>(kernel, 4);
        const int output_rows = input_rows * scale;
        for (int channel = 0; channel < channels; ++channel)
            for (int row = 0; row < output_rows; ++row)
                store_value(
                    out, static_cast<size_t>(channel) * output_rows + row,
                    load_value(input, static_cast<size_t>(channel) * input_rows + row / scale));
        return CL_SUCCESS;
    }
    if (name == "concat_C")
    {
        cl_mem first = mem_arg(kernel, 0), second = mem_arg(kernel, 2), out = mem_arg(kernel, 4);
        const int first_channels = kernel_arg<int>(kernel, 1),
                  second_channels = kernel_arg<int>(kernel, 3), rows = kernel_arg<int>(kernel, 5);
        for (int channel = 0; channel < first_channels + second_channels; ++channel)
            for (int row = 0; row < rows; ++row)
                store_value(
                    out, static_cast<size_t>(channel) * rows + row,
                    channel < first_channels
                        ? load_value(first, static_cast<size_t>(channel) * rows + row)
                        : load_value(second,
                                     static_cast<size_t>(channel - first_channels) * rows + row));
        return CL_SUCCESS;
    }
    if (name == "concat4_C")
    {
        cl_mem inputs[4] = {mem_arg(kernel, 0), mem_arg(kernel, 2), mem_arg(kernel, 4),
                            mem_arg(kernel, 6)};
        int widths[4] = {kernel_arg<int>(kernel, 1), kernel_arg<int>(kernel, 3),
                         kernel_arg<int>(kernel, 5), kernel_arg<int>(kernel, 7)};
        cl_mem out = mem_arg(kernel, 8);
        const int rows = kernel_arg<int>(kernel, 9);
        int destination_channel = 0;
        for (int part = 0; part < 4; ++part)
            for (int channel = 0; channel < widths[part]; ++channel, ++destination_channel)
                for (int row = 0; row < rows; ++row)
                    store_value(
                        out, static_cast<size_t>(destination_channel) * rows + row,
                        load_value(inputs[part], static_cast<size_t>(channel) * rows + row));
        return CL_SUCCESS;
    }
    if (name == "bias_add_NCL")
    {
        cl_mem out = mem_arg(kernel, 0), bias = mem_arg(kernel, 1);
        const int channels = kernel_arg<int>(kernel, 2), rows = kernel_arg<int>(kernel, 3);
        for (int channel = 0; channel < channels; ++channel)
            for (int row = 0; row < rows; ++row)
            {
                const size_t at = static_cast<size_t>(channel) * rows + row;
                store_value(out, at, load_value(out, at) + load_value(bias, channel));
            }
        return CL_SUCCESS;
    }
    if (name == "k_add_scale")
    {
        cl_mem out = mem_arg(kernel, 0), input = mem_arg(kernel, 1);
        const int count = kernel_arg<int>(kernel, 2);
        const float scale = kernel_arg<float>(kernel, 3);
        for (int i = 0; i < count; ++i)
            store_value(out, i, (load_value(out, i) + load_value(input, i)) * scale);
        return CL_SUCCESS;
    }
    if (name == "convtr1d_c4x4")
    {
        cl_mem input = mem_arg(kernel, 0), weights = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        const int input_channels = kernel_arg<int>(kernel, 3),
                  output_channels = kernel_arg<int>(kernel, 4);
        const int input_rows = kernel_arg<int>(kernel, 5), output_rows = kernel_arg<int>(kernel, 6);
        const int width = kernel_arg<int>(kernel, 7), stride = kernel_arg<int>(kernel, 8);
        const int padding = kernel_arg<int>(kernel, 9);
        std::vector<uint16_t> input_matrix(static_cast<size_t>(input_rows) * input_channels);
        for (int row = 0; row < input_rows; ++row)
            for (int channel = 0; channel < input_channels; ++channel)
                input_matrix[static_cast<size_t>(row) * input_channels + channel] =
                    reinterpret_cast<const uint16_t *>(
                        input->data)[static_cast<size_t>(channel) * input_rows + row];
        std::vector<uint16_t> tap_weights(static_cast<size_t>(output_channels) * input_channels);
        std::vector<uint16_t> product(static_cast<size_t>(input_rows) * output_channels);
        _cl_mem input_memory, weight_memory, product_memory;
        input_memory.data = reinterpret_cast<uint8_t *>(input_matrix.data());
        input_memory.size = input_matrix.size() * sizeof(uint16_t);
        weight_memory.data = reinterpret_cast<uint8_t *>(tap_weights.data());
        weight_memory.size = tap_weights.size() * sizeof(uint16_t);
        product_memory.data = reinterpret_cast<uint8_t *>(product.data());
        product_memory.size = product.size() * sizeof(uint16_t);
        std::memset(out->data, 0, out->size);
        const uint16_t one = float_to_half(1.0f), zero = 0;
        for (int tap = 0; tap < width; ++tap)
        {
            for (int output_channel = 0; output_channel < output_channels; ++output_channel)
                for (int input_channel = 0; input_channel < input_channels; ++input_channel)
                    tap_weights[static_cast<size_t>(output_channel) * input_channels +
                                input_channel] =
                        reinterpret_cast<const uint16_t *>(
                            weights->data)[(static_cast<size_t>(input_channel) * output_channels +
                                            output_channel) *
                                               width +
                                           tap];
            if (ps5cl_gemm(1, 0, 1, input_rows, output_channels, input_channels, &one,
                           &input_memory, 0, input_channels, &weight_memory, 0, input_channels,
                           &zero, &product_memory, 0, output_channels) != 0)
                return CL_INVALID_OPERATION;
            for (int row = 0; row < input_rows; ++row)
            {
                const int output_row = row * stride - padding + tap;
                if (output_row < 0 || output_row >= output_rows)
                    continue;
                for (int output_channel = 0; output_channel < output_channels; ++output_channel)
                {
                    const size_t destination =
                        static_cast<size_t>(output_channel) * output_rows + output_row;
                    const float value =
                        load_value(out, destination) +
                        half_to_float(
                            product[static_cast<size_t>(row) * output_channels + output_channel]);
                    store_value(out, destination, value);
                }
            }
        }
        return CL_SUCCESS;
    }
    if (name == "convtr1d")
    {
        cl_mem input = mem_arg(kernel, 0), weights = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        const int input_channels = kernel_arg<int>(kernel, 3),
                  output_channels = kernel_arg<int>(kernel, 4);
        const int input_rows = kernel_arg<int>(kernel, 5), output_rows = kernel_arg<int>(kernel, 6),
                  width = kernel_arg<int>(kernel, 7);
        const int stride = kernel_arg<int>(kernel, 8), padding = kernel_arg<int>(kernel, 9),
                  dilation = kernel_arg<int>(kernel, 10), groups = kernel_arg<int>(kernel, 11);
        const int inputs_per_group = input_channels / groups,
                  outputs_per_group = output_channels / groups;
#ifdef PS5CL_HOST_OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int output_channel = 0; output_channel < output_channels; ++output_channel)
            for (int output_row = 0; output_row < output_rows; ++output_row)
            {
                float sum = 0.0f;
                const int group = output_channel / outputs_per_group;
                for (int local_input = 0; local_input < inputs_per_group; ++local_input)
                    for (int tap = 0; tap < width; ++tap)
                    {
                        const int numerator = output_row + padding - tap * dilation;
                        if (numerator < 0 || numerator % stride)
                            continue;
                        const int input_row = numerator / stride;
                        if (input_row >= input_rows)
                            continue;
                        const int input_channel = group * inputs_per_group + local_input;
                        sum += load_value(input, static_cast<size_t>(input_channel) * input_rows +
                                                     input_row) *
                               load_value(weights,
                                          (static_cast<size_t>(input_channel) * outputs_per_group +
                                           output_channel % outputs_per_group) *
                                                  width +
                                              tap);
                    }
                store_value(out, static_cast<size_t>(output_channel) * output_rows + output_row,
                            sum);
            }
        return CL_SUCCESS;
    }
    if (name == "fourier_features")
    {
        cl_mem t = mem_arg(kernel, 0), w = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        const int rows = kernel_arg<int>(kernel, 3), half = kernel_arg<int>(kernel, 4);
        for (int row = 0; row < rows; ++row)
            for (int j = 0; j < half; ++j)
            {
                const float f = 6.2831853071795864769f * load_value(t, row) * load_value(w, j);
                store_value(out, static_cast<size_t>(row) * 2 * half + j, std::cos(f));
                store_value(out, static_cast<size_t>(row) * 2 * half + half + j, std::sin(f));
            }
        return CL_SUCCESS;
    }
    if (name == "qk_norm_to_heads" || name == "qk_norm_to_heads_wg")
    {
        cl_mem x = mem_arg(kernel, 0), gamma = mem_arg(kernel, 1), beta = mem_arg(kernel, 2),
               out = mem_arg(kernel, 3);
        const int n = kernel_arg<int>(kernel, 4), h = kernel_arg<int>(kernel, 5),
                  dh = kernel_arg<int>(kernel, 6);
        const float eps = kernel_arg<float>(kernel, 7);
        const int source_stride = kernel_arg<int>(kernel, 8),
                  source_offset = kernel_arg<int>(kernel, 9);
        for (int head = 0; head < h; ++head)
            for (int token = 0; token < n; ++token)
            {
                const size_t source =
                    static_cast<size_t>(token) * source_stride + source_offset + head * dh;
                const size_t destination = (static_cast<size_t>(head) * n + token) * dh;
                float mean = 0.0f;
                for (int d = 0; d < dh; ++d)
                    mean += load_value(x, source + d);
                mean /= dh;
                float variance = 0.0f;
                for (int d = 0; d < dh; ++d)
                {
                    const float v = load_value(x, source + d) - mean;
                    variance += v * v;
                }
                const float inv = 1.0f / std::sqrt(variance / dh + eps);
                for (int d = 0; d < dh; ++d)
                    store_value(out, destination + d,
                                (load_value(x, source + d) - mean) * inv * load_value(gamma, d) +
                                    load_value(beta, d));
            }
        return CL_SUCCESS;
    }
    if (name == "split_heads")
    {
        cl_mem x = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int n = kernel_arg<int>(kernel, 2), h = kernel_arg<int>(kernel, 3),
                  dh = kernel_arg<int>(kernel, 4);
        const int source_stride = kernel_arg<int>(kernel, 5),
                  source_offset = kernel_arg<int>(kernel, 6);
        for (int head = 0; head < h; ++head)
            for (int token = 0; token < n; ++token)
                for (int d = 0; d < dh; ++d)
                    store_value(out, (static_cast<size_t>(head) * n + token) * dh + d,
                                load_value(x, static_cast<size_t>(token) * source_stride +
                                                  source_offset + head * dh + d));
        return CL_SUCCESS;
    }
    if (name == "merge_heads")
    {
        cl_mem x = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int n = kernel_arg<int>(kernel, 2), h = kernel_arg<int>(kernel, 3),
                  dh = kernel_arg<int>(kernel, 4);
        for (int head = 0; head < h; ++head)
            for (int token = 0; token < n; ++token)
                for (int d = 0; d < dh; ++d)
                    store_value(out, static_cast<size_t>(token) * h * dh + head * dh + d,
                                load_value(x, (static_cast<size_t>(head) * n + token) * dh + d));
        return CL_SUCCESS;
    }
    if (name == "rope_apply")
    {
        cl_mem x = mem_arg(kernel, 0), frequencies = mem_arg(kernel, 1);
        const int h = kernel_arg<int>(kernel, 2), n = kernel_arg<int>(kernel, 3),
                  dh = kernel_arg<int>(kernel, 4), rotation = kernel_arg<int>(kernel, 5);
        for (int head = 0; head < h; ++head)
            for (int token = 0; token < n; ++token)
            {
                const size_t base = (static_cast<size_t>(head) * n + token) * dh;
                for (int d = 0; d < rotation / 2; ++d)
                {
                    const float a = load_value(x, base + d),
                                b = load_value(x, base + d + rotation / 2);
                    const float f =
                        load_value(frequencies, static_cast<size_t>(token) * rotation + d);
                    store_value(x, base + d, a * std::cos(f) - b * std::sin(f));
                    store_value(x, base + d + rotation / 2, b * std::cos(f) + a * std::sin(f));
                }
            }
        return CL_SUCCESS;
    }
    if (name == "attn_scores")
    {
        cl_mem q = mem_arg(kernel, 0), k = mem_arg(kernel, 1), scores = mem_arg(kernel, 2);
        const int h = kernel_arg<int>(kernel, 3), nq = kernel_arg<int>(kernel, 4),
                  nk = kernel_arg<int>(kernel, 5), dh = kernel_arg<int>(kernel, 6);
        const float scale = kernel_arg<float>(kernel, 7);
        for (int head = 0; head < h; ++head)
            for (int i = 0; i < nq; ++i)
                for (int j = 0; j < nk; ++j)
                {
                    float sum = 0.0f;
                    for (int d = 0; d < dh; ++d)
                        sum += load_value(q, (static_cast<size_t>(head) * nq + i) * dh + d) *
                               load_value(k, (static_cast<size_t>(head) * nk + j) * dh + d);
                    store_value(scores, (static_cast<size_t>(head) * nq + i) * nk + j, sum * scale);
                }
        return CL_SUCCESS;
    }
    if (name == "softmax_rows" || name == "k_softmax_rowwise")
    {
        cl_mem scores = mem_arg(kernel, 0);
        const int rows = kernel_arg<int>(kernel, 1), nk = kernel_arg<int>(kernel, 2);
#ifdef PS5CL_HOST_OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int row = 0; row < rows; ++row)
        {
            const size_t base = static_cast<size_t>(row) * nk;
            float maximum = -std::numeric_limits<float>::infinity();
            for (int j = 0; j < nk; ++j)
                maximum = std::max(maximum, load_value(scores, base + j));
            float sum = 0.0f;
            for (int j = 0; j < nk; ++j)
            {
                const float v = std::exp(load_value(scores, base + j) - maximum);
                store_value(scores, base + j, v);
                sum += v;
            }
            for (int j = 0; j < nk; ++j)
                store_value(scores, base + j, load_value(scores, base + j) / sum);
        }
        return CL_SUCCESS;
    }
    if (name == "attn_av")
    {
        cl_mem scores = mem_arg(kernel, 0), v = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        const int h = kernel_arg<int>(kernel, 3), nq = kernel_arg<int>(kernel, 4),
                  nk = kernel_arg<int>(kernel, 5), dh = kernel_arg<int>(kernel, 6);
        for (int head = 0; head < h; ++head)
            for (int i = 0; i < nq; ++i)
                for (int d = 0; d < dh; ++d)
                {
                    float sum = 0.0f;
                    for (int j = 0; j < nk; ++j)
                        sum += load_value(scores, (static_cast<size_t>(head) * nq + i) * nk + j) *
                               load_value(v, (static_cast<size_t>(head) * nk + j) * dh + d);
                    store_value(out, (static_cast<size_t>(head) * nq + i) * dh + d, sum);
                }
        return CL_SUCCESS;
    }
    if (name == "attn_fused" || name == "attn_fused_r8")
    {
        cl_mem q = mem_arg(kernel, 0), k = mem_arg(kernel, 1), v = mem_arg(kernel, 2),
               out = mem_arg(kernel, 3);
        const int h = kernel_arg<int>(kernel, 4), nq = kernel_arg<int>(kernel, 5),
                  nk = kernel_arg<int>(kernel, 6), dh = kernel_arg<int>(kernel, 7);
        const float scale = kernel_arg<float>(kernel, 8);
        std::vector<float> scores(nk);
        for (int head = 0; head < h; ++head)
            for (int i = 0; i < nq; ++i)
            {
                float maximum = -std::numeric_limits<float>::infinity();
                for (int j = 0; j < nk; ++j)
                {
                    float sum = 0.0f;
                    for (int d = 0; d < dh; ++d)
                        sum += load_value(q, (static_cast<size_t>(head) * nq + i) * dh + d) *
                               load_value(k, (static_cast<size_t>(head) * nk + j) * dh + d);
                    maximum = std::max(maximum, scores[j] = sum * scale);
                }
                float denominator = 0.0f;
                for (float &score : scores)
                    denominator += (score = std::exp(score - maximum));
                for (int d = 0; d < dh; ++d)
                {
                    float sum = 0.0f;
                    for (int j = 0; j < nk; ++j)
                        sum += scores[j] *
                               load_value(v, (static_cast<size_t>(head) * nk + j) * dh + d);
                    store_value(out, static_cast<size_t>(i) * h * dh + head * dh + d,
                                sum / denominator);
                }
            }
        return CL_SUCCESS;
    }
    if (name == "swiglu")
    {
        cl_mem projection = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int n = kernel_arg<int>(kernel, 2), inner = kernel_arg<int>(kernel, 3);
        for (int row = 0; row < n; ++row)
            for (int column = 0; column < inner; ++column)
            {
                const size_t base = static_cast<size_t>(row) * 2 * inner;
                const float value = load_value(projection, base + column),
                            gate = load_value(projection, base + inner + column);
                store_value(out, static_cast<size_t>(row) * inner + column,
                            value * gate / (1.0f + std::exp(-gate)));
            }
        return CL_SUCCESS;
    }
    if (name == "prepend_row")
    {
        cl_mem g = mem_arg(kernel, 0), x = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        const int n = kernel_arg<int>(kernel, 3), d = kernel_arg<int>(kernel, 4);
        for (int column = 0; column < d; ++column)
            store_value(out, column, load_value(g, column));
        for (int row = 0; row < n; ++row)
            for (int column = 0; column < d; ++column)
                store_value(out, static_cast<size_t>(row + 1) * d + column,
                            load_value(x, static_cast<size_t>(row) * d + column));
        return CL_SUCCESS;
    }
    if (name == "drop_rows")
    {
        cl_mem x = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int rows = kernel_arg<int>(kernel, 2), d = kernel_arg<int>(kernel, 3),
                  drop = kernel_arg<int>(kernel, 4);
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < d; ++column)
                store_value(out, static_cast<size_t>(row) * d + column,
                            load_value(x, static_cast<size_t>(row + drop) * d + column));
        return CL_SUCCESS;
    }
    if (name == "transpose_ct")
    {
        cl_mem input = mem_arg(kernel, 0), out = mem_arg(kernel, 1);
        const int c = kernel_arg<int>(kernel, 2), t = kernel_arg<int>(kernel, 3);
        for (int channel = 0; channel < c; ++channel)
            for (int token = 0; token < t; ++token)
                store_value(out, static_cast<size_t>(token) * c + channel,
                            load_value(input, static_cast<size_t>(channel) * t + token));
        return CL_SUCCESS;
    }
    if (name == "pingpong_step")
    {
        cl_mem x = mem_arg(kernel, 0), v = mem_arg(kernel, 1), noise = mem_arg(kernel, 2);
        const float sigma = kernel_arg<float>(kernel, 3), next = kernel_arg<float>(kernel, 4);
        const int n = kernel_arg<int>(kernel, 5);
        for (int i = 0; i < n; ++i)
            store_value(x, i,
                        (1.0f - next) * (load_value(x, i) - sigma * load_value(v, i)) +
                            next * load_value(noise, i));
        return CL_SUCCESS;
    }
    if (name == "element_add" || name == "k_add_inplace")
    {
        cl_mem a = mem_arg(kernel, 0), b = mem_arg(kernel, 1);
        const int n = kernel_arg<int>(kernel, 2);
        for (int i = 0; i < n; ++i)
            store_value(a, i, load_value(a, i) + load_value(b, i));
        return CL_SUCCESS;
    }
    if (name == "split_last_dim_2")
    {
        cl_mem source = mem_arg(kernel, 0), first = mem_arg(kernel, 1), second = mem_arg(kernel, 2);
        const int rows = kernel_arg<int>(kernel, 3), half = kernel_arg<int>(kernel, 4);
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < half; ++column)
            {
                store_value(first, static_cast<size_t>(row) * half + column,
                            load_value(source, static_cast<size_t>(row) * 2 * half + column));
                store_value(
                    second, static_cast<size_t>(row) * half + column,
                    load_value(source, static_cast<size_t>(row) * 2 * half + half + column));
            }
        return CL_SUCCESS;
    }
    if (name == "snake_beta" || name == "snake_beta_v8")
    {
        cl_mem x = mem_arg(kernel, 0), alpha = mem_arg(kernel, 1), beta = mem_arg(kernel, 2);
        const int channels = kernel_arg<int>(kernel, 3), length = kernel_arg<int>(kernel, 4);
        for (int channel = 0; channel < channels; ++channel)
        {
            const float a = std::exp(load_value(alpha, channel));
            const float inverse_b = 1.0f / (std::exp(load_value(beta, channel)) + 1e-9f);
            for (int at = 0; at < length; ++at)
            {
                const size_t index = static_cast<size_t>(channel) * length + at;
                const float value = load_value(x, index), sine = std::sin(value * a);
                store_value(x, index, value + inverse_b * sine * sine);
            }
        }
        return CL_SUCCESS;
    }
    if (name == "add_cl" || name == "add_cl_v8")
    {
        cl_mem a = mem_arg(kernel, 0), b = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        const int n = kernel_arg<int>(kernel, 3);
        for (int i = 0; i < n; ++i)
            store_value(out, i, load_value(a, i) + load_value(b, i));
        return CL_SUCCESS;
    }
    if (name == "bias_add_rows" || name == "bias_add_rows_v8")
    {
        cl_mem x = mem_arg(kernel, 0), bias = mem_arg(kernel, 1);
        const int channels = kernel_arg<int>(kernel, 2), length = kernel_arg<int>(kernel, 3);
        for (int channel = 0; channel < channels; ++channel)
            for (int at = 0; at < length; ++at)
            {
                const size_t index = static_cast<size_t>(channel) * length + at;
                store_value(x, index, load_value(x, index) + load_value(bias, channel));
            }
        return CL_SUCCESS;
    }
    if (name == "im2col_1d" || name == "im2col_1d_v8")
    {
        cl_mem input = mem_arg(kernel, 0), columns = mem_arg(kernel, 1);
        const int channels = kernel_arg<int>(kernel, 2), input_length = kernel_arg<int>(kernel, 3);
        const int output_length = kernel_arg<int>(kernel, 4), width = kernel_arg<int>(kernel, 5);
        const int padding = kernel_arg<int>(kernel, 6), dilation = kernel_arg<int>(kernel, 7);
        const int first = kernel_arg<int>(kernel, 8), count = kernel_arg<int>(kernel, 9);
#ifdef PS5CL_HOST_OPENMP
#pragma omp parallel for collapse(2) schedule(static)
#endif
        for (int channel = 0; channel < channels; ++channel)
            for (int tap = 0; tap < width; ++tap)
                for (int at = 0; at < count && first + at < output_length; ++at)
                {
                    const int source = first + at + tap * dilation - padding;
                    const float value =
                        source >= 0 && source < input_length
                            ? load_value(input,
                                         static_cast<size_t>(channel) * input_length + source)
                            : 0.0f;
                    store_value(columns, (static_cast<size_t>(channel) * width + tap) * count + at,
                                value);
                }
        return CL_SUCCESS;
    }
    if (name == "convt_col2im")
    {
        cl_mem columns = mem_arg(kernel, 0), bias = mem_arg(kernel, 1), out = mem_arg(kernel, 2);
        const int channels = kernel_arg<int>(kernel, 3), width = kernel_arg<int>(kernel, 4);
        const int stride = kernel_arg<int>(kernel, 5), padding = kernel_arg<int>(kernel, 6);
        const int input_length = kernel_arg<int>(kernel, 7),
                  output_length = kernel_arg<int>(kernel, 8);
        const int input_first = kernel_arg<int>(kernel, 9),
                  input_count = kernel_arg<int>(kernel, 10);
        const int output_first = kernel_arg<int>(kernel, 11),
                  output_count = kernel_arg<int>(kernel, 12);
        const int has_bias = kernel_arg<int>(kernel, 13);
#ifdef PS5CL_HOST_OPENMP
#pragma omp parallel for collapse(2) schedule(static)
#endif
        for (int channel = 0; channel < channels; ++channel)
            for (int local = 0; local < output_count; ++local)
            {
                const int output_at = output_first + local;
                float sum = has_bias ? load_value(bias, channel) : 0.0f;
                const int residue = (output_at + padding) % stride;
                for (int tap = residue; tap < width; tap += stride)
                {
                    const int input_at = (output_at + padding - tap) / stride;
                    const int column = input_at - input_first;
                    if (input_at >= 0 && input_at < input_length && column >= 0 &&
                        column < input_count)
                        sum += load_value(
                            columns,
                            (static_cast<size_t>(channel) * width + tap) * input_count + column);
                }
                if (output_at < output_length)
                    store_value(out, static_cast<size_t>(channel) * output_length + output_at, sum);
            }
        return CL_SUCCESS;
    }
    if (name == "conv_1d" || name == "conv_1d_t4x4")
    {
        cl_mem input = mem_arg(kernel, 0), weight = mem_arg(kernel, 1), bias = mem_arg(kernel, 2),
               out = mem_arg(kernel, 3);
        const int input_channels = kernel_arg<int>(kernel, 4),
                  output_channels = kernel_arg<int>(kernel, 5);
        const int input_length = kernel_arg<int>(kernel, 6),
                  output_length = kernel_arg<int>(kernel, 7);
        const int width = kernel_arg<int>(kernel, 8);
        const int stride = name == "conv_1d" ? kernel_arg<int>(kernel, 9) : 1;
        const int padding =
            name == "conv_1d" ? kernel_arg<int>(kernel, 10) : kernel_arg<int>(kernel, 9);
        const int dilation =
            name == "conv_1d" ? kernel_arg<int>(kernel, 11) : kernel_arg<int>(kernel, 10);
        const int has_bias =
            name == "conv_1d" ? kernel_arg<int>(kernel, 12) : kernel_arg<int>(kernel, 11);
#ifdef PS5CL_HOST_OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int output_channel = 0; output_channel < output_channels; ++output_channel)
            for (int output_at = 0; output_at < output_length; ++output_at)
            {
                float sum = 0.0f;
                for (int input_channel = 0; input_channel < input_channels; ++input_channel)
                    for (int tap = 0; tap < width; ++tap)
                    {
                        const int input_at = output_at * stride + tap * dilation - padding;
                        if (input_at >= 0 && input_at < input_length)
                            sum += load_value(input,
                                              static_cast<size_t>(input_channel) * input_length +
                                                  input_at) *
                                   load_value(weight, (static_cast<size_t>(output_channel) *
                                                           input_channels +
                                                       input_channel) *
                                                              width +
                                                          tap);
                    }
                if (has_bias)
                    sum += load_value(bias, output_channel);
                store_value(out, static_cast<size_t>(output_channel) * output_length + output_at,
                            sum);
            }
        return CL_SUCCESS;
    }
    if (name == "conv_1d_t4x4v2")
    {
        cl_mem input = mem_arg(kernel, 0), weight = mem_arg(kernel, 1), bias = mem_arg(kernel, 2),
               out = mem_arg(kernel, 3);
        const int input_channels = kernel_arg<int>(kernel, 4),
                  output_channels = kernel_arg<int>(kernel, 5);
        const int input_length = kernel_arg<int>(kernel, 6),
                  output_length = kernel_arg<int>(kernel, 7);
        const int width = kernel_arg<int>(kernel, 8), padding = kernel_arg<int>(kernel, 9),
                  dilation = kernel_arg<int>(kernel, 10), has_bias = kernel_arg<int>(kernel, 11);
        const int inner = input_channels * width;
#ifdef PS5CL_HOST_OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int output_channel = 0; output_channel < output_channels; ++output_channel)
            for (int output_at = 0; output_at < output_length; ++output_at)
            {
                float sum = 0.0f;
                for (int input_channel = 0; input_channel < input_channels; ++input_channel)
                    for (int tap = 0; tap < width; ++tap)
                    {
                        const int input_at = output_at + tap * dilation - padding;
                        if (input_at >= 0 && input_at < input_length)
                        {
                            const size_t packed = (static_cast<size_t>(output_channel / 4) * inner +
                                                   input_channel * width + tap) *
                                                      4 +
                                                  output_channel % 4;
                            sum += load_value(input,
                                              static_cast<size_t>(input_channel) * input_length +
                                                  input_at) *
                                   load_value(weight, packed);
                        }
                    }
                if (has_bias)
                    sum += load_value(bias, output_channel);
                store_value(out, static_cast<size_t>(output_channel) * output_length + output_at,
                            sum);
            }
        return CL_SUCCESS;
    }
    if (name == "conv_transpose_1d" || name == "conv_transpose_1d_t4x4")
    {
        cl_mem input = mem_arg(kernel, 0), weight = mem_arg(kernel, 1), bias = mem_arg(kernel, 2),
               out = mem_arg(kernel, 3);
        const int input_channels = kernel_arg<int>(kernel, 4),
                  output_channels = kernel_arg<int>(kernel, 5);
        const int input_length = kernel_arg<int>(kernel, 6),
                  output_length = kernel_arg<int>(kernel, 7);
        const int width = kernel_arg<int>(kernel, 8), stride = kernel_arg<int>(kernel, 9),
                  padding = kernel_arg<int>(kernel, 10);
        const int dilation = name == "conv_transpose_1d" ? kernel_arg<int>(kernel, 11) : 1;
        const int has_bias =
            name == "conv_transpose_1d" ? kernel_arg<int>(kernel, 12) : kernel_arg<int>(kernel, 11);
#ifdef PS5CL_HOST_OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int output_channel = 0; output_channel < output_channels; ++output_channel)
            for (int output_at = 0; output_at < output_length; ++output_at)
            {
                float sum = 0.0f;
                for (int tap = 0; tap < width; ++tap)
                {
                    const int numerator = output_at + padding - tap * dilation;
                    if (numerator < 0 || numerator % stride)
                        continue;
                    const int input_at = numerator / stride;
                    if (input_at < 0 || input_at >= input_length)
                        continue;
                    for (int input_channel = 0; input_channel < input_channels; ++input_channel)
                        sum += load_value(input, static_cast<size_t>(input_channel) * input_length +
                                                     input_at) *
                               load_value(weight,
                                          (static_cast<size_t>(input_channel) * output_channels +
                                           output_channel) *
                                                  width +
                                              tap);
                }
                if (has_bias)
                    sum += load_value(bias, output_channel);
                store_value(out, static_cast<size_t>(output_channel) * output_length + output_at,
                            sum);
            }
        return CL_SUCCESS;
    }
    std::fprintf(stderr, "[ps5cl] unsupported kernel: %s\n", kernel->name.c_str());
    return CL_INVALID_KERNEL_NAME;
}

} // namespace
