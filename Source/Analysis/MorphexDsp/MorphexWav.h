#pragma once

// Minimal PCM WAV reader/writer (port of io/wav.rs); stereo averages
// to mono like the Rust original.

#include "MorphexDspTypes.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace morphex::wav
{
    struct WavData
    {
        std::uint32_t sampleRate = 44100;
        dsp::Vector samples; // mono (or passthrough, see above)
    };

    namespace detail
    {
        inline std::uint32_t readU32 (const std::vector<char>& b, std::size_t p)
        {
            std::uint32_t v = 0;
            std::memcpy (&v, b.data() + p, 4);
            return v;
        }

        inline std::uint16_t readU16 (const std::vector<char>& b, std::size_t p)
        {
            std::uint16_t v = 0;
            std::memcpy (&v, b.data() + p, 2);
            return v;
        }

        inline std::int32_t readI32 (const std::vector<char>& b, std::size_t p)
        {
            std::int32_t v = 0;
            std::memcpy (&v, b.data() + p, 4);
            return v;
        }
    } // namespace detail

    inline WavData readWav (const std::string& path)
    {
        std::ifstream f (path, std::ios::binary);
        if (! f)
            throw std::runtime_error ("cannot open wav file: " + path);
        const std::vector<char> b ((std::istreambuf_iterator<char> (f)),
                                   std::istreambuf_iterator<char>());
        if (b.size() < 44 || std::memcmp (b.data(), "RIFF", 4) != 0
            || std::memcmp (b.data() + 8, "WAVE", 4) != 0)
            throw std::runtime_error ("not a RIFF/WAVE file: " + path);

        std::uint16_t audioFormat = 1, numChannels = 1, bitsPerSample = 16;
        std::uint32_t sampleRate = 44100;
        std::size_t dataPos = 0, dataLen = 0;

        std::size_t p = 12;
        while (p + 8 <= b.size())
        {
            const std::string id (b.data() + p, 4);
            const std::uint32_t size = detail::readU32 (b, p + 4);
            if (id == "fmt " && size >= 16 && p + 8 + size <= b.size())
            {
                audioFormat = detail::readU16 (b, p + 8);
                numChannels = detail::readU16 (b, p + 10);
                sampleRate = detail::readU32 (b, p + 12);
                bitsPerSample = detail::readU16 (b, p + 22);
            }
            else if (id == "data" && p + 8 + size <= b.size())
            {
                dataPos = p + 8;
                dataLen = size;
            }
            p += 8 + size + (size & 1);
        }
        if (dataLen == 0)
            throw std::runtime_error ("no data chunk in wav file: " + path);

        dsp::Vector samples;
        if (audioFormat == 3) // IEEE float
        {
            if (bitsPerSample == 32)
            {
                const std::size_t n = dataLen / 4;
                samples.reserve (n);
                for (std::size_t i = 0; i < n; ++i)
                {
                    float v = 0.0f;
                    std::memcpy (&v, b.data() + dataPos + i * 4, 4);
                    samples.push_back ((double) v);
                }
            }
            else if (bitsPerSample == 64)
            {
                const std::size_t n = dataLen / 8;
                samples.reserve (n);
                for (std::size_t i = 0; i < n; ++i)
                {
                    double v = 0.0;
                    std::memcpy (&v, b.data() + dataPos + i * 8, 8);
                    samples.push_back (v);
                }
            }
            else
            {
                throw std::runtime_error ("unsupported float WAV depth");
            }
        }
        else if (audioFormat == 1) // PCM int
        {
            const std::size_t bytes = bitsPerSample / 8;
            if (bytes < 1 || bytes > 4)
                throw std::runtime_error ("unsupported PCM WAV depth");
            const std::size_t n = dataLen / bytes;
            const double maxVal = (double) (1u << (bitsPerSample - 1));
            samples.reserve (n);
            for (std::size_t i = 0; i < n; ++i)
            {
                std::int32_t s = 0;
                const char* src = b.data() + dataPos + i * bytes;
                if (bytes == 1)
                    s = ((int) (std::uint8_t) src[0] - 128) << 24 >> 24;
                else if (bytes == 2)
                    s = (std::int16_t) ((std::uint8_t) src[0] | ((std::uint16_t) (std::uint8_t) src[1] << 8));
                else if (bytes == 3)
                    s = ((std::int32_t) (std::uint8_t) src[0])
                        | ((std::int32_t) (std::uint8_t) src[1] << 8)
                        | ((std::int32_t) (std::int8_t) src[2] << 16);
                else
                    s = detail::readI32 (b, dataPos + i * 4);
                samples.push_back ((double) s / maxVal);
            }
        }
        else
        {
            throw std::runtime_error ("unsupported WAV format (not PCM/float)");
        }

        if (numChannels == 2)
        {
            dsp::Vector mono;
            mono.reserve (samples.size() / 2);
            for (std::size_t i = 0; i + 1 < samples.size(); i += 2)
                mono.push_back ((samples[i] + samples[i + 1]) / 2.0);
            samples = std::move (mono);
        }

        return { sampleRate, std::move (samples) };
    }

    inline void writeWav (const dsp::Vector& samples, std::uint32_t sampleRate,
                          const std::string& path)
    {
        std::ofstream f (path, std::ios::binary);
        if (! f)
            throw std::runtime_error ("cannot write wav file: " + path);

        const std::uint32_t dataBytes = (std::uint32_t) samples.size() * 2;
        const std::uint32_t riffSize = 36 + dataBytes;
        auto writeU32 = [&] (std::uint32_t v)
        {
            f.put ((char) (v & 0xFF));
            f.put ((char) ((v >> 8) & 0xFF));
            f.put ((char) ((v >> 16) & 0xFF));
            f.put ((char) ((v >> 24) & 0xFF));
        };
        auto writeU16 = [&] (std::uint16_t v)
        {
            f.put ((char) (v & 0xFF));
            f.put ((char) ((v >> 8) & 0xFF));
        };

        f.write ("RIFF", 4);
        writeU32 (riffSize);
        f.write ("WAVEfmt ", 8);
        writeU32 (16);
        writeU16 (1); // PCM
        writeU16 (1); // mono
        writeU32 (sampleRate);
        writeU32 (sampleRate * 2); // byte rate
        writeU16 (2);              // block align
        writeU16 (16);             // bits
        f.write ("data", 4);
        writeU32 (dataBytes);

        for (double s : samples)
        {
            const double clamped = std::max (-1.0, std::min (1.0, s));
            const std::int16_t v = (std::int16_t) (clamped * 32767.0);
            writeU16 ((std::uint16_t) v);
        }
    }
} // namespace morphex::wav
