#include "rsa/core.hpp"
#include "rsa/protocol.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace rsa {
void Settings::validate() const {
    if (!std::isfinite(center_hz) || !std::isfinite(span_hz) || !std::isfinite(rbw_hz) ||
        !std::isfinite(reference_dbm) || span_hz <= 0 || span_hz > max_bandwidth_hz ||
        rbw_hz <= 0 || rbw_hz > span_hz || center_hz - span_hz / 2 < min_frequency_hz ||
        center_hz + span_hz / 2 > max_frequency_hz) {
        throw Error("invalid_settings", "Require a band within 9 kHz–6.2 GHz, span <= 40 MHz, "
                    "0 < RBW <= span, and finite numeric settings. Use sweep for a wider range.", 2);
    }
}

std::vector<std::complex<double>> decode_iq_f32le(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() % 8 != 0) {
        throw Error("invalid_iq", "IQ input must contain complete pairs of little-endian float32 values.", 5);
    }
    auto read = [&](std::size_t p) {
        const std::uint32_t bits = std::uint32_t(bytes[p]) | (std::uint32_t(bytes[p + 1]) << 8) |
                                  (std::uint32_t(bytes[p + 2]) << 16) | (std::uint32_t(bytes[p + 3]) << 24);
        const float value = std::bit_cast<float>(bits);
        if (!std::isfinite(value) || std::abs(value) > 1.0F) {
            throw Error("invalid_iq", "Offline IQ components must be finite normalized values in [-1, 1].", 5);
        }
        return static_cast<double>(value);
    };
    std::vector<std::complex<double>> result;
    result.reserve(bytes.size() / 8);
    for (std::size_t p = 0; p < bytes.size(); p += 8) result.emplace_back(read(p), read(p + 4));
    return result;
}

namespace {
Spectrum transform(std::span<const std::complex<double>> iq, double rate, double center) {
    const auto n = iq.size();
    std::vector<std::complex<double>> values(n);
    double sum = 0, squares = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(iq[i].real()) || !std::isfinite(iq[i].imag())) {
            throw Error("invalid_iq", "FFT input contains non-finite samples.", 5);
        }
        const double window = 0.5 - 0.5 * std::cos(2 * std::numbers::pi *
                               static_cast<double>(i) / static_cast<double>(n));
        values[i] = iq[i] * window;
        sum += window;
        squares += window * window;
    }
    // Iterative radix-2 FFT, then shift negative frequencies before positive frequencies.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(values[i], values[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const auto rotation = std::polar(1.0, -2 * std::numbers::pi / static_cast<double>(length));
        for (std::size_t i = 0; i < n; i += length) {
            std::complex<double> weight{1, 0};
            for (std::size_t j = 0; j < length / 2; ++j) {
                const auto a = values[i + j];
                const auto b = values[i + j + length / 2] * weight;
                values[i + j] = a + b;
                values[i + j + length / 2] = a - b;
                weight *= rotation;
            }
        }
    }
    Spectrum result{center - rate / 2, rate / static_cast<double>(n),
                    rate * squares / (sum * sum), {}};
    result.levels_dbfs.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double amplitude = std::abs(values[(i + n / 2) % n]) / sum;
        result.levels_dbfs.push_back(20 * std::log10(std::max(amplitude, 1e-15)));
    }
    return result;
}
} // namespace
Spectrum spectrum(std::span<const std::complex<double>> iq, double rate, double center) {
    const auto n = iq.size();
    if (n < 8 || n > (1U << 20) || !std::has_single_bit(n) || !std::isfinite(rate) ||
        rate <= 0 || rate > max_bandwidth_hz || !std::isfinite(center) ||
        center - rate / 2 < min_frequency_hz || center + rate / 2 > max_frequency_hz) {
        throw Error("invalid_fft", "FFT requires 8–1048576 samples (power of two), a positive "
                    "sample rate <= 40 MHz, and a band within 9 kHz–6.2 GHz.", 2);
    }
    return transform(iq, rate, center);
}
Spectrum wire::if_spectrum(std::span<const std::int16_t> samples, const Settings& settings, double tuned) {
    settings.validate();
    if (!std::has_single_bit(samples.size()) || samples.size()<8 || samples.size()>(1U<<20))
        throw Error("invalid_fft", "IF FFT requires 8…1048576 samples, power of two.",2);
    const auto tune=wire::tuning(settings.center_hz);
    if(!std::isfinite(tuned) || std::abs(tuned-settings.center_hz)>250001)
        throw Error("invalid_fft","Invalid tuned center for this acquisition band.",2);
    std::vector<std::complex<double>> values; values.reserve(samples.size());
    for (auto sample:samples) values.emplace_back(static_cast<double>(sample)/32768.0,0);
    const auto raw=transform(values,wire::adc_rate,0);
    const double offset=tune.lf_path?0:tuned-wire::digital_if;
    const double first_if=settings.center_hz-settings.span_hz/2-offset;
    const double last_if=first_if+settings.span_hz;
    if (tune.lf_path ? (first_if<min_frequency_hz || last_if>wire::lf_limit_hz) : (first_if<8e6 || last_if>48e6))
        throw Error("unsupported_native_band", "Requested span exceeds the tuned 40 MHz IF passband; reduce span near off-grid centers.",2);
    const auto first=static_cast<std::size_t>(std::ceil(first_if/raw.step_hz));
    const auto last=static_cast<std::size_t>(std::floor(last_if/raw.step_hz));
    if (first>=last) throw Error("invalid_fft","Span must contain at least two FFT bins; reduce RBW.",2);
    Spectrum result{offset+static_cast<double>(first)*raw.step_hz,raw.step_hz,raw.enbw_hz,{}};
    for (auto i=first;i<=last;++i) {
        const auto level=raw.levels_dbfs[samples.size()/2+i];
        result.levels_dbfs.push_back(level<=-300.0 ? -300.0 : level+20*std::log10(2.0));
    }
    return result;
}
}  // namespace rsa
