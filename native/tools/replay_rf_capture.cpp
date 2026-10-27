// Offline regression tool: raw signed ADC samples plus recorded RF settings.
// This deliberately never constructs a USB Session or accesses a device.
#include "rsa/calibration.hpp"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("Usage: rsa306_replay_rf_capture TABLES RAW SETTINGS");
        std::vector<rsa::wire::StoredBlock> blocks;
        for (std::uint16_t i = 2; i <= 6; ++i) {
            const auto path = std::filesystem::path(argv[1]) / ("block-" + std::to_string(i) + ".bin");
            const auto size = std::filesystem::file_size(path);
            if (size > 65536) throw std::runtime_error("Oversized calibration block.");
            rsa::wire::Bytes bytes(static_cast<std::size_t>(size));
            std::ifstream input(path, std::ios::binary);
            input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!input) throw std::runtime_error("Cannot read calibration block.");
            blocks.push_back({i, std::move(bytes)});
        }
        rsa::FactoryCalibration calibration(blocks, "offline-replay");
        std::ifstream raw(std::filesystem::path(argv[2]), std::ios::binary);
        std::ifstream settings_file{std::filesystem::path(argv[3])};
        if (!raw || !settings_file) throw std::runtime_error("Cannot open replay inputs.");
        std::cout << std::setprecision(17);
        std::string line;
        std::size_t count = 0;
        while (std::getline(settings_file, line)) {
            std::istringstream input(line);
            std::size_t n;
            double center, span, rbw, tuned, attenuation, gain, first_if;
            unsigned high;
            std::string extra;
            if (!(input >> n >> center >> span >> rbw >> tuned >> attenuation >> gain >> first_if >> high) ||
                (input >> extra) || n < 8 || n > (1U << 20) || !std::has_single_bit(n) || high > 1 || center < 22e6)
                throw std::runtime_error("Invalid RF replay settings.");
            rsa::wire::NativeSettings settings{{center, span, rbw, 0}, attenuation, gain, true};
            settings.validate();
            auto tune = rsa::wire::tuning(center);
            tune.tuned_hz = tuned; tune.first_if_hz = first_if; tune.high_side = high != 0;
            rsa::wire::Bytes bytes(n * 2);
            raw.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!raw) throw std::runtime_error("Incomplete raw sample block.");
            std::vector<std::int16_t> samples;
            samples.reserve(n);
            for (std::size_t i = 0; i < n; ++i) samples.push_back(std::bit_cast<std::int16_t>(rsa::wire::u16(bytes, 2 * i)));
            const auto spectrum = rsa::wire::if_spectrum(samples, settings.spectrum, tune.tuned_hz);
            const auto levels = calibration.levels(spectrum, settings, tune);
            std::cout << "{\"start_hz\":" << spectrum.start_hz << ",\"step_hz\":" << spectrum.step_hz
                      << ",\"enbw_hz\":" << spectrum.enbw_hz << ",\"levels\":[";
            for (std::size_t i = 0; i < levels.size(); ++i) { if (i) std::cout << ','; std::cout << levels[i]; }
            std::cout << "]}\n";
            if (!std::cout) throw std::runtime_error("Cannot write replay result.");
            ++count;
        }
        if (!count || settings_file.bad() || raw.peek() != std::char_traits<char>::eof() || raw.bad())
            throw std::runtime_error("Empty settings or extra/unreadable sample data.");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
