#pragma once

#include "ps5_agc_gemm.h"

#ifndef PS5_AGC_DIRECT_ONLY
#include "ggml-backend.h"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef PS5_AGC_DIRECT_ONLY
    ggml_backend_reg_t ggml_backend_ps5agc_reg(void);
    ggml_backend_t ggml_backend_ps5agc_init(void);
#endif
    void ps5_agc_backend_print_stats(void);
    int ps5_agc_backend_reserve(void);
    int ps5_agc_backend_release_scratch(void);
    void *ps5_agc_backend_take_released_scratch_address(void);

#ifdef __cplusplus
}
#endif
