// ProsperoAI - Native PlayStation 5 local AI application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpt_input.hpp"
#include "ps5_keyboard.hpp"

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define INPUT_QUEUE_SIZE 64U
#define PAD_SAMPLE_SIZE 120U
#define PAD_SAMPLE_CAPACITY 64
#define PAD_BUTTON_INTERCEPTED UINT32_C(0x80000000)
#define STICK_LOW 64U
#define STICK_HIGH 192U
#define STICK_REPEAT_DELAY_MS UINT64_C(350)
#define STICK_REPEAT_MS UINT64_C(110)

struct button_map_t
{
    uint32_t button;
    gpt_input_key_t key;
};

extern "C"
{
    extern int scePadInit(void);
    extern int scePadOpen(int32_t user_id, int32_t port_type, int32_t index, const void *param);
    extern int scePadClose(int32_t handle);
    extern int scePadRead(int32_t handle, void *data, int32_t num);
    extern int sceUserServiceInitialize(void *init_params);
    extern int sceUserServiceGetInitialUser(int32_t *user_id);
    extern int sceUserServiceTerminate(void);
    extern int sceSysmoduleLoadModule(uint16_t module_id);
    extern int sceSysmoduleUnloadModule(uint16_t module_id);
    extern int sceKernelDebugOutText(int channel, const char *text);
    extern uint64_t SDL_GetTicks64(void);
}

static const button_map_t buttons[] = {
    {UINT32_C(0x00004000), GPT_INPUT_CROSS},   {UINT32_C(0x00002000), GPT_INPUT_CIRCLE},
    {UINT32_C(0x00008000), GPT_INPUT_SQUARE},  {UINT32_C(0x00001000), GPT_INPUT_TRIANGLE},
    {UINT32_C(0x00000008), GPT_INPUT_OPTIONS}, {UINT32_C(0x00000400), GPT_INPUT_L1},
    {UINT32_C(0x00000800), GPT_INPUT_R1},      {UINT32_C(0x00000010), GPT_INPUT_UP},
    {UINT32_C(0x00000040), GPT_INPUT_DOWN},    {UINT32_C(0x00000080), GPT_INPUT_LEFT},
    {UINT32_C(0x00000020), GPT_INPUT_RIGHT},
};

static gpt_input_event_t queue[INPUT_QUEUE_SIZE];
static unsigned char samples[PAD_SAMPLE_CAPACITY][PAD_SAMPLE_SIZE];
static unsigned queue_read;
static unsigned queue_write;
static uint32_t button_state;
static int analog_key = -1;
static uint64_t analog_repeat_at;
static int scroll_key = -1;
static uint64_t scroll_repeat_at;
static int32_t pad_handle = -1;
static int32_t keyboard_handles[ps5::keyboard::kMaxOpenHandles];
static ps5::keyboard::Data keyboard_samples[ps5::keyboard::kMaxSamples];
static std::array<uint16_t, ps5::keyboard::kMaxKeys>
    keyboard_previous[ps5::keyboard::kMaxOpenHandles];
static int keyboard_scroll_key = -1;
static uint64_t keyboard_scroll_repeat_at;
static bool keyboard_module_owned;
static bool owns_user_service;

static uint64_t monotonic_milliseconds(void)
{
    return SDL_GetTicks64();
}

static int stick_direction(uint8_t x, uint8_t y)
{
    const int horizontal = (int)x - 128;
    const int vertical = (int)y - 128;
    const int horizontal_size = horizontal < 0 ? -horizontal : horizontal;
    const int vertical_size = vertical < 0 ? -vertical : vertical;
    if (horizontal_size < 64 && vertical_size < 64)
        return -1;
    if (horizontal_size > vertical_size)
        return x < STICK_LOW ? GPT_INPUT_LEFT : x > STICK_HIGH ? GPT_INPUT_RIGHT : -1;
    return y < STICK_LOW ? GPT_INPUT_UP : y > STICK_HIGH ? GPT_INPUT_DOWN : -1;
}

