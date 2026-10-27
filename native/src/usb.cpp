#include "rsa/transport.hpp"

namespace rsa {
std::vector<Device> discover() { return usb::discover_devices(); }
}
