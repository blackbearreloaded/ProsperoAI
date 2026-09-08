#include "ps5_agc_backend.h"
#include "stable-diffusion.h"

#include "ggml-backend.h"

#include <cstddef>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C"
{
    int sceKernelDebugOutText(int, const char *);
    std::uint64_t sceKernelGetProcessTime(void);
    bool ps5SdReleaseDirectArenaIfEmpty(void);
}

namespace
{

sd_ctx_t *context;
char context_root[192];
void (*progress_callback)(const char *);
bool backend_registered;

void log_line(const char *format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    sceKernelDebugOutText(0, line);
}

void sd_log(sd_log_level_t level, const char *message, void *)
{
    if (level >= SD_LOG_WARN)
        log_line("[prosperoai:sd] %s", message ? message : "");
}

void sd_progress(int step, int steps, float, void *)
{
    if (!progress_callback)
        return;
    char status[96];
    std::snprintf(status, sizeof(status), "Generating image on the GPU  ·  %d/%d", step, steps);
    progress_callback(status);
}

bool write_ppm(const char *path, const sd_image_t &image)
{
    if (!image.data || image.width == 0 || image.height == 0 || image.channel < 3)
        return false;
    std::FILE *file = std::fopen(path, "wb");
    if (!file)
        return false;
    std::fprintf(file, "P6\n%u %u\n255\n", image.width, image.height);
    const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
    bool ok = true;
    if (image.channel == 3)
    {
        ok = std::fwrite(image.data, 3, pixels, file) == pixels;
    }
    else
    {
        for (std::size_t pixel = 0; pixel < pixels && ok; ++pixel)
            ok = std::fwrite(image.data + pixel * image.channel, 1, 3, file) == 3;
    }
    return std::fclose(file) == 0 && ok;
}

bool write_tga(const char *path, const sd_image_t &image)
{
    if (!image.data || image.width != 512 || image.height != 512 || image.channel < 3)
        return false;
    std::FILE *file = std::fopen(path, "wb");
    if (!file)
        return false;
    unsigned char header[18]{};
    header[2] = 2;
    header[12] = static_cast<unsigned char>(image.width);
    header[13] = static_cast<unsigned char>(image.width >> 8);
    header[14] = static_cast<unsigned char>(image.height);
    header[15] = static_cast<unsigned char>(image.height >> 8);
    header[16] = 32;
    header[17] = 0x28;
    bool ok = std::fwrite(header, 1, sizeof(header), file) == sizeof(header);
    unsigned char row[512 * 4];
    for (unsigned y = 0; y < image.height && ok; ++y)
    {
        for (unsigned x = 0; x < image.width; ++x)
        {
            const unsigned char *source =
                image.data + (static_cast<std::size_t>(y) * image.width + x) * image.channel;
            row[x * 4 + 0] = source[2];
            row[x * 4 + 1] = source[1];
            row[x * 4 + 2] = source[0];
            row[x * 4 + 3] = 255;
        }
        ok = std::fwrite(row, 4, image.width, file) == image.width;
    }
    return std::fclose(file) == 0 && ok;
}

void release_context()
{
    if (context)
    {
        free_sd_ctx(context);
        context = nullptr;
        context_root[0] = '\0';
    }
    const bool scratch_released = ps5_agc_backend_release_scratch() == 0;
    const bool arena_released = ps5SdReleaseDirectArenaIfEmpty();
    log_line("[prosperoai] sd_context_released=1 scratch_released=%d "
             "arena_released=%d\n",
             scratch_released, arena_released);
}

bool ensure_context(const char *root)
{
    if (context && std::strcmp(context_root, root) == 0)
        return true;
    release_context();
    if (!backend_registered)
    {
        ggml_backend_register(ggml_backend_ps5agc_reg());
        backend_registered = true;
    }
    sd_set_log_callback(sd_log, nullptr);
    sd_set_progress_callback(sd_progress, nullptr);

    sd_ctx_params_t parameters;
    sd_ctx_params_init(&parameters);
    parameters.model_path = root;
    parameters.n_threads = 1;
    parameters.wtype = SD_TYPE_F16;
    parameters.rng_type = CUDA_RNG;
    parameters.prediction = EPS_PRED;
    parameters.enable_mmap = false;
    parameters.eager_load = false;
    parameters.diffusion_conv_direct = false;
    parameters.vae_conv_direct = false;
    parameters.backend = "CPU,diffusion=PS5AGC0,vae=PS5AGC0";
    parameters.params_backend = "PS5AGC0";
    context = new_sd_ctx(&parameters);
    if (!context || !sd_ctx_supports_image_generation(context))
    {
        release_context();
        return false;
    }
    std::snprintf(context_root, sizeof(context_root), "%s", root);
    return true;
}

} // namespace

extern "C" void ps5_sd_shutdown()
{
    release_context();
}

extern "C" int ps5_sd_generate(const char *root, const char *prompt, char *response,
                               std::size_t response_capacity, std::uint64_t *elapsed_microseconds,
                               void (*progress)(const char *))
{
    constexpr char oracle_output[] = "/download0/prosperoai-image.ppm";
    static unsigned image_serial;
    char display_output[64];
    std::snprintf(display_output, sizeof(display_output), "/download0/prosperoai-image-%u.tga",
                  image_serial++ % 4);
    if (!root || !*root || !prompt || !*prompt || !response || response_capacity == 0)
        return 1;
    progress_callback = progress;
    if (progress_callback)
        progress_callback("Loading SD-Turbo");
    std::remove(oracle_output);
    std::remove(display_output);
    const std::uint64_t start = sceKernelGetProcessTime();
    if (!ensure_context(root))
    {
        std::snprintf(response, response_capacity, "Could not load the SD-Turbo model.");
        return 2;
    }

    sd_img_gen_params_t parameters;
    sd_img_gen_params_init(&parameters);
    parameters.prompt = prompt;
    parameters.negative_prompt = "";
    parameters.width = 512;
    parameters.height = 512;
    parameters.seed = 42;
    parameters.batch_count = 1;
    parameters.sample_params.sample_steps = 1;
    parameters.sample_params.guidance.txt_cfg = 1.0f;

    sd_image_t *images = nullptr;
    int image_count = 0;
    const bool generated = generate_image(context, &parameters, &images, &image_count);
    const bool written = generated && image_count == 1 && images &&
                         write_ppm(oracle_output, images[0]) &&
                         write_tga(display_output, images[0]);
    free_sd_images(images, image_count);
    ps5_agc_backend_print_stats();
    const std::uint64_t elapsed = sceKernelGetProcessTime() - start;
    if (elapsed_microseconds)
        *elapsed_microseconds = elapsed;
    progress_callback = nullptr;
    if (!written)
    {
        std::snprintf(response, response_capacity,
                      "SD-Turbo generation failed. Check the system log.");
        return 3;
    }
    std::snprintf(response, response_capacity, "Image generated locally.\n%s", display_output);
    sceKernelDebugOutText(0, "[prosperoai] sd_complete=1\n");
    return 0;
}
