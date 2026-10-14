#!/usr/bin/env python3
"""OpenCode MCP bridge to ProsperoAI's PS5 search endpoints: a multi-site search
and a reader that turns one result into plain text."""

import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request


def send(message):
    sys.stdout.write(json.dumps(message, ensure_ascii=False, separators=(",", ":")) + "\n")
    sys.stdout.flush()


SCOPES = ("web", "code", "video", "reference", "qa", "package", "paper", "music", "image")


def ps5_url(route="/api/search"):
    base = os.environ.get("PROSPEROAI_BASE_URL", "").rstrip("/")
    parsed = urllib.parse.urlparse(base)
    if parsed.scheme not in ("http", "https") or not parsed.netloc or parsed.username or parsed.password:
        raise ValueError("Set PROSPEROAI_BASE_URL to the trusted PS5 app origin.")
    if parsed.path or parsed.query or parsed.fragment:
        raise ValueError("PROSPEROAI_BASE_URL must be only the scheme and host, with optional port.")
    return base + route


def post(route, body):
    headers = {"Content-Type": "application/json"}
    key = os.environ.get("PROSPEROAI_API_KEY")
    if key:
        headers["Authorization"] = "Bearer " + key
    request = urllib.request.Request(ps5_url(route), data=json.dumps(body).encode(),
                                     headers=headers, method="POST")
    with urllib.request.urlopen(request, timeout=35) as response:
        payload = json.loads(response.read(256 * 1024 + 1))
    if len(json.dumps(payload).encode()) > 256 * 1024:
        raise ValueError("PS5 response exceeded 256 KiB")
    return payload


def call_search(arguments):
    query = arguments.get("query") if isinstance(arguments, dict) else None
    limit = arguments.get("limit", 5) if isinstance(arguments, dict) else 5
    scopes = arguments.get("scopes") if isinstance(arguments, dict) else None
    if not isinstance(query, str) or not 2 <= len(query) <= 256 or any(ord(c) < 32 for c in query):
        raise ValueError("query must contain 2–256 printable characters")
    if not isinstance(limit, int) or isinstance(limit, bool) or not 1 <= limit <= 25:
        raise ValueError("limit must be between 1 and 25")
    body = {"query": query, "limit": limit}
    if scopes is not None:
        if not isinstance(scopes, list) or not scopes or any(scope not in SCOPES for scope in scopes):
            raise ValueError("scopes must name one or more of: " + ", ".join(SCOPES))
        body["scopes"] = scopes
    return post("/api/search", body)


def call_fetch(arguments):
    url = arguments.get("url") if isinstance(arguments, dict) else None
    limit = arguments.get("limit", 16384) if isinstance(arguments, dict) else 16384
    if not isinstance(url, str) or not url.startswith(("http://", "https://")) or len(url) > 2048:
        raise ValueError("url must be an http or https address of at most 2048 characters")
    if not isinstance(limit, int) or isinstance(limit, bool) or not 256 <= limit <= 131072:
        raise ValueError("limit must be between 256 and 131072 bytes")
    return post("/api/fetch", {"url": url, "limit": limit})


def handle(message):
    method = message.get("method")
    request_id = message.get("id")
    if method == "notifications/initialized":
        return
    if method == "initialize":
        send({"jsonrpc": "2.0", "id": request_id,
              "result": {"protocolVersion": message.get("params", {}).get("protocolVersion", "2024-11-05"),
                         "capabilities": {"tools": {}},
                         "serverInfo": {"name": "prosperoai-search", "version": "2.0.0"}}})
    elif method == "ping":
        send({"jsonrpc": "2.0", "id": request_id, "result": {}})
    elif method == "tools/list":
        send({"jsonrpc": "2.0", "id": request_id, "result": {"tools": [{
            "name": "web_search",
            "description": "Search many sites from the PS5 at once: general web, code hosting, question sites, reference works, package registries, papers, video, music and images. Returns titles, snippets, source URLs and which provider found each result.",
            "inputSchema": {"type": "object", "properties": {
                "query": {"type": "string", "minLength": 2, "maxLength": 256},
                "limit": {"type": "integer", "minimum": 1, "maximum": 25, "default": 5},
                "scopes": {"type": "array", "items": {"type": "string", "enum": list(SCOPES)},
                           "minItems": 1}},
                "required": ["query"], "additionalProperties": False}}, {
            "name": "fetch_page",
            "description": "Read one page found by web_search as plain text. No JavaScript, so static pages only. The text is capped.",
            "inputSchema": {"type": "object", "properties": {
                "url": {"type": "string", "maxLength": 2048},
                "limit": {"type": "integer", "minimum": 256, "maximum": 131072, "default": 16384}},
                "required": ["url"], "additionalProperties": False}}]}})
    elif method == "tools/call":
        try:
            params = message.get("params", {})
            name = params.get("name")
            if name == "web_search":
                result = call_search(params.get("arguments", {}))
            elif name == "fetch_page":
                result = call_fetch(params.get("arguments", {}))
            else:
                raise ValueError("Unknown tool")
            send({"jsonrpc": "2.0", "id": request_id,
                  "result": {"content": [{"type": "text", "text": json.dumps(result, ensure_ascii=False)}],
                             "structuredContent": result, "isError": False}})
        except (ValueError, urllib.error.URLError, TimeoutError, json.JSONDecodeError) as error:
            send({"jsonrpc": "2.0", "id": request_id,
                  "result": {"content": [{"type": "text", "text": str(error)}], "isError": True}})
    elif request_id is not None:
        send({"jsonrpc": "2.0", "id": request_id,
              "error": {"code": -32601, "message": "Method not found"}})


def main():
    for line in sys.stdin:
        try:
            message = json.loads(line)
            if isinstance(message, dict):
                handle(message)
        except json.JSONDecodeError:
            send({"jsonrpc": "2.0", "id": None,
                  "error": {"code": -32700, "message": "Parse error"}})


if __name__ == "__main__":
    main()
