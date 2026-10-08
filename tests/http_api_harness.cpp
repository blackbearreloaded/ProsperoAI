// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual HTTP handlers with fragmented I/O and deterministic inference.
#define PS5_LLAMA_VULKAN
#include "../src/http_server.cpp"
#include <iostream>
#include <iterator>
static std::string input, wire;
static std::size_t position;
static unsigned current_model;
static int fake_send(int, const void *data, unsigned long size, int) {
    size = std::min<unsigned long>(size, 13); // exercise partial writes
    wire.append(static_cast<const char *>(data), size); return size;
}
static int fake_recv(int, void *data, unsigned long size, int) {
    size = std::min<unsigned long>({size, 7, input.size()-position});
    std::memcpy(data, input.data()+position, size); position += size; return size;
}
static int fake_close(int) { return 0; }
unsigned gpt_runtime_model_count() { return 2; }
unsigned gpt_runtime_selected_model() { return current_model; }
const char *gpt_runtime_model_id(unsigned i) { return i == 0 ? "tiny.gguf" : "second.gguf"; }
const char *gpt_runtime_model_purpose(unsigned) { return "text-to-text"; }
bool gpt_runtime_select_model(unsigned i) { current_model = i; return i < 2; }
int gpt_runtime_generate(const gpt_runtime_message_t *messages, unsigned count,
    const gpt_runtime_settings_t &settings, char *out, std::size_t capacity,
    gpt_runtime_stats_t *stats, gpt_runtime_progress_fn progress) {
    if (settings.model_id) current_model = std::strcmp(settings.model_id, "second.gguf") == 0 ? 1 : 0;
    std::string last = messages[count-1].content;
    if (last == "fail") { std::snprintf(out, capacity, "Context full"); return 1; }
    std::string text = settings.grammar ?
        last == "bad-tool" ? "{\"tool_calls\":[{\"name\":\"unknown\",\"arguments\":{}}]}" :
        last == "bad-json" ? "{" :
        last.find("Tool result for") == 0 ? "{\"content\":\"tool result received\"}" :
        "{\"tool_calls\":[{\"name\":\"lookup\",\"arguments\":{\"query\":\"a}b\\\"c\"}}]}" :
        last == "echo" ? messages[0].content : "Hello čau 😀";
    std::snprintf(out, capacity, "%s", text.c_str());
    *stats = {}; stats->prompt_tokens = 10; stats->generated_tokens = 4;
    stats->output_limit_reached = settings.max_output_tokens == 1;
    if (progress) {
        for (std::size_t i = 1; i <= text.size(); ++i) {
            auto prefix = text.substr(0, i); progress(prefix.c_str());
        }
    }
    return 0;
}
int main(int argc, char **argv) {
    if (argc > 1) api_key = argv[1];
    input.assign(std::istreambuf_iterator<char>(std::cin), {});
    NetApi api{}; api.send = fake_send; api.recv = fake_recv; api.socket_close = fake_close;
    handle_connection(api, 1);
    std::cout << wire;
}