static int right_stick_direction(uint8_t y)
{
    return y < STICK_LOW ? GPT_INPUT_SCROLL_UP : y > STICK_HIGH ? GPT_INPUT_SCROLL_DOWN : -1;
}

static void queue_push(gpt_input_key_t key, bool pressed, char text = '\0')
{
    const unsigned next = (queue_write + 1U) % INPUT_QUEUE_SIZE;
    if (next == queue_read)
    {
        /* ponytail: bounded input; discard the oldest event instead of allocating. */
        queue_read = (queue_read + 1U) % INPUT_QUEUE_SIZE;
    }
    queue[queue_write] = {key, pressed, text};
    queue_write = next;
}

static bool keyset_contains(const std::array<uint16_t, ps5::keyboard::kMaxKeys> &keys,
                            uint16_t usage)
{
    for (uint16_t key : keys)
        if (key == usage)
            return true;
    return false;
}

static char keyboard_character(uint16_t usage, uint32_t modifiers, uint32_t leds)
{
    const bool shifted =
        (modifiers & (ps5::keyboard::kModifierLeftShift | ps5::keyboard::kModifierRightShift)) != 0;
    if (usage >= 0x04 && usage <= 0x1d)
    {
        const bool caps = (leds & ps5::keyboard::kLedCapsLock) != 0;
        const char letter = static_cast<char>('a' + usage - 0x04);
        return shifted != caps ? static_cast<char>(letter - 'a' + 'A') : letter;
    }
    if (usage >= 0x1e && usage <= 0x27)
    {
        static const char plain[] = "1234567890";
        static const char upper[] = "!@#$%^&*()";
        return (shifted ? upper : plain)[usage - 0x1e];
    }
    switch (usage)
    {
    case 0x2c:
        return ' ';
    case 0x2d:
        return shifted ? '_' : '-';
    case 0x2e:
        return shifted ? '+' : '=';
    case 0x2f:
        return shifted ? '{' : '[';
    case 0x30:
        return shifted ? '}' : ']';
    case 0x31:
        return shifted ? '|' : '\\';
    case 0x33:
        return shifted ? ':' : ';';
    case 0x34:
        return shifted ? '"' : '\'';
    case 0x35:
        return shifted ? '~' : '`';
    case 0x36:
        return shifted ? '<' : ',';
    case 0x37:
        return shifted ? '>' : '.';
    case 0x38:
        return shifted ? '?' : '/';
    default:
        return '\0';
    }
}

static int keyboard_scroll_direction(uint16_t usage)
{
    /* Arrow, Page, Home/End, and keypad navigation HID usages. */
    if (usage == 0x52 || usage == 0x4b || usage == 0x4a || usage == 0x60 || usage == 0x61)
        return GPT_INPUT_SCROLL_UP;
    if (usage == 0x51 || usage == 0x4e || usage == 0x4d || usage == 0x5a || usage == 0x5b)
        return GPT_INPUT_SCROLL_DOWN;
    return -1;
}

static void process_keyboard_press(const ps5::keyboard::Data &sample, uint16_t usage)
{
    if (usage == 0x28)
        queue_push(GPT_INPUT_ENTER, true);
    else if (usage == 0x2a)
        queue_push(GPT_INPUT_BACKSPACE, true);
    else if (const int scroll = keyboard_scroll_direction(usage); scroll >= 0)
    {
        queue_push(static_cast<gpt_input_key_t>(scroll), true);
        char line[80];
        snprintf(line, sizeof(line), "[prosperoai] keyboard_scroll usage=%04X direction=%s\n",
                 usage, scroll == GPT_INPUT_SCROLL_UP ? "up" : "down");
        sceKernelDebugOutText(0, line);
    }
    else if (usage == 0x29)
        queue_push(GPT_INPUT_CIRCLE, true);
    else if (const char text = keyboard_character(usage, sample.modifiers, sample.leds))
        queue_push(GPT_INPUT_TEXT, true, text);
}

