/* Autoregressive SmolLM2 tokens on the PS5 GPU. */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#define COMPUTE_CANARY_Q8_MATVEC 1
#define COMPUTE_CANARY_Q8_SCALE 1
#define COMPUTE_CANARY_REAL_LAYER 1
#define COMPUTE_CANARY_LINKED_AGC 1
#define COMPUTE_CANARY_SUBMIT 1
#if defined(PS5_MODEL_3000) || defined(PS5_MODEL_7000) || \
    defined(PS5_MODEL_QWEN35)
#define PS5_LARGE_MODEL 1
#define COMPLETION_POLLS 120000u
#endif
#define main run_fixture_unused
#include "compute_canary_core.inc"
#undef main

#include "token_logits_agc.inc"
#ifdef PS5_PRENORMALIZED_LOGITS
#include "logits_prepare_agc.inc"
#endif
#include "chat_prompt.h"
#include "real_layer_kv_agc.inc"
#include "tokenizer.h"
#if defined(PS5_QWEN_SEGMENT_DIAGNOSTIC) || \
    defined(PS5_LOGITS_DIAGNOSTIC) || \
    defined(PS5_DIRECT_MAP_DIAGNOSTIC)
int sceKernelDebugOutText(int, const char *);
#endif
#ifdef PS5_DUAL_BACKEND
void *ps5_agc_backend_take_released_scratch_address(void);
#endif
#ifdef PS5_STAGED_LAYER
#include "real_layer_pre_agc.inc"
#include "ff_gate_up_agc.inc"
#include "ff_quantize_agc.inc"
#include "ff_down_agc.inc"
#endif
#ifdef PS5_SPLIT_ATTN
#include "attn_prepare_agc.inc"
#include "attn_qkv_agc.inc"
#include "attn_rope_agc.inc"
#ifdef PS5_NOPE_EVERY_FOURTH
#include "attn_nope_agc.inc"
#endif
#ifdef PS5_MODEL_QWEN35
#include "attn_context_recurrent_agc.inc"
#include "attn_context_attention_agc.inc"
#else
#include "attn_context_agc.inc"
#endif
#include "attn_quant_agc.inc"
#include "attn_output_agc.inc"
#include "attn_ff_prepare_agc.inc"
#endif

#if defined(PS5_MODEL_QWEN35)
#undef REAL_LAYER_Q_OFFSET
#undef REAL_LAYER_K_OFFSET
#undef REAL_LAYER_V_OFFSET
#undef REAL_LAYER_O_OFFSET
#undef REAL_LAYER_GATE_OFFSET
#undef REAL_LAYER_UP_OFFSET
#undef REAL_LAYER_DOWN_OFFSET
#undef REAL_LAYER_WEIGHT_DWORDS
#define REAL_LAYER_Q_OFFSET UINT32_C(0x00000000)
#define REAL_LAYER_K_OFFSET UINT32_C(0x00500000)
#define REAL_LAYER_V_OFFSET UINT32_C(0x00780000)
#define REAL_LAYER_O_OFFSET UINT32_C(0x00820000)
#define REAL_LAYER_SSM_OUT_OFFSET UINT32_C(0x00aa0000)
#define REAL_LAYER_GATE_OFFSET UINT32_C(0x00e20000)
#define REAL_LAYER_UP_OFFSET UINT32_C(0x015a0000)
#define REAL_LAYER_DOWN_OFFSET UINT32_C(0x01d20000)
#define REAL_LAYER_WEIGHT_DWORDS UINT32_C(0x02680000)
#elif defined(PS5_MODEL_7000)
#undef REAL_LAYER_Q_OFFSET
#undef REAL_LAYER_K_OFFSET
#undef REAL_LAYER_V_OFFSET
#undef REAL_LAYER_O_OFFSET
#undef REAL_LAYER_GATE_OFFSET
#undef REAL_LAYER_UP_OFFSET
#undef REAL_LAYER_DOWN_OFFSET
#undef REAL_LAYER_WEIGHT_DWORDS
#define REAL_LAYER_Q_OFFSET 0u
#define REAL_LAYER_K_OFFSET 2621440u
#define REAL_LAYER_V_OFFSET 3276800u
#define REAL_LAYER_O_OFFSET 3932160u
#define REAL_LAYER_GATE_OFFSET 6553600u
#define REAL_LAYER_UP_OFFSET 15728640u
#define REAL_LAYER_DOWN_OFFSET 24903680u
#define REAL_LAYER_WEIGHT_DWORDS 34078720u
#elif defined(PS5_MODEL_3000)
#undef REAL_LAYER_Q_OFFSET
#undef REAL_LAYER_K_OFFSET
#undef REAL_LAYER_V_OFFSET
#undef REAL_LAYER_O_OFFSET
#undef REAL_LAYER_GATE_OFFSET
#undef REAL_LAYER_UP_OFFSET
#undef REAL_LAYER_DOWN_OFFSET
#undef REAL_LAYER_WEIGHT_DWORDS
#define REAL_LAYER_Q_OFFSET 0u
#define REAL_LAYER_K_OFFSET 655360u
#define REAL_LAYER_V_OFFSET 819200u
#define REAL_LAYER_O_OFFSET 983040u
#define REAL_LAYER_GATE_OFFSET 1638400u
#define REAL_LAYER_UP_OFFSET 5160960u
#define REAL_LAYER_DOWN_OFFSET 8683520u
#define REAL_LAYER_WEIGHT_DWORDS 12206080u
#elif defined(PS5_MODEL_1700)
#undef REAL_LAYER_Q_OFFSET
#undef REAL_LAYER_K_OFFSET
#undef REAL_LAYER_V_OFFSET
#undef REAL_LAYER_O_OFFSET
#undef REAL_LAYER_GATE_OFFSET
#undef REAL_LAYER_UP_OFFSET
#undef REAL_LAYER_DOWN_OFFSET
#undef REAL_LAYER_WEIGHT_DWORDS
#define REAL_LAYER_Q_OFFSET 0u
#define REAL_LAYER_K_OFFSET 655360u
#define REAL_LAYER_V_OFFSET 1310720u
#define REAL_LAYER_O_OFFSET 1966080u
#define REAL_LAYER_GATE_OFFSET 2621440u
#define REAL_LAYER_UP_OFFSET 5242880u
#define REAL_LAYER_DOWN_OFFSET 7864320u
#define REAL_LAYER_WEIGHT_DWORDS 10485760u
#endif

uint32_t *sceAgcDcbWaitRegMem(void *command, int mode, uint8_t compare,
                              uint8_t control, uint8_t policy,
                              uint64_t address, uint64_t reference,
                              uint64_t mask, uint32_t poll);

typedef struct ps5_aio_result {
    int64_t return_value;
    uint32_t state;
    uint32_t reserved;
} ps5_aio_result_t;

typedef struct ps5_aio_request {
    int64_t offset;
    size_t bytes;
    void *buffer;
    ps5_aio_result_t *result;
    int fd;
    uint32_t reserved;
} ps5_aio_request_t;

int sceKernelAioSubmitReadCommandsMultiple(ps5_aio_request_t *, uint32_t,
                                           uint32_t, int *);
int sceKernelAioWaitRequests(int *, uint32_t, int *, uint32_t, uint32_t *);
int sceKernelAioDeleteRequests(int *, uint32_t, int *);
int sceKernelOpen(const char *, int, int);
int sceKernelClose(int);

#if defined(PS5_MODEL_QWEN35)
#define MODEL_BYTES UINT64_C(6828326912)
#define MODEL_ALLOCATION_BYTES UINT32_C(0x63000000)
#define MODEL_RESIDENT_BYTES MODEL_ALLOCATION_BYTES
#define MODEL_OUTPUT_NORM_OFFSET UINT32_C(0x00010000)
#define MODEL_EMBEDDING_OFFSET UINT32_C(0x00014000)
#define MODEL_OUTPUT_WEIGHT_OFFSET UINT32_C(0x26000000)
#define LAYER_CACHE_BYTES UINT64_C(0x134000000)
#define TOKEN_LAYERS 32u
#define LAYER_CACHE_STRIDE UINT32_C(0x09a00000)
#define LAYER_CACHE_NORM_OFFSET UINT32_C(0x09900000)
#define TOKEN_WORK_BYTES UINT32_C(0x41000000)
#define TOKEN_WIDTH 4096u
#define TOKEN_KV_WIDTH 1024u
#define TOKEN_FF_WIDTH 12288u
#define TOKEN_HEADS 16u
#define TOKEN_KV_HEADS 4u
#define TOKEN_TENSOR_COUNT 427u
#define TOKEN_VOCAB 248320u
#define TOKEN_EOS 248046u
#elif defined(PS5_MODEL_7000)
#define MODEL_BYTES UINT64_C(4677189632)
#define MODEL_ALLOCATION_BYTES UINT32_C(0x0e000000)
#define MODEL_RESIDENT_BYTES MODEL_ALLOCATION_BYTES
#define MODEL_OUTPUT_NORM_OFFSET UINT32_C(0xc000)
#define MODEL_EMBEDDING_OFFSET UINT32_C(0x10000)
#define MODEL_OUTPUT_WEIGHT_OFFSET UINT32_C(0x05100000)
#define LAYER_CACHE_BYTES UINT64_C(0x108000000)
#define TOKEN_LAYERS 32u
#define LAYER_CACHE_STRIDE UINT32_C(0x08400000)
#define LAYER_CACHE_NORM_OFFSET UINT32_C(0x08200000)
#define TOKEN_WORK_BYTES UINT32_C(0x45000000)
#define TOKEN_WIDTH 4096u
#define TOKEN_KV_WIDTH 1024u
#define TOKEN_FF_WIDTH 14336u
#define TOKEN_HEADS 32u
#define TOKEN_KV_HEADS 8u
#define TOKEN_TENSOR_COUNT 291u
#define TOKEN_VOCAB 32768u
#define TOKEN_EOS 2u
#elif defined(PS5_MODEL_3000)
#define MODEL_BYTES UINT32_C(2698444800)
#define MODEL_ALLOCATION_BYTES UINT32_C(0x10000000)
#define MODEL_RESIDENT_BYTES MODEL_ALLOCATION_BYTES
#define MODEL_OUTPUT_NORM_OFFSET UINT32_C(0xc000)
#define MODEL_EMBEDDING_OFFSET UINT32_C(0x10000)
#define LAYER_CACHE_BYTES UINT32_C(0x68e80000)
#define TOKEN_LAYERS 36u
#define LAYER_CACHE_STRIDE UINT32_C(0x2ea0000)
#define LAYER_CACHE_NORM_OFFSET UINT32_C(0x2e90000)
#define TOKEN_WORK_BYTES UINT32_C(0x8200000)
#define TOKEN_WIDTH 2048u
#define TOKEN_KV_WIDTH 512u
#define TOKEN_FF_WIDTH 11008u
#define TOKEN_HEADS 16u
#define TOKEN_KV_HEADS 4u
#define TOKEN_TENSOR_COUNT 326u
#define TOKEN_VOCAB 128256u
#define TOKEN_EOS 128012u
#elif defined(PS5_MODEL_1700)
#define MODEL_BYTES UINT32_C(1205878784)
#define MODEL_ALLOCATION_BYTES UINT32_C(0x48000000)
#define LAYER_CACHE_BYTES UINT32_C(0x3c300000)
#define LAYER_CACHE_STRIDE UINT32_C(0x2820000)
#define LAYER_CACHE_NORM_OFFSET UINT32_C(0x2800000)
#ifdef PS5_BATCH_PREFILL
#define TOKEN_WORK_BYTES UINT32_C(0xf000000)
#else
#define TOKEN_WORK_BYTES UINT32_C(0xc400000)
#endif
#define TOKEN_LAYERS 24u
#define TOKEN_WIDTH 2048u
#define TOKEN_KV_WIDTH 2048u
#define TOKEN_FF_WIDTH 8192u
#define TOKEN_HEADS 32u
#define TOKEN_KV_HEADS 32u
#define TOKEN_TENSOR_COUNT 218u
#elif defined(PS5_MODEL_360)
#define MODEL_BYTES UINT32_C(288440320)
#define MODEL_ALLOCATION_BYTES UINT32_C(0x11400000)
#define LAYER_CACHE_BYTES UINT32_C(0xc000000)
#define LAYER_CACHE_STRIDE UINT32_C(0x600000)
#define LAYER_CACHE_NORM_OFFSET UINT32_C(0x5dc000)
#define TOKEN_WORK_BYTES UINT32_C(0x2c00000)
#define TOKEN_LAYERS 32u
#define TOKEN_WIDTH 960u
#define TOKEN_KV_WIDTH 320u
#define TOKEN_FF_WIDTH 2560u
#define TOKEN_HEADS 15u
#define TOKEN_KV_HEADS 5u
#define TOKEN_TENSOR_COUNT 290u
#else
#define MODEL_BYTES UINT32_C(99254272)
#define MODEL_ALLOCATION_BYTES UINT32_C(0x6000000)
#define LAYER_CACHE_BYTES UINT32_C(0x4000000)
#define LAYER_CACHE_STRIDE UINT32_C(0x220000)
#define LAYER_CACHE_NORM_OFFSET UINT32_C(0x21c000)
#define TOKEN_WORK_BYTES UINT32_C(0x1a00000)
#define TOKEN_LAYERS 30u
#define TOKEN_WIDTH 576u
#define TOKEN_KV_WIDTH 192u
#define TOKEN_FF_WIDTH 1536u
#define TOKEN_HEADS 9u
#define TOKEN_KV_HEADS 3u
#define TOKEN_TENSOR_COUNT 272u
#endif
#ifndef MODEL_RESIDENT_BYTES
#define MODEL_RESIDENT_BYTES MODEL_BYTES
#endif
#ifdef PS5_LARGE_MODEL
#define GPU_MAP_PADDING_BYTES UINT32_C(0x200000)
#define GPU_ALIGNED(pointer)                                                 \
    ((uint8_t *)(((uintptr_t)(pointer) + GPU_MAP_PADDING_BYTES - 1u) &       \
                 ~(uintptr_t)(GPU_MAP_PADDING_BYTES - 1u)))
#else
#define GPU_MAP_PADDING_BYTES 0u
#define GPU_ALIGNED(pointer) (pointer)
#endif
#define WORK_MAPPING_BYTES (TOKEN_WORK_BYTES + GPU_MAP_PADDING_BYTES)
#define MODEL_MAPPING_BYTES (MODEL_ALLOCATION_BYTES + GPU_MAP_PADDING_BYTES)
#if defined(PS5_MODEL_7000)
#define LAYER_CACHE_BANK_COUNT 4u
#define LAYERS_PER_CACHE_BANK (TOKEN_LAYERS / LAYER_CACHE_BANK_COUNT)
#define LAYER_CACHE_BANK_BYTES \
    ((uint64_t)LAYERS_PER_CACHE_BANK * LAYER_CACHE_STRIDE)
#elif defined(PS5_MODEL_3000)
#define LAYER_CACHE_BANK_COUNT 3u
#define LAYERS_PER_CACHE_BANK (TOKEN_LAYERS / LAYER_CACHE_BANK_COUNT)
#define LAYER_CACHE_BANK_BYTES \
    (LAYERS_PER_CACHE_BANK * LAYER_CACHE_STRIDE)
#else
#define LAYER_CACHE_BANK_COUNT 1u
#define LAYERS_PER_CACHE_BANK TOKEN_LAYERS
#define LAYER_CACHE_BANK_BYTES LAYER_CACHE_BYTES
#endif
#define LAYER_CACHE_BANK_MAPPING_BYTES \
    (LAYER_CACHE_BANK_BYTES + GPU_MAP_PADDING_BYTES)
#ifdef PS5_LARGE_MODEL
#define PRIMARY_MAPPING_BYTES                                      \
    (GPU_MAP_PADDING_BYTES + TOKEN_WORK_BYTES +                    \
     MODEL_ALLOCATION_BYTES + LAYER_CACHE_BYTES)
#endif
#if defined(PS5_MODEL_7000) || defined(PS5_MODEL_QWEN35)
#define TOKEN_MAX_COUNT 4096u
#define TOKEN_KV_CAPACITY 4096u
#else
#define TOKEN_MAX_COUNT 512u
#define TOKEN_KV_CAPACITY 512u
#endif
#define MODEL_STREAM_CHUNK_BYTES UINT32_C(0x08000000)
#define MODEL_FLAG_RUNTIME_LAYOUT UINT32_C(2)
#define MODEL_AIO_REQUESTS 16u
#define MODEL_AIO_CHUNK_BYTES UINT32_C(0x01000000)
#ifndef TOKEN_VOCAB
#define TOKEN_VOCAB 49152u
#endif
#ifndef TOKEN_EOS
#define TOKEN_EOS 2u
#endif
#ifdef PS5_LARGE_MODEL
_Static_assert((uint64_t)MODEL_EMBEDDING_OFFSET +
                       (uint64_t)TOKEN_VOCAB * TOKEN_WIDTH <=
                   MODEL_ALLOCATION_BYTES,
               "compact global model exceeds its direct-memory allocation");
