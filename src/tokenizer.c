#include "tokenizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOKENIZER_MAGIC UINT32_C(0x4b543550)
#define TOKENIZER_GPT2_VERSION 1u
#define TOKENIZER_SPM_VERSION 2u
#define TOKENIZER_QWEN35_VERSION 3u
#define TOKENIZER_HEADER_BYTES 20u
#define TOKENIZER_MAX_PIECE_BYTES 1024u
#define TOKENIZER_MAX_SPM_BYTES 8192u

typedef struct tokenizer_merge
{
    uint32_t left;
    uint32_t right;
    uint32_t merged;
    uint32_t rank;
} tokenizer_merge_t;

uint32_t ps5_tokenizer_load_stage;
uint64_t ps5_tokenizer_load_detail;

static uint8_t tokenizer_storage[7u * 1024u * 1024u];

static uint32_t read_u32(const uint8_t *at)
{
    uint32_t value;
    memcpy(&value, at, sizeof(value));
    return value;
}

int ps5_tokenizer_load(ps5_tokenizer_t *tokenizer, const char *path)
{
    FILE *input;
    long length;
    uint32_t token_bytes;
    size_t offsets_at = TOKENIZER_HEADER_BYTES + 256u * sizeof(uint32_t);
    size_t token_at;
    size_t extra_at;
    size_t required;

    memset(tokenizer, 0, sizeof(*tokenizer));
    ps5_tokenizer_load_stage = 1u;
    ps5_tokenizer_load_detail = 0u;
    input = fopen(path, "rb");
    if (!input || fseek(input, 0, SEEK_END) != 0 || (length = ftell(input)) < 0 ||
        fseek(input, 0, SEEK_SET) != 0)
        goto fail;
    ps5_tokenizer_load_stage = 2u;
    ps5_tokenizer_load_detail = (uint64_t)length;
    tokenizer->data = (size_t)length <= sizeof(tokenizer_storage) ? tokenizer_storage : NULL;
    ps5_tokenizer_load_stage = 20u;
    if (!tokenizer->data || fread(tokenizer->data, 1, (size_t)length, input) != (size_t)length)
        goto fail;
    ps5_tokenizer_load_stage = 21u;
    fclose(input);
    input = NULL;
    tokenizer->bytes = (size_t)length;
    ps5_tokenizer_load_stage = 3u;
    if (tokenizer->bytes < TOKENIZER_HEADER_BYTES || read_u32(tokenizer->data) != TOKENIZER_MAGIC)
        goto fail;
    tokenizer->version = read_u32(tokenizer->data + 4);
    ps5_tokenizer_load_stage = 4u;
    ps5_tokenizer_load_detail = tokenizer->version;
    if (tokenizer->version != TOKENIZER_GPT2_VERSION &&
        tokenizer->version != TOKENIZER_SPM_VERSION &&
        tokenizer->version != TOKENIZER_QWEN35_VERSION)
        goto fail;
    tokenizer->vocab_count = read_u32(tokenizer->data + 8);
    tokenizer->merge_count = read_u32(tokenizer->data + 12);
    token_bytes = read_u32(tokenizer->data + 16);
    ps5_tokenizer_load_stage = 5u;
    token_at = offsets_at + ((size_t)tokenizer->vocab_count + 1u) * sizeof(uint32_t);
    extra_at = (token_at + token_bytes + 3u) & ~(size_t)3u;
    if (tokenizer->version != TOKENIZER_SPM_VERSION)
    {
        required = extra_at + (size_t)tokenizer->merge_count * sizeof(tokenizer_merge_t);
        if (tokenizer->merge_count == 0)
            goto fail;
    }
    else
    {
        size_t types_at = extra_at + (size_t)tokenizer->vocab_count * sizeof(float);
        size_t order_at = (types_at + tokenizer->vocab_count + 3u) & ~(size_t)3u;
        required = order_at + (size_t)tokenizer->vocab_count * sizeof(uint32_t);
        if (tokenizer->merge_count != 0)
            goto fail;
        tokenizer->scores = (const float *)(tokenizer->data + extra_at);
        tokenizer->token_types = tokenizer->data + types_at;
        tokenizer->token_order = (const uint32_t *)(tokenizer->data + order_at);
    }
    ps5_tokenizer_load_stage = 6u;
    ps5_tokenizer_load_detail = required;
    if (tokenizer->vocab_count == 0 || required != tokenizer->bytes)
        goto fail;
    tokenizer->byte_tokens = (const uint32_t *)(tokenizer->data + TOKENIZER_HEADER_BYTES);
    tokenizer->token_offsets = (const uint32_t *)(tokenizer->data + offsets_at);
    tokenizer->token_bytes = tokenizer->data + token_at;
    if (tokenizer->version != TOKENIZER_SPM_VERSION)
        tokenizer->merges = tokenizer->data + extra_at;
    ps5_tokenizer_load_stage = 7u;
    if (tokenizer->token_offsets[0] != 0 ||
        tokenizer->token_offsets[tokenizer->vocab_count] != token_bytes)
        goto fail;
    {
        uint32_t index;
        ps5_tokenizer_load_stage = 8u;
        for (index = 0; index < tokenizer->vocab_count; ++index)
        {
            if (tokenizer->token_offsets[index] > tokenizer->token_offsets[index + 1u] ||
                (tokenizer->version == TOKENIZER_SPM_VERSION &&
                 tokenizer->token_order[index] >= tokenizer->vocab_count))
                goto fail;
        }
    }
    ps5_tokenizer_load_stage = 9u;
    ps5_tokenizer_load_detail = tokenizer->bytes;
    return 0;

fail:
    if (input)
        fclose(input);
    ps5_tokenizer_close(tokenizer);
    return -1;
}

