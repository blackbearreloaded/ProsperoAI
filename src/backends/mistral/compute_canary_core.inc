/* One-workgroup PS5 GPU-compute canary.
 *
 * The default build captures a complete DCB but cannot submit it. Define
 * COMPUTE_CANARY_SUBMIT only for the lock-protected hardware candidate.
 */

#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include <ps5/kernel.h>

#ifdef COMPUTE_CANARY_STABLE_AUDIO_GEMM
#include "media_gemm_agc.inc"
#include "stable_audio_gemm.inc"
#define EMBEDDED_AGC_PACKAGE media_gemm_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN media_gemm_agc_package_len
#define COMPUTE_CANARY_MEDIA_GEMM 1
#elif defined(COMPUTE_CANARY_MEDIA_IM2COL)
#include "media_im2col_agc.inc"
#include "media_im2col.inc"
#define EMBEDDED_AGC_PACKAGE media_im2col_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN media_im2col_agc_package_len
#elif defined(COMPUTE_CANARY_MEDIA_GEMM)
#include "media_gemm_agc.inc"
#include "media_gemm.inc"
#define EMBEDDED_AGC_PACKAGE media_gemm_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN media_gemm_agc_package_len
#elif defined(COMPUTE_CANARY_MEDIA_LINEAR)
#include "media_linear_agc.inc"
#include "media_linear.inc"
#define EMBEDDED_AGC_PACKAGE media_linear_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN media_linear_agc_package_len
#elif defined(COMPUTE_CANARY_REAL_LAYER)
#include "real_layer_agc.inc"
#include "real_layer.inc"
#define EMBEDDED_AGC_PACKAGE real_layer_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN real_layer_agc_package_len
#elif defined(COMPUTE_CANARY_REAL_TENSOR)
#include "real_tensor_agc.inc"
#include "real_tensor.inc"
#define EMBEDDED_AGC_PACKAGE real_tensor_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN real_tensor_agc_package_len
#elif defined(COMPUTE_CANARY_Q4_0_F32_SCALE)
#include "q4_0_f32_software_agc.inc"
#define EMBEDDED_AGC_PACKAGE q4_0_f32_software_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN q4_0_f32_software_agc_package_len
#elif defined(COMPUTE_CANARY_Q4_0_SCALE)
#include "q4_0_software_agc.inc"
#define EMBEDDED_AGC_PACKAGE q4_0_software_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN q4_0_software_agc_package_len
#elif defined(COMPUTE_CANARY_Q4_SCALE)
#include "q4_software_agc.inc"
#define EMBEDDED_AGC_PACKAGE q4_software_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN q4_software_agc_package_len
#elif defined(COMPUTE_CANARY_TRANSFORMER)
#include "transformer_block_agc.inc"
#define EMBEDDED_AGC_PACKAGE transformer_block_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN transformer_block_agc_package_len
#elif defined(COMPUTE_CANARY_Q8_DOT2_MATRIX)
#include "q8_dot2_matrix_agc.inc"
#define EMBEDDED_AGC_PACKAGE q8_dot2_matrix_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN q8_dot2_matrix_agc_package_len
#elif defined(COMPUTE_CANARY_Q8_DOT_MATRIX)
#include "q8_dot_matrix_agc.inc"
#define EMBEDDED_AGC_PACKAGE q8_dot_matrix_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN q8_dot_matrix_agc_package_len
#elif defined(COMPUTE_CANARY_Q8_DOT_PROBE)
#include "q8_dot_probe_agc.inc"
#define EMBEDDED_AGC_PACKAGE q8_dot_probe_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN q8_dot_probe_agc_package_len
#elif defined(COMPUTE_CANARY_Q8_SOFTWARE)
#include "q8_software_agc.inc"
#define EMBEDDED_AGC_PACKAGE q8_software_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN q8_software_agc_package_len
#elif defined(COMPUTE_CANARY_Q8_SCALE)
#include "q8_scale_agc.inc"
#define EMBEDDED_AGC_PACKAGE q8_scale_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN q8_scale_agc_package_len
#elif defined(COMPUTE_CANARY_Q8_MATVEC)
#include "q8_matvec_agc.inc"
#define EMBEDDED_AGC_PACKAGE q8_matvec_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN q8_matvec_agc_package_len
#elif defined(COMPUTE_CANARY_MATVEC)
#include "matvec_agc.inc"
#define EMBEDDED_AGC_PACKAGE matvec_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN matvec_agc_package_len
#elif defined(COMPUTE_CANARY_READ_PROBE)
#include "read_probe_agc.inc"
#define EMBEDDED_AGC_PACKAGE read_probe_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN read_probe_agc_package_len
#elif defined(COMPUTE_CANARY_DOT_PROBE)
#include "dot_probe_agc.inc"
#define EMBEDDED_AGC_PACKAGE dot_probe_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN dot_probe_agc_package_len
#else
#include "fill_agc.inc"
#define EMBEDDED_AGC_PACKAGE fill_agc_package
#define EMBEDDED_AGC_PACKAGE_LEN fill_agc_package_len
#endif

#if defined(COMPUTE_CANARY_Q8_MATVEC) || defined(COMPUTE_CANARY_MATVEC) || \
    defined(COMPUTE_CANARY_READ_PROBE) || defined(COMPUTE_CANARY_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_MATRIX) || \
    defined(COMPUTE_CANARY_Q8_DOT2_MATRIX) || \
    defined(COMPUTE_CANARY_TRANSFORMER)
#ifdef COMPUTE_CANARY_MEDIA_IM2COL
#define WORK_BYTES 0x70000u
#define COMMAND_OFFSET 0x64000u
#define INPUT_OFFSET 0x4000u
#define WEIGHT_OFFSET 0x18000u
#define OUTPUT_OFFSET WEIGHT_OFFSET
#define CONTROL_OFFSET 0x60000u
#define MARKER_OFFSET 0x61000u
#define OUTPUT_DWORDS MEDIA_IM2COL_PACKED_DWORDS
#define DISPATCH_GROUPS_X ((OUTPUT_DWORDS + 63u) / 64u)
#define INPUT_DWORDS (MEDIA_IM2COL_N * MEDIA_IM2COL_IC * \
                      MEDIA_IM2COL_IH * MEDIA_IM2COL_IW)