#endif
#if defined(PS5_MODEL_7000) || defined(PS5_MODEL_QWEN35)
_Static_assert(MODEL_OUTPUT_WEIGHT_OFFSET +
                       (uint64_t)TOKEN_VOCAB * TOKEN_WIDTH <=
                   MODEL_ALLOCATION_BYTES,
               "output weights exceed the global allocation");
#endif
#define TOKEN_EMBED_BLOCKS (TOKEN_VOCAB * TOKEN_WIDTH / 32u)
#define TOKEN_EMBED_SUPER_BLOCKS (TOKEN_VOCAB * TOKEN_WIDTH / 256u)
#define TOKEN_KV_LAYER_BYTES \
    (2u * TOKEN_KV_CAPACITY * TOKEN_KV_WIDTH * (uint32_t)sizeof(float))
#ifdef PS5_LARGE_MODEL
#define TOKEN_LAYER_OUTPUT_FLOATS \
    (TOKEN_WIDTH + TOKEN_FF_WIDTH / 32u + TOKEN_FF_WIDTH / 4u + \
     2u * TOKEN_FF_WIDTH)
#else
#define TOKEN_LAYER_OUTPUT_FLOATS \
    (2u * TOKEN_WIDTH + TOKEN_KV_WIDTH + 2u * TOKEN_FF_WIDTH)
#endif
#define LAYER_DESCRIPTOR_DWORDS 24u
#ifdef PS5_MODEL_QWEN35
#define LAYER_CONTROL_DWORDS 28u
#define LAYER_POSITION_INDEX 24u
#define QWEN_NORM_DESCRIPTOR_BYTES UINT32_C(0x29000)
#define QWEN_RECURRENT_STATE_BYTES UINT32_C(0x218000)
#else
#define LAYER_CONTROL_DWORDS 22u
#define LAYER_POSITION_INDEX 21u
#endif

#define LAYER_HEADER_OFFSET UINT32_C(0x0000)
#define LAYER_CODE_OFFSET UINT32_C(0x4000)
#define LAYER_DESCRIPTOR_OFFSET UINT32_C(0xb000)
#define LOGITS_HEADER_OFFSET UINT32_C(0xc000)
#ifdef PS5_MODEL_QWEN35
#define LOGITS_CODE_OFFSET UINT32_C(0x150000)
#define LOGITS_CODE_BYTES UINT32_C(0x10000)
#else
#define LOGITS_CODE_OFFSET UINT32_C(0xd000)
#define LOGITS_CODE_BYTES UINT32_C(0x1000)
#endif
#define LOGITS_DESCRIPTOR_OFFSET UINT32_C(0xe000)
#ifdef PS5_PRENORMALIZED_LOGITS
#define LOGITS_PREPARE_HEADER_OFFSET UINT32_C(0x90000)
#define LOGITS_PREPARE_CODE_OFFSET UINT32_C(0x94000)
#endif
#define TOKEN_STATE_OFFSET UINT32_C(0x10000)
#define LAYER_OUTPUT_OFFSET UINT32_C(0x100000)
#ifdef PS5_MODEL_QWEN35
#define LAYER_CONTROL_OFFSET UINT32_C(0x220000)
#else
#define LAYER_CONTROL_OFFSET UINT32_C(0x118000)
#endif
#ifdef PS5_STAGED_LAYER
#define FF_GATE_HEADER_OFFSET UINT32_C(0x120000)
#define FF_GATE_CODE_OFFSET UINT32_C(0x124000)
#define FF_QUANT_HEADER_OFFSET UINT32_C(0x130000)
#define FF_QUANT_CODE_OFFSET UINT32_C(0x134000)
#define FF_DOWN_HEADER_OFFSET UINT32_C(0x140000)
#define FF_DOWN_CODE_OFFSET UINT32_C(0x144000)
#ifdef PS5_SPLIT_ATTN
#define ATTN_QKV_HEADER_OFFSET UINT32_C(0x30000)
#define ATTN_QKV_CODE_OFFSET UINT32_C(0x34000)
#define ATTN_ROPE_HEADER_OFFSET UINT32_C(0x40000)
#define ATTN_ROPE_CODE_OFFSET UINT32_C(0x44000)
#ifdef PS5_NOPE_EVERY_FOURTH
#define ATTN_NOPE_HEADER_OFFSET UINT32_C(0x90000)
#define ATTN_NOPE_CODE_OFFSET UINT32_C(0x94000)
#endif
#define ATTN_CONTEXT_HEADER_OFFSET UINT32_C(0x50000)
#define ATTN_CONTEXT_CODE_OFFSET UINT32_C(0x54000)
#ifdef PS5_MODEL_QWEN35
#define ATTN_CONTEXT_ATTENTION_HEADER_OFFSET UINT32_C(0xa0000)
#define ATTN_CONTEXT_ATTENTION_CODE_OFFSET UINT32_C(0xa4000)
#endif
#define ATTN_QUANT_HEADER_OFFSET UINT32_C(0x60000)
#define ATTN_QUANT_CODE_OFFSET UINT32_C(0x64000)
#define ATTN_OUTPUT_HEADER_OFFSET UINT32_C(0x70000)
#define ATTN_OUTPUT_CODE_OFFSET UINT32_C(0x74000)
#define FF_PREPARE_HEADER_OFFSET UINT32_C(0x80000)
#define FF_PREPARE_CODE_OFFSET UINT32_C(0x84000)
#endif
#define LAYER_COMMAND_OFFSET UINT32_C(0x180000)
#ifdef PS5_SPLIT_ATTN
#ifdef PS5_SINGLE_COMMAND
#define LAYER_MARKER_OFFSET UINT32_C(0x1d0000)
#define SECOND_COMMAND_OFFSET UINT32_C(0x1a0000)
#define SECOND_MARKER_OFFSET UINT32_C(0x1b0000)
#define THIRD_COMMAND_OFFSET UINT32_C(0x1c0000)
#define THIRD_MARKER_OFFSET UINT32_C(0x1d0000)
#define SERIAL_LAYER_COMMAND_OFFSET UINT32_C(0x1e0000)
#define SERIAL_LAYER_MARKER_OFFSET UINT32_C(0x1f0000)
#define SERIAL_SECOND_COMMAND_OFFSET UINT32_C(0x200000)
#define SERIAL_SECOND_MARKER_OFFSET UINT32_C(0x210000)
#define SERIAL_THIRD_COMMAND_OFFSET UINT32_C(0x220000)
#define SERIAL_THIRD_MARKER_OFFSET UINT32_C(0x230000)
#define TOKEN_COMMAND_BYTES UINT32_C(0x40000)
#else
#ifdef PS5_MODEL_QWEN35
#define LAYER_MARKER_OFFSET UINT32_C(0x190000)
#ifndef QWEN_LAYERS_PER_COMMAND
#define QWEN_LAYERS_PER_COMMAND TOKEN_LAYERS
#endif
#define QWEN_COMMAND_SEGMENTS \
    ((TOKEN_LAYERS + QWEN_LAYERS_PER_COMMAND - 1u) / \
     QWEN_LAYERS_PER_COMMAND)
#else
#define LAYER_MARKER_OFFSET UINT32_C(0x190000)
#endif
#define SECOND_COMMAND_OFFSET UINT32_C(0x1a0000)
#define SECOND_MARKER_OFFSET UINT32_C(0x1b0000)
#define THIRD_COMMAND_OFFSET UINT32_C(0x1c0000)
#define THIRD_MARKER_OFFSET UINT32_C(0x1d0000)
#ifdef PS5_BATCH_PREFILL
#define SERIAL_LAYER_COMMAND_OFFSET UINT32_C(0x1e0000)
#define SERIAL_LAYER_MARKER_OFFSET UINT32_C(0x1f0000)
#define SERIAL_SECOND_COMMAND_OFFSET UINT32_C(0x200000)
#define SERIAL_SECOND_MARKER_OFFSET UINT32_C(0x210000)
#define SERIAL_THIRD_COMMAND_OFFSET UINT32_C(0x220000)
#define SERIAL_THIRD_MARKER_OFFSET UINT32_C(0x230000)
#endif
#define TOKEN_COMMAND_BYTES UINT32_C(0x10000)
#endif
#else
#define LAYER_MARKER_OFFSET UINT32_C(0x190000)
#define TOKEN_COMMAND_BYTES UINT32_C(0x10000)
#endif
#ifdef PS5_GPU_TIMING
#ifdef PS5_MODEL_QWEN35
#define GPU_TIMESTAMP_OFFSET UINT32_C(0x230000)
#else
#define GPU_TIMESTAMP_OFFSET UINT32_C(0x191000)
#endif
#ifdef PS5_SPLIT_ATTN
#define GPU_TIMING_PHASES 10u
#else
#define GPU_TIMING_PHASES 4u
#endif
#define GPU_TIMESTAMP_COUNT (TOKEN_LAYERS * GPU_TIMING_PHASES + 2u)
#define GPU_TIMESTAMP_BYTES (GPU_TIMESTAMP_COUNT * (uint32_t)sizeof(uint64_t))
#endif
#else
#define LAYER_COMMAND_OFFSET UINT32_C(0x270000)
#define LAYER_MARKER_OFFSET UINT32_C(0x278000)
#define TOKEN_COMMAND_BYTES COMMAND_BYTES
#endif
#define TOKEN_LOGITS_OFFSET UINT32_C(0x240000)
#ifdef PS5_MODEL_QWEN35
#define TOKEN_KV_OFFSET UINT32_C(0x340000)
#elif defined(PS5_LARGE_MODEL)
#define TOKEN_KV_OFFSET UINT32_C(0x300000)
#else
#define TOKEN_KV_OFFSET UINT32_C(0x280000)
#endif
_Static_assert(TOKEN_LOGITS_OFFSET + TOKEN_VOCAB * sizeof(float) <=
                   TOKEN_KV_OFFSET,
               "logits overlap the KV or recurrent-state cache");
#ifdef PS5_MODEL_QWEN35
_Static_assert(TOKEN_KV_OFFSET + TOKEN_LAYERS * TOKEN_KV_LAYER_BYTES <=
                   TOKEN_WORK_BYTES,
               "Qwen KV and recurrent state exceed the work allocation");
_Static_assert(LAYER_OUTPUT_OFFSET +
                       TOKEN_LAYER_OUTPUT_FLOATS * sizeof(float) <=
                   LAYER_CONTROL_OFFSET,
               "Qwen layer scratch overlaps its control table");
#endif
#ifdef PS5_BATCH_PREFILL
#ifndef PREFILL_BATCH_SIZE
#define PREFILL_BATCH_SIZE 256u
#endif
#define PREFILL_BUFFER_BYTES \
    (PREFILL_BATCH_SIZE * TOKEN_LAYER_OUTPUT_FLOATS * \
     (uint32_t)sizeof(float))
#define PREFILL_STATE_A_OFFSET \
    (TOKEN_KV_OFFSET + TOKEN_LAYERS * TOKEN_KV_LAYER_BYTES)
#define PREFILL_STATE_B_OFFSET \
    (PREFILL_STATE_A_OFFSET + PREFILL_BUFFER_BYTES)
_Static_assert(PREFILL_STATE_B_OFFSET + PREFILL_BUFFER_BYTES <=
                   TOKEN_WORK_BYTES,
               "prefill buffers exceed GPU work allocation");
#endif

#pragma pack(push, 1)
typedef struct token_model_header {
    char magic[4];
    uint32_t version;
    uint32_t header_bytes;
    uint32_t entry_bytes;
    uint32_t tensor_count;
    uint32_t block_count;
    uint32_t context_length;
    uint32_t embedding_length;
    uint32_t feed_forward_length;
    uint32_t head_count;
    uint32_t head_count_kv;
    uint32_t vocab_size;
    uint32_t rope_dimension_count;
    uint32_t data_offset;
    uint32_t flags;
    uint32_t segment_bytes;
    uint8_t source_sha256[32];
} token_model_header_t;

typedef struct token_tensor_entry {
    char name[80];
    uint32_t kind;
    uint32_t dimension_count;
    uint32_t dimensions[4];
    uint64_t offset;
    uint64_t bytes;
    uint32_t source_type;
    uint32_t reserved;
} token_tensor_entry_t;
#pragma pack(pop)

uint32_t ps5_compute_stage;
uint32_t ps5_compute_layer;
uint32_t ps5_compute_position;
uint32_t ps5_compute_generated_tokens[TOKEN_MAX_COUNT];
uint32_t ps5_compute_prompt_count;
uint32_t ps5_compute_generated_count;
uint32_t ps5_compute_expected_count;
uint32_t ps5_compute_context_full;
uint32_t ps5_compute_submit_rc = UINT32_MAX;
uint32_t ps5_compute_suspend_rc = UINT32_MAX;
uint32_t ps5_compute_polls;
uint32_t ps5_compute_marker_value;
uint32_t ps5_compute_model_path;
uint32_t ps5_compute_tokenizer_path;
uint32_t ps5_compute_prefill_batches;
uint64_t ps5_compute_prefill_us;
uint64_t ps5_compute_elapsed_us;
uint64_t ps5_compute_load_us;
uint64_t ps5_compute_load_map_us;
uint64_t ps5_compute_load_globals_us;
uint64_t ps5_compute_load_layers_us;
uint64_t ps5_compute_load_flush_us;
uint32_t ps5_compute_model_runtime_layout;
uint32_t ps5_compute_model_aio;
uint32_t ps5_compute_aio_submit_rc;
uint32_t ps5_compute_aio_wait_rc;
uint32_t ps5_compute_aio_delete_rc;
uint64_t ps5_compute_aio_result_value;
uint32_t ps5_compute_aio_result_state;
uint32_t ps5_compute_aio_wait_state;
uint32_t ps5_compute_aio_mismatch_index;
int32_t ps5_compute_aio_fd;
uint64_t ps5_compute_debug_ptrs[8];
#if defined(PS5_LAYER0_DIAGNOSTIC) || \
    defined(PS5_QWEN_DIAGNOSTIC_PHASES) || \
    defined(PS5_QWEN_CONTEXT_DIAGNOSTIC_PHASES)
uint32_t ps5_compute_debug_state_bits[16];
#endif
#ifdef PS5_GPU_TIMING
uint64_t ps5_compute_gpu_ticks[GPU_TIMING_PHASES + 1u];
uint32_t ps5_compute_gpu_samples;
#endif
char ps5_compute_response[4096];
static const ps5_chat_message_t *chat_messages;
static uint32_t chat_message_count;
static uint32_t chat_generated_count;
static void (*chat_progress)(const char *);
static uint8_t *resident_model;
static uint8_t *resident_layer_caches[LAYER_CACHE_BANK_COUNT];
static uint8_t *resident_work;
#ifdef PS5_LARGE_MODEL
static uint8_t *resident_primary_mapping;
static int64_t resident_primary_start = -1;
#endif
static uint32_t resident_tokens[TOKEN_MAX_COUNT];
static uint32_t resident_token_count;
static uint32_t resident_prompt_count;
static uint32_t resident_agc_initialized;
static char selected_model_file[256] = "/app0/model.ps5lm";
static char selected_tokenizer_file[256] = "/app0/tokenizer.ps5tok";
static uint32_t selected_model_revision = 1u;
static uint32_t resident_model_revision;
uint32_t ps5_compute_model_reused;
uint32_t ps5_compute_kv_reused;
#ifdef PS5_QWEN_CONTEXT_DIAGNOSTIC_PHASES
static uint32_t qwen_context_diagnostic_active;
#endif

int ps5_compute_select_model_files(const char *model_file,
                                   const char *tokenizer_file)
{
    size_t model_length;
    size_t tokenizer_length;

    if (!model_file || !tokenizer_file)
        return -1;
    model_length = strlen(model_file);
    tokenizer_length = strlen(tokenizer_file);
    if (!model_length || model_length >= sizeof(selected_model_file) ||
        !tokenizer_length ||
        tokenizer_length >= sizeof(selected_tokenizer_file))
        return -1;
    memcpy(selected_model_file, model_file, model_length + 1u);
    memcpy(selected_tokenizer_file, tokenizer_file, tokenizer_length + 1u);
    if (++selected_model_revision == 0u)
        selected_model_revision = 1u;
    resident_token_count = 0u;
    resident_prompt_count = 0u;
    return 0;
}

