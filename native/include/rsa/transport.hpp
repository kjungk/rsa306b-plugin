#pragma once
#include "rsa/core.hpp"
#include <array>
#include <functional>
#include <memory>

namespace rsa::usb {
inline constexpr std::size_t bulk_slots = 16;
inline constexpr std::size_t bulk_bytes = 262144;

// Completions may arrive in any order. Consume the byte stream in submission
// order, and submit a slot again only after its completed bytes are consumed.
class CompletionOrder {
    std::array<bool, bulk_slots> ready_{};
    std::size_t head_ = 0;
public:
    void reset() { ready_.fill(false); head_ = 0; }
    void complete(std::size_t slot) { ready_.at(slot) = true; }
    bool ready() const { return ready_[head_]; }
    std::size_t head() const { return head_; }
    void consume() { ready_[head_] = false; head_ = (head_ + 1) % bulk_slots; }
};

class Transport {
public:
    // Return false when enough samples have arrived. drain() cancels the rest
    // without delivering further chunks to the caller.
    using Consumer = std::function<bool(std::span<const std::uint8_t>)>;
    virtual ~Transport() = default;
    virtual void initialize() = 0;
    virtual std::size_t interrupt(std::uint8_t endpoint, std::span<std::uint8_t> data,
                                  unsigned timeout_ms) = 0;
    virtual void start_bulk(Consumer consume) = 0;
    virtual void pump() = 0;
    virtual void drain() = 0;
    virtual bool has_pending() const noexcept = 0;
};

// Factory performs no USB I/O. Acquisition assigns the result before initialize
// so exceptional pending I/O cannot outlive its cooperative DeviceLock owner.
std::unique_ptr<Transport> open();
std::vector<Device> discover_devices();
}