void ps5_tokenizer_close(ps5_tokenizer_t *tokenizer)
{
    memset(tokenizer, 0, sizeof(*tokenizer));
}

static const tokenizer_merge_t *find_merge(const ps5_tokenizer_t *tokenizer, uint32_t left,
                                           uint32_t right)
{
    const tokenizer_merge_t *merges = (const tokenizer_merge_t *)tokenizer->merges;
    uint32_t low = 0;
    uint32_t high = tokenizer->merge_count;

    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2u;
        const tokenizer_merge_t *merge = merges + middle;
        if (merge->left < left || (merge->left == left && merge->right < right))
            low = middle + 1u;
        else
            high = middle;
    }
    if (low < tokenizer->merge_count && merges[low].left == left && merges[low].right == right)
        return merges + low;
    return NULL;
}

static int encode_piece(const ps5_tokenizer_t *tokenizer, const uint8_t *text, size_t bytes,
                        uint32_t *output, uint32_t capacity)
{
    uint32_t symbols[TOKENIZER_MAX_PIECE_BYTES];
    uint32_t count = (uint32_t)bytes;
    uint32_t index;

    if (bytes > TOKENIZER_MAX_PIECE_BYTES || count > capacity)
        return -1;
    for (index = 0; index < count; ++index)
        symbols[index] = tokenizer->byte_tokens[text[index]];
    while (count > 1u)
    {
        const tokenizer_merge_t *best = NULL;
        uint32_t best_at = 0;
        for (index = 0; index + 1u < count; ++index)
        {
            const tokenizer_merge_t *merge =
                find_merge(tokenizer, symbols[index], symbols[index + 1u]);
            if (merge && (!best || merge->rank < best->rank))
            {
                best = merge;
                best_at = index;
            }
        }
        if (!best)
            break;
        symbols[best_at] = best->merged;
        memmove(symbols + best_at + 1u, symbols + best_at + 2u,
                (count - best_at - 2u) * sizeof(*symbols));
        --count;
    }
    memcpy(output, symbols, count * sizeof(*symbols));
    return (int)count;
}