void ps5_compute_shutdown(void)
{
#ifdef PS5_LARGE_MODEL
    if (resident_primary_mapping)
        munmap(resident_primary_mapping, PRIMARY_MAPPING_BYTES);
    if (resident_primary_start >= 0)
        sceKernelReleaseDirectMemory(resident_primary_start,
                                     PRIMARY_MAPPING_BYTES);
    resident_primary_mapping = NULL;
    resident_primary_start = -1;
#endif
    resident_model = NULL;
    resident_work = NULL;
    memset(resident_layer_caches, 0, sizeof(resident_layer_caches));
    resident_token_count = 0u;
    resident_prompt_count = 0u;
    resident_model_revision = 0u;
}

static const token_tensor_entry_t *find_tensor(
    const token_model_header_t *header, const token_tensor_entry_t *entries,
    const char *name)
{
    uint32_t index;

    for (index = 0; index < header->tensor_count; ++index)
        if (strncmp(entries[index].name, name, sizeof(entries[index].name)) == 0)
            return &entries[index];
    return NULL;
}

static int create_program(agc_api_t *agc, const uint8_t *package,
                          size_t package_bytes, uint8_t *header_destination,
                          size_t header_capacity, uint8_t *code_destination,
                          size_t code_capacity, agc_register_t **program,
                          uint8_t *program_count)
{
    const uint8_t *header;
    const uint8_t *code;
    size_t header_bytes = 0;
    size_t code_bytes = 0;
    void *shader = NULL;

    if (shader_sections(package, package_bytes, &header, &header_bytes, &code,
                        &code_bytes) != 0 ||
        validate_shader_header(header, header_bytes, code_bytes) != 0 ||
        header_bytes > header_capacity || code_bytes > code_capacity)
        return -1;
    memcpy(header_destination, header, header_bytes);
    memcpy(code_destination, code, code_bytes);
    if (agc->create_shader(&shader, header_destination, code_destination) != 0 ||
        shader != header_destination || header_destination[0x5a] != 0)
        return -1;
    *program = *(agc_register_t **)(header_destination + 32);
    *program_count = header_destination[92];
    return *program_count == 8 ? 0 : -1;
}

static int acquire_read_range(agc_api_t *agc, agc_command_buffer_t *command,
                              const void *buffer, uint64_t bytes)
{
    uint64_t address = (uint64_t)(uintptr_t)buffer;

    while (bytes != 0u) {
        uint64_t boundary = (address + UINT64_C(0x40000000)) &
                            ~UINT64_C(0x3fffffff);
        uint64_t available = boundary - address;
        uint32_t chunk = available < bytes ? (uint32_t)available
                                           : (uint32_t)bytes;

        if (!agc->acquire_mem(command, 0, 0, GPU_READ_ACQUIRE_GCR,
                              address, chunk, GPU_READ_ACQUIRE_POLL))
            return 0;
        address += chunk;
        bytes -= chunk;
    }
    return 1;
}

static int build_token_command(
    agc_api_t *agc, agc_register_t *layer_program,
    uint8_t layer_program_count, uint32_t *layer_descriptors,
#ifdef PS5_STAGED_LAYER
    agc_register_t *gate_program, uint8_t gate_program_count,
    agc_register_t *quant_program, uint8_t quant_program_count,
    agc_register_t *down_program, uint8_t down_program_count,
#endif
#ifdef PS5_SPLIT_ATTN
    agc_register_t *qkv_program, uint8_t qkv_program_count,
    agc_register_t *rope_program, uint8_t rope_program_count,
#ifdef PS5_NOPE_EVERY_FOURTH
    agc_register_t *nope_program, uint8_t nope_program_count,
#endif
    agc_register_t *context_program, uint8_t context_program_count,
#ifdef PS5_MODEL_QWEN35
    agc_register_t *attention_context_program,
    uint8_t attention_context_program_count,
#endif
    agc_register_t *attn_quant_program, uint8_t attn_quant_program_count,
    agc_register_t *output_program, uint8_t output_program_count,
    agc_register_t *ff_prepare_program, uint8_t ff_prepare_program_count,
#endif
    agc_register_t *logits_program, uint8_t logits_program_count,
#ifdef PS5_PRENORMALIZED_LOGITS
    agc_register_t *logits_prepare_program,
    uint8_t logits_prepare_program_count,
#endif
    uint32_t *logits_descriptors, const void *work, uint32_t work_bytes,
    const void *weights, uint64_t weight_bytes, const void *model,
#ifdef PS5_BATCH_PREFILL
    uint32_t dispatch_tokens,
#endif
    uint64_t model_bytes, uint32_t layer_begin, uint32_t layer_end,
    int include_logits, volatile uint32_t *marker, uint32_t *words,
#ifdef PS5_GPU_TIMING
    volatile uint64_t *timestamps,
#endif
    uint32_t *word_count)
{
    agc_command_buffer_t command = {0};
    uint32_t user_data[COMPUTE_USER_DWORDS] = {0};
    uint32_t layer;

    memset(words, 0, TOKEN_COMMAND_BYTES);
    *marker = 0;
    command.bottom = words;
    command.top = words + TOKEN_COMMAND_BYTES / sizeof(*words);
    command.up = words;
    command.down = command.top;
    command.callback = (uintptr_t)command_out_of_space;
    out_of_space = 0;
    if (!agc->set_sh(&command, layer_program, layer_program_count) ||
        !acquire_read_range(agc, &command, work, work_bytes) ||
        !acquire_read_range(agc, &command, weights, weight_bytes) ||
        !acquire_read_range(agc, &command, model, model_bytes))
        return -1;
#ifdef PS5_GPU_TIMING
    if (!agc->release_mem(
            &command, 40, 0, 0, 0,
            (void *)&timestamps[layer_begin * GPU_TIMING_PHASES], 3,
            0, 0, 0, 0, 0))
        return -1;
#endif
    for (layer = layer_begin; layer < layer_end; ++layer) {
#ifdef PS5_STAGED_LAYER
#ifdef PS5_SPLIT_ATTN
        agc_register_t *programs[10] = {
            layer_program, qkv_program, rope_program, context_program,
            attn_quant_program, output_program, ff_prepare_program,
            gate_program, quant_program, down_program,
        };
        uint8_t counts[10] = {
            layer_program_count, qkv_program_count, rope_program_count,
            context_program_count, attn_quant_program_count,
            output_program_count, ff_prepare_program_count,
            gate_program_count, quant_program_count, down_program_count,
        };
        uint32_t groups[10] = {
#ifdef PS5_MODEL_QWEN35
            1u, 12352u, 32u, 32u, TOKEN_WIDTH / 32u, TOKEN_WIDTH, 1u,
            TOKEN_FF_WIDTH,
            (TOKEN_FF_WIDTH / 32u + 255u) / 256u,
            TOKEN_WIDTH,
#else
            1u,
#ifdef PS5_COOPERATIVE_QKV
            TOKEN_WIDTH + 2u * TOKEN_KV_WIDTH,
#else
            (TOKEN_WIDTH + 2u * TOKEN_KV_WIDTH + 63u) / 64u,
#endif
            ((TOKEN_WIDTH + TOKEN_KV_WIDTH) / 2u + 63u) / 64u,
#ifdef PS5_COOPERATIVE_ATTN_CONTEXT
            TOKEN_HEADS,
#else
            (TOKEN_WIDTH + 63u) / 64u,
#endif
            1u,
#ifdef PS5_COOPERATIVE_ATTN_OUTPUT
            TOKEN_WIDTH,
#else
            (TOKEN_WIDTH + 63u) / 64u,
#endif
            1u,
#ifdef PS5_COOPERATIVE_GATE_UP
            TOKEN_FF_WIDTH,
#else
            (TOKEN_FF_WIDTH + 63u) / 64u,
#endif
            (TOKEN_FF_WIDTH / 32u + 255u) / 256u,
#ifdef PS5_COOPERATIVE_FF_DOWN
            TOKEN_WIDTH,
#else
            (TOKEN_WIDTH + 63u) / 64u,
#endif
#endif
        };
#ifdef PS5_MODEL_QWEN35
        if ((layer + 1u) % 4u == 0u) {
            programs[3] = attention_context_program;
            counts[3] = attention_context_program_count;
            groups[1] = 10240u;
            groups[2] = TOKEN_HEADS;
            groups[3] = TOKEN_HEADS;
        }
#endif
        uint32_t phase_count = 10u;
#ifdef PS5_QWEN_DIAGNOSTIC_PHASES
        if (layer + 1u == layer_end)
            phase_count = PS5_QWEN_DIAGNOSTIC_PHASES;
#endif
#ifdef PS5_QWEN_CONTEXT_DIAGNOSTIC_PHASES
        if (qwen_context_diagnostic_active &&
            layer == PS5_QWEN_CONTEXT_DIAGNOSTIC_LAYER)
            phase_count = PS5_QWEN_CONTEXT_DIAGNOSTIC_PHASES;
#endif
#ifdef PS5_NOPE_EVERY_FOURTH
        if ((layer + 1u) % 4u == 0u) {
            programs[2] = nope_program;
            counts[2] = nope_program_count;
        }
#endif
#else
        agc_register_t *programs[4] = {
            layer_program, gate_program, quant_program, down_program,
        };
        uint8_t counts[4] = {
            layer_program_count, gate_program_count,
            quant_program_count, down_program_count,
        };
        uint32_t groups[4] = {
            1u, TOKEN_FF_WIDTH / 64u, 1u, TOKEN_WIDTH / 64u,
        };
        uint32_t phase_count = 4u;
#endif
        uint32_t phase;

        user_data[DESCRIPTOR_USER_DWORD] = (uint32_t)(uintptr_t)(
            layer_descriptors + layer * LAYER_DESCRIPTOR_DWORDS);
        for (phase = 0; phase < phase_count; ++phase) {
            uint32_t sync_value =
                (layer - layer_begin) * phase_count + phase + 1u;

            if (!agc->set_sh(&command, programs[phase], counts[phase]) ||
                !agc->set_sh_direct(&command, COMPUTE_USER_DATA_0, user_data,
                                    COMPUTE_USER_DWORDS) ||
#ifdef PS5_BATCH_PREFILL
                !agc->dispatch(&command, dispatch_tokens, groups[phase], 1,
                               COMPUTE_DISPATCH_MODIFIER) ||
#else
                !agc->dispatch(&command, groups[phase], 1, 1,
                               COMPUTE_DISPATCH_MODIFIER) ||
#endif
#ifdef PS5_GPU_TIMING
                !agc->release_mem(
                    &command, 40, 0, 0, 0,
            (void *)&timestamps[1u + layer * GPU_TIMING_PHASES + phase], 3,
                    0, 0, 0, 0, 0) ||
#endif
                !agc->release_mem(&command, 40, 0x30c, 0, 0,
                                  (void *)marker, 1, sync_value, 0, 0, 0, 0) ||
                !sceAgcDcbWaitRegMem(
                    &command, 0, 3, 0, 0, (uint64_t)(uintptr_t)marker,
                    sync_value, UINT32_MAX, GPU_READ_ACQUIRE_POLL))
                return -1;
        }
#ifdef PS5_MODEL_QWEN35
        /* A new submission supplies this visibility boundary implicitly.
           Grouped Qwen layers need it explicitly before the next layer reads
           state written through the previous layer's descriptors. */
        if (layer + 1u < layer_end &&
            !acquire_read_range(agc, &command, work, work_bytes))
            return -1;
#endif
#else
        uint32_t sync_value = layer - layer_begin + 1u;

        user_data[DESCRIPTOR_USER_DWORD] = (uint32_t)(uintptr_t)(
            layer_descriptors + layer * LAYER_DESCRIPTOR_DWORDS);
        if (!agc->set_sh_direct(&command, COMPUTE_USER_DATA_0, user_data,
                                COMPUTE_USER_DWORDS) ||
            !agc->dispatch(&command, 1, 1, 1, COMPUTE_DISPATCH_MODIFIER))
            return -1;
        if (!agc->release_mem(&command, 40, 0x30c, 0, 0,
                              (void *)marker, 1, sync_value, 0, 0, 0, 0) ||
            !sceAgcDcbWaitRegMem(
                &command, 0, 3, 0, 0, (uint64_t)(uintptr_t)marker,
                sync_value, UINT32_MAX, GPU_READ_ACQUIRE_POLL))
            return -1;
#endif
    }
    if (include_logits) {
        user_data[DESCRIPTOR_USER_DWORD] =
            (uint32_t)(uintptr_t)logits_descriptors;
#ifdef PS5_PRENORMALIZED_LOGITS
        {
            uint32_t sync_value = (layer_end - layer_begin) * 10u + 1u;
            if (!agc->set_sh(&command, logits_prepare_program,
                             logits_prepare_program_count) ||
                !agc->set_sh_direct(&command, COMPUTE_USER_DATA_0, user_data,
                                    COMPUTE_USER_DWORDS) ||
                !agc->dispatch(&command, 1, 1, 1,
                               COMPUTE_DISPATCH_MODIFIER) ||
                !agc->release_mem(&command, 40, 0x30c, 0, 0,
                                  (void *)marker, 1, sync_value, 0, 0, 0, 0) ||
                !sceAgcDcbWaitRegMem(
                    &command, 0, 3, 0, 0, (uint64_t)(uintptr_t)marker,
                    sync_value, UINT32_MAX, GPU_READ_ACQUIRE_POLL))
                return -1;
        }
#endif
        uint32_t groups_x = TOKEN_VOCAB > 65535u ? 65535u : TOKEN_VOCAB;
        uint32_t groups_y = (TOKEN_VOCAB + groups_x - 1u) / groups_x;

        if (!agc->set_sh(&command, logits_program, logits_program_count) ||
            !agc->set_sh_direct(&command, COMPUTE_USER_DATA_0, user_data,
                                COMPUTE_USER_DWORDS) ||
            !agc->dispatch(&command, groups_x, groups_y, 1,
                           COMPUTE_DISPATCH_MODIFIER))
            return -1;
#ifdef PS5_GPU_TIMING
        if (!agc->release_mem(&command, 40, 0, 0, 0,
                              (void *)&timestamps[GPU_TIMESTAMP_COUNT - 1u],
                              3, 0, 0, 0, 0, 0))
            return -1;
#endif
    }
    if (!agc->release_mem(&command, 40, 0x30c, 0, 0, (void *)marker, 1,
                          COMPLETION_MARKER, 0, 0, 0, 0))
        return -1;
    *word_count = (uint32_t)(command.up - words);
    return !out_of_space && command.up >= command.bottom &&
                   command.up <= command.top && *word_count
               ? 0
               : -1;
}

static int execute_command(agc_api_t *agc, uint8_t *work, uint32_t *words,
                           uint32_t word_count, volatile uint32_t *marker
#ifdef PS5_GPU_TIMING
                           , volatile uint64_t *timestamps
#endif
)
{
    agc_submit_description_t submit = {words, word_count, 0, {0}};
    int submit_rc;
    int suspend_rc;

    *marker = 0;
#ifdef PS5_GPU_TIMING
    memset((void *)timestamps, 0, GPU_TIMESTAMP_BYTES);
#endif
#ifdef PS5_MODEL_QWEN35
    /* Qwen commands and controls are CPU-written between submissions. */
    flush_cache(words, word_count * sizeof(*words));
    flush_cache(work + LAYER_CONTROL_OFFSET,
                TOKEN_LAYERS * LAYER_CONTROL_DWORDS * sizeof(uint32_t));
    flush_cache((const void *)marker, sizeof(*marker));
#else
    flush_cache(work, TOKEN_KV_OFFSET);
#endif
    submit_rc = agc->submit(&submit);
    suspend_rc = submit_rc == 0 ? agc->suspend_point() : -1;
    ps5_compute_submit_rc = (uint32_t)submit_rc;
    ps5_compute_suspend_rc = (uint32_t)suspend_rc;
    ps5_compute_polls = submit_rc == 0 && suspend_rc == 0
                            ? wait_for_marker(marker)
                            : 0;
    flush_cache((const void *)marker, sizeof(*marker));
    ps5_compute_marker_value = *marker;
#ifdef PS5_GPU_TIMING
    if (ps5_compute_polls) {
        uint32_t layer;
        uint32_t phase;

        flush_cache((void *)timestamps, GPU_TIMESTAMP_BYTES);
        // ponytail: aggregate phases only; retain per-layer data if skew matters.
        for (layer = 0; layer < TOKEN_LAYERS; ++layer)
            for (phase = 0; phase < GPU_TIMING_PHASES; ++phase) {
                uint32_t at = layer * GPU_TIMING_PHASES + phase;
                uint64_t delta = timestamps[at + 1u] - timestamps[at];
                /* A segmented command starts at a layer boundary whose prior
                   slot is zero. Do not mistake that absolute clock value for
                   the preceding segment's final phase. */
                if (timestamps[at + 1u] >= timestamps[at] &&
                    delta < UINT64_C(1000000000))
                    ps5_compute_gpu_ticks[phase] +=
                        delta;
            }
        if (timestamps[GPU_TIMESTAMP_COUNT - 1u] >=
            timestamps[GPU_TIMESTAMP_COUNT - 2u])
            ps5_compute_gpu_ticks[GPU_TIMING_PHASES] +=
                timestamps[GPU_TIMESTAMP_COUNT - 1u] -
                timestamps[GPU_TIMESTAMP_COUNT - 2u];
        ++ps5_compute_gpu_samples;
    }
#endif
    return submit_rc == 0 && suspend_rc == 0 && ps5_compute_polls ? 0 : -1;
}

