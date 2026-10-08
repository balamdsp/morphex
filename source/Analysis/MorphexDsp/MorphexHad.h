#pragma once

// .had matrix codec + XML serialization, byte-compatible with the Rust
// analyzer (port of io/had.rs).

#include "MorphexDspTypes.h"
#include "MorphexHpsModel.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>

namespace morphex::had
{
    constexpr unsigned int fileVersion = 1;
    constexpr unsigned int decimalPlaces = 3;
    constexpr int defaultHearingThreshold = -100;

    // Shortest-round-trip-ish float printing (serde emits e.g. 0.05,
    // 246.66688606145465): %.17g with trailing-zero stripping.
    inline std::string printF64 (double v)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.17g", v);
        std::string s (buf);
        const auto dot = s.find ('.');
        if (dot != std::string::npos)
        {
            const auto exp = s.find_first_of ("eE", dot);
            std::string tail = (exp == std::string::npos) ? "" : s.substr (exp);
            std::string mant = s.substr (dot + 1, (exp == std::string::npos ? s.size() : exp) - dot - 1);
            while (mant.size() > 1 && mant.back() == '0')
                mant.pop_back();
            s = s.substr (0, dot + 1) + mant + tail;
        }
        return s;
    }

    inline std::vector<std::string> encodeMatrix (const dsp::Matrix& values, bool negate,
                                                  unsigned int dp)
    {
        if (values.empty())
            return {};
        const double factor = std::pow (10.0, (int) dp);
        std::size_t width = 0;
        for (const auto& r : values)
            width = std::max (width, r.size());

        std::vector<std::vector<long long>> scaled;
        scaled.reserve (values.size());
        for (const auto& frame : values)
        {
            std::vector<long long> row;
            row.reserve (width);
            for (std::size_t i = 0; i < width; ++i)
            {
                const double v = i < frame.size() ? frame[i] : 0.0;
                long long rounded = (long long) std::round (v * factor);
                row.push_back (negate ? -rounded : rounded);
            }
            scaled.push_back (std::move (row));
        }

        std::vector<std::string> rows;
        rows.reserve (scaled.size());
        for (std::size_t f = 0; f < scaled.size(); ++f)
        {
            std::string row = "[";
            for (std::size_t i = 0; i < width; ++i)
            {
                const long long v = (f == 0) ? scaled[f][i] : scaled[f][i] - scaled[f - 1][i];
                if (i > 0)
                    row += ',';
                row += std::to_string (v);
            }
            row += ']';
            rows.push_back (std::move (row));
        }
        return rows;
    }

    inline dsp::Matrix decodeMatrix (const std::vector<std::string>& rows, bool negate,
                                     unsigned int dp)
    {
        const double factor = std::pow (10.0, (int) dp);
        dsp::Matrix result;
        dsp::Vector prev;
        bool havePrev = false;

        for (const auto& row : rows)
        {
            if (row.size() < 2 || row.front() != '[')
                continue;
            const std::string inner = row.substr (1, row.size() - 2);
            dsp::Vector values;
            std::size_t pos = 0;
            while (pos <= inner.size())
            {
                const std::size_t comma = inner.find (',', pos);
                const std::string tok = inner.substr (pos, comma == std::string::npos
                                                             ? std::string::npos
                                                             : comma - pos);
                // Trim whitespace
                const std::size_t a = tok.find_first_not_of (" \t\r\n");
                const std::size_t b = tok.find_last_not_of (" \t\r\n");
                if (a != std::string::npos)
                {
                    try
                    {
                        double v = std::stod (tok.substr (a, b - a + 1)) / factor;
                        values.push_back (negate ? -v : v);
                    }
                    catch (...)
                    {
                    }
                }
                if (comma == std::string::npos)
                    break;
                pos = comma + 1;
            }

            if (havePrev)
            {
                dsp::Vector decoded;
                const std::size_t n = std::min (values.size(), prev.size());
                decoded.reserve (n);
                for (std::size_t i = 0; i < n; ++i)
                    decoded.push_back (values[i] + prev[i]);
                result.push_back (decoded);
                prev = decoded;
            }
            else
            {
                result.push_back (values);
                prev = values;
                havePrev = true;
            }
        }
        return result;
    }

    inline double estimateF0 (const dsp::Matrix& hfreq, double minF0, double maxF0)
    {
        dsp::Vector first;
        for (const auto& frame : hfreq)
        {
            if (! frame.empty() && frame[0] > 0.0)
                first.push_back (frame[0]);
        }
        if (first.empty())
            return 0.5 * (minF0 + maxF0);
        std::sort (first.begin(), first.end());
        const std::size_t mid = first.size() / 2;
        if (first.size() % 2 == 0)
            return 0.5 * (first[mid - 1] + first[mid]);
        return first[mid];
    }

    inline unsigned int freqToMidi (double f0)
    {
        if (f0 <= 0.0)
            return 0;
        const long long note = (long long) std::round (69.0 + 12.0 * std::log2 (f0 / 440.0));
        return (unsigned int) std::max<long long> (0, std::min<long long> (127, note));
    }

    struct HadDocument
    {
        // sound
        std::uint32_t fs = 44100, note = 0, velocity = 0;
        std::uint32_t maxHarmonics = 0, maxFrames = 0;
        std::uint32_t loopStart = 0, loopEnd = 0;
        // parameters
        std::string windowType = "blackman";
        std::uint32_t windowSize = 1001, fftSize = 1024;
        int magnitudeThreshold = -100;
        double minSineDur = 0.05;
        std::uint32_t maxHarm = 200;
        double f0Guess = 0.0;
        std::uint32_t minF0 = 100, maxF0 = 1000, maxF0Error = 50;
        double harmDevSlope = 0.01, stocFact = 0.1;
        std::uint32_t synthesisFftSize = 512, hopSize = 128;
        // synthesis rows
        std::vector<std::string> hF, hM, sF, sM, cRows;
    };

    inline std::string generateHadXml (const HadDocument& h)
    {
        std::string xml = "<had><file><v>";
        xml += std::to_string (fileVersion);
        xml += "</v><dp>";
        xml += std::to_string (decimalPlaces);
        xml += "</dp></file><sound><fs>";
        xml += std::to_string (h.fs);
        xml += "</fs><note>";
        xml += std::to_string (h.note);
        xml += "</note><velocity>";
        xml += std::to_string (h.velocity);
        xml += "</velocity><max_harmonics>";
        xml += std::to_string (h.maxHarmonics);
        xml += "</max_harmonics><max_frames>";
        xml += std::to_string (h.maxFrames);
        xml += "</max_frames><loop><start>";
        xml += std::to_string (h.loopStart);
        xml += "</start><end>";
        xml += std::to_string (h.loopEnd);
        xml += "</end></loop></sound><parameters><window_type>";
        xml += h.windowType;
        xml += "</window_type><window_size>";
        xml += std::to_string (h.windowSize);
        xml += "</window_size><fft_size>";
        xml += std::to_string (h.fftSize);
        xml += "</fft_size><magnitude_threshold>";
        xml += std::to_string (h.magnitudeThreshold);
        xml += "</magnitude_threshold><hearing_threshold>";
        xml += std::to_string (defaultHearingThreshold);
        xml += "</hearing_threshold><min_sine_dur>";
        xml += printF64 (h.minSineDur);
        xml += "</min_sine_dur><max_harm>";
        xml += std::to_string (h.maxHarm);
        xml += "</max_harm><f0_guess>";
        xml += printF64 (h.f0Guess);
        xml += "</f0_guess><min_f0>";
        xml += std::to_string (h.minF0);
        xml += "</min_f0><max_f0>";
        xml += std::to_string (h.maxF0);
        xml += "</max_f0><max_f0_error>";
        xml += std::to_string (h.maxF0Error);
        xml += "</max_f0_error><harm_dev_slope>";
        xml += printF64 (h.harmDevSlope);
        xml += "</harm_dev_slope><stoc_fact>";
        xml += printF64 (h.stocFact);
        xml += "</stoc_fact><synthesis_fft_size>";
        xml += std::to_string (h.synthesisFftSize);
        xml += "</synthesis_fft_size><hop_size>";
        xml += std::to_string (h.hopSize);
        xml += "</hop_size></parameters><synthesis><h>";
        for (const auto& r : h.hF)
        {
            xml += "<f>";
            xml += r;
            xml += "</f>";
        }
        for (const auto& r : h.hM)
        {
            xml += "<m>";
            xml += r;
            xml += "</m>";
        }
        xml += "</h><s>";
        for (const auto& r : h.sF)
        {
            xml += "<f>";
            xml += r;
            xml += "</f>";
        }
        for (const auto& r : h.sM)
        {
            xml += "<m>";
            xml += r;
            xml += "</m>";
        }
        xml += "</s>";
        for (const auto& r : h.cRows)
        {
            xml += "<c>";
            xml += r;
            xml += "</c>";
        }
        xml += "</synthesis></had>";
        return xml;
    }

    inline HadDocument buildHad (const dsp::hps::AnalysisParams& params, std::uint32_t fs,
                                 const dsp::Matrix& hfreq, const dsp::Matrix& hmag,
                                 const dsp::Matrix& sfreq, const dsp::Matrix& smag,
                                 const dsp::Matrix& stocEnv)
    {
        HadDocument h;
        h.fs = fs;
        h.f0Guess = estimateF0 (hfreq, params.minF0, params.maxF0);
        h.note = freqToMidi (h.f0Guess);
        h.velocity = 0;
        std::size_t mh = 0;
        for (const auto& f : hfreq)
            mh = std::max (mh, f.size());
        h.maxHarmonics = (std::uint32_t) mh;
        h.maxFrames = (std::uint32_t) hfreq.size();
        h.loopStart = 0;
        h.loopEnd = 0;
        h.windowType = params.windowType;
        h.windowSize = (std::uint32_t) params.windowSize;
        h.fftSize = (std::uint32_t) params.fftSize;
        h.magnitudeThreshold = (int) std::round (params.magnitudeThreshold);
        h.minSineDur = params.minSineDur;
        h.maxHarm = (std::uint32_t) params.maxHarm;
        h.minF0 = (std::uint32_t) std::round (params.minF0);
        h.maxF0 = (std::uint32_t) std::round (params.maxF0);
        h.maxF0Error = (std::uint32_t) std::round (params.maxF0Error);
        h.harmDevSlope = params.harmDevSlope;
        h.stocFact = params.stocFact;
        h.synthesisFftSize = (std::uint32_t) dsp::hps::synthesisFftSize;
        h.hopSize = (std::uint32_t) dsp::hps::hopSize;
        h.hF = encodeMatrix (hfreq, false, decimalPlaces);
        h.hM = encodeMatrix (hmag, true, decimalPlaces);
        h.sF = encodeMatrix (sfreq, false, decimalPlaces);
        h.sM = encodeMatrix (smag, true, decimalPlaces);
        h.cRows = encodeMatrix (stocEnv, true, decimalPlaces);
        return h;
    }
} // namespace morphex::had
