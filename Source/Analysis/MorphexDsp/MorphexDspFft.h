#pragma once

// Radix-2 FFT with O(n^2) fallback for odd lengths; negative-exponent
// forward and 1/N inverse match scipy (port of the Rust fft.rs).

#include "MorphexDspTypes.h"

#include <cassert>
#include <stdexcept>

namespace morphex::dsp::fft
{
    namespace detail
    {
        inline void dftForward (ComplexVector& buffer)
        {
            const std::size_t n = buffer.size();
            const double pi2 = 2.0 * constPi();
            ComplexVector out (n);
            for (std::size_t k = 0; k < n; ++k)
            {
                Complex sum (0.0, 0.0);
                for (std::size_t j = 0; j < n; ++j)
                {
                    const double angle = -pi2 * (double) (j * k) / (double) n;
                    sum += buffer[j] * Complex (std::cos (angle), std::sin (angle));
                }
                out[k] = sum;
            }
            buffer = out;
        }

        inline void dftInverse (ComplexVector& buffer)
        {
            const std::size_t n = buffer.size();
            const double pi2 = 2.0 * constPi();
            const double scale = 1.0 / (double) n;
            ComplexVector out (n);
            for (std::size_t k = 0; k < n; ++k)
            {
                Complex sum (0.0, 0.0);
                for (std::size_t j = 0; j < n; ++j)
                {
                    const double angle = pi2 * (double) (j * k) / (double) n;
                    sum += buffer[j] * Complex (std::cos (angle), std::sin (angle));
                }
                out[k] = sum * scale;
            }
            buffer = out;
        }

        inline void radix2Fft (ComplexVector& buffer, bool inverse)
        {
            const std::size_t n = buffer.size();
            // Bit-reversal permutation
            for (std::size_t i = 1, j = 0; i < n; ++i)
            {
                std::size_t bit = n >> 1;
                for (; j & bit; bit >>= 1)
                    j ^= bit;
                j ^= bit;
                if (i < j)
                    std::swap (buffer[i], buffer[j]);
            }
            // Cooley-Tukey butterflies
            for (std::size_t len = 2; len <= n; len <<= 1)
            {
                const double angle = (inverse ? 2.0 : -2.0) * constPi() / (double) len;
                const Complex wlen (std::cos (angle), std::sin (angle));
                for (std::size_t i = 0; i < n; i += len)
                {
                    Complex w (1.0, 0.0);
                    for (std::size_t j = 0; j < len / 2; ++j)
                    {
                        const Complex u = buffer[i + j];
                        const Complex v = buffer[i + j + len / 2] * w;
                        buffer[i + j] = u + v;
                        buffer[i + j + len / 2] = u - v;
                        w *= wlen;
                    }
                }
            }
            if (inverse)
            {
                const double scale = 1.0 / (double) n;
                for (auto& v : buffer)
                    v *= scale;
            }
        }
    } // namespace detail

    inline void fftForward (ComplexVector& buffer)
    {
        if (buffer.empty())
            return;
        if (isPowerOfTwo (buffer.size()))
            detail::radix2Fft (buffer, false);
        else
            detail::dftForward (buffer);
    }

    inline void fftInverse (ComplexVector& buffer)
    {
        if (buffer.empty())
            return;
        if (isPowerOfTwo (buffer.size()))
            detail::radix2Fft (buffer, true);
        else
            detail::dftInverse (buffer);
    }

    // Swap halves (port of the Rust swap loop; exact for odd lengths too).
    inline void fftShift (ComplexVector& buffer)
    {
        const std::size_t n = buffer.size();
        const std::size_t half = n / 2;
        for (std::size_t i = 0; i < half; ++i)
            std::swap (buffer[i], buffer[i + half]);
    }

    inline void fftShiftReal (Vector& buffer)
    {
        const std::size_t n = buffer.size();
        const std::size_t half = n / 2;
        for (std::size_t i = 0; i < half; ++i)
            std::swap (buffer[i], buffer[i + half]);
    }
} // namespace morphex::dsp::fft