static int copy_tensor(FILE *file, const uint8_t *model,
                       const token_tensor_entry_t *entry, void *destination)
{
    if (file) {
        if (fseek(file, (long)entry->offset, SEEK_SET) != 0 ||
            fread(destination, 1, (size_t)entry->bytes, file) != entry->bytes)
            return -1;
    } else {
        memcpy(destination, model + entry->offset, (size_t)entry->bytes);
    }
    return 0;
}

static int read_model_stream(FILE *file, void *destination, uint64_t bytes)
{
    uint64_t copied = 0;

    while (copied < bytes) {
        size_t chunk = (size_t)(bytes - copied);

        if (chunk > MODEL_STREAM_CHUNK_BYTES)
            chunk = MODEL_STREAM_CHUNK_BYTES;
        if (fread((uint8_t *)destination + (size_t)copied, 1, chunk, file) !=
            chunk)
            return -1;
        copied += chunk;
    }
    return 0;
}

static int read_model_aio(int fd, void *destination, uint64_t file_offset,
                          uint64_t bytes)
{
    ps5_aio_request_t requests[MODEL_AIO_REQUESTS];
    ps5_aio_result_t results[MODEL_AIO_REQUESTS];
    int ids[MODEL_AIO_REQUESTS];
    int wait_states[MODEL_AIO_REQUESTS];
    int delete_errors[MODEL_AIO_REQUESTS];

    while (bytes) {
        uint32_t count = 0;
        uint32_t index;
        uint64_t window_bytes = 0;
        int submit_rc;
        int wait_rc;
        int delete_rc;

        memset(requests, 0, sizeof(requests));
        memset(results, 0, sizeof(results));
        memset(ids, 0, sizeof(ids));
        memset(wait_states, 0, sizeof(wait_states));
        memset(delete_errors, 0, sizeof(delete_errors));
        while (count < MODEL_AIO_REQUESTS && window_bytes < bytes) {
            size_t chunk = (size_t)(bytes - window_bytes);

            if (chunk > MODEL_AIO_CHUNK_BYTES)
                chunk = MODEL_AIO_CHUNK_BYTES;
            requests[count].offset = (int64_t)(file_offset + window_bytes);
            requests[count].bytes = chunk;
            requests[count].buffer =
                (uint8_t *)destination + (size_t)window_bytes;
            requests[count].result = &results[count];
            requests[count].fd = fd;
            window_bytes += chunk;
            ++count;
        }
        submit_rc = sceKernelAioSubmitReadCommandsMultiple(
            requests, count, 3u, ids);
        ps5_compute_aio_submit_rc = (uint32_t)submit_rc;
        if (submit_rc != 0)
            return -1;
        wait_rc =
            sceKernelAioWaitRequests(ids, count, wait_states, 1u, NULL);
        ps5_compute_aio_wait_rc = (uint32_t)wait_rc;
        ps5_compute_aio_result_value = (uint64_t)results[0].return_value;
        ps5_compute_aio_result_state = results[0].state;
        ps5_compute_aio_wait_state = (uint32_t)wait_states[0];
        delete_rc = sceKernelAioDeleteRequests(ids, count, delete_errors);
        ps5_compute_aio_delete_rc = (uint32_t)delete_rc;
        if (wait_rc != 0 || delete_rc != 0)
            return -1;
        for (index = 0; index < count; ++index)
            if (delete_errors[index] != 0 ||
                results[index].return_value !=
                    (int64_t)requests[index].bytes) {
                ps5_compute_aio_mismatch_index = index;
                return -1;
            }
        destination = (uint8_t *)destination + (size_t)window_bytes;
        file_offset += window_bytes;
        bytes -= window_bytes;
    }
    return 0;
}

static int load_layer(FILE *file, const uint8_t *model,
                      const token_model_header_t *header,
                      const token_tensor_entry_t *entries, uint32_t layer,
                      uint8_t *weights, float *norms)
{
#ifdef PS5_MODEL_QWEN35
    static const char *const recurrent_matrix_names[8] = {
        "attn_qkv.weight", "attn_gate.weight", "ssm_alpha.weight",
        "ssm_beta.weight", "ssm_out.weight", "ffn_gate.weight",
        "ffn_up.weight", "ffn_down.weight",
    };
    static const char *const attention_matrix_names[8] = {
        "attn_q.weight", "attn_k.weight", "attn_v.weight",
        "attn_output.weight", NULL, "ffn_gate.weight",
        "ffn_up.weight", "ffn_down.weight",
    };
    static const uint32_t matrix_offsets[8] = {
        REAL_LAYER_Q_OFFSET, REAL_LAYER_K_OFFSET, REAL_LAYER_V_OFFSET,
        REAL_LAYER_O_OFFSET, REAL_LAYER_SSM_OUT_OFFSET,
        REAL_LAYER_GATE_OFFSET, REAL_LAYER_UP_OFFSET, REAL_LAYER_DOWN_OFFSET,
    };
    static const uint32_t recurrent_kinds[8] = {1u, 1u, 2u, 2u, 5u, 1u, 1u, 1u};
    static const uint32_t attention_kinds[8] = {1u, 1u, 1u, 1u, 0u, 1u, 1u, 1u};
    static const char *const common_norm_names[2] = {
        "attn_norm.weight", "post_attention_norm.weight",
    };
    static const uint32_t common_norm_offsets[2] = {0u, 0x4000u};
    static const char *const recurrent_float_names[4] = {
        "ssm_conv1d.weight", "ssm_dt.bias", "ssm_a", "ssm_norm.weight",
    };
    static const uint32_t recurrent_float_offsets[4] = {
        0x8000u, 0x28000u, 0x28400u, 0x28800u,
    };
    static const uint32_t recurrent_float_bytes[4] = {
        8192u * 4u * 4u, 32u * 4u, 32u * 4u, 128u * 4u,
    };
    static const char *const attention_float_names[2] = {
        "attn_q_norm.weight", "attn_k_norm.weight",
    };
    static const uint32_t attention_float_offsets[2] = {0x8000u, 0x8400u};
    const int recurrent = (layer + 1u) % 4u != 0u;
    const char *const *matrix_names = recurrent ? recurrent_matrix_names
                                                 : attention_matrix_names;
    const uint32_t *matrix_kinds = recurrent ? recurrent_kinds
                                              : attention_kinds;
    char name[80];
    uint32_t index;

    for (index = 0; index < 8u; ++index) {
        const token_tensor_entry_t *entry;
        uint32_t expected_kind = matrix_kinds[index];

        if (!matrix_names[index])
            continue;
        if (index == 7u && layer < 4u)
            expected_kind = 4u;
        snprintf(name, sizeof(name), "blk.%u.%s", layer, matrix_names[index]);
        entry = find_tensor(header, entries, name);
        if (!entry || entry->kind != expected_kind ||
            copy_tensor(file, model, entry,
                        weights + (size_t)matrix_offsets[index] * 4u) != 0)
            return -1;
    }
    for (index = 0; index < 2u; ++index) {
        const token_tensor_entry_t *entry;

        snprintf(name, sizeof(name), "blk.%u.%s", layer,
                 common_norm_names[index]);
        entry = find_tensor(header, entries, name);
        if (!entry || entry->kind != 0u || entry->bytes != TOKEN_WIDTH * 4u ||
            copy_tensor(file, model, entry,
                        (uint8_t *)norms + common_norm_offsets[index]) != 0)
            return -1;
    }
    if (recurrent) {
        for (index = 0; index < 4u; ++index) {
            const token_tensor_entry_t *entry;

            snprintf(name, sizeof(name), "blk.%u.%s", layer,
                     recurrent_float_names[index]);
            entry = find_tensor(header, entries, name);
            if (!entry || entry->kind != 0u ||
                entry->bytes != recurrent_float_bytes[index] ||
                copy_tensor(file, model, entry,
                            (uint8_t *)norms +
                                recurrent_float_offsets[index]) != 0)
                return -1;
        }
    } else {
        for (index = 0; index < 2u; ++index) {
            const token_tensor_entry_t *entry;

            snprintf(name, sizeof(name), "blk.%u.%s", layer,
                     attention_float_names[index]);
            entry = find_tensor(header, entries, name);
            if (!entry || entry->kind != 0u || entry->bytes != 256u * 4u ||
                copy_tensor(file, model, entry,
                            (uint8_t *)norms +
                                attention_float_offsets[index]) != 0)
                return -1;
        }
    }
    return 0;
#else
    static const char *const matrix_names[7] = {
        "attn_q.weight", "attn_k.weight", "attn_v.weight",
        "attn_output.weight", "ffn_gate.weight", "ffn_up.weight",
        "ffn_down.weight",
    };
    static const uint32_t matrix_offsets[8] = {
        REAL_LAYER_Q_OFFSET, REAL_LAYER_K_OFFSET, REAL_LAYER_V_OFFSET,
        REAL_LAYER_O_OFFSET, REAL_LAYER_GATE_OFFSET, REAL_LAYER_UP_OFFSET,
        REAL_LAYER_DOWN_OFFSET, REAL_LAYER_WEIGHT_DWORDS,
    };
    char name[80];
    uint32_t index;

    for (index = 0; index < 7; ++index) {
        const token_tensor_entry_t *entry;
        uint64_t expected_bytes =
            (uint64_t)(matrix_offsets[index + 1] - matrix_offsets[index]) * 4;

        snprintf(name, sizeof(name), "blk.%u.%s", layer, matrix_names[index]);
        entry = find_tensor(header, entries, name);
        if (!entry || entry->kind != 1 || entry->bytes != expected_bytes)
            return -1;
        if (copy_tensor(file, model, entry,
                        weights + (size_t)matrix_offsets[index] * 4) != 0)
            return -1;
    }
    snprintf(name, sizeof(name), "blk.%u.attn_norm.weight", layer);
    {
        const token_tensor_entry_t *entry = find_tensor(header, entries, name);
        if (!entry || entry->kind != 0 || entry->bytes != TOKEN_WIDTH * 4u)
            return -1;
        if (copy_tensor(file, model, entry, norms) != 0)
            return -1;
    }
    snprintf(name, sizeof(name), "blk.%u.ffn_norm.weight", layer);
    {
        const token_tensor_entry_t *entry = find_tensor(header, entries, name);
        if (!entry || entry->kind != 0 || entry->bytes != TOKEN_WIDTH * 4u)
            return -1;
        if (copy_tensor(file, model, entry, norms + TOKEN_WIDTH) != 0)
            return -1;
    }
    return 0;
#endif
}

static int load_embedding(const uint8_t *model,
                          const token_tensor_entry_t *embedding,
                          uint32_t token, float *state)
{
    const float *scales;
    const int8_t *values;
    uint32_t block;

    if (!embedding || (embedding->kind != 1 && embedding->kind != 2 &&
                       embedding->kind != 3) ||
        embedding->dimension_count != 2 ||
        embedding->dimensions[0] != TOKEN_WIDTH ||
        embedding->dimensions[1] != TOKEN_VOCAB)
        return -1;
    if (embedding->kind == 3) {
        const float *q6_scales = (const float *)(model + embedding->offset);
        const uint8_t *quants = model + embedding->offset +
            (uint64_t)TOKEN_EMBED_SUPER_BLOCKS * 16u * sizeof(float);
        uint32_t column;

        for (column = 0; column < TOKEN_WIDTH; ++column) {
            uint32_t super = token * (TOKEN_WIDTH / 256u) + column / 256u;
            uint32_t within = column & 255u;
            uint32_t chunk = within / 128u;
            uint32_t section = (within & 127u) / 32u;
            uint32_t lane = within & 31u;
            uint32_t ql_at = chunk * 64u + lane +
                             ((section & 1u) * 32u);
            uint32_t qh_at = 128u + chunk * 32u + lane;
            uint32_t low = (quants[super * 192u + ql_at] >>
                            (section >= 2u ? 4u : 0u)) & 15u;
            uint32_t high = (quants[super * 192u + qh_at] >>
                             (section * 2u)) & 3u;
            uint32_t scale = chunk * 8u + lane / 16u + section * 2u;

            state[column] = q6_scales[super * 16u + scale] *
                            (float)((int)(low | (high << 4u)) - 32);
        }
        return 0;
    }
    scales = (const float *)(model + embedding->offset);
    if (embedding->kind == 1) {
        const uint8_t *packed = model + embedding->offset +
            (uint64_t)TOKEN_EMBED_BLOCKS * sizeof(float);

        for (block = 0; block < TOKEN_WIDTH / 32u; ++block) {
            uint32_t global_block = token * (TOKEN_WIDTH / 32u) + block;
            uint32_t item;

            for (item = 0; item < 32u; ++item) {
                uint8_t pair = packed[global_block * 16u + (item & 15u)];
                uint32_t quant = item < 16u ? pair & 15u : pair >> 4u;

                state[block * 32u + item] =
                    scales[global_block] * (float)((int)quant - 8);
            }
        }
        return 0;
    }
    values = (const int8_t *)(model + embedding->offset +
                              (uint64_t)TOKEN_EMBED_BLOCKS * sizeof(float));
    for (block = 0; block < TOKEN_WIDTH / 32u; ++block) {
        uint32_t global_block = token * (TOKEN_WIDTH / 32u) + block;
        uint32_t item;

        for (item = 0; item < 32u; ++item)
            state[block * 32u + item] =
                scales[global_block] * values[global_block * 32u + item];
    }
    return 0;
}

static int load_prompt(uint32_t *tokens, uint32_t *token_count,
                       uint32_t *generated_count)
{
    FILE *input = fopen("/app0/prompt.tokens", "r");
    uint32_t token;

    *token_count = 0;
    if (!input || fscanf(input, "%u", generated_count) != 1)
        goto fail;
    while (fscanf(input, "%u", &token) == 1) {
        if (*token_count == TOKEN_MAX_COUNT || token >= TOKEN_VOCAB)
            goto fail;
        tokens[(*token_count)++] = token;
    }
    if (!feof(input) || *token_count == 0 || *generated_count == 0 ||
        *generated_count > TOKEN_MAX_COUNT ||
        *token_count + *generated_count - 1u > TOKEN_KV_CAPACITY)
        goto fail;
    fclose(input);
    return 0;

fail:
    if (input)
        fclose(input);
    return -1;
}

static int load_expected(uint32_t *tokens, uint32_t *token_count)
{
    FILE *input = fopen("/app0/expected.tokens", "r");
    uint32_t token;

    *token_count = 0;
    if (!input)
        return 0;
    while (fscanf(input, "%u", &token) == 1) {
        if (*token_count == TOKEN_MAX_COUNT || token >= TOKEN_VOCAB)
            goto fail;
        tokens[(*token_count)++] = token;
    }
    if (!feof(input))
        goto fail;
    fclose(input);
    return 0;

fail:
    fclose(input);
    return -1;
}

static int load_generation_count(uint32_t expected_count,
                                 uint32_t *generated_count)
{
    FILE *input;
    uint32_t count;

    if (expected_count) {
        *generated_count = expected_count;
        return 0;
    }
    input = fopen("/app0/generate.txt", "r");
    if (!input) {
        *generated_count = 16u;
        return 0;
    }
    if (fscanf(input, "%u", &count) != 1 || count == 0u ||
        count > TOKEN_MAX_COUNT) {
        fclose(input);
        return -1;
    }
    fclose(input);
    *generated_count = count;
    return 0;
}

