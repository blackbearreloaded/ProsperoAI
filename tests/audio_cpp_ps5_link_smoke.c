#include "audiocpp.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern int sceKernelDebugOutText(int channel, const char *text);
extern int sceKernelUsleep(uint32_t microseconds);

#ifndef SA3_SMOKE_STAGE
#define SA3_SMOKE_STAGE 4
#endif

#define MODEL_PATH "/app0/models/stable-audio-3-small-sfx-q8_0.gguf"
#define OUTPUT_PATH "/download0/sa3-vulkan-smoke.wav"

static void write_u16(FILE *file, uint16_t value)
{
    fputc((int)(value & 0xff), file);
    fputc((int)((value >> 8) & 0xff), file);
}

static void write_u32(FILE *file, uint32_t value)
{
    write_u16(file, (uint16_t)(value & 0xffff));
    write_u16(file, (uint16_t)(value >> 16));
}

static int write_wav(const char *path, const float *samples, size_t frames, int sample_rate,
                     int channels)
{
    const uint64_t sample_count = (uint64_t)frames * (uint64_t)channels;
    const uint64_t data_bytes = sample_count * 2;
    FILE *file;
    size_t i;

    if (sample_count == 0 || data_bytes > UINT32_MAX - 36 || sample_rate <= 0 || channels <= 0)
        return 0;
    file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    fwrite("RIFF", 1, 4, file);
    write_u32(file, (uint32_t)(36 + data_bytes));
    fwrite("WAVEfmt ", 1, 8, file);
    write_u32(file, 16);
    write_u16(file, 1);
    write_u16(file, (uint16_t)channels);
    write_u32(file, (uint32_t)sample_rate);
    write_u32(file, (uint32_t)sample_rate * (uint32_t)channels * 2);
    write_u16(file, (uint16_t)(channels * 2));
    write_u16(file, 16);
    fwrite("data", 1, 4, file);
    write_u32(file, (uint32_t)data_bytes);
    for (i = 0; i < (size_t)sample_count; ++i)
    {
        float value = samples[i];
        int32_t pcm;
        if (value > 1.0f)
            value = 1.0f;
        if (value < -1.0f)
            value = -1.0f;
        pcm = (int32_t)(value * (value < 0.0f ? 32768.0f : 32767.0f));
        write_u16(file, (uint16_t)(int16_t)pcm);
    }
    if (fclose(file) != 0)
        return 0;
    return 1;
}

static void hold_title(const char *message)
{
    sceKernelDebugOutText(0, message);
    for (;;)
        sceKernelUsleep(100000);
}

