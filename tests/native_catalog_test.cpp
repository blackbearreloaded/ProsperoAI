// SPDX-License-Identifier: GPL-3.0-or-later
// Real Vulkan GGUF discovery; model selection must not initialize inference.
#include "gpt_runtime.hpp"
#include "model_paths.hpp"
#include "llama.h"
#include <cassert>
#include <cstring>
extern "C"
{
    int sceKernelDebugOutText(int, const char *)
    {
        return 0;
    }
    void llama_free(llama_context *context)
    {
        assert(context == nullptr);
    }
    void llama_model_free(llama_model *model)
    {
        assert(model == nullptr);
    }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    prospero::model_root_slot() = argv[1];
    assert(gpt_runtime_model_count() == 24);
    assert(std::strcmp(gpt_runtime_model_id(0), "variant-00.gguf") == 0);
    assert(std::strcmp(gpt_runtime_model_id(23), "variant-23.gguf") == 0);
    assert(gpt_runtime_select_model(23));
    assert(!gpt_runtime_select_model(24));
    assert(gpt_runtime_selected_model() == 23);
}