static int load_text_prompt(ps5_tokenizer_t *tokenizer, uint32_t *tokens,
                            uint32_t *token_count, uint32_t generated_count)
{
    char text[512];
    FILE *input;
    size_t bytes;
    int count;
    ps5_chat_message_t message = {"user", text};
    const ps5_chat_message_t *messages = &message;
    uint32_t message_count = 1u;

    if (chat_messages) {
        messages = chat_messages;
        message_count = chat_message_count;
        if (ps5_tokenizer_load(tokenizer, selected_tokenizer_file) != 0)
            return -1;
        ps5_compute_tokenizer_path = 1;
        count = ps5_chat_build_prompt(tokenizer, messages, message_count, tokens,
                                      TOKEN_MAX_COUNT);
        if (count <= 0 || (uint32_t)count + generated_count - 1u >
                              TOKEN_KV_CAPACITY) {
            ps5_compute_context_full = 1u;
            return -1;
        }
        *token_count = (uint32_t)count;
        return 0;
    }

    input = fopen("/app0/prompt.txt", "rb");
    if (!input)
        return 1;
    bytes = fread(text, 1, sizeof(text) - 1u, input);
    if (ferror(input) || !feof(input)) {
        fclose(input);
        return -1;
    }
    fclose(input);
    while (bytes && (text[bytes - 1u] == '\r' || text[bytes - 1u] == '\n'))
        --bytes;
    text[bytes] = '\0';
    if (!bytes || ps5_tokenizer_load(tokenizer, selected_tokenizer_file) != 0)
        return -1;
    ps5_compute_tokenizer_path = 1;
    count = ps5_chat_build_prompt(tokenizer, messages, message_count, tokens,
                                  TOKEN_MAX_COUNT);
    if (count <= 0 || (uint32_t)count + generated_count - 1u >
                          TOKEN_KV_CAPACITY)
        return -1;
    *token_count = (uint32_t)count;
    return 0;
}

static int decode_response(const ps5_tokenizer_t *tokenizer, uint32_t count)
{
    uint32_t decode_count = count;
    uint32_t index;

    for (index = 0; index < count; ++index)
        if (ps5_compute_generated_tokens[index] == TOKEN_EOS) {
            decode_count = index;
            break;
        }
    return ps5_tokenizer_decode(tokenizer, ps5_compute_generated_tokens,
                                decode_count, ps5_compute_response,
                                sizeof(ps5_compute_response));
}

static void set_layer_position(uint32_t *layer_control, uint32_t position)
{
#ifdef PS5_MODEL_QWEN35
    uint32_t layer;

    for (layer = 0; layer < TOKEN_LAYERS; ++layer)
        layer_control[layer * LAYER_CONTROL_DWORDS + LAYER_POSITION_INDEX] =
            position;
#else
    layer_control[LAYER_POSITION_INDEX] = position;
#endif
}