static void process_keyboard_sample(unsigned index, const ps5::keyboard::Data &sample)
{
    std::array<uint16_t, ps5::keyboard::kMaxKeys> current{};
    if (ps5::keyboard::is_usable(sample))
        current = sample.keycodes;
    for (unsigned at = 0; at < current.size(); ++at)
    {
        const uint16_t usage = current[at];
        bool duplicate = false;
        for (unsigned earlier = 0; earlier < at; ++earlier)
            duplicate |= current[earlier] == usage;
        if (usage && !duplicate && !keyset_contains(keyboard_previous[index], usage))
            process_keyboard_press(sample, usage);
    }
    keyboard_previous[index] = current;
}

static void poll_keyboards(void)
{
    for (unsigned index = 0; index < ps5::keyboard::kMaxOpenHandles; ++index)
    {
        const int32_t handle = keyboard_handles[index];
        if (handle < 0)
            continue;
        const int count = sceKeyboardRead(handle, keyboard_samples, ps5::keyboard::kMaxSamples);
        unsigned returned = count > 0 ? static_cast<unsigned>(count) : 0;
        if (returned > ps5::keyboard::kMaxSamples)
            returned = ps5::keyboard::kMaxSamples;
        for (unsigned sample = 0; sample < returned; ++sample)
            process_keyboard_sample(index, keyboard_samples[sample]);
    }
    int held_scroll = -1;
    for (const auto &keys : keyboard_previous)
        for (uint16_t usage : keys)
            if (keyboard_scroll_direction(usage) >= 0)
                held_scroll = keyboard_scroll_direction(usage);
    if (held_scroll != keyboard_scroll_key)
    {
        keyboard_scroll_key = held_scroll;
        keyboard_scroll_repeat_at = monotonic_milliseconds() + STICK_REPEAT_DELAY_MS;
    }
}
static void process_sample(const unsigned char *sample)
{
    uint32_t current;
    memcpy(&current, sample, sizeof(current));
    const bool neutral = sample[76] == 0 || (current & PAD_BUTTON_INTERCEPTED) != 0;
    if (neutral)
        current = 0;

    const uint32_t changed = button_state ^ current;
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
    {
        if ((changed & buttons[i].button) != 0)
        {
            queue_push(buttons[i].key, (current & buttons[i].button) != 0);
        }
    }
    button_state = current;

    int current_analog = neutral ? -1 : stick_direction(sample[4], sample[5]);
    if (current_analog != analog_key)
    {
        if (analog_key >= 0)
            queue_push((gpt_input_key_t)analog_key, false);
        analog_key = current_analog;
        if (analog_key >= 0)
        {
            queue_push((gpt_input_key_t)analog_key, true);
            analog_repeat_at = monotonic_milliseconds() + STICK_REPEAT_DELAY_MS;
        }
    }

    const int current_scroll = neutral ? -1 : right_stick_direction(sample[7]);
    if (current_scroll != scroll_key)
    {
        if (scroll_key >= 0)
            queue_push((gpt_input_key_t)scroll_key, false);
        scroll_key = current_scroll;
        if (scroll_key >= 0)
        {
            queue_push((gpt_input_key_t)scroll_key, true);
            scroll_repeat_at = monotonic_milliseconds() + STICK_REPEAT_DELAY_MS;
        }
    }
}