#elif defined(COMPUTE_CANARY_MEDIA_GEMM)
#ifdef COMPUTE_CANARY_STABLE_AUDIO_GEMM
#define WORK_BYTES 0x220000u
#define COMMAND_OFFSET 0x21c000u
#define INPUT_OFFSET 0x4000u
#define WEIGHT_OFFSET 0x10000u
#define OUTPUT_OFFSET 0x210000u
#define CONTROL_OFFSET 0x218000u
#define MARKER_OFFSET 0x219000u
#else
#define WORK_BYTES 0x50000u
#define COMMAND_OFFSET 0x44000u
#define INPUT_OFFSET 0x4000u
#define WEIGHT_OFFSET 0x8000u
#define OUTPUT_OFFSET 0x3c000u
#define CONTROL_OFFSET 0x3e800u
#define MARKER_OFFSET 0x3f000u
#endif
#define OUTPUT_DWORDS (MEDIA_GEMM_ROWS * MEDIA_GEMM_BATCH)
#define DISPATCH_GROUPS_X MEDIA_GEMM_ROWS
#define DISPATCH_GROUPS_Y MEDIA_GEMM_BATCH
#define INPUT_DWORDS (MEDIA_GEMM_COLUMNS * MEDIA_GEMM_BATCH)
#elif defined(COMPUTE_CANARY_MEDIA_LINEAR)
#define WORK_BYTES 0x44000u
#define COMMAND_OFFSET 0x40000u
#define INPUT_OFFSET 0x4000u
#define WEIGHT_OFFSET 0x8000u
#define OUTPUT_OFFSET 0x3c000u
#define MARKER_OFFSET 0x3d000u
#define OUTPUT_DWORDS MEDIA_LINEAR_ROWS
#define DISPATCH_GROUPS_X MEDIA_LINEAR_ROWS
#define INPUT_DWORDS MEDIA_LINEAR_COLUMNS
#elif defined(COMPUTE_CANARY_REAL_LAYER)
#define WORK_BYTES 0x240000u
#define COMMAND_OFFSET 0x238000u
#define INPUT_OFFSET 0xc000u
#define WEIGHT_OFFSET 0x10000u
#define NORM_OFFSET 0x230000u
#define OUTPUT_OFFSET 0x232000u
#define CONTROL_OFFSET 0x234000u
#define MARKER_OFFSET 0x235000u
#define OUTPUT_DWORDS 1344u
#define DISPATCH_GROUPS_X 1u
#define INPUT_DWORDS 576u
#elif defined(COMPUTE_CANARY_REAL_TENSOR)
#define WORK_BYTES 0x48000u
#define COMMAND_OFFSET 0x40000u
#define INPUT_OFFSET 0x4000u
#define WEIGHT_OFFSET 0x8000u
#define OUTPUT_OFFSET 0x3c000u
#define MARKER_OFFSET 0x3d000u
#define OUTPUT_DWORDS 576u
#define DISPATCH_GROUPS_X 576u
#define INPUT_DWORDS 576u
#elif defined(COMPUTE_CANARY_TRANSFORMER)
#define WORK_BYTES 0x20000u
#define COMMAND_OFFSET 0x1c000u
#define INPUT_OFFSET 0x3000u
#define WEIGHT_OFFSET 0x4000u
#define OUTPUT_OFFSET 0xf000u
#define MARKER_OFFSET 0x10000u
#define OUTPUT_DWORDS 64u
#define DISPATCH_GROUPS_X 1u
#define INPUT_DWORDS 192u
#define WEIGHT_DWORDS 10240u
#elif defined(COMPUTE_CANARY_Q4_0_F32_SCALE)
#define WORK_BYTES 0x2820000u
#define COMMAND_OFFSET 0x281c000u
#define INPUT_OFFSET 0x4000u
#define WEIGHT_OFFSET 0x8000u
#define OUTPUT_OFFSET 0x2808000u
#define MARKER_OFFSET 0x2818000u
#define OUTPUT_DWORDS 16384u
#define DISPATCH_GROUPS_X 16384u
#define INPUT_DWORDS 4096u
#elif defined(COMPUTE_CANARY_Q4_0_SCALE)
#define WORK_BYTES 0x2420000u
#define COMMAND_OFFSET 0x241c000u
#define INPUT_OFFSET 0x4000u
#define WEIGHT_OFFSET 0x8000u
#define OUTPUT_OFFSET 0x2408000u
#define MARKER_OFFSET 0x2418000u
#define OUTPUT_DWORDS 16384u
#define DISPATCH_GROUPS_X 16384u
#define INPUT_DWORDS 4096u
#elif defined(COMPUTE_CANARY_Q4_SCALE)
#define WORK_BYTES 0x2020000u
#define COMMAND_OFFSET 0x201c000u
#define INPUT_OFFSET 0x4000u
#define WEIGHT_OFFSET 0x8000u
#define OUTPUT_OFFSET 0x2008000u
#define MARKER_OFFSET 0x2018000u
#define OUTPUT_DWORDS 16384u
#define DISPATCH_GROUPS_X 16384u
#define INPUT_DWORDS 4096u
#elif defined(COMPUTE_CANARY_Q8_SCALE)
#define WORK_BYTES 0x4020000u
#define COMMAND_OFFSET 0x401c000u
#define INPUT_OFFSET 0x4000u
#define WEIGHT_OFFSET 0x8000u
#define OUTPUT_OFFSET 0x4008000u
#define MARKER_OFFSET 0x4018000u
#define OUTPUT_DWORDS 16384u
#define DISPATCH_GROUPS_X 16384u
#define INPUT_DWORDS 4096u
#else
#define WORK_BYTES 0x10000u
#define COMMAND_OFFSET 0xc000u
#define INPUT_OFFSET 0x3000u
#define WEIGHT_OFFSET 0x4000u
#define OUTPUT_OFFSET 0x8000u
#define MARKER_OFFSET 0x9000u
#ifdef COMPUTE_CANARY_READ_PROBE
#define OUTPUT_DWORDS 128u
#elif defined(COMPUTE_CANARY_DOT_PROBE)
#define OUTPUT_DWORDS 1u
#elif defined(COMPUTE_CANARY_Q8_DOT2_MATRIX)
#define OUTPUT_DWORDS 6u
#elif defined(COMPUTE_CANARY_Q8_DOT_MATRIX)
#define OUTPUT_DWORDS 8u
#elif defined(COMPUTE_CANARY_Q8_DOT_PROBE)
#define OUTPUT_DWORDS 6u
#else
#define OUTPUT_DWORDS 64u
#endif
#define DISPATCH_GROUPS_X 1u
#define INPUT_DWORDS 64u
#endif
#if defined(COMPUTE_CANARY_TRANSFORMER)
/* Defined above because a decoder block contains differently shaped matrices. */
#elif defined(COMPUTE_CANARY_Q8_MATVEC)
#ifdef COMPUTE_CANARY_MEDIA_IM2COL
#define WEIGHT_DWORDS MEDIA_IM2COL_PACKED_DWORDS
#elif defined(COMPUTE_CANARY_MEDIA_LINEAR)
#define WEIGHT_DWORDS (MEDIA_LINEAR_ROWS * MEDIA_LINEAR_COLUMNS / 2u)
#elif defined(COMPUTE_CANARY_MEDIA_GEMM)
#define WEIGHT_DWORDS (MEDIA_GEMM_ROWS * MEDIA_GEMM_COLUMNS / 2u)
#elif defined(COMPUTE_CANARY_REAL_LAYER)
#define WEIGHT_DWORDS REAL_LAYER_WEIGHT_DWORDS
#elif defined(COMPUTE_CANARY_REAL_TENSOR)
#define WEIGHT_DWORDS 51840u
#elif defined(COMPUTE_CANARY_Q4_0_F32_SCALE)
#define WEIGHT_DWORDS 0xa00000u
#elif defined(COMPUTE_CANARY_Q4_0_SCALE)
#define WEIGHT_DWORDS 0x900000u
#elif defined(COMPUTE_CANARY_Q4_SCALE)
#define WEIGHT_DWORDS (OUTPUT_DWORDS * INPUT_DWORDS / 8u)
#else
#define WEIGHT_DWORDS (OUTPUT_DWORDS * INPUT_DWORDS / 4u)
#endif
#else
#define WEIGHT_DWORDS (64u * 64u)
#endif
#ifdef COMPUTE_CANARY_REAL_TENSOR
#define GPU_READ_INPUT_BYTES INPUT_DWORDS
#else
#define GPU_READ_INPUT_BYTES (INPUT_DWORDS * sizeof(float))
#endif
#define GPU_READ_WEIGHT_BYTES (WEIGHT_DWORDS * sizeof(uint32_t))
#ifdef COMPUTE_CANARY_Q4_0_SCALE
#ifdef COMPUTE_CANARY_REAL_TENSOR
#define Q4_0_SCALE_DWORDS 10368u
#elif defined(COMPUTE_CANARY_Q4_0_F32_SCALE)
#define Q4_0_SCALE_DWORDS 0x200000u
#else
#define Q4_0_SCALE_DWORDS 0x100000u
#endif
#define Q4_0_DATA_OFFSET Q4_0_SCALE_DWORDS
#endif
#ifdef COMPUTE_CANARY_REAL_LAYER
#define GPU_READ_ACQUIRE_BYTES (CONTROL_OFFSET + 21u * 4u - INPUT_OFFSET)
#elif defined(COMPUTE_CANARY_MEDIA_IM2COL)
#define GPU_READ_ACQUIRE_BYTES (CONTROL_OFFSET + 16u * 4u - INPUT_OFFSET)
#elif defined(COMPUTE_CANARY_MEDIA_GEMM)
#define GPU_READ_ACQUIRE_BYTES (CONTROL_OFFSET + 7u * 4u - INPUT_OFFSET)
#else
#define GPU_READ_ACQUIRE_BYTES                                              \
    (WEIGHT_OFFSET + GPU_READ_WEIGHT_BYTES - INPUT_OFFSET)
#endif
#define GPU_READ_ACQUIRE_GCR UINT32_C(0x4380)
#define GPU_READ_ACQUIRE_POLL UINT32_C(0xa0)
#elif defined(COMPUTE_CANARY_SCALE)
#define WORK_BYTES 0x408000u
#define COMMAND_OFFSET 0x404000u
#define OUTPUT_OFFSET 0x3000u
#define MARKER_OFFSET 0x403000u
#define OUTPUT_DWORDS 0x100000u
#define DISPATCH_GROUPS_X 0x4000u
#else
#define WORK_BYTES 0x10000u
#define COMMAND_OFFSET 0x8000u
#define OUTPUT_OFFSET 0x3000u
#define MARKER_OFFSET 0x4000u
#define OUTPUT_DWORDS 64u
#define DISPATCH_GROUPS_X 1u
#endif
#define COMMAND_BYTES 0x4000u
#ifndef DISPATCH_GROUPS_Y
#define DISPATCH_GROUPS_Y 1u
#endif
#define DIRECT_MEMORY_TYPE 12
#define MAP_PROTECTION 0x33
#define COMPUTE_USER_DATA_0 0x240u
#define COMPUTE_USER_DWORDS 3u
#define DESCRIPTOR_USER_DWORD 2u
#define OUTPUT_BYTES (OUTPUT_DWORDS * sizeof(uint32_t))
#define RAW_BUFFER_WORD3 UINT32_C(0x31016fac)
#define COMPLETION_MARKER UINT32_C(0x50533543)
#ifndef COMPLETION_POLLS
#define COMPLETION_POLLS 2000u
#endif
#ifdef COMPUTE_CANARY_PERSISTENT
#define DISPATCH_REPEATS 256u
#else
#define DISPATCH_REPEATS 1u
#endif
#ifdef COMPUTE_CANARY_REAL_LAYER
#define SHADER_CODE_OFFSET 0x4000u
#define SHADER_CODE_BYTES 0x7000u
#define DESCRIPTOR_OFFSET 0xb000u
#else
#define SHADER_CODE_OFFSET 0x1000u
#define SHADER_CODE_BYTES 0x1000u
#define DESCRIPTOR_OFFSET 0x2000u
#endif
#if defined(COMPUTE_CANARY_Q8_MATVEC) || defined(COMPUTE_CANARY_MATVEC) || \
    defined(COMPUTE_CANARY_DOT_PROBE) || defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_MATRIX) || \
    defined(COMPUTE_CANARY_Q8_DOT2_MATRIX) || \
    defined(COMPUTE_CANARY_TRANSFORMER)
/* ACO compiled these arithmetic candidates for wave32; bit 15 is CS_W32_EN. */
#define COMPUTE_DISPATCH_MODIFIER UINT32_C(0x00008000)
#else
#define COMPUTE_DISPATCH_MODIFIER UINT32_C(0)
#endif

int64_t sceKernelGetDirectMemorySize(void);
int32_t sceKernelAllocateDirectMemory(int64_t, int64_t, size_t, size_t, int,
                                      int64_t *);
int32_t sceKernelMapDirectMemory(void **, size_t, int, int, int64_t, size_t);
int32_t sceKernelReleaseDirectMemory(int64_t, size_t);
int32_t sceKernelUsleep(uint32_t);
#if defined(COMPUTE_CANARY_SUBMIT) && \
    (defined(COMPUTE_CANARY_SCALE) || defined(COMPUTE_CANARY_Q8_SCALE) || \
     defined(COMPUTE_CANARY_TRANSFORMER) || \
     defined(COMPUTE_CANARY_MEDIA_GEMM) || \
     defined(COMPUTE_CANARY_MEDIA_IM2COL))
uint64_t sceKernelGetProcessTime(void);
#endif

typedef struct agc_register {
    uint16_t offset;
    uint16_t padding;
    uint32_t value;
} agc_register_t;

typedef struct agc_command_buffer {
    uint32_t *bottom;
    uint32_t *top;
    uint32_t *up;
    uint32_t *down;
    uintptr_t callback;
    void *user_data;
    uint32_t reserved_dwords;
    uint32_t padding;
} agc_command_buffer_t;

typedef struct agc_submit_description {
    void *words;
    uint32_t word_count;
    uint8_t flag;
    uint8_t padding[3];
} agc_submit_description_t;

typedef struct agc_api {
    int (*init)(uint32_t);
    int (*create_shader)(void **, void *, void *);
    uint32_t *(*set_sh)(void *, const void *, uint32_t);
    uint32_t *(*set_sh_direct)(void *, uint32_t, const uint32_t *, uint32_t);
#if defined(COMPUTE_CANARY_Q8_MATVEC) || defined(COMPUTE_CANARY_MATVEC) || \
    defined(COMPUTE_CANARY_READ_PROBE) || defined(COMPUTE_CANARY_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_MATRIX) || \
    defined(COMPUTE_CANARY_Q8_DOT2_MATRIX) || \
    defined(COMPUTE_CANARY_TRANSFORMER)
    uint32_t *(*acquire_mem)(void *, uint8_t, uint32_t, uint32_t, uint64_t,
                             uint64_t, uint32_t);
#endif
    uint32_t *(*dispatch)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t *(*release_mem)(void *, uint8_t, int16_t, uint64_t, int8_t,
                             void *, uint32_t, uint64_t, uint16_t, uint16_t,
                             int8_t, int32_t);
#ifdef COMPUTE_CANARY_SUBMIT
    int (*suspend_point)(void);
    int (*submit)(const agc_submit_description_t *);
#endif
} agc_api_t;