int run_model_token(void)
{
#ifdef PS5_MODEL_QWEN35
    static const uint32_t recurrent_params[24] = {
        REAL_LAYER_Q_OFFSET, 8192u, TOKEN_WIDTH,
        REAL_LAYER_K_OFFSET, 4096u, TOKEN_WIDTH,
        REAL_LAYER_V_OFFSET, 32u, TOKEN_WIDTH,
        REAL_LAYER_O_OFFSET, 32u, TOKEN_WIDTH,
        REAL_LAYER_SSM_OUT_OFFSET, TOKEN_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_GATE_OFFSET, TOKEN_FF_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_UP_OFFSET, TOKEN_FF_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_DOWN_OFFSET, TOKEN_WIDTH, TOKEN_FF_WIDTH,
    };
    static const uint32_t attention_params[24] = {
        REAL_LAYER_Q_OFFSET, 8192u, TOKEN_WIDTH,
        REAL_LAYER_K_OFFSET, TOKEN_KV_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_V_OFFSET, TOKEN_KV_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_O_OFFSET, TOKEN_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_SSM_OUT_OFFSET, 0u, 0u,
        REAL_LAYER_GATE_OFFSET, TOKEN_FF_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_UP_OFFSET, TOKEN_FF_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_DOWN_OFFSET, TOKEN_WIDTH, TOKEN_FF_WIDTH,
    };
#else
    static const uint32_t layer_params[22] = {
        REAL_LAYER_Q_OFFSET, TOKEN_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_K_OFFSET, TOKEN_KV_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_V_OFFSET, TOKEN_KV_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_O_OFFSET, TOKEN_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_GATE_OFFSET, TOKEN_FF_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_UP_OFFSET, TOKEN_FF_WIDTH, TOKEN_WIDTH,
        REAL_LAYER_DOWN_OFFSET, TOKEN_WIDTH, TOKEN_FF_WIDTH,
        0u,
    };
#endif
    // ponytail: the runtime is single-session; static buffers avoid the PS5
    // worker thread's small default stack at 4K context.
    static uint32_t prompt[TOKEN_MAX_COUNT];
    static uint32_t expected[TOKEN_MAX_COUNT];
    ps5_tokenizer_t tokenizer = {0};
    int64_t work_start = -1;
#ifndef PS5_LARGE_MODEL
    int64_t model_start = -1;
#endif
    int64_t layer_cache_starts[LAYER_CACHE_BANK_COUNT];
    int64_t direct_size;
    uint8_t *work = resident_work;
    uint8_t *model = resident_model;
    uint8_t *layer_caches[LAYER_CACHE_BANK_COUNT];
    uint8_t *work_mapping = work;
#ifndef PS5_LARGE_MODEL
    uint8_t *model_mapping = model;
#endif
    uint8_t *layer_cache_mappings[LAYER_CACHE_BANK_COUNT];
    FILE *file = NULL;
    int aio_fd = -1;
    token_model_header_t *header;
    token_tensor_entry_t *entries;
    const token_tensor_entry_t *embedding;
    const token_tensor_entry_t *logits_weights;
    const token_tensor_entry_t *output_norm;
    agc_api_t agc = {0};
    agc_register_t *layer_program = NULL;
    agc_register_t *logits_program = NULL;
#ifdef PS5_PRENORMALIZED_LOGITS
    agc_register_t *logits_prepare_program = NULL;
#endif
    uint8_t layer_program_count = 0;
    uint8_t logits_program_count = 0;
#ifdef PS5_PRENORMALIZED_LOGITS
    uint8_t logits_prepare_program_count = 0;
#endif
#ifdef PS5_STAGED_LAYER
    agc_register_t *gate_program = NULL;
    agc_register_t *quant_program = NULL;
    agc_register_t *down_program = NULL;
    uint8_t gate_program_count = 0;
    uint8_t quant_program_count = 0;
    uint8_t down_program_count = 0;
#endif
#ifdef PS5_SPLIT_ATTN
    agc_register_t *qkv_program = NULL;
    agc_register_t *rope_program = NULL;
#ifdef PS5_NOPE_EVERY_FOURTH
    agc_register_t *nope_program = NULL;
#endif
    agc_register_t *context_program = NULL;
#ifdef PS5_MODEL_QWEN35
    agc_register_t *attention_context_program = NULL;
#endif
    agc_register_t *attn_quant_program = NULL;
    agc_register_t *output_program = NULL;
    agc_register_t *ff_prepare_program = NULL;
    uint8_t qkv_program_count = 0;
    uint8_t rope_program_count = 0;
#ifdef PS5_NOPE_EVERY_FOURTH
    uint8_t nope_program_count = 0;
#endif
    uint8_t context_program_count = 0;
#ifdef PS5_MODEL_QWEN35
    uint8_t attention_context_program_count = 0;
#endif
    uint8_t attn_quant_program_count = 0;
    uint8_t output_program_count = 0;
    uint8_t ff_prepare_program_count = 0;
#endif
    uint32_t *layer_descriptors;
    uint32_t *logits_descriptors;
    float *state;
    uint8_t *layer_weights;
    float *layer_norms;
    float *layer_output;
#ifdef PS5_BATCH_PREFILL
    float *prefill_state_a;
    float *prefill_state_b;
    uint64_t batched_prefill_us = 0;
    uint32_t command_tokens = PREFILL_BATCH_SIZE;
#ifdef PS5_SINGLE_COMMAND
    uint32_t single_prefill_position;
#endif
#endif
    uint32_t *layer_control;
    float *logits;
    uint8_t *kv_cache;
    uint32_t *layer_words;
    volatile uint32_t *layer_marker;
#ifdef PS5_GPU_TIMING
    volatile uint64_t *gpu_timestamps;
#endif
#ifdef PS5_SPLIT_ATTN
    uint32_t *second_words;
    volatile uint32_t *second_marker;
    uint32_t second_word_count = 0;
    uint32_t *third_words;
    volatile uint32_t *third_marker;
    uint32_t third_word_count = 0;
#endif
    uint32_t layer_word_count = 0;
    uint64_t start_us;
    uint64_t load_start_us = sceKernelGetProcessTime();
    uint32_t index;
    uint32_t position;
    uint32_t serial_start;
    uint32_t input_token;
    uint32_t generation_limit;
    uint64_t load_phase_us;
    int result = 1;
    int model_new = resident_model == NULL;
    int model_reload = model_new ||
                       resident_model_revision != selected_model_revision;
    int work_new = resident_work == NULL;
    int runtime_layout = 0;

    for (index = 0; index < LAYER_CACHE_BANK_COUNT; ++index) {
        layer_cache_starts[index] = -1;
        layer_caches[index] = resident_layer_caches[index];
        layer_cache_mappings[index] = layer_caches[index];
    }
    for (index = 0; index < TOKEN_MAX_COUNT; ++index)
        ps5_compute_generated_tokens[index] = UINT32_MAX;
    memset(ps5_compute_response, 0, sizeof(ps5_compute_response));
    ps5_compute_submit_rc = UINT32_MAX;
    ps5_compute_suspend_rc = UINT32_MAX;
    ps5_compute_polls = 0;
    ps5_compute_marker_value = 0;
    ps5_compute_model_path = 0;
    ps5_compute_tokenizer_path = 0;
    ps5_compute_context_full = 0;
    ps5_compute_prefill_batches = 0;
    ps5_compute_prefill_us = 0;
    ps5_compute_elapsed_us = 0;
    ps5_compute_load_us = 0;
    ps5_compute_load_map_us = 0;
    ps5_compute_load_globals_us = 0;
    ps5_compute_load_layers_us = 0;
    ps5_compute_load_flush_us = 0;
    ps5_compute_model_runtime_layout = 0;
    ps5_compute_model_aio = 0;
    ps5_compute_aio_submit_rc = UINT32_MAX;
    ps5_compute_aio_wait_rc = UINT32_MAX;
    ps5_compute_aio_delete_rc = UINT32_MAX;
    ps5_compute_aio_result_value = UINT64_MAX;
    ps5_compute_aio_result_state = UINT32_MAX;
    ps5_compute_aio_wait_state = UINT32_MAX;
    ps5_compute_aio_mismatch_index = UINT32_MAX;
    ps5_compute_aio_fd = -1;
#ifdef PS5_GPU_TIMING
    memset(ps5_compute_gpu_ticks, 0, sizeof(ps5_compute_gpu_ticks));
    ps5_compute_gpu_samples = 0;
#endif
    if (model_reload)
        resident_model_revision = 0u;
    ps5_compute_model_reused = model_reload ? 0u : 1u;
    ps5_compute_kv_reused = 0;
    ps5_compute_stage = 1;
    if (chat_messages) {
        ps5_compute_expected_count = 0;
        ps5_compute_generated_count = chat_generated_count;
    } else if (load_expected(expected, &ps5_compute_expected_count) != 0 ||
               load_generation_count(ps5_compute_expected_count,
                                     &ps5_compute_generated_count) != 0)
        goto done;
    {
        int text_rc = load_text_prompt(
            &tokenizer, prompt, &ps5_compute_prompt_count,
            ps5_compute_generated_count);
        if (text_rc < 0 ||
            (text_rc > 0 &&
             load_prompt(prompt, &ps5_compute_prompt_count,
                         &ps5_compute_generated_count) != 0) ||
            (ps5_compute_expected_count != 0 &&
             ps5_compute_expected_count != ps5_compute_generated_count))
            goto done;
    }
#ifdef PS5_QWEN_SMOKE_PROMPT_TOKENS
    static const uint32_t qwen_smoke_prompt[13] = {
        248045u, 846u, 198u, 9419u, 248046u, 198u, 248045u,
        74455u, 198u, 248068u, 271u, 248069u, 271u,
    };
    _Static_assert(PS5_QWEN_SMOKE_PROMPT_TOKENS <= 13,
                   "Qwen smoke prefix is too long");
    memcpy(prompt, qwen_smoke_prompt,
           PS5_QWEN_SMOKE_PROMPT_TOKENS * sizeof(uint32_t));
    ps5_compute_prompt_count = PS5_QWEN_SMOKE_PROMPT_TOKENS;
    ps5_compute_generated_count = 1u;
#endif
    generation_limit = ps5_compute_generated_count;
    if (!ps5_compute_tokenizer_path &&
        ps5_tokenizer_load(&tokenizer, selected_tokenizer_file) == 0)
        ps5_compute_tokenizer_path = 1u;
    if (!work_new)
        ps5_compute_kv_reused = ps5_chat_reusable_prefix(
            resident_tokens, resident_token_count, prompt,
            ps5_compute_prompt_count);
#ifdef PS5_MODEL_QWEN35
    /* Recurrent state can continue only from the exact cached frontier; unlike
       attention KV, it cannot be rewound to an arbitrary shorter prefix. */
#ifdef PS5_DISABLE_QWEN_PREFIX_REUSE
    ps5_compute_kv_reused = 0u;
#else
    if (ps5_compute_kv_reused != resident_token_count) {
        uint32_t suffix = ps5_compute_kv_reused;

        /* Re-encoding a decoded reply can choose different BPE pieces than
           the tokens the model generated. Preserve those exact resident
           pieces and join at the assistant end marker. */
        while (suffix < ps5_compute_prompt_count &&
               prompt[suffix] != TOKEN_EOS)
            ++suffix;
        if (ps5_compute_kv_reused >= resident_prompt_count &&
            suffix < ps5_compute_prompt_count) {
            uint32_t suffix_count = ps5_compute_prompt_count - suffix;

            if (resident_token_count <= TOKEN_MAX_COUNT - suffix_count &&
                resident_token_count + suffix_count +
                        ps5_compute_generated_count - 1u <=
                    TOKEN_KV_CAPACITY) {
                memmove(prompt + resident_token_count, prompt + suffix,
                        suffix_count * sizeof(*prompt));
                memcpy(prompt, resident_tokens,
                       resident_token_count * sizeof(*prompt));
                ps5_compute_prompt_count =
                    resident_token_count + suffix_count;
                ps5_compute_kv_reused = resident_token_count;
            } else {
                ps5_compute_kv_reused = 0u;
            }
        } else {
            ps5_compute_kv_reused = 0u;
        }
    }
#endif
#endif
    serial_start = ps5_compute_kv_reused;
#ifdef PS5_SINGLE_COMMAND
    single_prefill_position = serial_start;
#endif
#ifdef PS5_BATCH_PREFILL
    command_tokens = ps5_compute_prompt_count > serial_start + 1u
                         ? ps5_compute_prompt_count - serial_start - 1u
                         : 0u;
    if (command_tokens > PREFILL_BATCH_SIZE)
        command_tokens = PREFILL_BATCH_SIZE;
    if (command_tokens < 2u)
        command_tokens = 2u;
#ifdef PS5_LARGE_MODEL
#ifdef PS5_SINGLE_COMMAND
    command_tokens = ps5_compute_prompt_count - serial_start;
    if (command_tokens > PREFILL_BATCH_SIZE)
        command_tokens = PREFILL_BATCH_SIZE;
#elif defined(PS5_LAYER0_DIAGNOSTIC)
    command_tokens = 2u;
#else
    command_tokens = 1u;
#endif
#endif
#endif
    if (load_agc(NULL, &agc) != 0)
        goto done;
    if (!resident_agc_initialized) {
        if (agc.init(8) != 0)
            goto done;
        resident_agc_initialized = 1u;
    }
    direct_size = sceKernelGetDirectMemorySize();
    ps5_compute_stage = 2;
#ifdef PS5_LARGE_MODEL
    if (work_new != model_new)
        goto done;
    if (work_new) {
        int map_flags = 0;
        int allocate_rc = sceKernelAllocateDirectMemory(
            0, direct_size, PRIMARY_MAPPING_BYTES, 0x4000,
            DIRECT_MEMORY_TYPE, &work_start);
#ifdef PS5_DUAL_BACKEND
        /* Reuse the low-aperture range that the media backend just released.
           Without this, FreeBSD's next-fit cursor remains above 0x3... even
           though the lower virtual range and its direct memory are free. */
        work_mapping = (uint8_t *)ps5_agc_backend_take_released_scratch_address();
        if (work_mapping && (uint32_t)((uintptr_t)work_mapping >> 32) == 2u)
            map_flags = MAP_FIXED;
        else
            work_mapping = NULL;
#endif
        int map_rc = allocate_rc == 0
                         ? sceKernelMapDirectMemory(
                               (void **)&work_mapping, PRIMARY_MAPPING_BYTES,
                               MAP_PROTECTION, map_flags, work_start, 0x4000)
                         : -1;

        work = map_rc == 0 && work_mapping ? GPU_ALIGNED(work_mapping) : NULL;
#ifdef PS5_DIRECT_MAP_DIAGNOSTIC
        {
            char map_log[256];
            snprintf(map_log, sizeof(map_log),
                     "[prosperogpt] direct_map alloc_rc=%d map_rc=%d "
                     "direct_size=0x%llx bytes=0x%llx start=0x%llx "
                     "mapping=0x%llx aligned=0x%llx high=%u\n",
                     allocate_rc, map_rc, (unsigned long long)direct_size,
                     (unsigned long long)PRIMARY_MAPPING_BYTES,
                     (unsigned long long)work_start,
                     (unsigned long long)(uintptr_t)work_mapping,
                     (unsigned long long)(uintptr_t)work,
                     work ? (uint32_t)((uintptr_t)work >> 32) : 0u);
            sceKernelDebugOutText(0, map_log);
        }
#endif
        if (allocate_rc != 0 || map_rc != 0 || !work ||
            (uint32_t)((uintptr_t)work >> 32) != 2u)
            goto done;
        model = work + TOKEN_WORK_BYTES;
        layer_caches[0] = model + MODEL_ALLOCATION_BYTES;
        layer_cache_mappings[0] = work_mapping;
        for (index = 1; index < LAYER_CACHE_BANK_COUNT; ++index) {
            layer_caches[index] =
                layer_caches[0] + index * LAYER_CACHE_BANK_BYTES;
            layer_cache_mappings[index] = work_mapping;
        }
    }
#else
    if (work_new &&
        (sceKernelAllocateDirectMemory(0, direct_size, WORK_MAPPING_BYTES, 0x4000,
                                       DIRECT_MEMORY_TYPE, &work_start) != 0 ||
         sceKernelMapDirectMemory((void **)&work_mapping, WORK_MAPPING_BYTES,
                                  MAP_PROTECTION, 0, work_start, 0x4000) != 0 ||
         !work_mapping ||
         (work = GPU_ALIGNED(work_mapping)) == NULL ||
         (uint32_t)((uintptr_t)work >> 32) != 2u))
        goto done;
    if (model_new) {
        if (sceKernelAllocateDirectMemory(0, direct_size, MODEL_MAPPING_BYTES,
                                          0x4000, DIRECT_MEMORY_TYPE,
                                          &model_start) != 0 ||
            sceKernelMapDirectMemory((void **)&model_mapping,
                                     MODEL_MAPPING_BYTES, MAP_PROTECTION, 0,
                                     model_start, 0x4000) != 0 ||
            !model_mapping ||
            (model = GPU_ALIGNED(model_mapping)) == NULL ||
            (uint32_t)((uintptr_t)model >> 32) != 2u)
            goto done;
        for (index = 0; index < LAYER_CACHE_BANK_COUNT; ++index) {
            if (sceKernelAllocateDirectMemory(
                    0, direct_size, LAYER_CACHE_BANK_MAPPING_BYTES, 0x4000,
                    DIRECT_MEMORY_TYPE, &layer_cache_starts[index]) != 0 ||
                sceKernelMapDirectMemory(
                    (void **)&layer_cache_mappings[index],
                    LAYER_CACHE_BANK_MAPPING_BYTES, MAP_PROTECTION, 0,
                    layer_cache_starts[index], 0x4000) != 0 ||
                !layer_cache_mappings[index] ||
                (layer_caches[index] =
                     GPU_ALIGNED(layer_cache_mappings[index])) == NULL ||
                (uint32_t)((uintptr_t)layer_caches[index] >> 32) != 2u)
                goto done;
        }
    }
#endif
    ps5_compute_debug_ptrs[0] = (uintptr_t)work;
    ps5_compute_debug_ptrs[1] = (uintptr_t)model;
    ps5_compute_debug_ptrs[2] = (uintptr_t)layer_caches[0];
    ps5_compute_load_map_us = sceKernelGetProcessTime() - load_start_us;
    load_phase_us = sceKernelGetProcessTime();
    ps5_compute_stage = 3;
    if (model_reload) {
        file = fopen(selected_model_file, "rb");
        ps5_compute_model_path = file ? 1u : 0u;
#ifdef PS5_LARGE_MODEL
        if (!file || fread(model, 1, 256u, file) != 256u ||
            fread(model + 256u, sizeof(token_tensor_entry_t),
                  TOKEN_TENSOR_COUNT, file) != TOKEN_TENSOR_COUNT)
            goto done;
#else
        if (!file || fread(model, 1, MODEL_BYTES, file) != MODEL_BYTES)
            goto done;
        fclose(file);
        file = NULL;
#endif
    } else {
        ps5_compute_model_path = 1u;
    }
    header = (token_model_header_t *)model;
    entries = (token_tensor_entry_t *)(model + 256);
    if (sizeof(*header) != 96 || sizeof(*entries) != 128 ||
        memcmp(header->magic, "P5LM", 4) || header->version != 1 ||
        header->header_bytes != 256 || header->entry_bytes != 128 ||
        header->tensor_count != TOKEN_TENSOR_COUNT ||
        header->block_count != TOKEN_LAYERS ||
        header->embedding_length != TOKEN_WIDTH ||
        header->feed_forward_length != TOKEN_FF_WIDTH ||
        header->head_count != TOKEN_HEADS ||
        header->head_count_kv != TOKEN_KV_HEADS ||
        header->vocab_size != TOKEN_VOCAB)
        goto done;
    runtime_layout = (header->flags & MODEL_FLAG_RUNTIME_LAYOUT) != 0;
    ps5_compute_model_runtime_layout = runtime_layout ? 1u : 0u;
#ifdef PS5_LARGE_MODEL
    if (model_reload && runtime_layout) {
        aio_fd = sceKernelOpen(selected_model_file, 0, 0);
        ps5_compute_aio_fd = aio_fd;
        if (aio_fd >= 0 &&
            read_model_aio(aio_fd, model, 0, MODEL_ALLOCATION_BYTES) == 0) {
            ps5_compute_model_aio = 1u;
        } else if (fseek(file, 0, SEEK_SET) != 0 ||
                   read_model_stream(file, model, MODEL_ALLOCATION_BYTES) !=
                       0) {
            goto done;
        }
        header = (token_model_header_t *)model;
        entries = (token_tensor_entry_t *)(model + 256);
    }
#endif
    embedding = find_tensor(header, entries, "token_embd.weight");
    output_norm = find_tensor(header, entries, "output_norm.weight");
    if (!embedding || !output_norm || output_norm->kind != 0 ||
        output_norm->bytes != TOKEN_WIDTH * 4u)
        goto done;
    logits_weights = embedding;
#if defined(PS5_MODEL_7000) || defined(PS5_MODEL_QWEN35)
    logits_weights = find_tensor(header, entries, "output.weight");
    if (!logits_weights || logits_weights->kind != 3 ||
        logits_weights->bytes != (uint64_t)TOKEN_VOCAB * TOKEN_WIDTH)
        goto done;
#endif
#ifdef PS5_LARGE_MODEL
    if (model_reload && !runtime_layout) {
        token_tensor_entry_t *compact_embedding =
            (token_tensor_entry_t *)embedding;
        token_tensor_entry_t *compact_output_norm =
            (token_tensor_entry_t *)output_norm;
#if defined(PS5_MODEL_7000) || defined(PS5_MODEL_QWEN35)
        token_tensor_entry_t *compact_logits_weights =
            (token_tensor_entry_t *)logits_weights;

        if (embedding->kind != 1 ||
            embedding->bytes != (uint64_t)TOKEN_EMBED_BLOCKS * 20u ||
            copy_tensor(file, model, output_norm,
                        model + MODEL_OUTPUT_NORM_OFFSET) != 0 ||
            copy_tensor(file, model, embedding,
                        model + MODEL_EMBEDDING_OFFSET) != 0 ||
            copy_tensor(file, model, logits_weights,
                        model + MODEL_OUTPUT_WEIGHT_OFFSET) != 0)
            goto done;
        compact_logits_weights->offset = MODEL_OUTPUT_WEIGHT_OFFSET;
#else
        if (embedding->kind != 3 ||
            embedding->bytes != (uint64_t)TOKEN_VOCAB * TOKEN_WIDTH ||
            copy_tensor(file, model, output_norm,
                        model + MODEL_OUTPUT_NORM_OFFSET) != 0 ||
            copy_tensor(file, model, embedding,
                        model + MODEL_EMBEDDING_OFFSET) != 0)
            goto done;
#endif
        compact_output_norm->offset = MODEL_OUTPUT_NORM_OFFSET;
        compact_embedding->offset = MODEL_EMBEDDING_OFFSET;
    }
#endif
    ps5_compute_load_globals_us = sceKernelGetProcessTime() - load_phase_us;
    ps5_compute_stage = 4;
    if (model_reload) {
        load_phase_us = sceKernelGetProcessTime();
        if (runtime_layout) {
            if (ps5_compute_model_aio &&
                read_model_aio(aio_fd, layer_caches[0],
                               MODEL_ALLOCATION_BYTES,
                               LAYER_CACHE_BYTES) != 0)
                ps5_compute_model_aio = 0;
            if (!ps5_compute_model_aio) {
                if (fseek(file, MODEL_ALLOCATION_BYTES, SEEK_SET) != 0 ||
                    read_model_stream(file, layer_caches[0],
                                      LAYER_CACHE_BYTES) != 0)
                    goto done;
            }
        } else {
            for (index = 0; index < TOKEN_LAYERS; ++index) {
                uint32_t bank = index / LAYERS_PER_CACHE_BANK;
                uint32_t bank_layer = index % LAYERS_PER_CACHE_BANK;

                layer_weights = layer_caches[bank] +
                                (uint64_t)bank_layer * LAYER_CACHE_STRIDE;
                layer_norms =
                    (float *)(layer_weights + LAYER_CACHE_NORM_OFFSET);
                if (load_layer(file, model, header, entries, index,
                               layer_weights, layer_norms) != 0)
                    goto done;
            }
        }
#ifdef PS5_LARGE_MODEL
        fclose(file);
        file = NULL;
        if (aio_fd >= 0) {
            sceKernelClose(aio_fd);
            aio_fd = -1;
        }
#endif
        ps5_compute_load_layers_us = sceKernelGetProcessTime() - load_phase_us;
        // ponytail: the process owns these immutable mappings until title close.
        resident_model = model;
        for (index = 0; index < LAYER_CACHE_BANK_COUNT; ++index)
            resident_layer_caches[index] = layer_caches[index];
#ifdef PS5_LARGE_MODEL
        resident_work = work;
        if (work_start >= 0) {
            resident_primary_mapping = work_mapping;
            resident_primary_start = work_start;
        }
#endif
    }
    if (model_reload)
        ps5_compute_load_us = sceKernelGetProcessTime() - load_start_us;
    ps5_compute_stage = 60;
    if (create_program(&agc,
#ifdef PS5_SPLIT_ATTN
                       attn_prepare_agc_package,
                       attn_prepare_agc_package_len,
#elif defined(PS5_STAGED_LAYER)
                       real_layer_pre_agc_package,
                       real_layer_pre_agc_package_len,
#else
                       real_layer_kv_agc_package,
                       real_layer_kv_agc_package_len,
#endif
                       work + LAYER_HEADER_OFFSET, 0x4000,
                       work + LAYER_CODE_OFFSET, 0x7000, &layer_program,
                       &layer_program_count) != 0 ||
#ifdef PS5_SPLIT_ATTN
        create_program(&agc, attn_qkv_agc_package,
                       attn_qkv_agc_package_len,
                       work + ATTN_QKV_HEADER_OFFSET, 0x4000,
                       work + ATTN_QKV_CODE_OFFSET, 0xc000, &qkv_program,
                       &qkv_program_count) != 0 ||
        create_program(&agc, attn_rope_agc_package,
                       attn_rope_agc_package_len,
                       work + ATTN_ROPE_HEADER_OFFSET, 0x4000,
                       work + ATTN_ROPE_CODE_OFFSET, 0xc000, &rope_program,
                       &rope_program_count) != 0 ||
#ifdef PS5_NOPE_EVERY_FOURTH
        create_program(&agc, attn_nope_agc_package,
                       attn_nope_agc_package_len,
                       work + ATTN_NOPE_HEADER_OFFSET, 0x4000,
                       work + ATTN_NOPE_CODE_OFFSET, 0xc000, &nope_program,
                       &nope_program_count) != 0 ||
#endif
        create_program(&agc,
#ifdef PS5_MODEL_QWEN35
                       attn_context_recurrent_agc_package,
                       attn_context_recurrent_agc_package_len,
#else
                       attn_context_agc_package,
                       attn_context_agc_package_len,
#endif
                       work + ATTN_CONTEXT_HEADER_OFFSET, 0x4000,
                       work + ATTN_CONTEXT_CODE_OFFSET, 0xc000,
                       &context_program, &context_program_count) != 0 ||
#ifdef PS5_MODEL_QWEN35
        create_program(&agc, attn_context_attention_agc_package,
                       attn_context_attention_agc_package_len,
                       work + ATTN_CONTEXT_ATTENTION_HEADER_OFFSET, 0x4000,
                       work + ATTN_CONTEXT_ATTENTION_CODE_OFFSET, 0xc000,
                       &attention_context_program,
                       &attention_context_program_count) != 0 ||
#endif
        create_program(&agc, attn_quant_agc_package,
                       attn_quant_agc_package_len,
                       work + ATTN_QUANT_HEADER_OFFSET, 0x4000,
                       work + ATTN_QUANT_CODE_OFFSET, 0xc000,
                       &attn_quant_program, &attn_quant_program_count) != 0 ||
        create_program(&agc, attn_output_agc_package,
                       attn_output_agc_package_len,
                       work + ATTN_OUTPUT_HEADER_OFFSET, 0x4000,
                       work + ATTN_OUTPUT_CODE_OFFSET, 0xc000, &output_program,
                       &output_program_count) != 0 ||
        create_program(&agc, attn_ff_prepare_agc_package,
                       attn_ff_prepare_agc_package_len,
                       work + FF_PREPARE_HEADER_OFFSET, 0x4000,
                       work + FF_PREPARE_CODE_OFFSET, 0xc000,
                       &ff_prepare_program, &ff_prepare_program_count) != 0 ||
#endif
#ifdef PS5_STAGED_LAYER
        create_program(&agc, ff_gate_up_agc_package,
                       ff_gate_up_agc_package_len,
                       work + FF_GATE_HEADER_OFFSET, 0x4000,
                       work + FF_GATE_CODE_OFFSET, 0xc000, &gate_program,
                       &gate_program_count) != 0 ||
        create_program(&agc, ff_quantize_agc_package,
                       ff_quantize_agc_package_len,
                       work + FF_QUANT_HEADER_OFFSET, 0x4000,
                       work + FF_QUANT_CODE_OFFSET, 0xc000, &quant_program,
                       &quant_program_count) != 0 ||
        create_program(&agc, ff_down_agc_package,
                       ff_down_agc_package_len,
                       work + FF_DOWN_HEADER_OFFSET, 0x4000,
                       work + FF_DOWN_CODE_OFFSET, 0xc000, &down_program,
                       &down_program_count) != 0 ||
#endif
        create_program(&agc, token_logits_agc_package,
                       token_logits_agc_package_len,
                       work + LOGITS_HEADER_OFFSET, 0x1000,
                       work + LOGITS_CODE_OFFSET, LOGITS_CODE_BYTES,
                       &logits_program,
                       &logits_program_count) != 0
#ifdef PS5_PRENORMALIZED_LOGITS
        || create_program(&agc, logits_prepare_agc_package,
                          logits_prepare_agc_package_len,
                          work + LOGITS_PREPARE_HEADER_OFFSET, 0x4000,
                          work + LOGITS_PREPARE_CODE_OFFSET, 0xc000,
                          &logits_prepare_program,
                          &logits_prepare_program_count) != 0
#endif
        )
        goto done;

    layer_descriptors = (uint32_t *)(work + LAYER_DESCRIPTOR_OFFSET);
    logits_descriptors = (uint32_t *)(work + LOGITS_DESCRIPTOR_OFFSET);
    state = (float *)(work + TOKEN_STATE_OFFSET);
    layer_weights = layer_caches[0];
    layer_norms =
        (float *)(layer_caches[0] + LAYER_CACHE_NORM_OFFSET);
    layer_output = (float *)(work + LAYER_OUTPUT_OFFSET);
#ifdef PS5_BATCH_PREFILL
    prefill_state_a = (float *)(work + PREFILL_STATE_A_OFFSET);
    prefill_state_b = (float *)(work + PREFILL_STATE_B_OFFSET);
#endif
    layer_control = (uint32_t *)(work + LAYER_CONTROL_OFFSET);
    logits = (float *)(work + TOKEN_LOGITS_OFFSET);
    kv_cache = work + TOKEN_KV_OFFSET;
#ifdef PS5_MODEL_QWEN35
    if (ps5_compute_kv_reused == 0u)
        for (index = 0; index < TOKEN_LAYERS; ++index)
            if ((index + 1u) % 4u != 0u) {
                void *recurrent_state =
                    kv_cache + index * TOKEN_KV_LAYER_BYTES;
                memset(recurrent_state, 0, QWEN_RECURRENT_STATE_BYTES);
                flush_cache(recurrent_state, QWEN_RECURRENT_STATE_BYTES);
            }
#endif
    ps5_compute_debug_ptrs[3] = (uintptr_t)layer_descriptors;
    ps5_compute_debug_ptrs[4] = (uintptr_t)state;
    ps5_compute_debug_ptrs[5] = (uintptr_t)layer_caches[0];
    ps5_compute_debug_ptrs[6] = (uintptr_t)(model + embedding->offset);
    ps5_compute_debug_ptrs[7] = (uintptr_t)logits_descriptors;
    layer_words = (uint32_t *)(work + LAYER_COMMAND_OFFSET);
    layer_marker = (volatile uint32_t *)(work + LAYER_MARKER_OFFSET);
#ifdef PS5_SPLIT_ATTN
    second_words = (uint32_t *)(work + SECOND_COMMAND_OFFSET);
    second_marker = (volatile uint32_t *)(work + SECOND_MARKER_OFFSET);
    third_words = (uint32_t *)(work + THIRD_COMMAND_OFFSET);
    third_marker = (volatile uint32_t *)(work + THIRD_MARKER_OFFSET);
#endif
#ifdef PS5_GPU_TIMING
    gpu_timestamps = (volatile uint64_t *)(work + GPU_TIMESTAMP_OFFSET);
#endif
#ifdef PS5_MODEL_QWEN35
    for (index = 0; index < TOKEN_LAYERS; ++index) {
        uint32_t *control = layer_control + index * LAYER_CONTROL_DWORDS;
        int recurrent = (index + 1u) % 4u != 0u;

        memcpy(control, recurrent ? recurrent_params : attention_params,
               24u * sizeof(uint32_t));
        control[24] = 0u;
        control[25] = index;
        control[26] = recurrent ? 1u : 0u;
        control[27] = index < 4u ? 4u : 1u;
    }
#else
    memcpy(layer_control, layer_params, sizeof(layer_params));
#endif
    for (index = 0; index < TOKEN_LAYERS; ++index) {
        uint32_t *descriptor =
            layer_descriptors + index * LAYER_DESCRIPTOR_DWORDS;
#ifdef PS5_BATCH_PREFILL
        float *input = index & 1u ? prefill_state_b : prefill_state_a;
        float *output = index & 1u ? prefill_state_a : prefill_state_b;
        uint32_t state_bytes = PREFILL_BUFFER_BYTES;
#else
        float *input = index & 1u ? layer_output : state;
        float *output = index & 1u ? state : layer_output;
        uint32_t state_bytes = TOKEN_WIDTH * 4u;
#endif

        uint32_t bank = index / LAYERS_PER_CACHE_BANK;
        uint32_t bank_layer = index % LAYERS_PER_CACHE_BANK;

        layer_weights = layer_caches[bank] +
                        (uint64_t)bank_layer * LAYER_CACHE_STRIDE;
        layer_norms = (float *)(layer_weights + LAYER_CACHE_NORM_OFFSET);
        set_raw_descriptor(descriptor, input, state_bytes);
        set_raw_descriptor(descriptor + 4, layer_weights,
                           REAL_LAYER_WEIGHT_DWORDS * 4u);
        set_raw_descriptor(descriptor + 8, layer_norms,
#ifdef PS5_MODEL_QWEN35
                           QWEN_NORM_DESCRIPTOR_BYTES);
#else
                           TOKEN_WIDTH * 2u * 4u);
#endif
        set_raw_descriptor(descriptor + 12, output,
#ifdef PS5_BATCH_PREFILL
                           state_bytes);
#else
                           TOKEN_LAYER_OUTPUT_FLOATS * 4u);
