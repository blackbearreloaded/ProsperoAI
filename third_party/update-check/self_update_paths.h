/*
 * ProsperoAI - Paths for the self-update kit.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The kit's defaults name /app0 and /download0. With filesystem access the app's
 * folder and its data are elsewhere (include/storage.hpp), so the app says where:
 * 0 the helper in the app's folder, 1 the app's param.json, 2 the file that keeps the
 * catalog's highest sequence. The build puts this file in front of self_update_ps5.c.
 */
#ifndef PROSPEROAI_SELF_UPDATE_PATHS_H
#define PROSPEROAI_SELF_UPDATE_PATHS_H

#ifdef __cplusplus
extern "C"
#endif
const char *prosperoai_self_update_path(int which);

#define SELF_UPDATE_HELPER_PATH prosperoai_self_update_path(0)
#define SELF_UPDATE_PARAM_PATH prosperoai_self_update_path(1)
#define SELF_UPDATE_SEQUENCE_PATH prosperoai_self_update_path(2)

#endif
