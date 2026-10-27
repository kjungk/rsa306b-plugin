#include "rsa/cypress_abi.hpp"
#include "rsa/transport.hpp"
#include <functional>
#include <iostream>
#include <numeric>
#include <random>

namespace {
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
void rejected(const std::function<void()>& action) {
    try {action();}catch(const rsa::Error&) {return;}
    throw std::runtime_error("Malformed driver/descriptor data was accepted");
}
std::vector<std::uint8_t> hex(std::string_view text) {
    std::vector<std::uint8_t> bytes;
    const auto nibble=[](char c){return c>='0' && c<='9'?c-'0':c-'a'+10;};
    for(std::size_t i=0;i<text.size();i+=2)
        bytes.push_back(static_cast<std::uint8_t>((nibble(text[i])<<4)|nibble(text[i+1])));
    return bytes;
}
void abi_packets() {
    namespace abi=rsa::usb::cypress;
    // Fixed Cypress driver packet layout:
    // USB GET_DESCRIPTOR(device,18), 5-second timeout, packed 38-byte header.
    const auto expected=hex("8006000100001200050000000000000000000000000000000000000000002600000012000000");
    const auto request=abi::control_header(0x80,6,0x0100,0,18,5000);
    require(expected.size()==request.size() && std::equal(expected.begin(),expected.end(),request.begin()),
            "EP0 request disagrees with independently verified probe bytes");
    const auto get_interface=abi::control_header(0x81,10,0,0,1,1000);
    require(get_interface[0]==0x81 && get_interface[1]==10 && abi::get16(get_interface,6)==1,
            "GET_INTERFACE must be a standard interface read");
    const auto direct=abi::direct_header(0x84);
    for(std::size_t i=0;i<direct.size();++i)
        require(direct[i]==(i==13?0x84:0),"Direct request must use only endpoint with zero buffer offsets");
    require(abi::direct_transfer==((0x22U<<16)|(18U<<2)|3U),"Direct IOCTL must use METHOD_NEITHER");
    require(abi::get_speed==((0x22U<<16)|(19U<<2)),"Speed IOCTL mismatch");
    auto status=direct;
    abi::check_status(status);
    abi::put32(status,14,0xc0000001U);rejected([&]{abi::check_status(status);});
    status=direct;abi::put32(status,18,0xc0000011U);rejected([&]{abi::check_status(status);});
    rejected([&]{abi::check_status(std::span(status).first(21));});
}
void descriptors() {
    namespace abi=rsa::usb::cypress;
    // RSA306B USB configuration descriptor regression vector
    // on 2026-10-26. This is public device topology, not stored calibration.
    const auto bytes=hex("0902390001010080320904000003ff0000000705840200040006300f0000000705030300040106300200000c0705830300040106300200000c");
    rsa::Device device{0x0699,0xa306,0x0300,0,1,5000,abi::parse_configuration(bytes)};
    abi::check_topology(device);
    require(device.interfaces.front().endpoints.front().address==0x84,"Captured bulk endpoint lost");
    // Every truncated prefix must fail rather than allowing out-of-bounds reads.
    for(std::size_t size=0;size<bytes.size();++size)
        rejected([&]{(void)abi::parse_configuration(std::span(bytes).first(size));});
    for(const auto offset:{9U,18U,25U,31U,38U,44U,51U}) {
        auto bad=bytes;bad[offset]=0;
        rejected([&]{(void)abi::parse_configuration(bad);});
    }
    auto bad=bytes;bad[13]=2;rejected([&]{(void)abi::parse_configuration(bad);});
    bad=bytes;bad[4]=2;rejected([&]{(void)abi::parse_configuration(bad);});
    bad=bytes;bad[46]=3;rejected([&]{(void)abi::parse_configuration(bad);}); // Duplicate endpoint.
    device.interfaces.front().alternate=1;rejected([&]{abi::check_topology(device);});
    device.interfaces.front().alternate=0;
    device.interfaces.front().endpoints.front().transfer_type=3;rejected([&]{abi::check_topology(device);});
}
void completion_order() {
    // Driver callbacks can be observed in any order; shuffled completions must
    // still reconstruct the monotonically numbered wire stream across reuse.
    rsa::usb::CompletionOrder order;
    std::array<std::size_t,rsa::usb::bulk_slots> arrivals{};
    std::iota(arrivals.begin(),arrivals.end(),0);
    std::mt19937 generator(306);
    std::vector<std::size_t> delivered;
    for(std::size_t cycle=0;cycle<200;++cycle) {
        std::shuffle(arrivals.begin(),arrivals.end(),generator);
        for(const auto slot:arrivals) {
            order.complete(slot);
            while(order.ready()) {
                delivered.push_back(cycle*rsa::usb::bulk_slots+order.head());
                order.consume();
            }
        }
    }
    require(delivered.size()==3200,"Lost a queued completion");
    for(std::size_t i=0;i<delivered.size();++i)require(delivered[i]==i,"Out-of-order bytes reached the frame collector");
    order.complete(5);order.reset();require(!order.ready(),"Fresh capture inherited stale completion");
    order.complete(1);require(!order.ready(),"Later completion skipped the pending head");
    order.complete(0);require(order.ready(),"Head completion was lost");
    order.consume();require(order.ready() && order.head()==1,"Buffered later completion was lost");
}
}
int main() {
    try {abi_packets();descriptors();completion_order();}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
    std::cout<<"Verified Cypress ABI bytes, captured USB topology, malformed descriptors, and ordered bulk completions.\n";
}
