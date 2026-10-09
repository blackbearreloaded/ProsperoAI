// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/gpt_input.cpp"
#include <cassert>

extern "C" uint64_t SDL_GetTicks64(void)
{
    return 0;
}

int main()
{
    unsigned char sample[PAD_SAMPLE_SIZE]{};
    sample[76] = 1;
    sample[4] = sample[5] = sample[6] = sample[7] = 128;
    gpt_input_event_t event{};
    const auto expect = [&](gpt_input_key_t key, bool pressed)
    {
        assert(gpt_input_next(&event));
        assert(event.key == key && event.pressed == pressed);
        assert(!gpt_input_next(&event));
    };
    for (const auto [offset, key] : {std::pair{8, GPT_INPUT_L2}, std::pair{9, GPT_INPUT_R2}})
    {
        sample[offset] = 127;
        process_sample(sample);
        assert(!gpt_input_next(&event));
        sample[offset] = 128;
        process_sample(sample);
        expect(key, true);
        sample[offset] = 255;
        process_sample(sample);
        assert(!gpt_input_next(&event));
        sample[offset] = 0;
        process_sample(sample);
        expect(key, false);
    }
    uint32_t bits = 0x200;
    memcpy(sample, &bits, sizeof(bits));
    process_sample(sample);
    expect(GPT_INPUT_R2, true);
    sample[76] = 0;
    process_sample(sample);
    expect(GPT_INPUT_R2, false);
}