#ifdef COMPUTE_CANARY_LINKED_AGC
int sceAgcInit(uint32_t);
int sceAgcCreateShader(void **, void *, void *);
uint32_t *sceAgcDcbSetShRegistersIndirect(void *, const void *, uint32_t);
uint32_t *sceAgcCbSetShRegisterRangeDirect(void *, uint32_t, const uint32_t *,
                                           uint32_t);
uint32_t *sceAgcDcbAcquireMem(void *, uint8_t, uint32_t, uint32_t, uint64_t,
                              uint64_t, uint32_t);
uint32_t *sceAgcCbDispatch(void *, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t *sceAgcCbReleaseMem(void *, uint8_t, int16_t, uint64_t, int8_t, void *,
                             uint32_t, uint64_t, uint16_t, uint16_t, int8_t,
                             int32_t);
#ifdef COMPUTE_CANARY_SUBMIT
int sceAgcSuspendPoint(void);
int sceAgcDriverSubmitDcb(const agc_submit_description_t *);
#endif
#endif

static volatile unsigned out_of_space;

static uint8_t command_out_of_space(agc_command_buffer_t *buffer,
                                    uint32_t words, void *user_data)
{
    (void)buffer;
    (void)words;
    (void)user_data;
    out_of_space = 1;
    return 0;
}

static uint32_t fnv1a32(const void *data, size_t bytes)
{
    const uint8_t *at = data;
    uint32_t hash = UINT32_C(2166136261);

    while (bytes--) {
        hash ^= *at++;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

#ifdef COMPUTE_CANARY_TRANSFORMER
#define TRANSFORMER_WIDTH 64u
#define TRANSFORMER_FF_WIDTH 128u
#define TRANSFORMER_Q_BASE 0u
#define TRANSFORMER_K_BASE 1024u
#define TRANSFORMER_V_BASE 2048u
#define TRANSFORMER_O_BASE 3072u
#define TRANSFORMER_GATE_BASE 4096u
#define TRANSFORMER_UP_BASE 6144u
#define TRANSFORMER_DOWN_BASE 8192u

#ifdef COMPUTE_CANARY_SUBMIT
static int transformer_weight(const uint32_t *weights, unsigned base,
                              unsigned row, unsigned column,
                              unsigned columns)
{
    uint32_t packed = weights[base + row * (columns / 4u) + column / 4u];
    int value = (int)((packed >> ((column & 3u) * 8u)) & 0xffu);

    return value >= 128 ? value - 256 : value;
}

static float transformer_dot(const uint32_t *weights, unsigned base,
                             unsigned row, unsigned columns,
                             const float *values)
{
    float sum = 0.0f;
    unsigned column;

    for (column = 0; column < columns; ++column)
        sum += (float)transformer_weight(weights, base, row, column, columns) *
               values[column];
    return sum * 0.03125f;
}

static void transformer_norm(const float *input, float *output)
{
    float sum = 0.0f;
    float scale;
    unsigned index;

    for (index = 0; index < TRANSFORMER_WIDTH; ++index)
        sum += input[index] * input[index];
    scale = 1.0f / sqrtf(sum / (float)TRANSFORMER_WIDTH + 0.00001f);
    for (index = 0; index < TRANSFORMER_WIDTH; ++index)
        output[index] = input[index] * scale;
}

static void transformer_reference(const float *state, const uint32_t *weights,
                                  float *output)
{
    float norm[64], q[64], k[64], v[64], attended[64], projected[64];
    float norm2[64], ffn[128], probabilities[8];
    unsigned head, index, row;

    transformer_norm(state, norm);
    for (row = 0; row < TRANSFORMER_WIDTH; ++row) {
        q[row] = transformer_dot(weights, TRANSFORMER_Q_BASE, row,
                                 TRANSFORMER_WIDTH, norm);
        k[row] = transformer_dot(weights, TRANSFORMER_K_BASE, row,
                                 TRANSFORMER_WIDTH, norm);
        v[row] = transformer_dot(weights, TRANSFORMER_V_BASE, row,
                                 TRANSFORMER_WIDTH, norm);
    }
    for (head = 0; head < 4u; ++head) {
        float scores[2] = {0.0f, 0.0f};
        float peak, a, b;

        for (index = 0; index < 16u; ++index) {
            unsigned at = head * 16u + index;

            scores[0] += q[at] * state[TRANSFORMER_WIDTH + at];
            scores[1] += q[at] * k[at];
        }
        scores[0] *= 0.25f;
        scores[1] *= 0.25f;
        peak = scores[0] > scores[1] ? scores[0] : scores[1];
        a = expf(scores[0] - peak);
        b = expf(scores[1] - peak);
        probabilities[head * 2u] = a / (a + b);
        probabilities[head * 2u + 1u] = b / (a + b);
    }
    for (index = 0; index < TRANSFORMER_WIDTH; ++index) {
        head = index / 16u;
        attended[index] =
            probabilities[head * 2u] * state[2u * TRANSFORMER_WIDTH + index] +
            probabilities[head * 2u + 1u] * v[index];
    }
    for (row = 0; row < TRANSFORMER_WIDTH; ++row)
        projected[row] = state[row] +
                         transformer_dot(weights, TRANSFORMER_O_BASE, row,
                                         TRANSFORMER_WIDTH, attended);
    transformer_norm(projected, norm2);
    for (row = 0; row < TRANSFORMER_FF_WIDTH; ++row) {
        float gate = transformer_dot(weights, TRANSFORMER_GATE_BASE, row,
                                     TRANSFORMER_WIDTH, norm2);
        float up = transformer_dot(weights, TRANSFORMER_UP_BASE, row,
                                   TRANSFORMER_WIDTH, norm2);

        ffn[row] = gate / (1.0f + expf(-gate)) * up;
    }
    for (row = 0; row < TRANSFORMER_WIDTH; ++row)
        output[row] = projected[row] +
                      transformer_dot(weights, TRANSFORMER_DOWN_BASE, row,
                                      TRANSFORMER_FF_WIDTH, ffn);
}
#endif
#endif

static void set_raw_descriptor(uint32_t *descriptor, const void *buffer,
                               uint32_t bytes)
{
    descriptor[0] = (uint32_t)(uintptr_t)buffer;
    descriptor[1] = (uint32_t)((uintptr_t)buffer >> 32);
    descriptor[2] = bytes;
    descriptor[3] = RAW_BUFFER_WORD3;
}

#ifdef COMPUTE_CANARY_SUBMIT
static void flush_cache(const void *address, size_t bytes)
{
    const uint8_t *at = address;
    const uint8_t *end = at + bytes;

    for (; at < end; at += 64)
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}
#endif

static int shader_sections(const uint8_t *elf, size_t elf_size,
                           const uint8_t **header, size_t *header_size,
                           const uint8_t **code, size_t *code_size)
{
    uint64_t section_offset;
    uint16_t entry_size, count, names_index, index;
    const uint8_t *names;
    size_t names_size;

    if (elf_size < 64 || memcmp(elf, "\x7f" "ELF", 4) != 0)
        return -1;
    memcpy(&section_offset, elf + 40, sizeof(section_offset));
    memcpy(&entry_size, elf + 58, sizeof(entry_size));
    memcpy(&count, elf + 60, sizeof(count));
    memcpy(&names_index, elf + 62, sizeof(names_index));
    if (!section_offset || entry_size < 64 || !count || names_index >= count ||
        section_offset > elf_size ||
        (size_t)count > (elf_size - (size_t)section_offset) / entry_size)
        return -1;
    {
        const uint8_t *record =
            elf + section_offset + names_index * entry_size;
        uint64_t offset, size;

        memcpy(&offset, record + 24, sizeof(offset));
        memcpy(&size, record + 32, sizeof(size));
        if (offset > elf_size || size > elf_size - (size_t)offset)
            return -1;
        names = elf + offset;
        names_size = (size_t)size;
    }

    *header = NULL;
    *code = NULL;
    for (index = 0; index < count; ++index) {
        const uint8_t *record = elf + section_offset + index * entry_size;
        uint32_t name_offset;
        uint64_t offset, size;
        const char *name;

        memcpy(&name_offset, record, sizeof(name_offset));
        memcpy(&offset, record + 24, sizeof(offset));
        memcpy(&size, record + 32, sizeof(size));
        if (name_offset >= names_size || offset > elf_size ||
            size > elf_size - (size_t)offset ||
            !memchr(names + name_offset, 0, names_size - name_offset))
            return -1;
        name = (const char *)names + name_offset;
        if (strcmp(name, ".shader_header") == 0) {
            *header = elf + offset;
            *header_size = (size_t)size;
        } else if (strcmp(name, ".shader_text") == 0) {
            *code = elf + offset;
            *code_size = (size_t)size;
        }
    }
    return *header && *code && *header_size >= 96 && *code_size ? 0 : -1;
}

static int validate_shader_header(const uint8_t *header, size_t header_size,
                                  size_t code_size)
{
    uint32_t magic, version, declared_header, declared_code;
    uint64_t resource_offset, serialized_code;

    if (header_size < 96)
        return -1;
    memcpy(&magic, header, sizeof(magic));
    memcpy(&version, header + 4, sizeof(version));
    memcpy(&resource_offset, header + 8, sizeof(resource_offset));
    memcpy(&serialized_code, header + 16, sizeof(serialized_code));
    memcpy(&declared_header, header + 64, sizeof(declared_header));
    memcpy(&declared_code, header + 68, sizeof(declared_code));
    if (magic != UINT32_C(0x34333231) || version != 24 ||
        !resource_offset || serialized_code || declared_header != header_size ||
        declared_code != code_size || header[0x5a] != 0)
        return -1;
    return 0;
}

static int load_agc(void *module, agc_api_t *agc)
{
#ifdef COMPUTE_CANARY_LINKED_AGC
    (void)module;
    agc->init = sceAgcInit;
    agc->create_shader = sceAgcCreateShader;
    agc->set_sh = sceAgcDcbSetShRegistersIndirect;
    agc->set_sh_direct = sceAgcCbSetShRegisterRangeDirect;
    agc->acquire_mem = sceAgcDcbAcquireMem;
    agc->dispatch = sceAgcCbDispatch;
    agc->release_mem = sceAgcCbReleaseMem;
#ifdef COMPUTE_CANARY_SUBMIT
    agc->suspend_point = sceAgcSuspendPoint;
    agc->submit = sceAgcDriverSubmitDcb;
#endif
    return 0;
#else
#define LOAD(member, symbol)                                                \
    do {                                                                    \
        *(void **)(&(agc->member)) = dlsym((module), (symbol));             \
        if (!(agc->member))                                                 \
            return -1;                                                      \
    } while (0)
    LOAD(init, "sceAgcInit");
    LOAD(create_shader, "sceAgcCreateShader");
    LOAD(set_sh, "sceAgcDcbSetShRegistersIndirect");
    LOAD(set_sh_direct, "sceAgcCbSetShRegisterRangeDirect");
#if defined(COMPUTE_CANARY_Q8_MATVEC) || defined(COMPUTE_CANARY_MATVEC) || \
    defined(COMPUTE_CANARY_READ_PROBE) || defined(COMPUTE_CANARY_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_MATRIX) || \
    defined(COMPUTE_CANARY_Q8_DOT2_MATRIX) || \
    defined(COMPUTE_CANARY_TRANSFORMER)
    LOAD(acquire_mem, "sceAgcDcbAcquireMem");
#endif
    LOAD(dispatch, "sceAgcCbDispatch");
    LOAD(release_mem, "sceAgcCbReleaseMem");
#ifdef COMPUTE_CANARY_SUBMIT
    LOAD(suspend_point, "sceAgcSuspendPoint");
#endif
#undef LOAD
    return 0;
#endif
}

#ifdef COMPUTE_CANARY_SUBMIT
static unsigned wait_for_marker(volatile uint32_t *marker)
{
    unsigned polls;

    for (polls = 0; polls < COMPLETION_POLLS; ++polls) {
        flush_cache((const void *)marker, sizeof(*marker));
        if (*marker == COMPLETION_MARKER)
            return polls + 1;
        sceKernelUsleep(1000);
    }
    return 0;
}
#endif

int main(void)
{
    const uint8_t *header, *code;
    size_t header_size = 0, code_size = 0;
    int64_t direct_start = -1;
    uint8_t *memory = NULL;
#ifndef COMPUTE_CANARY_LINKED_AGC
    void *agc_module = NULL;
#endif
#if defined(COMPUTE_CANARY_SUBMIT) && !defined(COMPUTE_CANARY_LINKED_AGC)
    void *driver_module = NULL;
#endif
    agc_api_t agc = {0};
    void *shader = NULL;
    agc_register_t *program;
    uint8_t program_count;
    uint32_t *descriptor, *output, *words;
#if defined(COMPUTE_CANARY_Q8_MATVEC) || defined(COMPUTE_CANARY_MATVEC) || \
    defined(COMPUTE_CANARY_READ_PROBE) || defined(COMPUTE_CANARY_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_MATRIX) || \
    defined(COMPUTE_CANARY_Q8_DOT2_MATRIX) || \
    defined(COMPUTE_CANARY_TRANSFORMER)
    float *input, *fp_output;
#ifdef COMPUTE_CANARY_REAL_LAYER
    float *norm_weights;
    uint32_t *control;
#elif defined(COMPUTE_CANARY_MEDIA_GEMM) || \
    defined(COMPUTE_CANARY_MEDIA_IM2COL)
    uint32_t *control;
#endif
#if defined(COMPUTE_CANARY_Q8_MATVEC) || defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_MATRIX) || \
    defined(COMPUTE_CANARY_Q8_DOT2_MATRIX) || \
    defined(COMPUTE_CANARY_TRANSFORMER)
    uint32_t *packed_weights;
#ifdef COMPUTE_CANARY_Q8_DOT
    uint32_t *packed_input;
#endif
#if defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_MATRIX) || \
    defined(COMPUTE_CANARY_Q8_DOT2_MATRIX)
    uint32_t *probe_input;
#endif
#else
    float *weights;
#endif
#endif
    volatile uint32_t *marker;
    agc_command_buffer_t command = {0};
    uint32_t user_data[COMPUTE_USER_DWORDS] = {0};
    uint32_t word_count = 0, dcb_hash = 0;
    int init_rc = -1, init_errno = 0, create_rc = -1, result = 1;
    unsigned index;

    if (shader_sections(EMBEDDED_AGC_PACKAGE, EMBEDDED_AGC_PACKAGE_LEN,
                        &header, &header_size, &code, &code_size) != 0 ||
        validate_shader_header(header, header_size, code_size) != 0 ||
        header_size > 0x1000 || code_size > SHADER_CODE_BYTES) {
        printf("[ps5_compute] invalid embedded package\n");
        return 2;
    }

#ifdef COMPUTE_CANARY_LINKED_AGC
    if (load_agc(NULL, &agc) != 0) {
#else
    agc_module = dlopen("libSceAgc.sprx", RTLD_NOW | RTLD_LOCAL);
    if (!agc_module || load_agc(agc_module, &agc) != 0) {
#endif
        printf("[ps5_compute] AGC load failed: %s\n", dlerror());
        goto cleanup;
    }
    errno = 0;
    init_rc = agc.init(8);
    init_errno = errno;
    if (init_rc != 0)
        goto receipt;

#ifdef COMPUTE_CANARY_SUBMIT
#ifndef COMPUTE_CANARY_LINKED_AGC
    driver_module = dlopen("libSceAgcDriver.sprx", RTLD_NOW | RTLD_LOCAL);
    if (driver_module)
        *(void **)(&agc.submit) =
            dlsym(driver_module, "sceAgcDriverSubmitDcb");
    if (!driver_module || !agc.submit) {
        printf("[ps5_compute] AGC driver load failed: %s\n", dlerror());
        goto cleanup;
    }
#endif
#endif

    if (sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(),
                                      WORK_BYTES, 0x4000,
                                      DIRECT_MEMORY_TYPE, &direct_start) != 0 ||
        sceKernelMapDirectMemory((void **)&memory, WORK_BYTES, MAP_PROTECTION,
                                 0, direct_start, 0x4000) != 0 || !memory)
        goto receipt;

    memset(memory, 0, WORK_BYTES);
    memcpy(memory, header, header_size);
    memcpy(memory + SHADER_CODE_OFFSET, code, code_size);
    create_rc = agc.create_shader(&shader, memory,
                                  memory + SHADER_CODE_OFFSET);
    if (create_rc != 0 || shader != memory || memory[0x5a] != 0 ||
        *(void **)(memory + 16) != memory + SHADER_CODE_OFFSET)
        goto receipt;

    program = *(agc_register_t **)(memory + 32);
    program_count = memory[92];
    if (program_count != 8 || program < (agc_register_t *)memory ||
        (uint8_t *)program > memory + WORK_BYTES ||
        (size_t)program_count >
            (size_t)(memory + WORK_BYTES - (uint8_t *)program) /
                sizeof(*program))
        goto receipt;

    descriptor = (uint32_t *)(memory + DESCRIPTOR_OFFSET);
    output = (uint32_t *)(memory + OUTPUT_OFFSET);
    marker = (volatile uint32_t *)(memory + MARKER_OFFSET);
    words = (uint32_t *)(memory + COMMAND_OFFSET);
    if ((uint32_t)((uintptr_t)memory >> 32) != 2u)
        goto receipt;

#if defined(COMPUTE_CANARY_Q8_MATVEC) || defined(COMPUTE_CANARY_MATVEC) || \
    defined(COMPUTE_CANARY_READ_PROBE) || defined(COMPUTE_CANARY_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_MATRIX) || \
    defined(COMPUTE_CANARY_Q8_DOT2_MATRIX) || \
    defined(COMPUTE_CANARY_TRANSFORMER)
    input = (float *)(memory + INPUT_OFFSET);
    fp_output = (float *)output;
    set_raw_descriptor(descriptor, input, GPU_READ_INPUT_BYTES);
#if defined(COMPUTE_CANARY_Q8_MATVEC) || defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_MATRIX) || \
    defined(COMPUTE_CANARY_Q8_DOT2_MATRIX) || \
    defined(COMPUTE_CANARY_TRANSFORMER)
#if defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_MATRIX) || \
    defined(COMPUTE_CANARY_Q8_DOT2_MATRIX)
    probe_input = (uint32_t *)input;
#endif
#ifdef COMPUTE_CANARY_Q8_DOT
    packed_input = (uint32_t *)input;
#endif
    packed_weights = (uint32_t *)(memory + WEIGHT_OFFSET);
    set_raw_descriptor(descriptor + 4, packed_weights, GPU_READ_WEIGHT_BYTES);
#else
    weights = (float *)(memory + WEIGHT_OFFSET);
    set_raw_descriptor(descriptor + 4, weights, GPU_READ_WEIGHT_BYTES);
#endif
#ifdef COMPUTE_CANARY_REAL_LAYER
    norm_weights = (float *)(memory + NORM_OFFSET);
    control = (uint32_t *)(memory + CONTROL_OFFSET);
    set_raw_descriptor(descriptor + 8, norm_weights, 1152u * sizeof(float));
    set_raw_descriptor(descriptor + 12, fp_output,
                       OUTPUT_DWORDS * sizeof(*fp_output));
    set_raw_descriptor(descriptor + 16, control, 21u * sizeof(*control));
#elif defined(COMPUTE_CANARY_MEDIA_GEMM)
    control = (uint32_t *)(memory + CONTROL_OFFSET);
    set_raw_descriptor(descriptor + 8, fp_output,
                       OUTPUT_DWORDS * sizeof(*fp_output));
    set_raw_descriptor(descriptor + 12, control, 7u * sizeof(*control));
#elif defined(COMPUTE_CANARY_MEDIA_IM2COL)
    control = (uint32_t *)(memory + CONTROL_OFFSET);
    set_raw_descriptor(descriptor + 8, control, 16u * sizeof(*control));
#else
    set_raw_descriptor(descriptor + 8, fp_output,
                       OUTPUT_DWORDS * sizeof(*fp_output));
#endif
#ifdef COMPUTE_CANARY_MEDIA_IM2COL
    {
        static const uint32_t params[16] = {
            MEDIA_IM2COL_IW, MEDIA_IM2COL_IH, MEDIA_IM2COL_IC,
            MEDIA_IM2COL_OW, MEDIA_IM2COL_OH, MEDIA_IM2COL_KW,
            MEDIA_IM2COL_KH, MEDIA_IM2COL_N,
            1u, 1u, 1u, 1u, 1u, 1u,
            MEDIA_IM2COL_IW * MEDIA_IM2COL_IH,
            MEDIA_IM2COL_IC * MEDIA_IM2COL_IW * MEDIA_IM2COL_IH,
        };

        memcpy(control, params, sizeof(params));
    }
    memcpy(input, media_im2col_input_bits, sizeof(media_im2col_input_bits));
#elif defined(COMPUTE_CANARY_MEDIA_GEMM)
    {
        static const uint32_t params[7] = {
            MEDIA_GEMM_ROWS, MEDIA_GEMM_COLUMNS, MEDIA_GEMM_BATCH,
            MEDIA_GEMM_COLUMNS, MEDIA_GEMM_COLUMNS, MEDIA_GEMM_ROWS,
            0u,
        };

        memcpy(control, params, sizeof(params));
    }
    memcpy(input, media_gemm_input_bits, sizeof(media_gemm_input_bits));
    memcpy(packed_weights, media_gemm_weight_bits,
           sizeof(media_gemm_weight_bits));
#elif defined(COMPUTE_CANARY_MEDIA_LINEAR)
    memcpy(input, media_linear_input_bits, sizeof(media_linear_input_bits));
    memcpy(packed_weights, media_linear_weight_bits,
           sizeof(media_linear_weight_bits));
#elif defined(COMPUTE_CANARY_REAL_LAYER)
    {
        static const uint32_t params[21] = {
            REAL_LAYER_Q_OFFSET, 576u, 576u,
            REAL_LAYER_K_OFFSET, 192u, 576u,
            REAL_LAYER_V_OFFSET, 192u, 576u,
            REAL_LAYER_O_OFFSET, 576u, 576u,
            REAL_LAYER_GATE_OFFSET, 1536u, 576u,
            REAL_LAYER_UP_OFFSET, 1536u, 576u,
            REAL_LAYER_DOWN_OFFSET, 576u, 1536u,
        };

        memcpy(input, real_layer_input_bits, sizeof(real_layer_input_bits));
        memcpy(packed_weights, real_layer_weights,
               sizeof(real_layer_weights));
        memcpy(norm_weights, real_layer_norm_bits,
               sizeof(real_layer_norm_bits));
        memcpy(control, params, sizeof(params));
    }
#elif defined(COMPUTE_CANARY_TRANSFORMER)
    for (index = 0; index < TRANSFORMER_WIDTH; ++index) {
        input[index] = (float)((int)(index % 9u) - 4) * 0.125f;
        input[TRANSFORMER_WIDTH + index] =
            (float)((int)((index * 3u) % 11u) - 5) * 0.0625f;
        input[2u * TRANSFORMER_WIDTH + index] =
            (float)((int)((index * 5u) % 13u) - 6) * 0.0625f;
    }
    for (index = 0; index < WEIGHT_DWORDS; ++index) {
        uint32_t packed = 0;
        unsigned byte_index;

        for (byte_index = 0; byte_index < 4u; ++byte_index) {
            unsigned element = index * 4u + byte_index;
            int weight = (int)((element * 5u + element / 64u * 3u +
                                element / 1024u) % 7u) - 3;

            packed |= (uint32_t)(uint8_t)(int8_t)weight << (byte_index * 8u);
        }
        packed_weights[index] = packed;
    }
#elif defined(COMPUTE_CANARY_Q8_DOT2_MATRIX)
    memset(probe_input, 0, GPU_READ_INPUT_BYTES);
    memset(packed_weights, 0, GPU_READ_WEIGHT_BYTES);
    probe_input[0] = UINT32_C(0xfffe0003);
    packed_weights[0] = UINT32_C(0x0004fffb);
#elif defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
      defined(COMPUTE_CANARY_Q8_DOT_MATRIX)
    memset(probe_input, 0, GPU_READ_INPUT_BYTES);
    memset(packed_weights, 0, GPU_READ_WEIGHT_BYTES);
    for (index = 0; index < 16; ++index) {
        if (index & 1u) {
            probe_input[index] = UINT32_C(0xff01807f);
            packed_weights[index] = UINT32_C(0xffff0101);
        } else {
            probe_input[index] = UINT32_C(0x04fd02ff);
            packed_weights[index] = UINT32_C(0xf807fa05);
        }
    }
#elif defined(COMPUTE_CANARY_Q8_DOT)
#ifdef COMPUTE_CANARY_REAL_TENSOR
    memcpy(packed_input, real_tensor_packed_input,
           sizeof(real_tensor_packed_input));
#else
    for (index = 0; index < INPUT_DWORDS / 4u; ++index) {
        uint32_t packed = 0;
        unsigned byte_index;

        for (byte_index = 0; byte_index < 4; ++byte_index) {
            unsigned column = index * 4u + byte_index;
            int activation = ((int)(column % 7u) - 3) * 4;

            packed |= (uint32_t)(uint8_t)(int8_t)activation <<
                      (byte_index * 8u);
        }
        packed_input[index] = packed;
    }
#endif
#else
    for (index = 0; index < INPUT_DWORDS; ++index)
        input[index] = (float)((int)(index % 7u) - 3);
#endif
#ifndef COMPUTE_CANARY_REAL_LAYER
#if defined(COMPUTE_CANARY_MEDIA_LINEAR) || \
    defined(COMPUTE_CANARY_MEDIA_GEMM) || \
    defined(COMPUTE_CANARY_MEDIA_IM2COL)
    /* The pinned real-model fixture was copied above. */
#elif defined(COMPUTE_CANARY_Q8_MATVEC)
#ifdef COMPUTE_CANARY_REAL_TENSOR
    memcpy(packed_weights, real_tensor_weights, sizeof(real_tensor_weights));
#elif defined(COMPUTE_CANARY_Q4_0_SCALE)
#ifdef COMPUTE_CANARY_Q4_0_F32_SCALE
    for (index = 0; index < Q4_0_SCALE_DWORDS; ++index)
        packed_weights[index] =
            index & 1u ? UINT32_C(0x3f000000) : UINT32_C(0x3e800000);
#else
    for (index = 0; index < Q4_0_SCALE_DWORDS; ++index)
        packed_weights[index] = UINT32_C(0x38003400);
#endif
    for (index = 0; index < 0x800000u; ++index) {
        unsigned block = index / 4u;
        unsigned word = index % 4u;
        unsigned row = block / 128u;
        unsigned row_block = block % 128u;
        uint32_t packed = 0;
        unsigned byte_index;

        for (byte_index = 0; byte_index < 4u; ++byte_index) {
            unsigned column0 = row_block * 32u + word * 4u + byte_index;
            unsigned column1 = column0 + 16u;
            int weight0 = (int)((row * 3u + column0 * 5u) % 15u) - 7;
            int weight1 = (int)((row * 3u + column1 * 5u) % 15u) - 7;
            uint32_t byte = (uint32_t)(weight0 + 8) |
                            (uint32_t)(weight1 + 8) << 4u;

            packed |= byte << (byte_index * 8u);
        }
        packed_weights[Q4_0_DATA_OFFSET + index] = packed;
    }
#elif defined(COMPUTE_CANARY_Q4_SCALE)
    for (index = 0; index < WEIGHT_DWORDS; ++index) {
        uint32_t packed = 0;
        unsigned nibble_index;

        for (nibble_index = 0; nibble_index < 8u; ++nibble_index) {
            unsigned element = index * 8u + nibble_index;
            unsigned row = element / INPUT_DWORDS;
            unsigned column = element % INPUT_DWORDS;
            int weight = (int)((row * 3u + column * 5u) % 15u) - 7;

            packed |= (uint32_t)(weight & 0xf) << (nibble_index * 4u);
        }
        packed_weights[index] = packed;
    }
#else
    for (index = 0; index < WEIGHT_DWORDS; ++index) {
        uint32_t packed = 0;
        unsigned byte_index;

        for (byte_index = 0; byte_index < 4; ++byte_index) {
            unsigned element = index * 4u + byte_index;
            unsigned row = element / INPUT_DWORDS;
            unsigned column = element % INPUT_DWORDS;
            int weight = (int)((row * 3u + column * 5u) % 15u) - 7;

            packed |= (uint32_t)(uint8_t)(int8_t)weight << (byte_index * 8u);
        }
        packed_weights[index] = packed;
    }
#endif
#elif !defined(COMPUTE_CANARY_Q8_DOT_PROBE) && \
      !defined(COMPUTE_CANARY_Q8_DOT_MATRIX) && \
      !defined(COMPUTE_CANARY_Q8_DOT2_MATRIX) && \
      !defined(COMPUTE_CANARY_TRANSFORMER)
    for (index = 0; index < WEIGHT_DWORDS; ++index) {
        unsigned row = index / 64u;
        unsigned column = index % 64u;

        weights[index] = (float)((int)((row * 3u + column * 5u) % 9u) - 4);
    }
#endif
#endif
    for (index = 0; index < OUTPUT_DWORDS; ++index)
        fp_output[index] = -12345.0f;
#else
    set_raw_descriptor(descriptor, output, OUTPUT_BYTES);
    for (index = 0; index < OUTPUT_DWORDS; ++index)
        output[index] = UINT32_C(0xdeadbeef);
#endif
    *marker = 0;
    user_data[DESCRIPTOR_USER_DWORD] = (uint32_t)(uintptr_t)descriptor;

    command.bottom = words;
    command.top = words + COMMAND_BYTES / sizeof(*words);
    command.up = words;
    command.down = command.top;
    command.callback = (uintptr_t)command_out_of_space;

    if (!agc.set_sh(&command, program, program_count) ||
        !agc.set_sh_direct(&command, COMPUTE_USER_DATA_0, user_data,
                           COMPUTE_USER_DWORDS))
        goto receipt;
#if defined(COMPUTE_CANARY_Q8_MATVEC) || defined(COMPUTE_CANARY_MATVEC) || \
    defined(COMPUTE_CANARY_READ_PROBE) || defined(COMPUTE_CANARY_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_MATRIX) || \
    defined(COMPUTE_CANARY_Q8_DOT2_MATRIX) || \
    defined(COMPUTE_CANARY_TRANSFORMER)
    /* Make CPU-written activations and weights visible to shader reads. */
    if (!agc.acquire_mem(&command, 0, 0, GPU_READ_ACQUIRE_GCR,
                         (uint64_t)(uintptr_t)input, GPU_READ_ACQUIRE_BYTES,
                         GPU_READ_ACQUIRE_POLL))
        goto receipt;
#endif
    for (index = 0; index < DISPATCH_REPEATS; ++index) {
        if (!agc.dispatch(&command, DISPATCH_GROUPS_X, DISPATCH_GROUPS_Y, 1,
                          COMPUTE_DISPATCH_MODIFIER))
            goto receipt;
    }
    /* BOTTOM_OF_PIPE_TS plus GLV/GL1 invalidation and GL2 WB+INV. */
    if (!agc.release_mem(&command, 40, 0x30c, 0, 0, (void *)marker, 1,
                         COMPLETION_MARKER, 0, 0, 0, 0))
        goto receipt;

    word_count = (uint32_t)(command.up - words);
    if (out_of_space || command.up < command.bottom ||
        command.up > command.top || !word_count)
        goto receipt;
    dcb_hash = fnv1a32(words, word_count * sizeof(*words));

#if defined(COMPUTE_CANARY_Q8_MATVEC) || defined(COMPUTE_CANARY_MATVEC) || \
    defined(COMPUTE_CANARY_READ_PROBE) || defined(COMPUTE_CANARY_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_PROBE) || \
    defined(COMPUTE_CANARY_Q8_DOT_MATRIX) || \
    defined(COMPUTE_CANARY_Q8_DOT2_MATRIX) || \
    defined(COMPUTE_CANARY_TRANSFORMER)
    printf("[ps5_compute] acquire base=%08" PRIx32 ":%08" PRIx32
           " bytes=%08" PRIx32 " gcr=%08" PRIx32 " poll=%08" PRIx32
           "\n", (uint32_t)(uintptr_t)input,
           (uint32_t)((uintptr_t)input >> 32),
           (uint32_t)GPU_READ_ACQUIRE_BYTES, GPU_READ_ACQUIRE_GCR,
           GPU_READ_ACQUIRE_POLL);
    printf("[ps5_compute] descriptors=%08" PRIx32 ":%08" PRIx32
           ":%08" PRIx32 ":%08" PRIx32 ",%08" PRIx32 ":%08" PRIx32
           ":%08" PRIx32 ":%08" PRIx32 ",%08" PRIx32 ":%08" PRIx32
           ":%08" PRIx32 ":%08" PRIx32 " user2=%08" PRIx32 "\n",
           descriptor[0], descriptor[1], descriptor[2], descriptor[3],
           descriptor[4], descriptor[5], descriptor[6], descriptor[7],
           descriptor[8], descriptor[9], descriptor[10], descriptor[11],
           user_data[2]);
#ifdef COMPUTE_CANARY_REAL_LAYER
    printf("[ps5_compute] descriptors34=%08" PRIx32 ":%08" PRIx32
           ":%08" PRIx32 ":%08" PRIx32 ",%08" PRIx32 ":%08" PRIx32
           ":%08" PRIx32 ":%08" PRIx32 "\n",
           descriptor[12], descriptor[13], descriptor[14], descriptor[15],
           descriptor[16], descriptor[17], descriptor[18], descriptor[19]);
#elif defined(COMPUTE_CANARY_MEDIA_GEMM)
    printf("[ps5_compute] descriptor3=%08" PRIx32 ":%08" PRIx32
           ":%08" PRIx32 ":%08" PRIx32 "\n",
           descriptor[12], descriptor[13], descriptor[14], descriptor[15]);
#endif
#else
    printf("[ps5_compute] descriptor=%08" PRIx32 ":%08" PRIx32
           ":%08" PRIx32 ":%08" PRIx32 " user2=%08" PRIx32 "\n",
           descriptor[0], descriptor[1], descriptor[2], descriptor[3],
           user_data[2]);
#endif
    printf("[ps5_compute] dcb words=%" PRIu32 " fnv=%08" PRIx32 "\n",
           word_count, dcb_hash);
    for (index = 0; index < word_count; ++index)
        printf("[ps5_compute] dcb[%02u]=%08" PRIx32 "\n", index,
               words[index]);

#ifdef COMPUTE_CANARY_SUBMIT
    {
        agc_submit_description_t submit = {words, word_count, 0, {0}};
        unsigned polls, mismatches = 0;
        int submit_rc, suspend_rc;
#if defined(COMPUTE_CANARY_TRANSFORMER) || \
    defined(COMPUTE_CANARY_REAL_TENSOR) || defined(COMPUTE_CANARY_REAL_LAYER)
        float max_abs_error = 0.0f;
#endif
#ifdef COMPUTE_CANARY_READ_PROBE
        unsigned input_mismatches = 0, weight_mismatches = 0;
#endif
#if defined(COMPUTE_CANARY_SCALE) || defined(COMPUTE_CANARY_Q8_SCALE) || \
    defined(COMPUTE_CANARY_TRANSFORMER) || \
    defined(COMPUTE_CANARY_MEDIA_GEMM) || \
    defined(COMPUTE_CANARY_MEDIA_IM2COL)
        uint64_t start_us, elapsed_us;
#endif
#if defined(COMPUTE_CANARY_SCALE) || defined(COMPUTE_CANARY_Q8_SCALE)
        uint64_t mib_per_s_x1000;
#endif

        flush_cache(memory, WORK_BYTES);
#if defined(COMPUTE_CANARY_SCALE) || defined(COMPUTE_CANARY_Q8_SCALE) || \
    defined(COMPUTE_CANARY_TRANSFORMER) || \
    defined(COMPUTE_CANARY_MEDIA_GEMM) || \
    defined(COMPUTE_CANARY_MEDIA_IM2COL)
        start_us = sceKernelGetProcessTime();
#endif
        submit_rc = agc.submit(&submit);
        suspend_rc = submit_rc == 0 ? agc.suspend_point() : -1;
        polls = submit_rc == 0 && suspend_rc == 0
                    ? wait_for_marker(marker)
                    : 0;
#if defined(COMPUTE_CANARY_SCALE) || defined(COMPUTE_CANARY_Q8_SCALE) || \
    defined(COMPUTE_CANARY_TRANSFORMER) || \
    defined(COMPUTE_CANARY_MEDIA_GEMM) || \
    defined(COMPUTE_CANARY_MEDIA_IM2COL)
        elapsed_us = sceKernelGetProcessTime() - start_us;
#endif
        if (polls)
            flush_cache(output, OUTPUT_BYTES);
#ifdef COMPUTE_CANARY_MEDIA_IM2COL
        for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
            if (output[index] != media_im2col_expected_bits[index]) {
                if (!mismatches)
                    printf("[ps5_compute] media-im2col mismatch index=%u"
                           " got=%08" PRIx32 " expected=%08" PRIx32 "\n",
                           index, output[index], media_im2col_expected_bits[index]);
                mismatches++;
            }
        }
#elif defined(COMPUTE_CANARY_MEDIA_GEMM)
        for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
            float expected;
            float error;
            float tolerance;

            memcpy(&expected, &media_gemm_expected_bits[index],
                   sizeof(expected));
            error = fabsf(fp_output[index] - expected);
            tolerance = 0.0005f + 0.0005f * fabsf(expected);
            if (fp_output[index] != fp_output[index] || error > tolerance) {
                if (!mismatches)
                    printf("[ps5_compute] media-gemm mismatch index=%u"
                           " got=%g expected=%g error=%g tolerance=%g\n",
                           index, fp_output[index], expected, error,
                           tolerance);
                mismatches++;
            }
        }
#elif defined(COMPUTE_CANARY_MEDIA_LINEAR)
        for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
            float expected;
            float error;
            float tolerance;

            memcpy(&expected, &media_linear_expected_bits[index],
                   sizeof(expected));
            error = fabsf(fp_output[index] - expected);
            tolerance = 0.0005f + 0.0005f * fabsf(expected);
            if (fp_output[index] != fp_output[index] || error > tolerance) {
                if (!mismatches)
                    printf("[ps5_compute] media-linear mismatch row=%u"
                           " got=%g expected=%g error=%g tolerance=%g\n",
                           index, fp_output[index], expected, error,
                           tolerance);
                mismatches++;
            }
        }
#elif defined(COMPUTE_CANARY_REAL_LAYER)
        for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
            float expected;
            float error;
            float tolerance;

            memcpy(&expected, &real_layer_expected_bits[index],
                   sizeof(expected));
            error = fabsf(fp_output[index] - expected);
            tolerance = 0.01f + 0.01f * fabsf(expected);
            if (error > max_abs_error)
                max_abs_error = error;
            if (fp_output[index] != fp_output[index] || error > tolerance) {
                if (!mismatches)
                    printf("[ps5_compute] real-layer mismatch index=%u"
                           " got=%g expected=%g error=%g tolerance=%g\n",
                           index, fp_output[index], expected, error,
                           tolerance);
                mismatches++;
            }
        }
#elif defined(COMPUTE_CANARY_TRANSFORMER)
        {
            float expected[TRANSFORMER_WIDTH];

            transformer_reference(input, packed_weights, expected);
            for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
                float got = fp_output[index];
                float error = fabsf(got - expected[index]);
                float tolerance = 0.002f + 0.002f * fabsf(expected[index]);

                if (error > max_abs_error)
                    max_abs_error = error;
                if (got != got || error > tolerance) {
                    if (!mismatches)
                        printf("[ps5_compute] transformer mismatch row=%u"
                               " got=%g expected=%g error=%g tolerance=%g\n",
                               index, got, expected[index], error,
                               tolerance);
                    mismatches++;
                }
            }
        }
