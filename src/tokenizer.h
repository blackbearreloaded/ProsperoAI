#ifndef PS5_COMPUTE_TOKENIZER_H
#define PS5_COMPUTE_TOKENIZER_H

#include <stddef.h>
#include <stdint.h>

typedef struct ps5_tokenizer
{
    uint8_t *data;
    size_t bytes;
    uint32_t version;
    uint32_t vocab_count;
    uint32_t merge_count;
    const uint32_t *byte_tokens;
    const uint32_t *token_offsets;
    const uint8_t *token_bytes;
    const void *merges;
    const float *scores;
    const uint8_t *token_types;
    const uint32_t *token_order;
} ps5_tokenizer_t;

extern uint32_t ps5_tokenizer_load_stage;
extern uint64_t ps5_tokenizer_load_detail;

int ps5_tokenizer_load(ps5_tokenizer_t *tokenizer, const char *path);
void ps5_tokenizer_close(ps5_tokenizer_t *tokenizer);
int ps5_tokenizer_encode(const ps5_tokenizer_t *tokenizer, const char *text, uint32_t *tokens,
                         uint32_t capacity);
int ps5_tokenizer_decode(const ps5_tokenizer_t *tokenizer, const uint32_t *tokens, uint32_t count,
                         char *text, size_t capacity);

#endif
