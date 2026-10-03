// ProsperoAI - Embedded HTTP server: Ollama-compatible API and web chat UI.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Brings up the PS5 network stack (dynamically: libSceNet is resolved at
// runtime via sceKernelDlsym rather than linked as a static NEEDED module,
// see http_server.cpp for why) and starts listening on `port` for HTTP
// requests, on its own detached thread. Exposes an Ollama-compatible REST
// API (GET /api/tags, POST /api/generate, POST /api/chat) backed by
// gpt_runtime, plus a mobile-friendly chat page at GET /.
//
// Safe to call once during application startup. Returns false if the
// network stack, socket, or listener could not be brought up; the caller
// should keep running the on-console UI regardless, since network access is
// an addition to it, not a requirement. Diagnostic steps are appended to
// /download0/prosperoai-net-debug.txt while this is being brought up.
bool prospero_http_server_start(unsigned short port);