#endif
        set_raw_descriptor(
            descriptor + 16,
#ifdef PS5_MODEL_QWEN35
            layer_control + index * LAYER_CONTROL_DWORDS,
            LAYER_CONTROL_DWORDS * sizeof(uint32_t));
#else
            layer_control, sizeof(layer_params));
#endif
        set_raw_descriptor(descriptor + 20,
                           kv_cache + index * TOKEN_KV_LAYER_BYTES,
                           TOKEN_KV_LAYER_BYTES);
    }
#if defined(PS5_BATCH_PREFILL) && !defined(PS5_SINGLE_COMMAND)
    if (command_tokens == 1u) {
        for (index = 0; index < TOKEN_LAYERS; ++index) {
            uint32_t *descriptor =
                layer_descriptors + index * LAYER_DESCRIPTOR_DWORDS;
            float *input = index & 1u ? layer_output : state;
            float *output = index & 1u ? state : layer_output;

            set_raw_descriptor(descriptor, input,
                               TOKEN_LAYER_OUTPUT_FLOATS * 4u);
            set_raw_descriptor(descriptor + 12, output,
                               TOKEN_LAYER_OUTPUT_FLOATS * 4u);
        }
    }
#endif
    set_raw_descriptor(logits_descriptors, state,
#ifdef PS5_MODEL_QWEN35
                       TOKEN_WIDTH * 4u + TOKEN_WIDTH / 32u * 4u + TOKEN_WIDTH);
#else
                       TOKEN_WIDTH * 4u);
#endif
    set_raw_descriptor(logits_descriptors + 4,
                       model + logits_weights->offset,
                       (uint32_t)logits_weights->bytes);
    set_raw_descriptor(logits_descriptors + 8, model + output_norm->offset,
                       (uint32_t)output_norm->bytes);
    set_raw_descriptor(logits_descriptors + 12, logits, TOKEN_VOCAB * 4u);
    ps5_compute_stage = 5;
    if (model_reload) {
        load_phase_us = sceKernelGetProcessTime();
        flush_cache(model, MODEL_RESIDENT_BYTES);
        for (index = 0; index < LAYER_CACHE_BANK_COUNT; ++index)
            flush_cache(layer_caches[index], LAYER_CACHE_BANK_BYTES);
        ps5_compute_load_flush_us = sceKernelGetProcessTime() - load_phase_us;
        resident_model_revision = selected_model_revision;
    }
#ifdef PS5_MODEL_QWEN35
    /* Publish immutable programs and descriptors once; execute_command only
       flushes the small CPU-mutated ranges after this point. */
    flush_cache(work, TOKEN_KV_OFFSET);
#endif
#ifdef PS5_BATCH_PREFILL
    for (;;) {
#endif
#if defined(PS5_MODEL_QWEN35) && \
    !defined(PS5_QWEN_DIAGNOSTIC_PHASES) && \
    !defined(PS5_LOGITS_DIAGNOSTIC)
    /* Qwen commands are rebuilt per short segment below. */
#else
    if (build_token_command(
            &agc, layer_program, layer_program_count, layer_descriptors,
#ifdef PS5_STAGED_LAYER
            gate_program, gate_program_count, quant_program,
            quant_program_count, down_program, down_program_count,
#endif
#ifdef PS5_SPLIT_ATTN
            qkv_program, qkv_program_count, rope_program, rope_program_count,
#ifdef PS5_NOPE_EVERY_FOURTH
            nope_program, nope_program_count,
#endif
            context_program, context_program_count,
#ifdef PS5_MODEL_QWEN35
            attention_context_program, attention_context_program_count,
#endif
            attn_quant_program,
            attn_quant_program_count, output_program, output_program_count,
            ff_prepare_program, ff_prepare_program_count,
#endif
            logits_program, logits_program_count,
#ifdef PS5_PRENORMALIZED_LOGITS
            logits_prepare_program, logits_prepare_program_count,
#endif
            logits_descriptors, work,
            TOKEN_WORK_BYTES,
#ifdef PS5_LOGITS_DIAGNOSTIC
            layer_caches[0], 0u,
#else
            layer_caches[0],
#ifdef PS5_SINGLE_COMMAND
            LAYER_CACHE_BYTES,
#else
            LAYER_CACHE_BANK_BYTES,
#endif
#endif
            model,
#ifdef PS5_BATCH_PREFILL
            command_tokens,
#endif
            MODEL_RESIDENT_BYTES,
#ifdef PS5_QWEN_DIAGNOSTIC_LAYER
            0u,
#else
            0u,
#endif
#ifdef PS5_SPLIT_ATTN
#ifdef PS5_LOGITS_DIAGNOSTIC
            0u, 1,
#elif defined(PS5_SINGLE_COMMAND)
            TOKEN_LAYERS, 1,
#elif defined(PS5_QWEN_DIAGNOSTIC_PHASES) && \
    defined(PS5_QWEN_DIAGNOSTIC_LAYER)
            PS5_QWEN_DIAGNOSTIC_LAYER + 1u, 0,
#elif defined(PS5_QWEN_DIAGNOSTIC_PHASES)
            1u, 0,
#elif defined(PS5_LAYER0_DIAGNOSTIC)
            1u, 0,
#else
            TOKEN_LAYERS / 3u, 0,
#endif
#else
            TOKEN_LAYERS, 1,
#endif
            layer_marker, layer_words,
#ifdef PS5_GPU_TIMING
            gpu_timestamps,
#endif
            &layer_word_count) != 0
#if defined(PS5_SPLIT_ATTN) && !defined(PS5_SINGLE_COMMAND) && \
    !defined(PS5_QWEN_DIAGNOSTIC_PHASES)
        || build_token_command(
            &agc, layer_program, layer_program_count, layer_descriptors,
            gate_program, gate_program_count, quant_program,
            quant_program_count, down_program, down_program_count,
            qkv_program, qkv_program_count, rope_program, rope_program_count,
#ifdef PS5_NOPE_EVERY_FOURTH
            nope_program, nope_program_count,
#endif
            context_program, context_program_count,
#ifdef PS5_MODEL_QWEN35
            attention_context_program, attention_context_program_count,
#endif
            attn_quant_program,
            attn_quant_program_count, output_program, output_program_count,
            ff_prepare_program, ff_prepare_program_count,
            logits_program, logits_program_count,
#ifdef PS5_PRENORMALIZED_LOGITS
            logits_prepare_program, logits_prepare_program_count,
#endif
            logits_descriptors, work,
            TOKEN_WORK_BYTES,
#ifdef PS5_LAYER0_DIAGNOSTIC
            layer_caches[0],
#else
            layer_caches[(TOKEN_LAYERS / 3u) / LAYERS_PER_CACHE_BANK],
#endif
            LAYER_CACHE_BANK_BYTES, model,
#ifdef PS5_BATCH_PREFILL
            command_tokens,
#endif
            MODEL_RESIDENT_BYTES,
#ifdef PS5_LAYER0_DIAGNOSTIC
            0u, 1u, 0,
#else
            TOKEN_LAYERS / 3u, TOKEN_LAYERS * 2u / 3u, 0,
#endif
            second_marker, second_words, &second_word_count) != 0
        || build_token_command(
            &agc, layer_program, layer_program_count, layer_descriptors,
            gate_program, gate_program_count, quant_program,
            quant_program_count, down_program, down_program_count,
            qkv_program, qkv_program_count, rope_program, rope_program_count,
#ifdef PS5_NOPE_EVERY_FOURTH
            nope_program, nope_program_count,
#endif
            context_program, context_program_count,
#ifdef PS5_MODEL_QWEN35
            attention_context_program, attention_context_program_count,
#endif
            attn_quant_program,
            attn_quant_program_count, output_program, output_program_count,
            ff_prepare_program, ff_prepare_program_count,
            logits_program, logits_program_count,
#ifdef PS5_PRENORMALIZED_LOGITS
            logits_prepare_program, logits_prepare_program_count,
#endif
            logits_descriptors, work,
            TOKEN_WORK_BYTES,
            layer_caches[(TOKEN_LAYERS * 2u / 3u) /
                         LAYERS_PER_CACHE_BANK],
            LAYER_CACHE_BANK_BYTES, model,
#ifdef PS5_BATCH_PREFILL
            command_tokens,
#endif
            MODEL_RESIDENT_BYTES, TOKEN_LAYERS * 2u / 3u, TOKEN_LAYERS,
#ifdef PS5_BATCH_PREFILL
            command_tokens == 1u,
#else
            1,
#endif
            third_marker, third_words, &third_word_count) != 0
#endif
        )
        goto done;
#endif
#ifdef PS5_QWEN_DIAGNOSTIC_PHASES
    if (load_embedding(
            model, embedding, prompt[0],
            state
            ) != 0)
        goto done;
    set_layer_position(layer_control, 0u);
    flush_cache(state, TOKEN_WIDTH * sizeof(float));
    ps5_compute_stage = 9100u + PS5_QWEN_DIAGNOSTIC_PHASES;
    if (execute_command(&agc, work, layer_words, layer_word_count,
                        layer_marker) != 0)
        goto done;
    {
#ifdef PS5_QWEN_DIAGNOSTIC_LAYER
        float *diagnostic_scratch =
            (PS5_QWEN_DIAGNOSTIC_LAYER & 1u) ? state : layer_output;
#else
        float *diagnostic_scratch = layer_output;
#endif
#ifdef PS5_QWEN_DIAGNOSTIC_OFFSET
        diagnostic_scratch += PS5_QWEN_DIAGNOSTIC_OFFSET;
#else
        if (PS5_QWEN_DIAGNOSTIC_PHASES >= 2 &&
            PS5_QWEN_DIAGNOSTIC_PHASES <= 4)
            diagnostic_scratch += 10368u;
#endif
        flush_cache(diagnostic_scratch,
                    sizeof(ps5_compute_debug_state_bits));
        memcpy(ps5_compute_debug_state_bits, diagnostic_scratch,
               sizeof(ps5_compute_debug_state_bits));
    }
    ps5_compute_stage = 9200u + PS5_QWEN_DIAGNOSTIC_PHASES;
    result = 0;
    goto done;
#endif
#ifdef PS5_SINGLE_COMMAND
    if (single_prefill_position < ps5_compute_prompt_count) {
        uint32_t chunk_start = single_prefill_position;

        if (chunk_start == serial_start)
            start_us = sceKernelGetProcessTime();
        ps5_compute_position = chunk_start + command_tokens - 1u;
        ps5_compute_stage = 80u + chunk_start;
        for (position = 0; position < command_tokens; ++position)
            if (load_embedding(
                    model, embedding, prompt[chunk_start + position],
                    prefill_state_a +
                        position * TOKEN_LAYER_OUTPUT_FLOATS) != 0)
                goto done;
        set_raw_descriptor(
            logits_descriptors,
            prefill_state_a +
                (command_tokens - 1u) * TOKEN_LAYER_OUTPUT_FLOATS,
            TOKEN_WIDTH * 4u);
        set_layer_position(layer_control, chunk_start);
        if (execute_command(&agc, work, layer_words, layer_word_count,
                            layer_marker
#ifdef PS5_GPU_TIMING
                            , gpu_timestamps
#endif
                            ) != 0)
            goto done;
        ++ps5_compute_prefill_batches;
        single_prefill_position += command_tokens;
        if (single_prefill_position < ps5_compute_prompt_count) {
            command_tokens = ps5_compute_prompt_count -
                             single_prefill_position;
            if (command_tokens > PREFILL_BATCH_SIZE)
                command_tokens = PREFILL_BATCH_SIZE;
            continue;
        }
        flush_cache(logits, TOKEN_VOCAB * sizeof(float));
        {
            float best = -3.402823466e+38f;
            uint32_t best_index = 0;
            for (index = 0; index < TOKEN_VOCAB; ++index)
                if (logits[index] > best) {
                    best = logits[index];
                    best_index = index;
                }
            ps5_compute_generated_tokens[0] = best_index;
            ps5_compute_generated_count = 1;
        }
        ps5_compute_prefill_us = sceKernelGetProcessTime() - start_us;
        if (ps5_compute_tokenizer_path && chat_progress &&
            decode_response(&tokenizer, 1u) < 0)
            goto done;
        if (chat_progress)
            chat_progress(ps5_compute_response);
        command_tokens = 1u;
        continue;
    }
    {
        uint32_t generated_index;

        for (generated_index = 1;
             generated_index < generation_limit;
             ++generated_index) {
            for (position = 0; position < command_tokens; ++position)
                if (load_embedding(
                        model, embedding,
                        ps5_compute_generated_tokens[generated_index - 1u],
                        prefill_state_a +
                            position * TOKEN_LAYER_OUTPUT_FLOATS) != 0)
                    goto done;
            set_raw_descriptor(logits_descriptors, prefill_state_a,
                               TOKEN_WIDTH * 4u);
            set_layer_position(
                layer_control,
                ps5_compute_prompt_count + generated_index - 1u);
            ps5_compute_stage = 9008u + generated_index;
            if (execute_command(&agc, work, layer_words, layer_word_count,
                                layer_marker
#ifdef PS5_GPU_TIMING
                                , gpu_timestamps
#endif
                                ) != 0)
                goto done;
            flush_cache(logits, TOKEN_VOCAB * sizeof(float));
            {
                float best = -3.402823466e+38f;
                uint32_t best_index = 0;
                for (index = 0; index < TOKEN_VOCAB; ++index)
                    if (logits[index] > best) {
                        best = logits[index];
                        best_index = index;
                    }
                ps5_compute_generated_tokens[generated_index] = best_index;
                ps5_compute_generated_count = generated_index + 1u;
                if (ps5_compute_tokenizer_path && chat_progress &&
                    decode_response(&tokenizer, generated_index + 1u) < 0)
                    goto done;
                if (chat_progress)
                    chat_progress(ps5_compute_response);
                if (best_index == TOKEN_EOS)
                    break;
            }
        }
    }
    ps5_compute_stage = 9020;
    ps5_compute_elapsed_us = sceKernelGetProcessTime() - start_us;
    ps5_compute_position = ps5_compute_prompt_count +
                           ps5_compute_generated_count - 2u;
    goto validate;
#endif
#ifdef PS5_LOGITS_DIAGNOSTIC
    for (index = 0; index < TOKEN_WIDTH; ++index)
        state[index] = ((int)(index % 17u) - 8) * 0.03125f;
    flush_cache(state, TOKEN_WIDTH * sizeof(float));
    ps5_compute_stage = 9001;
    sceKernelDebugOutText(0, "[prosperogpt] logits_diag_begin=1\n");
    if (execute_command(&agc, work, layer_words, layer_word_count,
                        layer_marker
#ifdef PS5_GPU_TIMING
                        , gpu_timestamps
#endif
                        ) != 0)
        goto done;
    sceKernelDebugOutText(0, "[prosperogpt] logits_diag_done=1\n");
    flush_cache(logits, TOKEN_VOCAB * sizeof(float));
    {
        float best = -3.402823466e+38f;
        uint32_t best_index = 0;
        for (index = 0; index < TOKEN_VOCAB; ++index)
            if (logits[index] > best) {
                best = logits[index];
                best_index = index;
            }
        ps5_compute_generated_tokens[0] = best_index;
        ps5_compute_generated_count = 1;
    }
    ps5_compute_stage = 9002;
    goto done;
#endif
#ifdef PS5_LAYER0_DIAGNOSTIC
    for (index = 0; index < TOKEN_WIDTH; ++index)
        prefill_state_a[index] =
            ((int)(index % 17u) - 8) * 0.03125f;
    for (index = 0; index < TOKEN_WIDTH; ++index)
        prefill_state_a[TOKEN_LAYER_OUTPUT_FLOATS + index] =
            ((int)(index % 19u) - 9) * 0.025f;
    set_layer_position(layer_control, 0u);
    if (execute_command(&agc, work, layer_words, layer_word_count,
                        layer_marker
#ifdef PS5_GPU_TIMING
                        , gpu_timestamps
#endif
                        ) != 0)
        goto done;
    flush_cache(prefill_state_b + TOKEN_LAYER_OUTPUT_FLOATS,
                16u * sizeof(float));
    memcpy(ps5_compute_debug_state_bits,
           prefill_state_b + TOKEN_LAYER_OUTPUT_FLOATS,
           sizeof(ps5_compute_debug_state_bits));
    ps5_compute_stage = 9006;
    goto done;
#endif
#ifdef PS5_BATCH_PREFILL
        if (command_tokens == 1u)
            break;
        start_us = sceKernelGetProcessTime();
        for (position = serial_start;
             position + command_tokens < ps5_compute_prompt_count;
             position += command_tokens) {
            uint32_t token;

            ps5_compute_position = position + command_tokens - 1u;
            ps5_compute_layer = TOKEN_LAYERS - 1u;
            ps5_compute_stage = 80u + position;
            set_layer_position(layer_control, position);
            for (token = 0; token < command_tokens; ++token)
                if (load_embedding(
                        model, embedding, prompt[position + token],
                        prefill_state_a +
                            token * TOKEN_LAYER_OUTPUT_FLOATS) != 0)
                    goto done;
            flush_cache(prefill_state_a,
                        command_tokens * TOKEN_LAYER_OUTPUT_FLOATS *
                            sizeof(float));
            if (execute_command(&agc, work, layer_words, layer_word_count,
                                layer_marker
#ifdef PS5_GPU_TIMING
                                , gpu_timestamps
#endif
                                ) != 0)
                goto done;
            ps5_compute_stage = 81u + position;
            if (execute_command(&agc, work, second_words, second_word_count,
                                second_marker
#ifdef PS5_GPU_TIMING
                                , gpu_timestamps
#endif
                                ) != 0)
                goto done;
            ps5_compute_stage = 82u + position;
            if (execute_command(&agc, work, third_words, third_word_count,
                                third_marker
#ifdef PS5_GPU_TIMING
                                , gpu_timestamps
#endif
                                ) != 0)
                goto done;
            ++ps5_compute_prefill_batches;
        }
        serial_start = position;
        batched_prefill_us = sceKernelGetProcessTime() - start_us;
        for (index = 0; index < TOKEN_LAYERS; ++index) {
            uint32_t *descriptor =
                layer_descriptors + index * LAYER_DESCRIPTOR_DWORDS;
            float *input = index & 1u ? layer_output : state;
            float *output = index & 1u ? state : layer_output;

            set_raw_descriptor(descriptor, input, TOKEN_WIDTH * 4u);
            set_raw_descriptor(descriptor + 12, output,
                               TOKEN_LAYER_OUTPUT_FLOATS * 4u);
        }
        layer_words = (uint32_t *)(work + SERIAL_LAYER_COMMAND_OFFSET);
        layer_marker =
            (volatile uint32_t *)(work + SERIAL_LAYER_MARKER_OFFSET);
        second_words = (uint32_t *)(work + SERIAL_SECOND_COMMAND_OFFSET);
        second_marker =
            (volatile uint32_t *)(work + SERIAL_SECOND_MARKER_OFFSET);
        third_words = (uint32_t *)(work + SERIAL_THIRD_COMMAND_OFFSET);
        third_marker =
            (volatile uint32_t *)(work + SERIAL_THIRD_MARKER_OFFSET);
        command_tokens = 1u;
    }
    start_us = sceKernelGetProcessTime() - batched_prefill_us;
