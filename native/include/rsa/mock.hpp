#pragma once
#include "rsa/protocol.hpp"

namespace rsa::mock {
// Analytic, generated values only: never copied from a physical analyzer.
std::vector<wire::StoredBlock> calibration();
wire::Capture capture(const wire::NativeSettings&, const wire::Tuning&,
                      std::size_t samples, std::uint64_t first_tick);
}
