#ifndef PS5_COMPUTE_CHAT_PROMPT_H
#define PS5_COMPUTE_CHAT_PROMPT_H

#include "tokenizer.h"

#include <stdint.h>

typedef struct ps5_chat_message
{
    const char *role;
    const char *content;
} ps5_chat_message_t;

int ps5_chat_build_prompt(const ps5_tokenizer_t *tokenizer, const ps5_chat_message_t *messages,
                          uint32_t message_count, uint32_t *tokens, uint32_t capacity);
uint32_t ps5_chat_reusable_prefix(const uint32_t *cached, uint32_t cached_count,
                                  const uint32_t *prompt, uint32_t prompt_count);

#endif
