import json
import os
import subprocess
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "tools" / "prosperoai_search_mcp.py"


class StubConsole(BaseHTTPRequestHandler):
    """Answers the two routes the bridge calls, so the whole MCP round trip can be
    exercised without a console."""

    seen = []

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        request = json.loads(self.rfile.read(length) or b"{}")
        StubConsole.seen.append((self.path, request))
        if self.path == "/api/search":
            payload = {"query": request.get("query"), "cached": False,
                       "data": [{"title": "VMA", "url": "https://github.com/x/vma",
                                 "snippet": "allocator", "source": "code.github", "score": 1.0}],
                       "providers": [{"id": "code.github", "outcome": "ok", "http_status": 200,
                                      "results": 1, "detail": ""}]}
        elif self.path == "/api/fetch":
            payload = {"url": request.get("url"), "text": "Readable page text", "bytes": 18}
        else:
            self.send_error(404)
            return
        body = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


class ProsperoSearchMcpTests(unittest.TestCase):
    def run_bridge(self, messages, base_url=None):
        wire = "".join(json.dumps(message) + "\n" for message in messages)
        environment = dict(os.environ)
        if base_url:
            environment["PROSPEROAI_BASE_URL"] = base_url
        result = subprocess.run([sys.executable, str(BRIDGE)], input=wire, text=True,
                                capture_output=True, check=True, timeout=15, env=environment)
        return [json.loads(line) for line in result.stdout.splitlines() if line]

    def console(self):
        StubConsole.seen = []
        server = ThreadingHTTPServer(("127.0.0.1", 0), StubConsole)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.shutdown)
        return f"http://127.0.0.1:{server.server_address[1]}"

    def test_initialize_and_advertise_web_search(self):
        replies = self.run_bridge([
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2024-11-05"}},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
        ])
        self.assertEqual(replies[0]["result"]["serverInfo"]["name"], "prosperoai-search")
        tools = replies[1]["result"]["tools"]
        self.assertEqual([tool["name"] for tool in tools], ["web_search", "fetch_page"])
        search = tools[0]["inputSchema"]["properties"]
        self.assertEqual(search["limit"]["maximum"], 25)
        # Scopes are what makes one query reach many sites, so the schema must offer them.
        self.assertIn("web", search["scopes"]["items"]["enum"])
        self.assertIn("code", search["scopes"]["items"]["enum"])
        self.assertEqual(tools[1]["inputSchema"]["required"], ["url"])

    def test_rejects_bad_arguments_without_contacting_ps5(self):
        replies = self.run_bridge([{
            "jsonrpc": "2.0", "id": 7, "method": "tools/call",
            "params": {"name": "web_search", "arguments": {"query": "x"}},
        }])
        self.assertTrue(replies[0]["result"]["isError"])
        self.assertIn("2–256", replies[0]["result"]["content"][0]["text"])

    def test_search_and_fetch_round_trip_through_the_bridge(self):
        base_url = self.console()
        replies = self.run_bridge([
            {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
             "params": {"name": "web_search",
                        "arguments": {"query": "vulkan memory", "limit": 5, "scopes": ["code"]}}},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/call",
             "params": {"name": "fetch_page", "arguments": {"url": "https://example.org/a"}}},
        ], base_url=base_url)
        self.assertFalse(replies[0]["result"]["isError"])
        found = replies[0]["result"]["structuredContent"]["data"][0]
        self.assertEqual(found["url"], "https://github.com/x/vma")
        self.assertEqual(found["source"], "code.github")
        self.assertFalse(replies[1]["result"]["isError"])
        self.assertIn("Readable page text", replies[1]["result"]["content"][0]["text"])
        routes = [path for path, _ in StubConsole.seen]
        self.assertEqual(routes, ["/api/search", "/api/fetch"])
        # The scopes the model asked for must reach the console unchanged.
        self.assertEqual(StubConsole.seen[0][1]["scopes"], ["code"])
        self.assertEqual(StubConsole.seen[1][1]["url"], "https://example.org/a")

    def test_bad_scope_or_address_never_reaches_the_console(self):
        base_url = self.console()
        replies = self.run_bridge([
            {"jsonrpc": "2.0", "id": 3, "method": "tools/call",
             "params": {"name": "web_search", "arguments": {"query": "vulkan", "scopes": ["nope"]}}},
            {"jsonrpc": "2.0", "id": 4, "method": "tools/call",
             "params": {"name": "fetch_page", "arguments": {"url": "ftp://example.org"}}},
            {"jsonrpc": "2.0", "id": 5, "method": "tools/call",
             "params": {"name": "unknown_tool", "arguments": {}}},
        ], base_url=base_url)
        for reply in replies:
            self.assertTrue(reply["result"]["isError"])
        self.assertEqual(StubConsole.seen, [])


if __name__ == "__main__":
    unittest.main()