#elif defined(COMPUTE_CANARY_READ_PROBE)
        for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
            float expected = index < INPUT_DWORDS
                                 ? input[index]
                                 : weights[index - INPUT_DWORDS];

            if (fp_output[index] != expected) {
                if (!mismatches) {
                    uint32_t got_bits, expected_bits;

                    memcpy(&got_bits, &fp_output[index], sizeof(got_bits));
                    memcpy(&expected_bits, &expected, sizeof(expected_bits));
                    printf("[ps5_compute] read-probe mismatch index=%u"
                           " got=%08" PRIx32 " expected=%08" PRIx32 "\n",
                           index, got_bits, expected_bits);
                }
                mismatches++;
                if (index < INPUT_DWORDS)
                    input_mismatches++;
                else
                    weight_mismatches++;
            }
        }
#elif defined(COMPUTE_CANARY_Q8_DOT2_MATRIX)
        {
            static const uint32_t expected[6] = {
                UINT32_C(0xffffffe9), UINT32_C(0x01234550),
                UINT32_C(0x0006ffe9), UINT32_C(0x123b5661),
                UINT32_C(0x0002fffe), UINT32_C(0xffffffe9),
            };

            for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
                if (output[index] != expected[index]) {
                    if (!mismatches)
                        printf("[ps5_compute] q8-dot2-matrix mismatch index=%u"
                               " got=%08" PRIx32 " expected=%08" PRIx32
                               "\n", index, output[index], expected[index]);
                    mismatches++;
                }
            }
        }