static int is_space(uint8_t value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static int is_digit(uint8_t value)
{
    return value >= '0' && value <= '9';
}

static int is_letter(uint8_t value)
{
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value >= 0x80u;
}

static uint8_t ascii_lower(uint8_t value)
{
    return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

static int is_qwen_letter_mark(uint8_t value)
{
    return is_letter(value);
}

static size_t qwen_contraction(const uint8_t *text, size_t bytes)
{
    static const char *const values[] = {
        "'re", "'ve", "'ll", "'s", "'t", "'m", "'d",
    };
    size_t index;

    for (index = 0; index < sizeof(values) / sizeof(values[0]); ++index)
    {
        size_t at;
        size_t length = strlen(values[index]);
        if (length > bytes)
            continue;
        for (at = 0; at < length; ++at)
            if (ascii_lower(text[at]) != (uint8_t)values[index][at])
                break;
        if (at == length)
            return length;
    }
    return 0;
}

static int encode_qwen35(const ps5_tokenizer_t *tokenizer, const char *text, uint32_t *tokens,
                         uint32_t capacity)
{
    const uint8_t *input = (const uint8_t *)text;
    size_t length = strlen(text);
    size_t at = 0;
    uint32_t count = 0;

    while (at < length)
    {
        size_t start = at;
        size_t suffix = qwen_contraction(input + at, length - at);
        int encoded;

        if (suffix)
        {
            at += suffix;
        }
        else if (input[at] != '\r' && input[at] != '\n' && !is_digit(input[at]) &&
                 (is_qwen_letter_mark(input[at]) ||
                  (at + 1u < length && is_qwen_letter_mark(input[at + 1u]))))
        {
            ++at;
            while (at < length && is_qwen_letter_mark(input[at]))
                ++at;
        }
        else if (is_digit(input[at]))
        {
            ++at;
        }
        else
        {
            size_t punct = input[at] == ' ' ? at + 1u : at;
            if (punct < length && !is_space(input[punct]) && !is_qwen_letter_mark(input[punct]) &&
                !is_digit(input[punct]))
            {
                at = punct + 1u;
                while (at < length && !is_space(input[at]) && !is_qwen_letter_mark(input[at]) &&
                       !is_digit(input[at]))
                    ++at;
                while (at < length && (input[at] == '\r' || input[at] == '\n'))
                    ++at;
            }
            else if (is_space(input[at]))
            {
                size_t end = at;
                size_t last_newline = 0;
                while (end < length && is_space(input[end]))
                {
                    if (input[end] == '\r' || input[end] == '\n')
                        last_newline = end + 1u;
                    ++end;
                }
                if (last_newline)
                    at = last_newline;
                else if (end - at > 1u && end < length)
                    at = end - 1u;
                else
                    at = end;
            }
            else
            {
                ++at;
            }
        }
        encoded =
            encode_piece(tokenizer, input + start, at - start, tokens + count, capacity - count);
        if (encoded < 0)
            return -1;
        count += (uint32_t)encoded;
    }
    return (int)count;
}

static size_t contraction(const uint8_t *text, size_t bytes)
{
    static const char *const values[] = {
        "'s", "'t", "'re", "'ve", "'m", "'ll", "'d",
    };
    size_t index;

    for (index = 0; index < sizeof(values) / sizeof(values[0]); ++index)
    {
        size_t length = strlen(values[index]);
        if (length <= bytes && !memcmp(text, values[index], length))
            return length;
    }
    return 0;
}

static int encode_gpt2(const ps5_tokenizer_t *tokenizer, const char *text, uint32_t *tokens,
                       uint32_t capacity)
{
    const uint8_t *input = (const uint8_t *)text;
    size_t length = strlen(text);
    size_t at = 0;
    uint32_t count = 0;

    while (at < length)
    {
        size_t start = at;
        size_t suffix = contraction(input + at, length - at);
        int letters;
        int encoded;

        if (suffix)
        {
            at += suffix;
        }
        else if (is_digit(input[at]))
        {
            ++at;
        }
        else
        {
            if (is_space(input[at]))
            {
                while (at < length && is_space(input[at]))
                    ++at;
                if (at == length || is_digit(input[at]) || input[at - 1u] != ' ')
                {
                    encoded = encode_piece(tokenizer, input + start, at - start, tokens + count,
                                           capacity - count);
                    if (encoded < 0)
                        return -1;
                    count += (uint32_t)encoded;
                    continue;
                }
                else
                {
                    if (at - start > 1u)
                    {
                        encoded = encode_piece(tokenizer, input + start, at - start - 1u,
                                               tokens + count, capacity - count);
                        if (encoded < 0)
                            return -1;
                        count += (uint32_t)encoded;
                    }
                    start = at - 1u;
                }
            }
            letters = is_letter(input[at]);
            while (at < length && !is_space(input[at]) && !is_digit(input[at]) &&
                   is_letter(input[at]) == letters)
                ++at;
        }

        encoded =
            encode_piece(tokenizer, input + start, at - start, tokens + count, capacity - count);
        if (encoded < 0)
            return -1;
        count += (uint32_t)encoded;
    }
    return (int)count;
}

typedef struct spm_symbol
{
    uint32_t start;
    uint32_t length;
} spm_symbol_t;

static int compare_token(const ps5_tokenizer_t *tokenizer, uint32_t token, const uint8_t *bytes,
                         uint32_t length)
{
    uint32_t begin = tokenizer->token_offsets[token];
    uint32_t token_length = tokenizer->token_offsets[token + 1u] - begin;
    uint32_t common = length < token_length ? length : token_length;
    int result = memcmp(bytes, tokenizer->token_bytes + begin, common);

    if (result)
        return result;
    if (length < token_length)
        return -1;
    return length > token_length ? 1 : 0;
}

static int find_spm_token(const ps5_tokenizer_t *tokenizer, const uint8_t *bytes, uint32_t length)
{
    uint32_t low = 0;
    uint32_t high = tokenizer->vocab_count;

    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2u;
        uint32_t token = tokenizer->token_order[middle];
        if (compare_token(tokenizer, token, bytes, length) > 0)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low < tokenizer->vocab_count)
    {
        uint32_t token = tokenizer->token_order[low];
        if (compare_token(tokenizer, token, bytes, length) == 0)
            return (int)token;
    }
    return -1;
}

static int encode_spm(const ps5_tokenizer_t *tokenizer, const char *text, uint32_t *tokens,
                      uint32_t capacity)
{
    static const uint8_t space_marker[] = {0xe2u, 0x96u, 0x81u};
    uint8_t normalized[TOKENIZER_MAX_SPM_BYTES];
    spm_symbol_t symbols[TOKENIZER_MAX_SPM_BYTES / 2u];
    const uint8_t *input = (const uint8_t *)text;
    size_t input_bytes = strlen(text);
    uint32_t normalized_bytes = 0;
    uint32_t symbol_count = 0;
    uint32_t count = 0;
    uint32_t index;

#define APPEND_SPACE_MARKER()                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (normalized_bytes + sizeof(space_marker) > sizeof(normalized))                          \
            return -1;                                                                             \
        memcpy(normalized + normalized_bytes, space_marker, sizeof(space_marker));                 \
        normalized_bytes += sizeof(space_marker);                                                  \
    } while (0)

    APPEND_SPACE_MARKER();
    for (index = 0; index < input_bytes; ++index)
    {
        if (input[index] == ' ')
        {
            APPEND_SPACE_MARKER();
        }
        else
        {
            if (normalized_bytes == sizeof(normalized))
                return -1;
            normalized[normalized_bytes++] = input[index];
        }
    }
#undef APPEND_SPACE_MARKER

    for (index = 0; index < normalized_bytes;)
    {
        uint32_t width = 1;
        uint8_t lead = normalized[index];
        if ((lead & 0xf0u) == 0xf0u)
            width = 4;
        else if ((lead & 0xe0u) == 0xe0u)
            width = 3;
        else if ((lead & 0xc0u) == 0xc0u)
            width = 2;
        if (width > normalized_bytes - index ||
            symbol_count == sizeof(symbols) / sizeof(symbols[0]))
            return -1;
        symbols[symbol_count].start = index;
        symbols[symbol_count].length = width;
        ++symbol_count;
        index += width;
    }

    while (symbol_count > 1u)
    {
        int best_token = -1;
        float best_score = 0.0f;
        uint32_t best_at = 0;
        for (index = 0; index + 1u < symbol_count; ++index)
        {
            uint32_t length = symbols[index].length + symbols[index + 1u].length;
            int token = find_spm_token(tokenizer, normalized + symbols[index].start, length);
            if (token >= 0 && (best_token < 0 || tokenizer->scores[token] > best_score))
            {
                best_token = token;
                best_score = tokenizer->scores[token];
                best_at = index;
            }
        }
        if (best_token < 0)
            break;
        symbols[best_at].length += symbols[best_at + 1u].length;
        memmove(symbols + best_at + 1u, symbols + best_at + 2u,
                (symbol_count - best_at - 2u) * sizeof(symbols[0]));
        --symbol_count;
    }

    for (index = 0; index < symbol_count; ++index)
    {
        int token =
            find_spm_token(tokenizer, normalized + symbols[index].start, symbols[index].length);
        if (token >= 0)
        {
            if (count == capacity)
                return -1;
            tokens[count++] = (uint32_t)token;
        }
        else
        {
            uint32_t byte;
            if (count + symbols[index].length > capacity)
                return -1;
            for (byte = 0; byte < symbols[index].length; ++byte)
                tokens[count++] = tokenizer->byte_tokens[normalized[symbols[index].start + byte]];
        }
    }
    return (int)count;
}

