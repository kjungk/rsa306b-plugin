#include "rsa/core.hpp"
#include <libusb.h>
#include <memory>

namespace rsa {
namespace {
void check(int status, const char* operation) {
    if (status < 0) {
        throw Error("usb_error", std::string(operation) + ": " + libusb_error_name(status), 4);
    }
}
double speed_mbps(int speed) {
    switch (speed) {
        case LIBUSB_SPEED_LOW: return 1.5;
        case LIBUSB_SPEED_FULL: return 12;
        case LIBUSB_SPEED_HIGH: return 480;
        case LIBUSB_SPEED_SUPER: return 5000;
        case LIBUSB_SPEED_SUPER_PLUS: return 10000;
        default: return 0;
    }
}
struct DeviceList {
    libusb_device** items = nullptr;
    ~DeviceList() { if (items) libusb_free_device_list(items, 1); }
};
}

std::vector<Device> discover() {
    libusb_context* raw = nullptr;
    check(libusb_init(&raw), "libusb_init");
    std::unique_ptr<libusb_context, decltype(&libusb_exit)> context(raw, libusb_exit);
    DeviceList list;
    const auto count = libusb_get_device_list(context.get(), &list.items);
    if (count < 0) check(static_cast<int>(count), "libusb_get_device_list");
    std::vector<Device> result;
    for (decltype(+count) i = 0; i < count; ++i) {
        auto* device = list.items[i];
        libusb_device_descriptor descriptor{};
        // An unrelated device can disappear during enumeration.
        if (libusb_get_device_descriptor(device, &descriptor) < 0) continue;
        if (descriptor.idVendor != 0x0699 || descriptor.idProduct != 0xa306) continue;
        Device found{descriptor.idVendor, descriptor.idProduct, descriptor.bcdUSB,
                     libusb_get_bus_number(device), libusb_get_device_address(device),
                     speed_mbps(libusb_get_device_speed(device)), {}};
        for (std::uint8_t c = 0; c < descriptor.bNumConfigurations; ++c) {
            libusb_config_descriptor* config_raw = nullptr;
            check(libusb_get_config_descriptor(device, c, &config_raw), "read configuration");
            std::unique_ptr<libusb_config_descriptor, decltype(&libusb_free_config_descriptor)>
                config(config_raw, libusb_free_config_descriptor);
            for (unsigned n = 0; n < config->bNumInterfaces; ++n) {
                const auto& interface = config->interface[n];
                for (int a = 0; a < interface.num_altsetting; ++a) {
                    const auto& alt = interface.altsetting[a];
                    Interface info{config->bConfigurationValue, alt.bInterfaceNumber,
                                   alt.bAlternateSetting, alt.bInterfaceClass, {}};
                    for (unsigned e = 0; e < alt.bNumEndpoints; ++e) {
                        const auto& endpoint = alt.endpoint[e];
                        info.endpoints.push_back({endpoint.bEndpointAddress,
                                                  static_cast<unsigned>(endpoint.bmAttributes & 3),
                                                  endpoint.wMaxPacketSize});
                    }
                    found.interfaces.push_back(std::move(info));
                }
            }
        }
        result.push_back(std::move(found));
    }
    return result;
}
}  // namespace rsa
