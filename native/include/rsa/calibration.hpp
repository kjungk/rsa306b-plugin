#pragma once
#include "rsa/protocol.hpp"
#include <memory>

namespace rsa {
// Applies the device's factory LF/RF tables. Does not certify current RF accuracy.
// Factory LF uses fixed VGA 199; factory RF uses route 0x71. Numerical model
// verification is separate from independent absolute/hardware acceptance.
class FactoryCalibration {
public:
    FactoryCalibration(const std::vector<wire::StoredBlock>&, std::string serial);
    ~FactoryCalibration();
    const std::string& id() const;
    const std::string& serial() const;
    double gain(double requested_center, unsigned rf_bits, double attenuation, double if_gain,
                unsigned lf_code = wire::lf_gain_code) const;
    std::vector<double> eq(const wire::Tuning&, unsigned rf_bits,
                           unsigned lf_code = wire::lf_gain_code) const;
    std::vector<double> lf_axis() const;
    std::vector<double> levels(const Spectrum&, const wire::NativeSettings&, const wire::Tuning&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
