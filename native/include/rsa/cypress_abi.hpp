#pragma once
#include "rsa/core.hpp"
#include <algorithm>
#include <array>
#include <cstddef>

namespace rsa::usb::cypress {
// Original byte encoders for the published CyUSB3 DeviceIoControl ABI. No
// Cypress/Tektronix library or proprietary implementation is included or linked.
// Reference: Infineon CyUSB3 USB Suite, cyioctl.h (packed SINGLE_TRANSFER),
// SHA-256 c4d324b306d4f871ab9ee028a02dd043ffbf4b5dd7e1b3a64c8604ff204c3cd0.
// https://www.infineon.com/assets/row/public/documents/30/416/infineon-cyusb3-usb-suite-source-software-code-example-en.zip
inline constexpr std::uint32_t get_address = 0x00220010;
inline constexpr std::uint32_t ep0_transfer = 0x00220020;
inline constexpr std::uint32_t direct_transfer = 0x0022004b;
inline constexpr std::uint32_t get_speed = 0x0022004c;
inline constexpr std::size_t header_bytes = 38;
inline constexpr std::size_t endpoint_offset = 13, nt_status_offset = 14,
    usbd_status_offset = 18, buffer_offset = 30, buffer_length_offset = 34;
using Header = std::array<std::uint8_t, header_bytes>;

inline void need(bool ok, const char* why) {
    if (!ok) throw Error("usb_descriptor_error", why, 4);
}
inline void put16(std::span<std::uint8_t> b, std::size_t offset, std::uint16_t value) {
    b[offset] = static_cast<std::uint8_t>(value);
    b[offset+1] = static_cast<std::uint8_t>(value >> 8);
}
inline void put32(std::span<std::uint8_t> b, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) b[offset+i] = static_cast<std::uint8_t>(value >> (8*i));
}
inline std::uint16_t get16(std::span<const std::uint8_t> b, std::size_t offset) {
    need(offset <= b.size() && b.size()-offset >= 2, "Truncated USB descriptor word.");
    return static_cast<std::uint16_t>(b[offset] | (std::uint16_t(b[offset+1]) << 8));
}
inline std::uint32_t get32(std::span<const std::uint8_t> b, std::size_t offset) {
    need(offset <= b.size() && b.size()-offset >= 4, "Truncated driver transfer status.");
    std::uint32_t value=0;
    for (unsigned i=0; i<4; ++i) value |= std::uint32_t(b[offset+i]) << (8*i);
    return value;
}
inline Header direct_header(std::uint8_t endpoint) {
    Header header{};
    header[endpoint_offset] = endpoint;
    return header; // Direct ABI uses a separate payload, with BufferOffset/Length=0.
}
inline Header control_header(std::uint8_t request_type, std::uint8_t request,
                             std::uint16_t value, std::uint16_t index,
                             std::uint16_t length, unsigned timeout_ms) {
    Header header{};
    header[0]=request_type; header[1]=request;
    put16(header,2,value); put16(header,4,index); put16(header,6,length);
    put32(header,8,std::max(1U,(timeout_ms+999U)/1000U));
    put32(header,buffer_offset,static_cast<std::uint32_t>(header_bytes));
    put32(header,buffer_length_offset,length);
    return header;
}
inline void check_status(std::span<const std::uint8_t> header) {
    const auto nt=get32(header,nt_status_offset), usbd=get32(header,usbd_status_offset);
    if (nt || usbd) throw Error("usb_transfer_error", "Driver transfer failed (NTSTATUS="+
        std::to_string(nt)+", USBD_STATUS="+std::to_string(usbd)+").",4);
}

inline std::vector<Interface> parse_configuration(std::span<const std::uint8_t> bytes) {
    need(bytes.size()>=9 && bytes[0]==9 && bytes[1]==2, "Invalid USB configuration descriptor.");
    need(get16(bytes,2)==bytes.size() && bytes.size()<=4096, "Invalid USB configuration length.");
    std::vector<Interface> interfaces;
    std::array<bool,256> interface_numbers{};
    unsigned expected_endpoints=0;
    for (std::size_t at=9; at<bytes.size();) {
        need(bytes.size()-at>=2 && bytes[at]>=2 && bytes[at]<=bytes.size()-at,
             "Truncated USB configuration item.");
        const auto length=bytes[at], type=bytes[at+1];
        if (type==4) {
            need(length>=9,"Truncated USB interface descriptor.");
            if (!interfaces.empty()) need(interfaces.back().endpoints.size()==expected_endpoints,
                                          "USB endpoint count mismatch.");
            interfaces.push_back({bytes[5],bytes[at+2],bytes[at+3],bytes[at+5],{}});
            interface_numbers[bytes[at+2]]=true;
            expected_endpoints=bytes[at+4];
        } else if (type==5) {
            need(length>=7 && !interfaces.empty(),"Invalid USB endpoint descriptor.");
            auto& endpoints=interfaces.back().endpoints;
            need(std::none_of(endpoints.begin(),endpoints.end(),[&](const Endpoint& e) {
                return e.address==bytes[at+2];
            }),"Duplicate USB endpoint descriptor.");
            endpoints.push_back({bytes[at+2],unsigned(bytes[at+3]&3),get16(bytes,at+4)});
        }
        at+=length;
    }
    need(!interfaces.empty() && interfaces.back().endpoints.size()==expected_endpoints,
         "USB interface/endpoint count mismatch.");
    need(std::count(interface_numbers.begin(),interface_numbers.end(),true)==bytes[4],
         "USB configuration interface count mismatch.");
    return interfaces;
}
inline void check_topology(const Device& device) {
    need(device.vendor==0x0699 && device.product==0xa306,"Unexpected USB device identity.");
    need(device.interfaces.size()==1,"RSA306B requires one USB interface/alternate.");
    const auto& i=device.interfaces.front();
    need(i.configuration==1 && i.number==0 && i.alternate==0 && i.endpoints.size()==3,
         "Unsupported RSA306B USB interface layout.");
    for (const auto endpoint : {0x03U,0x83U,0x84U}) {
        need(std::count_if(i.endpoints.begin(),i.endpoints.end(),[&](const Endpoint& e) {
            return e.address==endpoint && e.transfer_type==(endpoint==0x84?2U:3U) && e.max_packet_bytes==1024;
        })==1,"Unsupported RSA306B USB endpoint layout.");
    }
}
}
