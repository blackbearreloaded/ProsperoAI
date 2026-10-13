#include "engine/framework/assets/tensor_source.h"
#include "audiocpp.h"
#include <ggml.h>
#include <gguf.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <sys/resource.h>
#include <unistd.h>

static void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}

static void fixture(const std::filesystem::path &path, int64_t elements)
{
    auto *ctx = ggml_init({1024 * 1024, nullptr, true});
    require(ctx != nullptr, "ggml fixture context");
    auto *tensor = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, elements);
    ggml_set_name(tensor, "weight");
    auto *gguf = gguf_init_empty();
    gguf_add_tensor(gguf, tensor);
    require(gguf_write_to_file(gguf, path.c_str(), true), "write fixture metadata");
    FILE *file = std::fopen(path.c_str(), "ab");
    require(file != nullptr, "open fixture");
    if (elements == 4)
    {
        const float values[] = {1.0f, -2.0f, 3.0f, 4.0f};
        require(std::fwrite(values, sizeof(values), 1, file) == 1, "fixture values");
    }
    else
    {
        const auto offset = std::filesystem::file_size(path);
        require(ftruncate(fileno(file), offset + elements * sizeof(float)) == 0, "sparse fixture");
    }
    std::fclose(file);
    gguf_free(gguf);
    ggml_free(ctx);
}

int main(int argc, char **argv)
{
    try
    {
        require(argc == 3, "usage: test fixture-directory real-model.gguf");
        const auto dir = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(dir);
        const auto small = dir / "small.gguf";
        fixture(small, 4);
        auto source = engine::assets::open_tensor_source(small);
        const float expected[] = {1.0f, -2.0f, 3.0f, 4.0f};
        auto data = source->require_tensor_data("weight");
        require(data.bytes.size() == sizeof(expected) &&
                    std::memcmp(data.bytes.data(), expected, sizeof(expected)) == 0,
                "tensor read mismatch");
        source->release_storage();
        require(source->require_tensor_data("weight").bytes == data.bytes, "read after release");
        std::filesystem::resize_file(small, std::filesystem::file_size(small) - 1);
        bool rejected = false;
        try
        {
            (void)source->require_tensor_data("weight");
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        require(rejected, "short read must fail");
        const auto large = dir / "large.gguf";
        fixture(large, 600ll * 1024 * 1024 / sizeof(float));
        auto large_source = engine::assets::open_tensor_source(large);
        rejected = false;
        try
        {
            (void)large_source->require_tensor_data("weight");
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        require(rejected, "oversized tensor must fail before allocation");
        audiocpp_registry *registry = nullptr;
        audiocpp_model *model = nullptr;
        const audiocpp_model_config config = {"stable_audio", nullptr, nullptr, nullptr};
        require(audiocpp_registry_create(nullptr, &registry) == AUDIOCPP_OK, "registry");
        if (audiocpp_model_load(registry, argv[2], &config, nullptr, &model) != AUDIOCPP_OK)
            throw std::runtime_error(audiocpp_last_error());
        audiocpp_model_free(model);
        audiocpp_registry_free(registry);
        struct rusage usage
        {
        };
        getrusage(RUSAGE_SELF, &usage);
        require(usage.ru_maxrss < 128 * 1024, "metadata-only model load exceeded 128 MiB RSS");
        std::printf("PASS tensor read/release, truncation, allocation limit, SA3 model load; peak "
                    "RSS=%ld KiB\n",
                    usage.ru_maxrss);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