int ps5_tokenizer_encode(const ps5_tokenizer_t *tokenizer, const char *text, uint32_t *tokens,
                         uint32_t capacity)
{
    if (tokenizer->version == TOKENIZER_SPM_VERSION)
        return encode_spm(tokenizer, text, tokens, capacity);
    if (tokenizer->version == TOKENIZER_QWEN35_VERSION)
        return encode_qwen35(tokenizer, text, tokens, capacity);
    return encode_gpt2(tokenizer, text, tokens, capacity);
}

int ps5_tokenizer_decode(const ps5_tokenizer_t *tokenizer, const uint32_t *tokens, uint32_t count,
                         char *text, size_t capacity)
{
    size_t used = 0;
    int removed_spm_prefix = 0;
    uint32_t index;

    if (capacity == 0)
        return -1;
    for (index = 0; index < count; ++index)
    {
        uint32_t token = tokens[index];
        uint32_t begin;
        uint32_t end;
        size_t bytes;
        if (token >= tokenizer->vocab_count)
            return -1;
        if (tokenizer->version == TOKENIZER_SPM_VERSION && tokenizer->token_types[token] == 3u)
            continue;
        if (tokenizer->version == TOKENIZER_SPM_VERSION && tokenizer->token_types[token] == 6u)
        {
            uint32_t byte;
            for (byte = 0; byte < 256u; ++byte)
                if (tokenizer->byte_tokens[byte] == token)
                    break;
            if (byte == 256u || used + 1u >= capacity)
                return -1;
            text[used++] = (char)byte;
            continue;
        }
        begin = tokenizer->token_offsets[token];
        end = tokenizer->token_offsets[token + 1u];
        if (end < begin)
            return -1;
        bytes = end - begin;
        if (tokenizer->version == TOKENIZER_SPM_VERSION)
        {
            size_t at = 0;
            while (at < bytes)
            {
                if (at + 3u <= bytes && tokenizer->token_bytes[begin + at] == 0xe2u &&
                    tokenizer->token_bytes[begin + at + 1u] == 0x96u &&
                    tokenizer->token_bytes[begin + at + 2u] == 0x81u)
                {
                    at += 3u;
                    if (!removed_spm_prefix)
                    {
                        removed_spm_prefix = 1;
                        continue;
                    }
                    if (used + 1u >= capacity)
                        return -1;
                    text[used++] = ' ';
                }
                else
                {
                    if (used + 1u >= capacity)
                        return -1;
                    text[used++] = (char)tokenizer->token_bytes[begin + at++];
                }
            }
        }
        else
        {
            if (used + bytes >= capacity)
                return -1;
            memcpy(text + used, tokenizer->token_bytes + begin, bytes);
            used += bytes;
        }
    }
    text[used] = '\0';
    return (int)used;
}