#elif defined(COMPUTE_CANARY_Q8_DOT_MATRIX)
        {
            static const uint32_t expected[8] = {
                UINT32_C(0xffffffba), UINT32_C(0x01234521),
                UINT32_C(0x000011ba), UINT32_C(0x12346832),
                UINT32_C(0x012344d9), UINT32_C(0x123679ea),
                UINT32_C(0x01234521), UINT32_C(0x12346832),
            };

            for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
                if (output[index] != expected[index]) {
                    if (!mismatches)
                        printf("[ps5_compute] q8-dot-matrix mismatch index=%u"
                               " got=%08" PRIx32 " expected=%08" PRIx32
                               "\n", index, output[index], expected[index]);
                    mismatches++;
                }
            }
        }
#elif defined(COMPUTE_CANARY_Q8_DOT_PROBE)
        {
            static const uint32_t expected[6] = {
                UINT32_C(0xffffffba), UINT32_C(0xffffffba),
                UINT32_C(0xffffffff), UINT32_C(0xffffffcb),
                UINT32_C(0xffffffb9), UINT32_C(0xfffffdc8),
            };

            for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
                if (output[index] != expected[index]) {
                    if (!mismatches)
                        printf("[ps5_compute] q8-dot-probe mismatch index=%u"
                               " got=%08" PRIx32 " expected=%08" PRIx32
                               "\n", index, output[index], expected[index]);
                    mismatches++;
                }
            }
        }