bool gpt_input_init(void)
{
    const int user_init = sceUserServiceInitialize(nullptr);
    owns_user_service = user_init == 0;

    int32_t user_id = -1;
    if (sceUserServiceGetInitialUser(&user_id) < 0 || scePadInit() < 0)
    {
        gpt_input_shutdown();
        return false;
    }
    pad_handle = scePadOpen(user_id, 0, 0, nullptr);
    if (pad_handle < 0)
    {
        gpt_input_shutdown();
        return false;
    }
    for (int32_t &handle : keyboard_handles)
        handle = -1;
    for (auto &keys : keyboard_previous)
        keys.fill(0);
    keyboard_module_owned = false;
    unsigned keyboard_count = 0;
    const int keyboard_module = sceSysmoduleLoadModule(ps5::keyboard::kSysmoduleId);
    if (keyboard_module >= 0)
    {
        keyboard_module_owned = keyboard_module == 0;
        if (sceKeyboardInit() >= 0)
        {
            for (unsigned index = 0; index < ps5::keyboard::kMaxOpenHandles; ++index)
            {
                keyboard_handles[index] =
                    sceKeyboardOpen(user_id, ps5::keyboard::kTypeStandard, index, nullptr);
                if (keyboard_handles[index] >= 0)
                    ++keyboard_count;
            }
        }
    }
    char keyboard_log[64];
    snprintf(keyboard_log, sizeof(keyboard_log), "[prosperoai] keyboard_handles=%u\n",
             keyboard_count);
    sceKernelDebugOutText(0, keyboard_log);
    queue_read = queue_write = 0;
    button_state = 0;
    analog_key = -1;
    analog_repeat_at = 0;
    scroll_key = -1;
    scroll_repeat_at = 0;
    keyboard_scroll_key = -1;
    keyboard_scroll_repeat_at = 0;
    return true;
}

void gpt_input_poll(void)
{
    if (pad_handle < 0)
        return;
    const int count = scePadRead(pad_handle, samples, PAD_SAMPLE_CAPACITY);
    for (int i = 0; i < count; ++i)
        process_sample(samples[i]);
    poll_keyboards();
    const uint64_t now = monotonic_milliseconds();
    if (analog_key >= 0 && now >= analog_repeat_at)
    {
        queue_push((gpt_input_key_t)analog_key, true);
        analog_repeat_at = now + STICK_REPEAT_MS;
    }
    if (scroll_key >= 0 && now >= scroll_repeat_at)
    {
        queue_push((gpt_input_key_t)scroll_key, true);
        scroll_repeat_at = now + STICK_REPEAT_MS;
    }
    if (keyboard_scroll_key >= 0 && now >= keyboard_scroll_repeat_at)
    {
        queue_push(static_cast<gpt_input_key_t>(keyboard_scroll_key), true);
        keyboard_scroll_repeat_at = now + STICK_REPEAT_MS;
    }
}

bool gpt_input_next(gpt_input_event_t *event)
{
    if (event == nullptr || queue_read == queue_write)
        return false;
    *event = queue[queue_read];
    queue_read = (queue_read + 1U) % INPUT_QUEUE_SIZE;
    return true;
}

bool gpt_input_pressed(gpt_input_key_t key)
{
    if (key < 0 || key >= GPT_INPUT_COUNT)
        return false;
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
    {
        if (buttons[i].key == key)
            return (button_state & buttons[i].button) != 0;
    }
    return false;
}

void gpt_input_shutdown(void)
{
    if (pad_handle >= 0)
    {
        scePadClose(pad_handle);
        pad_handle = -1;
    }
    for (int32_t &handle : keyboard_handles)
    {
        if (handle >= 0)
            sceKeyboardClose(handle);
        handle = -1;
    }
    if (keyboard_module_owned)
    {
        sceSysmoduleUnloadModule(ps5::keyboard::kSysmoduleId);
        keyboard_module_owned = false;
    }
    if (owns_user_service)
    {
        sceUserServiceTerminate();
        owns_user_service = false;
    }
    queue_read = queue_write = 0;
    button_state = 0;
    analog_key = -1;
    analog_repeat_at = 0;
    scroll_key = -1;
    scroll_repeat_at = 0;
    keyboard_scroll_key = -1;
    keyboard_scroll_repeat_at = 0;
}
