#pragma once

#include <CL/cl.h>

#include <cstddef>
#include <string>
#include <unordered_map>

extern "C" int ps5cl_gemm(int half_storage, int transpose_a, int transpose_b,
                           size_t m, size_t n, size_t k, const void *alpha,
                           cl_mem a, size_t a_offset, size_t lda, cl_mem b,
                           size_t b_offset, size_t ldb, const void *beta,
                           cl_mem c, size_t c_offset, size_t ldc);
extern "C" int ps5cl_gemv(int half_storage, int transpose_a, size_t m,
                           size_t n, const void *alpha, cl_mem a,
                           size_t a_offset, size_t lda, cl_mem x,
                           size_t x_offset, size_t x_inc, const void *beta,
                           cl_mem y, size_t y_offset, size_t y_inc);

namespace clblast {

enum class StatusCode : int { kSuccess = 0, kInvalidValue = -30 };
enum class Layout { kRowMajor, kColMajor };
enum class Transpose { kNo, kYes, kConjugate };
enum class Precision { kHalf, kSingle };

inline StatusCode OverrideParameters(cl_device_id, const std::string&, Precision,
                                     const std::unordered_map<std::string, size_t>&)
{
    return StatusCode::kSuccess;
}

template <typename T>
StatusCode GemmTempBufferSize(Layout layout, Transpose, Transpose,
                              size_t, size_t, size_t, size_t, size_t,
                              size_t, size_t, size_t, size_t,
                              cl_command_queue*, size_t& bytes)
{
    if (layout != Layout::kRowMajor) return StatusCode::kInvalidValue;
    bytes = 1;
    return StatusCode::kSuccess;
}

template <typename T>
StatusCode Gemm(Layout layout, Transpose trans_a, Transpose trans_b,
                size_t m, size_t n, size_t k, T alpha,
                cl_mem a, size_t a_offset, size_t lda,
                cl_mem b, size_t b_offset, size_t ldb, T beta,
                cl_mem c, size_t c_offset, size_t ldc,
                cl_command_queue*, cl_event* = nullptr, cl_mem = nullptr)
{
    if (layout != Layout::kRowMajor) return StatusCode::kInvalidValue;
    return ps5cl_gemm(sizeof(T) == 2, trans_a != Transpose::kNo,
                      trans_b != Transpose::kNo, m, n, k, &alpha, a, a_offset,
                      lda, b, b_offset, ldb, &beta, c, c_offset, ldc) == 0
        ? StatusCode::kSuccess : StatusCode::kInvalidValue;
}

template <typename T>
StatusCode Gemv(Layout layout, Transpose trans_a, size_t m, size_t n, T alpha,
                cl_mem a, size_t a_offset, size_t lda,
                cl_mem x, size_t x_offset, size_t x_inc, T beta,
                cl_mem y, size_t y_offset, size_t y_inc,
                cl_command_queue*, cl_event* = nullptr)
{
    if (layout != Layout::kRowMajor) return StatusCode::kInvalidValue;
    return ps5cl_gemv(sizeof(T) == 2, trans_a != Transpose::kNo, m, n,
                      &alpha, a, a_offset, lda, x, x_offset, x_inc, &beta,
                      y, y_offset, y_inc) == 0
        ? StatusCode::kSuccess : StatusCode::kInvalidValue;
}

} // namespace clblast