#else
    start_us = sceKernelGetProcessTime();
#endif
    for (position = serial_start;
         position < ps5_compute_prompt_count + ps5_compute_generated_count - 1u;
         ++position) {
        uint32_t generated_index;

        ps5_compute_position = position;
        set_layer_position(layer_control, position);
        input_token = position < ps5_compute_prompt_count
                          ? prompt[position]
                          : ps5_compute_generated_tokens[position -
                                                         ps5_compute_prompt_count];
        if (load_embedding(model, embedding, input_token, state) != 0)
            goto done;
#ifdef PS5_MODEL_QWEN35
        /* The CPU owns the freshly loaded embedding; subsequent segments
           consume state written by the GPU and must not write it back. */
        flush_cache(state, TOKEN_WIDTH * sizeof(float));
#endif

        ps5_compute_layer = TOKEN_LAYERS - 1u;
#ifdef PS5_MODEL_QWEN35
        for (index = 0; index < QWEN_COMMAND_SEGMENTS; ++index) {
            uint32_t layer_begin = index * QWEN_LAYERS_PER_COMMAND;
            uint32_t layer_end = layer_begin + QWEN_LAYERS_PER_COMMAND;
            uint32_t *segment_words = index & 1u ? second_words : layer_words;
            volatile uint32_t *segment_marker =
                index & 1u ? second_marker : layer_marker;
            uint32_t segment_word_count = 0;

            if (layer_end > TOKEN_LAYERS)
                layer_end = TOKEN_LAYERS;
#ifdef PS5_QWEN_CONTEXT_DIAGNOSTIC_PHASES
            qwen_context_diagnostic_active =
                position == 1u &&
                PS5_QWEN_CONTEXT_DIAGNOSTIC_LAYER >= layer_begin &&
                PS5_QWEN_CONTEXT_DIAGNOSTIC_LAYER < layer_end;
            if (qwen_context_diagnostic_active)
                layer_end = PS5_QWEN_CONTEXT_DIAGNOSTIC_LAYER + 1u;
#endif
#ifdef PS5_QWEN_SEGMENT_DIAGNOSTIC
            char segment_log[96];

            snprintf(segment_log, sizeof(segment_log),
                     "[prosperogpt] qwen_segment_begin=%u position=%u\n",
                     index, position);
            sceKernelDebugOutText(0, segment_log);
#endif
            if (build_token_command(
                    &agc, layer_program, layer_program_count,
                    layer_descriptors, gate_program, gate_program_count,
                    quant_program, quant_program_count, down_program,
                    down_program_count, qkv_program, qkv_program_count,
                    rope_program, rope_program_count, context_program,
                    context_program_count, attention_context_program,
                    attention_context_program_count, attn_quant_program,
                    attn_quant_program_count, output_program,
                    output_program_count, ff_prepare_program,
                    ff_prepare_program_count, logits_program,
                    logits_program_count, logits_prepare_program,
                    logits_prepare_program_count, logits_descriptors, work,
                    TOKEN_WORK_BYTES,
                    layer_caches[0] +
                        (uint64_t)layer_begin * LAYER_CACHE_STRIDE,
                    (uint64_t)(layer_end - layer_begin) *
                        LAYER_CACHE_STRIDE,
                    model, MODEL_RESIDENT_BYTES, layer_begin, layer_end,
                    layer_end == TOKEN_LAYERS, segment_marker, segment_words
#ifdef PS5_GPU_TIMING
                    , gpu_timestamps
#endif
                    , &segment_word_count) != 0)
                goto done;
            ps5_compute_layer =
                layer_end - 1u;
            ps5_compute_stage = 100u + position * 100u + index;
            if (execute_command(&agc, work, segment_words,
                                segment_word_count, segment_marker
#ifdef PS5_GPU_TIMING
                                , gpu_timestamps
#endif
                                ) != 0)
                goto done;
#ifdef PS5_QWEN_CONTEXT_DIAGNOSTIC_PHASES
            if (qwen_context_diagnostic_active) {
                float *diagnostic_scratch =
                    (PS5_QWEN_CONTEXT_DIAGNOSTIC_LAYER & 1u)
                        ? state : layer_output;
#ifdef PS5_QWEN_CONTEXT_DIAGNOSTIC_OFFSET
                diagnostic_scratch += PS5_QWEN_CONTEXT_DIAGNOSTIC_OFFSET;
#else
                if (PS5_QWEN_CONTEXT_DIAGNOSTIC_PHASES >= 2u &&
                    PS5_QWEN_CONTEXT_DIAGNOSTIC_PHASES <= 4u)
                    diagnostic_scratch += 10368u;
#endif
                flush_cache(diagnostic_scratch,
                            sizeof(ps5_compute_debug_state_bits));
                memcpy(ps5_compute_debug_state_bits, diagnostic_scratch,
                       sizeof(ps5_compute_debug_state_bits));
                ps5_compute_stage =
                    9300u + PS5_QWEN_CONTEXT_DIAGNOSTIC_PHASES;
                result = 0;
                goto done;
            }
#endif
#ifdef PS5_QWEN_SEGMENT_DIAGNOSTIC
            snprintf(segment_log, sizeof(segment_log),
                     "[prosperogpt] qwen_segment_done=%u position=%u\n",
                     index, position);
            sceKernelDebugOutText(0, segment_log);
#endif
        }
#else
        ps5_compute_stage = 100u + position * 100u;
        if (execute_command(&agc, work, layer_words, layer_word_count,
                            layer_marker
#ifdef PS5_GPU_TIMING
                            , gpu_timestamps
#endif
                            ) != 0)
            goto done;
#ifdef PS5_SPLIT_ATTN
        ps5_compute_stage = 101u + position * 100u;
        if (execute_command(&agc, work, second_words, second_word_count,
                            second_marker
#ifdef PS5_GPU_TIMING
                            , gpu_timestamps
#endif
                            ) != 0)
            goto done;
        ps5_compute_stage = 102u + position * 100u;
        if (execute_command(&agc, work, third_words, third_word_count,
                            third_marker
#ifdef PS5_GPU_TIMING
                            , gpu_timestamps
#endif
                            ) != 0)
            goto done;
#endif
#endif
        if (position + 1u == ps5_compute_prompt_count)
            ps5_compute_prefill_us = sceKernelGetProcessTime() - start_us;
        if (position + 1u < ps5_compute_prompt_count)
            continue;
        flush_cache(logits, TOKEN_VOCAB * sizeof(float));
        generated_index = position + 1u - ps5_compute_prompt_count;
        {
            float best = -3.402823466e+38f;
            uint32_t best_index = 0;

            for (index = 0; index < TOKEN_VOCAB; ++index) {
                if (logits[index] > best) {
                    best = logits[index];
                    best_index = index;
                }
            }
            ps5_compute_generated_tokens[generated_index] = best_index;
            // ponytail: bounded output keeps full-response decoding inexpensive.
            if (ps5_compute_tokenizer_path && chat_progress &&
                decode_response(&tokenizer, generated_index + 1u) < 0)
                goto done;
            if (chat_progress)
                chat_progress(ps5_compute_response);
            if (best_index == TOKEN_EOS) {
                ps5_compute_generated_count = generated_index + 1u;
                break;
            }
        }
    }
    ps5_compute_elapsed_us = sceKernelGetProcessTime() - start_us;
    ps5_compute_stage = 7000;
validate:
    result = 0;
    for (index = 0; index < ps5_compute_expected_count; ++index)
        if (ps5_compute_generated_tokens[index] != expected[index])
            result = 1;
    if (ps5_compute_tokenizer_path &&
        decode_response(&tokenizer, ps5_compute_generated_count) < 0)
        result = 1;
    if (result == 0) {
        uint32_t processed_count = ps5_compute_position + 1u;

        for (index = 0; index < processed_count; ++index)
            resident_tokens[index] =
                index < ps5_compute_prompt_count
                    ? prompt[index]
                    : ps5_compute_generated_tokens[index -
                                                   ps5_compute_prompt_count];
        resident_token_count = processed_count;
        resident_prompt_count = ps5_compute_prompt_count;
        // ponytail: one active chat session owns this KV mapping until exit.
        resident_work = work;
    }

done:
    ps5_tokenizer_close(&tokenizer);
    if (aio_fd >= 0)
        sceKernelClose(aio_fd);
    if (file)
        fclose(file);
#ifdef PS5_LARGE_MODEL
    if (work && work != resident_work)
        munmap(work_mapping, PRIMARY_MAPPING_BYTES);
    if (work_start >= 0 && (!work || work != resident_work))
        sceKernelReleaseDirectMemory(work_start, PRIMARY_MAPPING_BYTES);
#else
    for (index = 0; index < LAYER_CACHE_BANK_COUNT; ++index) {
        if (layer_caches[index] &&
            layer_caches[index] != resident_layer_caches[index])
            munmap(layer_cache_mappings[index],
                   LAYER_CACHE_BANK_MAPPING_BYTES);
        if (layer_cache_starts[index] >= 0 &&
            layer_caches[index] != resident_layer_caches[index])
            sceKernelReleaseDirectMemory(layer_cache_starts[index],
                                         LAYER_CACHE_BANK_MAPPING_BYTES);
    }
    if (model && model != resident_model)
        munmap(model_mapping, MODEL_MAPPING_BYTES);
    if (model_start >= 0 && model != resident_model)
        sceKernelReleaseDirectMemory(model_start, MODEL_MAPPING_BYTES);
    if (work && work != resident_work)
        munmap(work_mapping, WORK_MAPPING_BYTES);
    if (work_start >= 0 && work != resident_work)
        sceKernelReleaseDirectMemory(work_start, WORK_MAPPING_BYTES);
#endif
    return result;
}

int run_model_chat(const ps5_chat_message_t *messages, uint32_t message_count,
                   uint32_t generated_count,
                   void (*progress)(const char *))
{
    int result;

    if (!messages || !message_count || !generated_count ||
        generated_count > TOKEN_MAX_COUNT)
        return 1;
    chat_messages = messages;
    chat_message_count = message_count;
    chat_generated_count = generated_count;
    chat_progress = progress;
    result = run_model_token();
    chat_messages = NULL;
    chat_message_count = 0;
    chat_generated_count = 0;
    chat_progress = NULL;
    return result;
}
