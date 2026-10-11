// SPDX-License-Identifier: GPL-3.0-or-later
// The pinned SD source uses the newer precision setter; Prism retains the
// operation-specific setters. Preserve its requested F32 accumulation.
#pragma once
#include "ggml.h"
inline bool ggml_prec_set_acc(ggml_tensor *tensor, ggml_prec precision)
{
    if (tensor->op == GGML_OP_MUL_MAT)
        ggml_mul_mat_set_prec(tensor, precision);
    else if (tensor->op == GGML_OP_FLASH_ATTN_EXT)
        ggml_flash_attn_ext_set_prec(tensor, precision);
    else
        GGML_ABORT("Unsupported Prism accumulation precision operation");
    return true;
}