#elif defined(COMPUTE_CANARY_DOT_PROBE)
        if (polls) {
            float expected = 0.0f;
            uint32_t got_bits, expected_bits;

            for (index = 0; index < INPUT_DWORDS; ++index)
                expected += weights[index] * input[index];
            if (fp_output[0] != expected) {
                memcpy(&got_bits, &fp_output[0], sizeof(got_bits));
                memcpy(&expected_bits, &expected, sizeof(expected_bits));
                printf("[ps5_compute] dot-probe mismatch got=%08" PRIx32
                       " expected=%08" PRIx32 "\n", got_bits,
                       expected_bits);
                mismatches = 1;
            }
        }
#elif defined(COMPUTE_CANARY_REAL_TENSOR)
        for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
            float expected;
            float error;
            float tolerance;

            memcpy(&expected, &real_tensor_expected_bits[index],
                   sizeof(expected));
            error = fabsf(fp_output[index] - expected);
            tolerance = 0.00001f + 0.00001f * fabsf(expected);
            if (error > max_abs_error)
                max_abs_error = error;
            if (fp_output[index] != fp_output[index] || error > tolerance) {
                if (!mismatches)
                    printf("[ps5_compute] real-tensor mismatch row=%u"
                           " got=%g expected=%g error=%g tolerance=%g\n",
                           index, fp_output[index], expected, error,
                           tolerance);
                mismatches++;
            }
        }