int main(void)
{
    audiocpp_registry *registry = NULL;
    audiocpp_model *model = NULL;
    audiocpp_session *session = NULL;
    audiocpp_options *session_options = NULL;
    audiocpp_request *request = NULL;
    audiocpp_result *result = NULL;
    audiocpp_model_config model_config = {"stable_audio", NULL, NULL, NULL};
    audiocpp_backend_config backend_config = {"vulkan", 0, 4};
    const float *samples = NULL;
    size_t frames = 0;
    int sample_rate = 0, channels = 0;
    char message[256];
    audiocpp_status status;
    size_t i;
    double energy = 0.0;
    float peak = 0.0f;
    int exit_code = 1;

    if ((audiocpp_abi_version() >> 16) != AUDIOCPP_ABI_VERSION_MAJOR)
    {
        sceKernelDebugOutText(0, "[SA3_VK] ABI version mismatch\n");
        goto cleanup;
    }
    status = audiocpp_registry_create(NULL, &registry);
    if (status != AUDIOCPP_OK)
    {
        snprintf(message, sizeof(message), "[SA3_VK] registry: %.190s\n", audiocpp_last_error());
        sceKernelDebugOutText(0, message);
        goto cleanup;
    }
    if (SA3_SMOKE_STAGE <= 1)
        hold_title("[SA3_VK] stage 1 ready: registry only; safe to close\n");

    sceKernelDebugOutText(0, "[SA3_VK] loading Stable Audio 3 model\n");
    status = audiocpp_model_load(registry, MODEL_PATH, &model_config, NULL, &model);
    if (status != AUDIOCPP_OK)
    {
        snprintf(message, sizeof(message), "[SA3_VK] model_load: %.180s\n", audiocpp_last_error());
        sceKernelDebugOutText(0, message);
        goto cleanup;
    }
    if (SA3_SMOKE_STAGE <= 2)
        hold_title("[SA3_VK] stage 2 ready: model loaded; safe to close\n");

    session_options = audiocpp_options_create();
    if (session_options == NULL ||
        audiocpp_options_set(session_options, "stable_audio.mem_saver", "true") != AUDIOCPP_OK)
    {
        sceKernelDebugOutText(0, "[SA3_VK] could not configure session\n");
        goto cleanup;
    }
    status = audiocpp_session_create(model, "gen", "offline", &backend_config, session_options,
                                     &session);
    if (status != AUDIOCPP_OK)
    {
        snprintf(message, sizeof(message), "[SA3_VK] session_create: %.180s\n",
                 audiocpp_last_error());
        sceKernelDebugOutText(0, message);
        goto cleanup;
    }
    if (SA3_SMOKE_STAGE <= 3)
        hold_title("[SA3_VK] stage 3 ready: Vulkan session created; safe to close\n");

    request = audiocpp_request_create();
    if (request == NULL ||
        audiocpp_request_set_text(request, "short clean bell chime", "en") != AUDIOCPP_OK ||
        audiocpp_request_set_option(request, "duration_sec", "1") != AUDIOCPP_OK ||
        audiocpp_request_set_option(request, "num_inference_steps", "1") != AUDIOCPP_OK ||
        audiocpp_request_set_option(request, "seed", "1") != AUDIOCPP_OK)
    {
        sceKernelDebugOutText(0, "[SA3_VK] request setup failed\n");
        goto cleanup;
    }
    sceKernelDebugOutText(0, "[SA3_VK] generating on Vulkan device 0\n");
    status = audiocpp_session_run(session, request, &result);
    if (status != AUDIOCPP_OK)
    {
        snprintf(message, sizeof(message), "[SA3_VK] generate: %.180s\n", audiocpp_last_error());
        sceKernelDebugOutText(0, message);
        goto cleanup;
    }
    status = audiocpp_result_audio(result, &samples, &frames, &sample_rate, &channels);
    if (status != AUDIOCPP_OK || samples == NULL || frames == 0 || sample_rate <= 0 ||
        channels <= 0)
    {
        sceKernelDebugOutText(0, "[SA3_VK] result has no valid PCM audio\n");
        goto cleanup;
    }
    for (i = 0; i < frames * (size_t)channels; ++i)
    {
        const float absolute = fabsf(samples[i]);
        energy += (double)samples[i] * samples[i];
        if (absolute > peak)
            peak = absolute;
    }
    if (energy < 1e-8 || peak < 1e-4f ||
        !write_wav(OUTPUT_PATH, samples, frames, sample_rate, channels))
    {
        sceKernelDebugOutText(0, "[SA3_VK] output silent or WAV write failed\n");
        goto cleanup;
    }
    snprintf(message, sizeof(message),
             "[SA3_VK] AUDIO_OK rate=%d channels=%d frames=%u peak=%.4f\n", sample_rate, channels,
             (unsigned int)frames, (double)peak);
    sceKernelDebugOutText(0, message);
    exit_code = 0;

cleanup:
    audiocpp_result_free(result);
    audiocpp_request_free(request);
    audiocpp_session_free(session);
    audiocpp_options_free(session_options);
    audiocpp_model_free(model);
    audiocpp_registry_free(registry);
    if (exit_code == 0)
        hold_title("[SA3_VK] stage 4 ready: WAV written and runtime released; safe to close\n");
    else
        sceKernelDebugOutText(0, "[SA3_VK] test failed; safe to close\n");
    for (;;)
        sceKernelUsleep(100000);
}
