#include "ps5_agc_backend.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C"
{
    int sceKernelDebugOutText(int, const char *);
    std::uint64_t sceKernelGetProcessTime(void);
}

int stable_audio_model_main(int argc, char **argv);

#ifndef PS5_STABLE_AUDIO_LATENT_LENGTH
#define PS5_STABLE_AUDIO_LATENT_LENGTH 256
#endif

extern "C" int ps5_stable_audio_generate(const char *prompt, char *response,
                                         std::size_t response_capacity,
                                         std::uint64_t *elapsed_microseconds,
                                         void (*progress)(const char *))
{
    constexpr char generated[] = "/download0/stable-audio-seed.wav";
    constexpr char output[] = "/download0/prosperoai-audio.wav";
    if (!prompt || !*prompt || !response || response_capacity == 0)
        return 1;
    if (progress)
    {
        char status[64];
        std::snprintf(status, sizeof(status), "Generating %.1f seconds of audio on the GPU",
                      PS5_STABLE_AUDIO_LATENT_LENGTH * 2048.0 / 44100.0);
        progress(status);
    }
    if (ps5_agc_backend_reserve() != 0)
    {
        std::snprintf(response, response_capacity,
                      "Could not reserve PS5 GPU memory for Stable Audio.");
        return 2;
    }

    std::remove(generated);
    std::remove(output);
    setenv("NNOPT_PROG_CACHE", "0", 1);
    setenv("NNOPT_VERIFY_WEIGHTS", "0", 1);
    setenv("NNOPT_SPLIT_M", "0", 1);
    setenv("NNOPT_LN_WG", "0", 1);
    setenv("NNOPT_QK_WG", "0", 1);
    setenv("NNOPT_ATTN_FUSED", "0", 1);
    setenv("NNOPT_VEC_KERNELS", "0", 1);
    setenv("NNOPT_CONV_T4X4", "0", 1);
    unsetenv("NNOPT_COND_FROM_ASSETS");
    setenv("NNOPT_DENOISE_GPU", "1", 1);

    char executable[] = "stable_audio_ps5";
    char length_flag[] = "--latent-len";
    char length[4];
    std::snprintf(length, sizeof(length), "%u", PS5_STABLE_AUDIO_LATENT_LENGTH);
    char *arguments[] = {executable, length_flag, length, const_cast<char *>(prompt), nullptr};
    const std::uint64_t start = sceKernelGetProcessTime();
    const int result = stable_audio_model_main(4, arguments);
    const std::uint64_t elapsed = sceKernelGetProcessTime() - start;
    if (elapsed_microseconds)
        *elapsed_microseconds = elapsed;
    ps5_agc_backend_print_stats();
    const int scratch_result = ps5_agc_backend_release_scratch();
    char scratch_log[80];
    std::snprintf(scratch_log, sizeof(scratch_log),
                  "[prosperoai] stable_audio_scratch_released=%u rc=%08X\n",
                  scratch_result == 0 ? 1U : 0U, static_cast<unsigned>(scratch_result));
    sceKernelDebugOutText(0, scratch_log);

    const bool renamed = result == 0 && std::rename(generated, output) == 0;
    std::remove("/download0/stable-audio-f32.bin");
    std::remove("/download0/stable-audio-t5.bin");

    std::FILE *file = renamed ? std::fopen(output, "rb") : nullptr;
    long output_bytes = 0;
    if (file)
    {
        std::fseek(file, 0, SEEK_END);
        output_bytes = std::ftell(file);
        std::fclose(file);
    }
    constexpr long expected_bytes = 44L + PS5_STABLE_AUDIO_LATENT_LENGTH * 2048L * 2L * 2L;
    if (result != 0 || output_bytes != expected_bytes)
    {
        std::snprintf(response, response_capacity,
                      "Stable Audio output was incomplete. Check the system log.");
        return result ? result : 3;
    }
    std::snprintf(response, response_capacity, "Audio generated locally at %s", output);
    sceKernelDebugOutText(0, "[prosperoai] stable_audio_complete=1\n");
    return 0;
}
