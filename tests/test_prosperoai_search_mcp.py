import json
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "tools" / "prosperoai_search_mcp.py"


class ProsperoSearchMcpTests(unittest.TestCase):
    def run_bridge(self, messages):
        wire = "".join(json.dumps(message) + "\n" for message in messages)
        result = subprocess.run([sys.executable, str(BRIDGE)], input=wire, text=True,
                                capture_output=True, check=True, timeout=5)
        return [json.loads(line) for line in result.stdout.splitlines() if line]

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


if __name__ == "__main__":
    unittest.main()
