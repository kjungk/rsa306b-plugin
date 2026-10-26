#include "rsa/core.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void rejected(const std::function<void()>& fn) {
    try { fn(); } catch (const rsa::Error&) { return; }
    throw std::runtime_error("Invalid input was accepted");
}
void tones() {
    constexpr std::size_t n = 1024;
    constexpr double rate = 1'024'000, center = 600'000'000;
    for (const int bin : {-137, 0, 131}) {
        std::vector<std::complex<double>> iq(n);
        for (std::size_t i = 0; i < n; ++i)
            iq[i] = std::polar(0.5, 2 * std::numbers::pi * bin * static_cast<double>(i) / n);
        const auto result = rsa::spectrum(iq, rate, center);
        const auto peak = std::max_element(result.levels_dbfs.begin(), result.levels_dbfs.end());
        const auto index = peak - result.levels_dbfs.begin();
        require(std::abs(*peak + 6.020599913279624) < 1e-8, "Tone amplitude normalization");
        require(result.start_hz + static_cast<double>(index) * result.step_hz == center + bin * 1000,
                "Frequency sign/shift is wrong");
        require(std::abs(result.enbw_hz - 1500) < 1e-8, "Hann ENBW must be 1.5 bins");
    }
}
void independent_dft() {
    // Direct O(N^2) oracle verifies the FFT across every bin, including off-bin content.
    constexpr std::size_t n = 16;
    std::vector<std::complex<double>> iq(n);
    for (std::size_t i = 0; i < n; ++i)
        iq[i] = {std::sin(static_cast<double>(i) * 0.83) * 0.7,
                 std::cos(static_cast<double>(i) * 1.37) * 0.4};
    const auto result = rsa::spectrum(iq, 16000, 600e6);
    for (std::size_t shifted = 0; shifted < n; ++shifted) {
        const auto bin = (shifted + n / 2) % n;
        std::complex<double> value{};
        double coherent_gain = 0;
        for (std::size_t j = 0; j < n; ++j) {
            const double window = (1 - std::cos(2 * std::numbers::pi * static_cast<double>(j) / n)) / 2;
            coherent_gain += window;
            value += iq[j] * window * std::polar(1.0, -2 * std::numbers::pi *
                                              static_cast<double>(bin * j) / n);
        }
        const double expected = 20 * std::log10(std::abs(value) / coherent_gain);
        require(std::abs(expected - result.levels_dbfs[shifted]) < 1e-9, "FFT disagrees with independent DFT");
    }
}
void decoding() {
    const std::vector<std::uint8_t> bytes{0, 0, 0, 0x3f, 0, 0, 0x80, 0xbf}; // +0.5, -1.0
    const auto decoded = rsa::decode_iq_f32le(bytes);
    require(decoded.size() == 1 && decoded[0] == std::complex<double>(0.5, -1), "IQ endian/order mismatch");
    rejected([] { rsa::decode_iq_f32le({}); });
    rejected([&] { rsa::decode_iq_f32le(std::span(bytes).first(7)); });
    auto invalid = bytes;
    invalid[2] = 0xc0; invalid[3] = 0x7f; // NaN
    rejected([&] { rsa::decode_iq_f32le(invalid); });
    invalid[2] = 0x80; invalid[3] = 0x7f; // Inf
    rejected([&] { rsa::decode_iq_f32le(invalid); });
    invalid[2] = 0; invalid[3] = 0x40; // 2.0, not normalized
    rejected([&] { rsa::decode_iq_f32le(invalid); });
}
void validation() {
    rsa::Settings{600e6, 40e6, 10e3, -20}.validate();
    rejected([] { rsa::Settings{600e6, 41e6, 10e3, -20}.validate(); });
    rejected([] { rsa::Settings{6.2e9, 20e6, 10e3, -20}.validate(); });
    rejected([] { rsa::Settings{9e3, 20e6, 10e3, -20}.validate(); });
    rejected([] { rsa::Settings{600e6, 20e6, 0, -20}.validate(); });
    rejected([] { rsa::Settings{600e6, 20e6, 30e6, -20}.validate(); });
    rejected([] { rsa::Settings{600e6, 20e6, 1e3, std::numeric_limits<double>::quiet_NaN()}.validate(); });
    std::vector<std::complex<double>> iq(16);
    const auto silence = rsa::spectrum(iq, 1e6, 600e6);
    require(std::all_of(silence.levels_dbfs.begin(), silence.levels_dbfs.end(),
                      [](double v) { return v == -300; }), "Silence must have finite floor");
    rejected([&] { rsa::spectrum(std::span(iq).first(15), 1e6, 600e6); });
    rejected([&] { rsa::spectrum(iq, -1, 600e6); });
    iq[0] = {std::numeric_limits<double>::infinity(), 0};
    rejected([&] { rsa::spectrum(iq, 1e6, 600e6); });
}
}
int main() {
    try { tones(); independent_dft(); decoding(); validation(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "Tone amplitude/frequency, DFT oracle, IQ decoding, malformed input, and bounds passed.\n";
}
