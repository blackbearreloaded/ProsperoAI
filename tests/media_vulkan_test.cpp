// SPDX-License-Identifier: GPL-3.0-or-later
#define PROSPERO_MEDIA_VULKAN
#include "../src/ps5_opencl.cpp"
#include <cassert>
int main()
{
    assert(prospero_media_vk_reserve() == 0);
    assert(prospero_media_vk::matmul(nullptr, nullptr, nullptr, 0, 0, 0) != 0);
    // Both B layouts, padded strides, offsets, alpha/beta and single-column output.
    for (int transpose : {0, 1})
        for (size_t n : {size_t(1), size_t(7), size_t(35)})
        {
            constexpr size_t m = 3, k = 17, lda = k + 2, offset = 2;
            const size_t ldb = (transpose ? k : n) + 3, ldc = n + 2;
            std::vector<uint16_t> av(offset + m * lda, float_to_half(-99)),
                bv(offset + (transpose ? n : k) * ldb, float_to_half(-99)),
                cv(offset + m * ldc, float_to_half(-99));
            for (size_t row = 0; row < m; ++row)
                for (size_t inner = 0; inner < k; ++inner)
                    av[offset + row * lda + inner] =
                        float_to_half(float(int(row + inner) % 5 - 2) * 0.25f);
            for (size_t col = 0; col < n; ++col)
                for (size_t inner = 0; inner < k; ++inner)
                    bv[offset + (transpose ? col * ldb + inner : inner * ldb + col)] =
                        float_to_half(float(int(col + inner) % 7 - 3) * 0.125f);
            for (size_t row = 0; row < m; ++row)
                for (size_t col = 0; col < n; ++col)
                    cv[offset + row * ldc + col] = float_to_half(1.0f);
            _cl_mem a{}, b{}, c{};
            a.data = reinterpret_cast<uint8_t *>(av.data());
            a.size = av.size() * 2;
            b.data = reinterpret_cast<uint8_t *>(bv.data());
            b.size = bv.size() * 2;
            c.data = reinterpret_cast<uint8_t *>(cv.data());
            c.size = cv.size() * 2;
            uint16_t alpha = float_to_half(0.5f), beta = float_to_half(-0.25f);
            assert(ps5cl_gemm(1, 0, transpose, m, n, k, &alpha, &a, offset, lda, &b, offset, ldb,
                              &beta, &c, offset, ldc) == 0);
            for (size_t row = 0; row < m; ++row)
                for (size_t col = 0; col < n; ++col)
                {
                    float sum = 0;
                    for (size_t inner = 0; inner < k; ++inner)
                        sum += load_value(&a, offset + row * lda + inner) *
                               load_value(&b, offset + (transpose ? col * ldb + inner
                                                                  : inner * ldb + col));
                    assert(std::fabs(load_value(&c, offset + row * ldc + col) -
                                     (sum * 0.5f - 0.25f)) < 0.002f);
                }
            assert(cv.front() == float_to_half(-99));
        }
    // Actual named shim kernels; compare half-output semantics with scalar reference.
    for (const char *name : {"silu_inplace", "k_gelu", "softmax_rows", "layernorm"})
    {
        constexpr int rows = 2, cols = 17;
        std::vector<uint16_t> data(rows * cols), output(rows * cols),
            gamma(cols, float_to_half(1.25f)), bias(cols, float_to_half(0.5f));
        for (int i = 0; i < rows * cols; ++i)
            data[i] = float_to_half(float(i % 9 - 4));
        const auto original = data;
        _cl_mem input{}, out{}, g{}, b{};
        input.data = reinterpret_cast<uint8_t *>(data.data());
        input.size = data.size() * 2;
        out.data = reinterpret_cast<uint8_t *>(output.data());
        out.size = output.size() * 2;
        g.data = reinterpret_cast<uint8_t *>(gamma.data());
        g.size = gamma.size() * 2;
        b.data = reinterpret_cast<uint8_t *>(bias.data());
        b.size = bias.size() * 2;
        _cl_kernel kernel;
        kernel.name = name;
        cl_mem ip = &input, op = &out, gp = &g, bp = &b;
        int count = rows * cols;
        float eps = 1e-5f;
        clSetKernelArg(&kernel, 0, sizeof(ip), &ip);
        if (kernel.name == "layernorm")
        {
            clSetKernelArg(&kernel, 1, sizeof(gp), &gp);
            clSetKernelArg(&kernel, 2, sizeof(bp), &bp);
            clSetKernelArg(&kernel, 3, sizeof(op), &op);
            clSetKernelArg(&kernel, 4, sizeof(rows), &rows);
            clSetKernelArg(&kernel, 5, sizeof(cols), &cols);
            clSetKernelArg(&kernel, 6, sizeof(eps), &eps);
        }
        else if (kernel.name == "softmax_rows")
        {
            clSetKernelArg(&kernel, 1, sizeof(rows), &rows);
            clSetKernelArg(&kernel, 2, sizeof(cols), &cols);
        }
        else
            clSetKernelArg(&kernel, 1, sizeof(count), &count);
        assert(run_kernel(&kernel) == CL_SUCCESS);
        for (int row = 0; row < rows; ++row)
        {
            float mean = 0, var = 0, sum = 0;
            for (int j = 0; j < cols; ++j)
            {
                float v = half_to_float(original[row * cols + j]);
                mean += v;
                sum += std::exp(v);
            }
            mean /= cols;
            for (int j = 0; j < cols; ++j)
            {
                float v = half_to_float(original[row * cols + j]) - mean;
                var += v * v;
            }
            var /= cols;
            for (int j = 0; j < cols; ++j)
            {
                float x = half_to_float(original[row * cols + j]), expected;
                if (kernel.name == "silu_inplace")
                    expected = x / (1 + std::exp(-x));
                else if (kernel.name == "k_gelu")
                    expected =
                        0.5f * x * (1 + std::tanh(0.7978845608f * (x + 0.044715f * x * x * x)));
                else if (kernel.name == "softmax_rows")
                    expected = std::exp(x) / sum;
                else
                    expected = (x - mean) / std::sqrt(var + eps) * 1.25f + 0.5f;
                float actual =
                    half_to_float((kernel.name == "layernorm" ? output : data)[row * cols + j]);
                assert(std::fabs(actual - expected) < 0.004f);
            }
        }
    }
    for (int transpose : {0, 1})
    {
        constexpr size_t m = 3, n = 5, lda = 7, offset = 2;
        const size_t rows = transpose ? n : m, inner = transpose ? m : n;
        std::vector<uint16_t> av(offset + m * lda, float_to_half(-99)),
            xv(offset + inner * 2, float_to_half(0)), yv(offset + rows * 2, float_to_half(1));
        for (size_t r = 0; r < m; ++r)
            for (size_t c = 0; c < n; ++c)
                av[offset + r * lda + c] = float_to_half(float(int(r + c) % 5 - 2));
        for (size_t d = 0; d < inner; ++d)
            xv[offset + d * 2] = float_to_half(float(d + 1) * 0.25f);
        _cl_mem a{}, x{}, y{};
        a.data = reinterpret_cast<uint8_t *>(av.data());
        a.size = av.size() * 2;
        x.data = reinterpret_cast<uint8_t *>(xv.data());
        x.size = xv.size() * 2;
        y.data = reinterpret_cast<uint8_t *>(yv.data());
        y.size = yv.size() * 2;
        uint16_t alpha = float_to_half(0.5f), beta = float_to_half(-0.25f);
        assert(ps5cl_gemv(1, transpose, m, n, &alpha, &a, offset, lda, &x, offset, 2, &beta, &y,
                          offset, 2) == 0);
        for (size_t r = 0; r < rows; ++r)
        {
            float sum = 0;
            for (size_t d = 0; d < inner; ++d)
                sum += load_value(&a, offset + (transpose ? d * lda + r : r * lda + d)) *
                       load_value(&x, offset + d * 2);
            assert(std::fabs(load_value(&y, offset + r * 2) - (sum * 0.5f - 0.25f)) < 0.002f);
        }
        assert(ps5cl_gemv(1, transpose, m, n, &alpha, &a, SIZE_MAX, lda, &x, offset, 2, &beta, &y,
                          offset, 2) != 0);
    }
    // Head-major and token-major attention: both score multiplication and values.
    for (bool interleaved : {false, true})
        for (bool scores : {false, true})
        {
            const int heads = 2, nq = 3, nk = interleaved ? 3 : 5, width = 7;
            const size_t reduction = scores ? width : nk, columns = scores ? nk : width;
            std::vector<uint16_t> first(heads * nq * reduction), second(heads * nk * width),
                result(heads * nq * columns);
            for (size_t i = 0; i < first.size(); ++i)
                first[i] = float_to_half(float(int(i) % 7 - 3) * 0.125f);
            for (size_t i = 0; i < second.size(); ++i)
                second[i] = float_to_half(float(int(i) % 9 - 4) * 0.125f);
            _cl_mem a{}, b{}, out{};
            a.data = reinterpret_cast<uint8_t *>(first.data());
            a.size = first.size() * 2;
            b.data = reinterpret_cast<uint8_t *>(second.data());
            b.size = second.size() * 2;
            out.data = reinterpret_cast<uint8_t *>(result.data());
            out.size = result.size() * 2;
            _cl_kernel kernel;
            kernel.name = interleaved ? (scores ? "k_scaled_attn_scores" : "k_scaled_attn_ctx")
                                      : (scores ? "attn_scores" : "attn_av");
            cl_mem ap = &a, bp = &b, op = &out;
            float scale = interleaved ? 1 / std::sqrt(float(width)) : 0.25f;
            clSetKernelArg(&kernel, 0, sizeof(ap), &ap);
            clSetKernelArg(&kernel, 1, sizeof(bp), &bp);
            clSetKernelArg(&kernel, 2, sizeof(op), &op);
            if (interleaved)
            {
                clSetKernelArg(&kernel, 3, sizeof(nq), &nq);
                clSetKernelArg(&kernel, 4, sizeof(heads), &heads);
                clSetKernelArg(&kernel, 5, sizeof(width), &width);
            }
            else
            {
                clSetKernelArg(&kernel, 3, sizeof(heads), &heads);
                clSetKernelArg(&kernel, 4, sizeof(nq), &nq);
                clSetKernelArg(&kernel, 5, sizeof(nk), &nk);
                clSetKernelArg(&kernel, 6, sizeof(width), &width);
                clSetKernelArg(&kernel, 7, sizeof(scale), &scale);
            }
            assert(run_kernel(&kernel) == CL_SUCCESS);
            for (int h = 0; h < heads; ++h)
                for (int row = 0; row < nq; ++row)
                    for (size_t col = 0; col < columns; ++col)
                    {
                        float sum = 0;
                        for (size_t d = 0; d < reduction; ++d)
                        {
                            size_t ai = scores && interleaved
                                            ? (size_t(row) * heads + h) * width + d
                                            : (size_t(h) * nq + row) * reduction + d;
                            size_t bi = scores ? (interleaved ? (col * heads + h) * width + d
                                                              : (size_t(h) * nk + col) * width + d)
                                               : (interleaved ? (d * heads + h) * width + col
                                                              : (size_t(h) * nk + d) * width + col);
                            sum += load_value(&a, ai) * load_value(&b, bi);
                        }
                        size_t ci = !scores && interleaved ? (size_t(row) * heads + h) * width + col
                                                           : (size_t(h) * nq + row) * columns + col;
                        assert(std::fabs(load_value(&out, ci) - sum * (scores ? scale : 1)) <
                               0.002f);
                    }
        }
    for (bool fp32 : {false, true})
    {
        const int k = 5, n = 7, biased = 1;
        std::vector<uint16_t> input(k, float_to_half(0.25f)), weights(n * k, float_to_half(0.5f)),
            bias(n, float_to_half(0.5f));
        std::vector<uint8_t> output(n * (fp32 ? 4 : 2));
        _cl_mem a{}, b{}, c{}, out{};
        a.data = reinterpret_cast<uint8_t *>(input.data());
        a.size = input.size() * 2;
        b.data = reinterpret_cast<uint8_t *>(weights.data());
        b.size = weights.size() * 2;
        c.data = reinterpret_cast<uint8_t *>(bias.data());
        c.size = bias.size() * 2;
        out.data = output.data();
        out.size = output.size();
        cl_mem ap = &a, bp = &b, cp = &c, op = &out;
        _cl_kernel kernel;
        kernel.name = fp32 ? "linear_apply_f32" : "linear_apply";
        clSetKernelArg(&kernel, 0, sizeof(ap), &ap);
        clSetKernelArg(&kernel, 1, sizeof(bp), &bp);
        clSetKernelArg(&kernel, 2, sizeof(cp), &cp);
        clSetKernelArg(&kernel, 3, sizeof(op), &op);
        clSetKernelArg(&kernel, 4, sizeof(k), &k);
        clSetKernelArg(&kernel, 5, sizeof(n), &n);
        clSetKernelArg(&kernel, 6, sizeof(biased), &biased);
        assert(run_kernel(&kernel) == CL_SUCCESS);
        for (int i = 0; i < n; ++i)
            assert(std::fabs((fp32 ? load_float(&out, i) : load_value(&out, i)) - 1.125f) < 0.002f);
    }
    // Convolution padding/stride/dilation/bias and a second staging tile.
    for (bool general : {false, true})
        for (int biased : {0, 1})
        {
            const int ic = 2, oc = 3, il = 550, ol = 270, width = 3, stride = general ? 2 : 1,
                      padding = 3, dilation = 2;
            std::vector<uint16_t> inputs(ic * il), weights(oc * ic * width), biases(oc),
                outputs(oc * ol + 1, float_to_half(-99));
            for (size_t i = 0; i < inputs.size(); ++i)
                inputs[i] = float_to_half(float(int(i % 11) - 5) * 0.125f);
            for (size_t i = 0; i < weights.size(); ++i)
                weights[i] = float_to_half(float(int(i % 7) - 3) * 0.25f);
            for (int i = 0; i < oc; ++i)
                biases[i] = float_to_half(0.25f * i);
            _cl_mem a{}, b{}, c{}, out{};
            a.data = reinterpret_cast<uint8_t *>(inputs.data());
            a.size = inputs.size() * 2;
            b.data = reinterpret_cast<uint8_t *>(weights.data());
            b.size = weights.size() * 2;
            c.data = reinterpret_cast<uint8_t *>(biases.data());
            c.size = biases.size() * 2;
            out.data = reinterpret_cast<uint8_t *>(outputs.data());
            out.size = outputs.size() * 2;
            _cl_kernel kernel;
            kernel.name = general ? "conv_1d" : "conv_1d_t4x4";
            cl_mem ap = &a, bp = &b, cp = biased ? &c : nullptr, op = &out;
            clSetKernelArg(&kernel, 0, sizeof(ap), &ap);
            clSetKernelArg(&kernel, 1, sizeof(bp), &bp);
            clSetKernelArg(&kernel, 2, sizeof(cp), &cp);
            clSetKernelArg(&kernel, 3, sizeof(op), &op);
            const int args[] = {ic, oc, il, ol, width, stride, padding, dilation, biased};
            for (int i = 0; i < 9; ++i)
            {
                if (!general && i == 5)
                    continue;
                clSetKernelArg(&kernel, 4 + i - (!general && i > 5 ? 1 : 0), sizeof(int), &args[i]);
            }
            assert(run_kernel(&kernel) == CL_SUCCESS);
            for (int channel = 0; channel < oc; ++channel)
                for (int row = 0; row < ol; ++row)
                {
                    float sum = biased ? load_value(&c, channel) : 0;
                    for (int source = 0; source < ic; ++source)
                        for (int tap = 0; tap < width; ++tap)
                        {
                            int at = row * stride + tap * dilation - padding;
                            if (at >= 0 && at < il)
                                sum += load_value(&a, source * il + at) *
                                       load_value(&b, (channel * ic + source) * width + tap);
                        }
                    assert(std::fabs(load_value(&out, channel * ol + row) - sum) < 0.002f);
                }
            assert(outputs.back() == float_to_half(-99));
            const int invalid = 0;
            clSetKernelArg(&kernel, 4, sizeof(invalid), &invalid);
            assert(run_kernel(&kernel) == CL_INVALID_VALUE);
        }
    prospero_media_vk::print_stats();
    prospero_media_vk::release();
    // A released media backend can be created again in the same process.
    assert(prospero_media_vk_reserve() == 0);
    prospero_media_vk::release();
}
