#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int ps5_agc_gemm_f16_f32(const uint16_t *a, size_t a_elements, const uint16_t *b,
                             size_t b_elements, float *c, uint32_t rows_a, uint32_t columns_b,
                             uint32_t reduction, uint32_t stride_a, uint32_t stride_b,
                             uint32_t transpose_b);

#ifdef __cplusplus
}
#endif
