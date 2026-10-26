#pragma once
#include "rsa/core.hpp"
#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace rsa::wire {
using Bytes = std::vector<std::uint8_t>;
using Register = std::pair<std::uint16_t, std::uint16_t>;
inline constexpr std::size_t frame_bytes = 16384, samples_per_frame = 8178;
inline constexpr double adc_rate = 112'000'000, digital_if = 28'000'000;
inline constexpr double min_center_hz = min_frequency_hz, min_sweep_hz = min_frequency_hz;
inline constexpr double lf_limit_hz = 22'000'000;
inline constexpr unsigned lf_gain_code = 199; // Fixed VGA; calibrated through this unit's block 2.
inline constexpr std::size_t max_sweep_segments = 512, max_trace_points = 4'194'304;
std::uint16_t u16(std::span<const std::uint8_t>, std::size_t);
std::uint32_t u32(std::span<const std::uint8_t>, std::size_t);
std::uint64_t u64(std::span<const std::uint8_t>, std::size_t);
Bytes words(std::span<const std::uint16_t>);
Bytes request(std::uint16_t sequence, std::uint16_t opcode, std::span<const std::uint8_t> payload = {});
Bytes response(std::span<const std::uint8_t>, std::uint16_t sequence, std::uint16_t opcode);
void check_post(std::span<const std::uint8_t> payload);
Bytes registers(std::span<const Register>);
std::uint16_t crc16(std::span<const std::uint8_t>);
std::size_t stored_block_size(std::uint16_t index, std::span<const std::uint8_t> header);
struct StoredBlock { std::uint16_t index; Bytes payload; };
struct Footer {
    std::uint16_t descriptor, status;
    std::uint32_t counter;
    std::uint64_t timestamp;
};
Footer footer(std::span<const std::uint8_t> frame);
struct Tuning {
    double requested_hz, tuned_hz;
    unsigned filter;
    std::array<std::uint32_t, 6> pll; // Registers 0…5; transmit in descending order.
    bool lf_path = false, high_side = true;
    double first_if_hz = 2440e6;
    std::uint32_t second_lo_word = 0x471e;
};
Tuning tuning(double center_hz);
void append_spi(std::vector<Register>&, std::uint16_t device_mask, std::uint32_t value);
std::size_t fft_size(double rbw_hz);
Spectrum if_spectrum(std::span<const std::int16_t>, const Settings&, double tuned_hz);

struct Capture {
    std::vector<std::int16_t> samples;
    std::uint64_t first_tick = 0, last_tick = 0;
    std::uint32_t first_counter = 0, last_counter = 0;
    std::uint16_t descriptor = 0, status = 0;
    std::size_t discarded_frames = 0, gap_events = 0;
    double host_start = 0, host_end = 0;
};
class FrameCollector {
public:
    FrameCollector(std::uint16_t descriptor, std::size_t target);
    void push(std::span<const std::uint8_t> bytes);
    bool complete() const;
    Capture result;
private:
    std::size_t target_;
    std::size_t contiguous_frames_ = 0;
    Bytes remainder_;
    std::optional<Footer> previous_;
};
struct NativeSettings {
    Settings spectrum;
    double attenuation_db = 0, if_gain_db = 17;
    bool factory_dbm = false; // LF code 199 / RF route 0x71, measured factory tables.
    void validate() const;
};
std::vector<Register> configuration_registers(const NativeSettings&, std::uint16_t tag);
// Overlapping, at most 20 MHz segments; the LF/RF join may extend past selected edges.
std::vector<NativeSettings> sweep_plan(double start_hz, double stop_hz, double rbw_hz,
                                      double attenuation_db, double if_gain_db, bool factory_dbm = false);
struct Identity {
    std::string model, serial;
    std::uint16_t firmware = 0, fpga = 0, hardware = 0, oscillator_dac = 0;
};
class Session {
public:
    // One construction attempt per worker process, including failed opens.
    // Repeated configure/acquire is supported until the first error. Reconnect
    // requires a fresh worker process; see docs/rsa306b-validation.md.
    explicit Session(std::function<bool()> cancelled,
                     std::function<void(bool, std::span<const std::uint8_t>)> trace = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    const Identity& identity() const;
    // Diagnostic read of known stored blocks before configuring/acquiring.
    // Returns CRC-checked payloads; it does not enable calibrated measurements.
    std::vector<StoredBlock> read_calibration();
    Tuning configure(const NativeSettings&);
    Capture acquire(std::size_t samples);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rsa::wire
