# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class HttpApiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix="prospero-http-")
        tmp = Path(cls.scratch.name)
        (tmp / "gpt_app.hpp").write_text("#pragma once\nclass ProsperoAiApp { public: static void SetExternalStatus(const char*) {} };\n")
        cls.binary = tmp / "http-test"
        compiler = shutil.which("clang++") or shutil.which("g++")
        if not compiler or not (ROOT / ".deps/llama.cpp/vendor/nlohmann/json.hpp").exists():
            raise unittest.SkipTest("C++ compiler and make deps required")
        subprocess.run([compiler, "-std=c++20", "-O1", "-pthread", "-ffunction-sections", "-fdata-sections",
                        "-Wl,--gc-sections", "-I"+str(tmp), "-I"+str(ROOT / "include"),
                        "-I"+str(ROOT / ".deps/llama.cpp/vendor"),
                        str(ROOT / "tests/http_api_harness.cpp"), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def request(self, path="/v1/chat/completions", body=None, key=None, headers=None):
        if body is None and path.endswith("completions"):
            body = {"model": "tiny.gguf", "messages": [{"role": "user", "content": "hello"}]}
        payload = json.dumps(body, ensure_ascii=False).encode() if body is not None else b""
        lines = [f"{'POST' if body is not None else 'GET'} {path} HTTP/1.1", "Host: test",
                 f"content-length: {len(payload)}"] + (headers or [])
        wire = subprocess.check_output([str(self.binary)] + ([key] if key else []),
                                      input="\r\n".join(lines).encode()+b"\r\n\r\n"+payload)
        head, data = wire.split(b"\r\n\r\n", 1)
        return head.decode(), data.decode()

    def test_models_and_selection(self):
        _, body = self.request("/v1/models")
        self.assertEqual([m["id"] for m in json.loads(body)["data"]], ["tiny.gguf", "second.gguf"])
        _, body = self.request(body={"model":"second.gguf", "messages":[{"role":"user","content":"x"}]})
        data = json.loads(body)
        self.assertEqual(data["model"], "second.gguf")
        self.assertEqual(data["usage"]["total_tokens"], 14)

    def test_sse_unicode_usage_and_finish(self):
        body = {"model":"tiny.gguf", "messages":[{"role":"user","content":"x"}],
                "stream":True, "max_tokens":1, "stream_options":{"include_usage":True}}
        head, wire = self.request(body=body)
        self.assertIn("text/event-stream", head)
        frames = [line[6:] for line in wire.splitlines() if line.startswith("data: ")]
        self.assertEqual(frames[-1], "[DONE]")
        chunks = [json.loads(frame) for frame in frames[:-1]]
        content = "".join(c["choices"][0]["delta"].get("content", "") for c in chunks if c["choices"])
        self.assertEqual(content, "Hello čau 😀")
        self.assertEqual(chunks[-2]["choices"][0]["finish_reason"], "length")
        self.assertEqual(chunks[-1]["choices"], [])
        self.assertEqual(chunks[-1]["usage"]["completion_tokens"], 4)

    def tools_request(self, content="lookup", stream=False):
        return {"model":"tiny.gguf", "messages":[{"role":"user","content":content}], "stream":stream,
                "tools":[{"type":"function", "function":{"name":"lookup", "parameters":{"type":"object"}}}]}

    def test_tool_calls_and_roundtrip(self):
        _, wire = self.request(body=self.tools_request())
        response = json.loads(wire)["choices"][0]
        self.assertEqual(response["finish_reason"], "tool_calls")
        call = response["message"]["tool_calls"][0]
        self.assertEqual(json.loads(call["function"]["arguments"])["query"], 'a}b"c')
        request = self.tools_request()
        request["messages"] += [response["message"], {"role":"tool", "tool_call_id":call["id"], "content":"result"}]
        _, wire = self.request(body=request)
        self.assertEqual(json.loads(wire)["choices"][0]["message"]["content"], "tool result received")

    def test_stream_tool_calls(self):
        _, wire = self.request(body=self.tools_request(stream=True))
        chunks = [json.loads(line[6:]) for line in wire.splitlines() if line.startswith("data: {")]
        calls = [c["choices"][0]["delta"]["tool_calls"] for c in chunks if "tool_calls" in c["choices"][0]["delta"]]
        self.assertEqual(calls[0][0]["index"], 0)
        self.assertEqual(chunks[-1]["choices"][0]["finish_reason"], "tool_calls")

    def test_invalid_requests_and_outputs(self):
        for body, status in [({}, "400"), ({"model":"missing", "messages":[{"role":"user","content":"x"}]}, "404"),
                             (self.tools_request("bad-tool"), "502"), (self.tools_request("bad-json"), "502")]:
            head, wire = self.request(body=body)
            self.assertIn(status, head)
            self.assertIn("message", json.loads(wire)["error"])

    def test_tool_choice_and_parallel_validation(self):
        request = self.tools_request()
        request["tool_choice"] = {"type":"function", "function":{"name":"lookup"}}
        request["parallel_tool_calls"] = False
        _, body = self.request(body=request)
        self.assertEqual(json.loads(body)["choices"][0]["finish_reason"], "tool_calls")
        request["tool_choice"]["function"]["name"] = "missing"
        head, _ = self.request(body=request)
        self.assertIn("400", head)
        request = self.tools_request()
        request["tool_choice"] = "none"
        _, body = self.request(body=request)
        self.assertNotIn("tool_calls", json.loads(body)["choices"][0]["message"])

    def test_bearer_auth_on_all_routes(self):
        for route in ["/v1/models", "/api/tags", "/"]:
            head, _ = self.request(route, key="secret")
            self.assertIn("401", head)
        head, _ = self.request("/v1/models", key="secret", headers=["authorization: Bearer secret"])
        self.assertIn("200", head)

    def test_nested_text_parts_and_escaped_content(self):
        text = 'quoted " and } braces čau 😀'
        body = {"model":"tiny.gguf", "messages":[{"role":"user", "content":[{"type":"text","text":text}]},
                                                  {"role":"user", "content":"echo"}]}
        _, wire = self.request(body=body)
        self.assertEqual(json.loads(wire)["choices"][0]["message"]["content"], text)

    def test_request_limit(self):
        head, wire = self.request(body={"padding":"x"*270000})
        self.assertIn("413", head)
        self.assertEqual(json.loads(wire)["error"]["code"], "request_too_large")

if __name__ == "__main__":
    unittest.main()