#elif defined(COMPUTE_CANARY_Q8_MATVEC)
        for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
#ifdef COMPUTE_CANARY_Q8_DOT
#ifdef COMPUTE_CANARY_Q4_0_SCALE
            float expected = 0.0f;
#else
            int expected_sum = 0;
#endif
#else
            float expected = 0.0f;
#endif
            unsigned column;

            for (column = 0; column < INPUT_DWORDS; ++column) {
#ifdef COMPUTE_CANARY_Q4_0_SCALE
                unsigned block = index * 128u + column / 32u;
                unsigned byte_index = column % 16u;
                uint32_t packed = packed_weights[
                    Q4_0_DATA_OFFSET + block * 4u + byte_index / 4u];
                unsigned shift = (byte_index % 4u) * 8u +
                                 (column % 32u >= 16u ? 4u : 0u);
                int weight = (int)((packed >> shift) & 0xfu) - 8;
                float block_scale = (block & 1u) ? 0.5f : 0.25f;
#elif defined(COMPUTE_CANARY_Q4_SCALE)
                uint32_t packed = packed_weights[
                    index * (INPUT_DWORDS / 8u) + column / 8u];
                int weight =
                    (int)((packed >> ((column % 8u) * 4u)) & 0xfu);

                if (weight >= 8)
                    weight -= 16;
#else
                uint32_t packed = packed_weights[
                    index * (INPUT_DWORDS / 4u) + column / 4u];
                int weight = (int)((packed >> ((column % 4u) * 8u)) & 0xffu);

                if (weight >= 128)
                    weight -= 256;
#endif
#ifdef COMPUTE_CANARY_Q8_DOT
                packed = packed_input[column / 4u];
                int activation =
                    (int)((packed >> ((column % 4u) * 8u)) & 0xffu);

                if (activation >= 128)
                    activation -= 256;
#ifdef COMPUTE_CANARY_Q4_0_SCALE
                expected +=
                    (float)(weight * activation) * block_scale * 0.25f;
#else
                expected_sum += weight * activation;
#endif
#else
                expected += (float)weight * input[column] * 0.25f;
#endif
            }
#if defined(COMPUTE_CANARY_Q8_DOT) && \
    !defined(COMPUTE_CANARY_Q4_0_SCALE)
            float expected = (float)expected_sum * 0.0625f;
#endif
            if (fp_output[index] != expected) {
                if (!mismatches) {
                    uint32_t got_bits, expected_bits;

                    memcpy(&got_bits, &fp_output[index], sizeof(got_bits));
                    memcpy(&expected_bits, &expected, sizeof(expected_bits));
                    printf("[ps5_compute] q8-matvec mismatch row=%u"
                           " got=%08" PRIx32 " expected=%08" PRIx32 "\n",
                           index, got_bits, expected_bits);
                }
                mismatches++;
            }
        }
#elif defined(COMPUTE_CANARY_MATVEC)
        for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
            float expected = 0.0f;
            unsigned column;

            for (column = 0; column < INPUT_DWORDS; ++column)
                expected += weights[index * INPUT_DWORDS + column] *
                            input[column];
            if (fp_output[index] != expected) {
                if (!mismatches) {
                    uint32_t got_bits, expected_bits;

                    memcpy(&got_bits, &fp_output[index], sizeof(got_bits));
                    memcpy(&expected_bits, &expected, sizeof(expected_bits));
                    printf("[ps5_compute] matvec mismatch row=%u got=%08" PRIx32
                           " expected=%08" PRIx32 "\n", index, got_bits,
                           expected_bits);
                }
                mismatches++;
            }
        }
#else
        for (index = 0; polls && index < OUTPUT_DWORDS; ++index) {
            uint32_t expected = UINT32_C(0xc0de0000) | index;

            if (output[index] != expected) {
                if (!mismatches)
                    printf("[ps5_compute] mismatch index=%u got=%08" PRIx32
                           " expected=%08" PRIx32 "\n", index,
                           output[index], expected);
                mismatches++;
            }
        }
