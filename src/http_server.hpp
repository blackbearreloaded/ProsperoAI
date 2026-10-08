// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Starts the SDK socket HTTP server. Ollama routes: /api/tags, /api/generate,
// /api/chat. OpenAI-compatible routes: /v1/models, /v1/chat/completions (SSE).
// Optional /app0/api_key.txt enables bearer authentication on all routes.
// Connections are bounded and completed worker threads are joined, not detached.
// Safe to call once during startup; failure leaves the console app running.
bool prospero_http_server_start(unsigned short port);
