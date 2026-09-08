#include "chat_prompt.h"

#include <string.h>

#if defined(PS5_MODEL_7000)
#define MISTRAL_BOS 1u
#define MISTRAL_EOS 2u
#define MISTRAL_INST 3u
#define MISTRAL_INST_END 4u
#define MISTRAL_SPACE 29473u
#elif defined(PS5_MODEL_QWEN35)
#define IM_START 248045u
#define IM_END 248046u
#define THINK_START 248068u
#define THINK_END 248069u
#elif defined(PS5_MODEL_3000)
#define IM_START 128011u
#define IM_END 128012u
#define THINK_START 128002u
#define THINK_END 128003u
#else
#define IM_START 1u
#define IM_END 2u
#endif

#if !defined(PS5_MODEL_7000) && !defined(PS5_MODEL_QWEN35)
static const char default_system[] =
    "You are a helpful AI assistant named SmolLM, trained by Hugging Face";
#endif

static int append_token(uint32_t token, uint32_t *tokens, uint32_t *count,
                        uint32_t capacity)
{
    if (*count == capacity)
        return -1;
    tokens[(*count)++] = token;
    return 0;
}

static int append_text(const ps5_tokenizer_t *tokenizer, const char *text,
                       uint32_t *tokens, uint32_t *count, uint32_t capacity)
{
    int added = ps5_tokenizer_encode(tokenizer, text, tokens + *count,
                                     capacity - *count);
    if (added < 0)
        return -1;
    *count += (uint32_t)added;
    return 0;
}

#ifndef PS5_MODEL_7000
static int append_message(const ps5_tokenizer_t *tokenizer, const char *role,
                          const char *content, uint32_t *tokens,
                          uint32_t *count, uint32_t capacity)
{
#ifdef PS5_MODEL_QWEN35
    if (strcmp(role, "assistant") == 0)
        return append_token(IM_START, tokens, count, capacity) ||
                       append_text(tokenizer, role, tokens, count, capacity) ||
                       append_text(tokenizer, "\n", tokens, count, capacity) ||
                       append_token(THINK_START, tokens, count, capacity) ||
                       append_text(tokenizer, "\n\n", tokens, count,
                                   capacity) ||
                       append_token(THINK_END, tokens, count, capacity) ||
                       append_text(tokenizer, "\n\n", tokens, count,
                                   capacity) ||
                       append_text(tokenizer, content, tokens, count,
                                   capacity) ||
                       append_token(IM_END, tokens, count, capacity) ||
                       append_text(tokenizer, "\n", tokens, count, capacity)
                   ? -1
                   : 0;
#endif
    return append_token(IM_START, tokens, count, capacity) ||
                   append_text(tokenizer, role, tokens, count, capacity) ||
                   append_text(tokenizer, "\n", tokens, count, capacity) ||
                   append_text(tokenizer, content, tokens, count, capacity) ||
                   append_token(IM_END, tokens, count, capacity) ||
                   append_text(tokenizer, "\n", tokens, count, capacity)
               ? -1
               : 0;
}
#endif

int ps5_chat_build_prompt(const ps5_tokenizer_t *tokenizer,
                          const ps5_chat_message_t *messages,
                          uint32_t message_count, uint32_t *tokens,
                          uint32_t capacity)
{
    uint32_t count = 0;
    uint32_t index;

    if (!tokenizer || !messages || !message_count || !tokens || !capacity)
        return -1;
#ifdef PS5_MODEL_7000
    if (append_token(MISTRAL_BOS, tokens, &count, capacity) != 0)
        return -1;
    for (index = 0; index < message_count; ++index) {
        const int user_turn = (index & 1u) == 0;
        if (!messages[index].role || !messages[index].content ||
            strcmp(messages[index].role,
                   user_turn ? "user" : "assistant") != 0)
            return -1;
        if (user_turn) {
            if (append_token(MISTRAL_INST, tokens, &count, capacity) != 0 ||
                append_token(MISTRAL_SPACE, tokens, &count, capacity) != 0 ||
                append_text(tokenizer, messages[index].content, tokens,
                            &count, capacity) != 0 ||
                append_token(MISTRAL_SPACE, tokens, &count, capacity) != 0 ||
                append_token(MISTRAL_INST_END, tokens, &count,
                             capacity) != 0)
                return -1;
        } else if (append_text(tokenizer, messages[index].content, tokens,
                               &count, capacity) != 0 ||
                   append_token(MISTRAL_EOS, tokens, &count,
                                capacity) != 0) {
            return -1;
        }
    }
    if ((message_count & 1u) == 0)
        return -1;
#else
#ifndef PS5_MODEL_QWEN35
    if (strcmp(messages[0].role, "system") != 0 &&
        append_message(tokenizer, "system", default_system, tokens, &count,
                       capacity) != 0)
        return -1;
#endif
    for (index = 0; index < message_count; ++index)
        if (!messages[index].role || !messages[index].content ||
            append_message(tokenizer, messages[index].role,
                           messages[index].content, tokens, &count,
                           capacity) != 0)
            return -1;
    if (append_token(IM_START, tokens, &count, capacity) != 0 ||
        append_text(tokenizer, "assistant\n", tokens, &count, capacity) != 0
#if defined(PS5_MODEL_3000) || defined(PS5_MODEL_QWEN35)
        || append_token(THINK_START, tokens, &count, capacity) != 0 ||
        append_text(tokenizer, "\n\n", tokens, &count, capacity) != 0 ||
        append_token(THINK_END, tokens, &count, capacity) != 0 ||
        append_text(tokenizer,
#ifdef PS5_MODEL_QWEN35
                    "\n\n",
#else
                    "\n",
#endif
                    tokens, &count, capacity) != 0
#endif
        )
        return -1;
#endif
    return (int)count;
}

uint32_t ps5_chat_reusable_prefix(const uint32_t *cached,
                                  uint32_t cached_count,
                                  const uint32_t *prompt,
                                  uint32_t prompt_count)
{
    uint32_t count = 0;

    if (!cached || !prompt)
        return 0;
    while (count < cached_count && count + 1u < prompt_count &&
           cached[count] == prompt[count])
        ++count;
    return count;
}
