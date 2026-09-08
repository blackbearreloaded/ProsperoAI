// ProsperoAI local session persistence.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace prospero_session {

constexpr unsigned MessageCapacity = 64;
constexpr unsigned CatalogCapacity = 64;
constexpr unsigned MessageBytes = 2048;

struct Message {
    char role[12];
    char timestamp[9];
    char content[MessageBytes];
};

struct Record {
    char id[48];
    char title[72];
    char model_id[48];
    char model_name[64];
    char purpose[24];
    char updated_time[9];
    std::uint64_t created_us;
    std::uint64_t updated_us;
    unsigned message_count;
    unsigned context_start;
};

bool create(Record *record, const char *model_id, const char *model_name,
            const char *purpose);
bool save(Record *record, const Message *messages, unsigned message_count);
bool load(const char *id, Record *record, Message *messages,
          unsigned message_capacity);
unsigned scan(Record *records, unsigned capacity);
bool erase(const char *id);
bool archive_media(const char *id, unsigned message_index,
                   const char *kind, const char *source,
                   char *output, std::size_t output_capacity);
bool valid_id(const char *id);

} // namespace prospero_session
