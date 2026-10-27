#pragma once

#include <complex>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rsa {
inline constexpr double min_frequency_hz = 9'000;
inline constexpr double max_frequency_hz = 6'200'000'000;
inline constexpr double max_bandwidth_hz = 40'000'000;

struct Error : std::runtime_error {
    std::string code;
    int exit_code;
    Error(std::string name, std::string message, int exit_status)
        : std::runtime_error(std::move(message)), code(std::move(name)), exit_code(exit_status) {}
};

struct Settings {
    double center_hz;
    double span_hz;
    double rbw_hz;
    double reference_dbm;
    void validate() const;
};

struct Endpoint {
    unsigned address;
    unsigned transfer_type;
    unsigned max_packet_bytes;
};
struct Interface {
    unsigned configuration;
    unsigned number;
    unsigned alternate;
    unsigned class_code;
    std::vector<Endpoint> endpoints;
};
struct Device {
    unsigned vendor;
    unsigned product;
    unsigned usb_version_bcd;
    unsigned bus;
    unsigned address;
    double speed_mbps;
    std::vector<Interface> interfaces;
};
// Descriptor reads only; never resets, claims, or sends vendor commands.
// Windows opens the existing driver exclusively for bounded descriptor requests.
std::vector<Device> discover();

// This is an explicit offline interchange format, NOT the RSA USB sample format.
std::vector<std::complex<double>> decode_iq_f32le(std::span<const std::uint8_t> bytes);
struct Spectrum {
    double start_hz;
    double step_hz;
    double enbw_hz;
    std::vector<double> levels_dbfs;
};
Spectrum spectrum(std::span<const std::complex<double>> iq, double sample_rate_hz,
                  double center_hz);
}  // namespace rsa