#endif
#ifdef COMPUTE_CANARY_MEDIA_IM2COL
        printf("[ps5_compute] mode=media-im2col-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u model=sd-turbo tensor=conv_in.weight"
               " input=1x4x64x64 kernel=3x3 output=1x64x64x36"
               " storage=fp16 elapsed_us=%" PRIu64 "\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches, elapsed_us);
#elif defined(COMPUTE_CANARY_MEDIA_GEMM)
        printf("[ps5_compute] mode=media-gemm-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u model=sd-turbo tensor=attn1.to_q.weight"
               " rows=%u columns=%u batch=%u weights=fp16 activations=fp32"
               " elapsed_us=%" PRIu64 "\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches, MEDIA_GEMM_ROWS, MEDIA_GEMM_COLUMNS,
               MEDIA_GEMM_BATCH, elapsed_us);
#elif defined(COMPUTE_CANARY_MEDIA_LINEAR)
        printf("[ps5_compute] mode=media-linear-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u model=sd-turbo tensor=attn1.to_q.weight"
               " rows=%u columns=%u weights=fp16 activations=fp32\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches, OUTPUT_DWORDS, INPUT_DWORDS);
#elif defined(COMPUTE_CANARY_REAL_LAYER)
        mib_per_s_x1000 = elapsed_us
            ? (uint64_t)GPU_READ_WEIGHT_BYTES * UINT64_C(1000000000) /
                  (elapsed_us * UINT64_C(1024) * UINT64_C(1024))
            : 0;
        printf("[ps5_compute] mode=real-layer-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u layer=0 width=576 kv=192 ff=1536 context=1"
               " activation=q8_0 block_scale=fp32 elapsed_us=%" PRIu64
               " lower_bound_mib_s_x1000=%" PRIu64 " max_abs_error=%g\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches, elapsed_us, mib_per_s_x1000, max_abs_error);
#elif defined(COMPUTE_CANARY_TRANSFORMER)
        printf("[ps5_compute] mode=transformer-block-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u width=64 heads=4 context=2 ff=128"
               " elapsed_us=%" PRIu64 " max_abs_error=%g\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches, elapsed_us, max_abs_error);
#elif defined(COMPUTE_CANARY_READ_PROBE)
        printf("[ps5_compute] mode=read-probe-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u input_mismatches=%u weight_mismatches=%u"
               " values=128 output_fnv=%08" PRIx32 "\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches, input_mismatches, weight_mismatches,
               fnv1a32(output, OUTPUT_BYTES));
#elif defined(COMPUTE_CANARY_Q8_DOT2_MATRIX)
        printf("[ps5_compute] mode=q8-dot2-matrix-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u values=%08" PRIx32 ",%08" PRIx32
               ",%08" PRIx32 ",%08" PRIx32 ",%08" PRIx32
               ",%08" PRIx32 "\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches, output[0], output[1], output[2], output[3],
               output[4], output[5]);
#elif defined(COMPUTE_CANARY_Q8_DOT_MATRIX)
        printf("[ps5_compute] mode=q8-dot-matrix-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u values=%08" PRIx32 ",%08" PRIx32
               ",%08" PRIx32 ",%08" PRIx32 ",%08" PRIx32
               ",%08" PRIx32 ",%08" PRIx32 ",%08" PRIx32 "\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches, output[0], output[1], output[2], output[3],
               output[4], output[5], output[6], output[7]);
#elif defined(COMPUTE_CANARY_Q8_DOT_PROBE)
        printf("[ps5_compute] mode=q8-dot-probe-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u values=%08" PRIx32 ",%08" PRIx32
               ",%08" PRIx32 ",%08" PRIx32 ",%08" PRIx32
               ",%08" PRIx32 "\n", (uint32_t)submit_rc,
               (uint32_t)suspend_rc, polls, *marker, mismatches, output[0],
               output[1], output[2], output[3], output[4], output[5]);
#elif defined(COMPUTE_CANARY_DOT_PROBE)
        printf("[ps5_compute] mode=dot-probe-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u value=%08" PRIx32 "\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches, output[0]);
#elif defined(COMPUTE_CANARY_REAL_TENSOR)
        mib_per_s_x1000 = elapsed_us
            ? (uint64_t)GPU_READ_WEIGHT_BYTES * DISPATCH_REPEATS *
                  UINT64_C(1000000000) /
                  (elapsed_us * UINT64_C(1024) * UINT64_C(1024))
            : 0;
#ifdef COMPUTE_CANARY_PERSISTENT
        printf("[ps5_compute] mode=persistent-real-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u tensor=blk.0.attn_q.weight rows=%u columns=%u"
               " repeats=%u source=q4_0 block_scale=fp32"
               " activation_scale=0.25 elapsed_us=%" PRIu64
               " lower_bound_mib_s_x1000=%" PRIu64 " max_abs_error=%g\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches, OUTPUT_DWORDS, INPUT_DWORDS, DISPATCH_REPEATS,
               elapsed_us, mib_per_s_x1000, max_abs_error);
#else
        printf("[ps5_compute] mode=real-tensor-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u tensor=blk.0.attn_q.weight rows=%u columns=%u"
               " source=q4_0 block_scale=fp32 activation_scale=0.25"
               " elapsed_us=%" PRIu64 " lower_bound_mib_s_x1000=%" PRIu64
               " max_abs_error=%g\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches, OUTPUT_DWORDS, INPUT_DWORDS, elapsed_us,
               mib_per_s_x1000, max_abs_error);
#endif
#elif defined(COMPUTE_CANARY_Q8_MATVEC)
#ifdef COMPUTE_CANARY_Q8_SCALE
        mib_per_s_x1000 = elapsed_us
            ? (uint64_t)GPU_READ_WEIGHT_BYTES * UINT64_C(1000000000) /
                  (elapsed_us * UINT64_C(1024) * UINT64_C(1024))
            : 0;
#ifdef COMPUTE_CANARY_Q8_DOT
#ifdef COMPUTE_CANARY_Q8_SOFTWARE
#ifdef COMPUTE_CANARY_Q4_0_SCALE
#ifdef COMPUTE_CANARY_Q4_0_F32_SCALE
        printf("[ps5_compute] mode=q4-0-f32-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u rows=%u columns=%u block_scale=fp32"
               " activation_scale=0.25"
#else
        printf("[ps5_compute] mode=q4-0-software-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u rows=%u columns=%u block_scale=fp16"
               " activation_scale=0.25"
#endif
#elif defined(COMPUTE_CANARY_Q4_SCALE)
        printf("[ps5_compute] mode=q4-software-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u rows=%u columns=%u weight_scale=0.25"
               " activation_scale=0.25"
#else
        printf("[ps5_compute] mode=q8-software-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u rows=%u columns=%u weight_scale=0.25"
               " activation_scale=0.25"
#endif
#else
        printf("[ps5_compute] mode=q8-dot4-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u rows=%u columns=%u weight_scale=0.25"
               " activation_scale=0.25"
#endif
#else
        printf("[ps5_compute] mode=q8-tree-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u rows=%u columns=%u scale=0.25"
#endif
               " elapsed_us=%" PRIu64 " lower_bound_mib_s_x1000=%" PRIu64
               "\n", (uint32_t)submit_rc, (uint32_t)suspend_rc, polls,
               *marker, mismatches, OUTPUT_DWORDS, INPUT_DWORDS, elapsed_us,
               mib_per_s_x1000);
#else
        printf("[ps5_compute] mode=q8-matvec-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u rows=64 columns=64 scale=0.25\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches);
#endif
#elif defined(COMPUTE_CANARY_MATVEC)
        printf("[ps5_compute] mode=matvec-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u rows=64 columns=64\n", (uint32_t)submit_rc,
               (uint32_t)suspend_rc, polls, *marker, mismatches);
#elif defined(COMPUTE_CANARY_SCALE)
        mib_per_s_x1000 = elapsed_us
            ? (uint64_t)OUTPUT_BYTES * UINT64_C(1000000000) /
                  (elapsed_us * UINT64_C(1024) * UINT64_C(1024))
            : 0;
        printf("[ps5_compute] mode=scale-live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u output_dwords=%u groups=%u elapsed_us=%" PRIu64
               " lower_bound_mib_s_x1000=%" PRIu64 "\n",
               (uint32_t)submit_rc, (uint32_t)suspend_rc, polls, *marker,
               mismatches, OUTPUT_DWORDS, DISPATCH_GROUPS_X, elapsed_us,
               mib_per_s_x1000);
#else
        printf("[ps5_compute] mode=live submit=%08" PRIx32
               " suspend=%08" PRIx32 " polls=%u marker=%08" PRIx32
               " mismatches=%u\n", (uint32_t)submit_rc,
               (uint32_t)suspend_rc, polls, *marker, mismatches);
#endif
        result = submit_rc == 0 && suspend_rc == 0 && polls && !mismatches
                     ? 0
                     : 1;
    }
#else
#ifdef COMPUTE_CANARY_MEDIA_IM2COL
    printf("[ps5_compute] mode=media-im2col-capture submitted=0"
           " model=sd-turbo tensor=conv_in.weight input=1x4x64x64"
           " kernel=3x3 output=1x64x64x36 storage=fp16\n");
#elif defined(COMPUTE_CANARY_MEDIA_GEMM)
    printf("[ps5_compute] mode=media-gemm-capture submitted=0"
           " model=sd-turbo tensor=attn1.to_q.weight rows=%u columns=%u"
           " batch=%u weights=fp16 activations=fp32\n",
           MEDIA_GEMM_ROWS, MEDIA_GEMM_COLUMNS, MEDIA_GEMM_BATCH);
#elif defined(COMPUTE_CANARY_MEDIA_LINEAR)
    printf("[ps5_compute] mode=media-linear-capture submitted=0"
           " model=sd-turbo tensor=attn1.to_q.weight rows=%u columns=%u"
           " weights=fp16 activations=fp32\n",
           OUTPUT_DWORDS, INPUT_DWORDS);
#elif defined(COMPUTE_CANARY_REAL_LAYER)
    printf("[ps5_compute] mode=real-layer-capture submitted=0 layer=0"
           " width=576 kv=192 ff=1536 context=1 activation=q8_0"
           " block_scale=fp32\n");
#elif defined(COMPUTE_CANARY_TRANSFORMER)
    printf("[ps5_compute] mode=transformer-block-capture submitted=0"
           " width=64 heads=4 context=2 ff=128\n");
#elif defined(COMPUTE_CANARY_READ_PROBE)
    printf("[ps5_compute] mode=read-probe-capture submitted=0 values=128\n");
#elif defined(COMPUTE_CANARY_Q8_DOT2_MATRIX)
    printf("[ps5_compute] mode=q8-dot2-matrix-capture submitted=0 values=6\n");
#elif defined(COMPUTE_CANARY_Q8_DOT_MATRIX)
    printf("[ps5_compute] mode=q8-dot-matrix-capture submitted=0 values=8\n");
#elif defined(COMPUTE_CANARY_Q8_DOT_PROBE)
    printf("[ps5_compute] mode=q8-dot-probe-capture submitted=0 values=6\n");
#elif defined(COMPUTE_CANARY_DOT_PROBE)
    printf("[ps5_compute] mode=dot-probe-capture submitted=0 values=1\n");
#elif defined(COMPUTE_CANARY_REAL_TENSOR)
#ifdef COMPUTE_CANARY_PERSISTENT
    printf("[ps5_compute] mode=persistent-real-capture submitted=0"
           " tensor=blk.0.attn_q.weight rows=%u columns=%u repeats=%u"
           " source=q4_0 block_scale=fp32 activation_scale=0.25\n",
           OUTPUT_DWORDS, INPUT_DWORDS, DISPATCH_REPEATS);
#else
    printf("[ps5_compute] mode=real-tensor-capture submitted=0"
           " tensor=blk.0.attn_q.weight rows=%u columns=%u"
           " source=q4_0 block_scale=fp32 activation_scale=0.25\n",
           OUTPUT_DWORDS, INPUT_DWORDS);
#endif
#elif defined(COMPUTE_CANARY_Q8_MATVEC)
#ifdef COMPUTE_CANARY_Q8_SCALE
#ifdef COMPUTE_CANARY_Q8_DOT
#ifdef COMPUTE_CANARY_Q8_SOFTWARE
#ifdef COMPUTE_CANARY_Q4_0_SCALE
#ifdef COMPUTE_CANARY_Q4_0_F32_SCALE
    printf("[ps5_compute] mode=q4-0-f32-capture submitted=0 rows=%u"
           " columns=%u block_scale=fp32 activation_scale=0.25\n",
           OUTPUT_DWORDS, INPUT_DWORDS);
#else
    printf("[ps5_compute] mode=q4-0-software-capture submitted=0 rows=%u"
           " columns=%u block_scale=fp16 activation_scale=0.25\n",
           OUTPUT_DWORDS, INPUT_DWORDS);
#endif
#elif defined(COMPUTE_CANARY_Q4_SCALE)
    printf("[ps5_compute] mode=q4-software-capture submitted=0 rows=%u"
           " columns=%u weight_scale=0.25 activation_scale=0.25\n",
           OUTPUT_DWORDS, INPUT_DWORDS);
#else
    printf("[ps5_compute] mode=q8-software-capture submitted=0 rows=%u"
           " columns=%u weight_scale=0.25 activation_scale=0.25\n",
           OUTPUT_DWORDS, INPUT_DWORDS);
#endif
#else
    printf("[ps5_compute] mode=q8-dot4-capture submitted=0 rows=%u"
           " columns=%u weight_scale=0.25 activation_scale=0.25\n",
           OUTPUT_DWORDS, INPUT_DWORDS);
#endif
#else
    printf("[ps5_compute] mode=q8-tree-capture submitted=0 rows=%u"
           " columns=%u scale=0.25\n", OUTPUT_DWORDS, INPUT_DWORDS);
#endif
#else
    printf("[ps5_compute] mode=q8-matvec-capture submitted=0 rows=64"
           " columns=64 scale=0.25\n");
#endif
#elif defined(COMPUTE_CANARY_MATVEC)
    printf("[ps5_compute] mode=matvec-capture submitted=0 rows=64 columns=64\n");
#elif defined(COMPUTE_CANARY_SCALE)
    printf("[ps5_compute] mode=scale-capture submitted=0 output_dwords=%u"
           " groups=%u\n", OUTPUT_DWORDS, DISPATCH_GROUPS_X);
#else
    printf("[ps5_compute] mode=capture submitted=0\n");
#endif
    result = 0;
#endif

receipt:
    printf("[ps5_compute] init=%08" PRIx32 " create=%08" PRIx32
           " stage=%u sh=%u words=%" PRIu32 " hash=%08" PRIx32
           " init_errno=%d\n",
           (uint32_t)init_rc, (uint32_t)create_rc,
           shader ? ((uint8_t *)shader)[0x5a] : UINT8_MAX,
           shader ? ((uint8_t *)shader)[92] : 0, word_count, dcb_hash,
           init_errno);
    fflush(stdout);

cleanup:
    if (memory)
        munmap(memory, WORK_BYTES);
    if (direct_start >= 0)
        sceKernelReleaseDirectMemory(direct_start, WORK_BYTES);
#if defined(COMPUTE_CANARY_SUBMIT) && !defined(COMPUTE_CANARY_LINKED_AGC)
    if (driver_module)
        dlclose(driver_module);
#endif
#ifndef COMPUTE_CANARY_LINKED_AGC
    if (agc_module)
        dlclose(agc_module);
#endif
    return result;
}
