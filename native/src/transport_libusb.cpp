#include "rsa/transport.hpp"
#include <libusb.h>
#include <chrono>
#include <thread>

namespace rsa::usb {
namespace {
using Clock=std::chrono::steady_clock;
void check(int code,const char* action) {
    if(code<0) throw Error("usb_error",std::string(action)+": "+libusb_error_name(code),4);
}
struct DeviceList {
    libusb_device** items=nullptr;
    ~DeviceList() {if(items)libusb_free_device_list(items,1);}
};
double speed_mbps(int speed) {
    switch(speed) {
        case LIBUSB_SPEED_LOW:return 1.5;
        case LIBUSB_SPEED_FULL:return 12;
        case LIBUSB_SPEED_HIGH:return 480;
        case LIBUSB_SPEED_SUPER:return 5000;
        case LIBUSB_SPEED_SUPER_PLUS:return 10000;
        default:return 0;
    }
}
class LibusbTransport final : public Transport {
    struct State;
    struct Slot {
        State* owner=nullptr;
        std::size_t index=0;
        std::unique_ptr<libusb_transfer,decltype(&libusb_free_transfer)> transfer{nullptr,libusb_free_transfer};
        std::array<std::uint8_t,bulk_bytes> bytes{};
        bool pending=false;
    };
    struct State {
        std::unique_ptr<libusb_context,decltype(&libusb_exit)> context{nullptr,libusb_exit};
        std::unique_ptr<libusb_device_handle,decltype(&libusb_close)> handle{nullptr,libusb_close};
        std::array<Slot,bulk_slots> slots;
        CompletionOrder order;
        Consumer consume;
        std::size_t pending=0;
        bool claimed=false,streaming=false;
        ~State() {if(claimed)libusb_release_interface(handle.get(),0);}
    };
    std::unique_ptr<State> state_=std::make_unique<State>();
    static void callback(libusb_transfer* transfer) noexcept {
        auto& slot=*static_cast<Slot*>(transfer->user_data);
        slot.pending=false;
        --slot.owner->pending;
        slot.owner->order.complete(slot.index);
    }
    void submit(Slot& slot) {
        check(libusb_submit_transfer(slot.transfer.get()),"submit bulk transfer");
        slot.pending=true;++state_->pending;
    }
    void events() {
        timeval wait{0,10000};
        const auto status=libusb_handle_events_timeout(state_->context.get(),&wait);
        if(status!=LIBUSB_ERROR_INTERRUPTED)check(status,"handle USB events");
    }
public:
    void initialize() override {
        auto& s=*state_;
        libusb_context* raw=nullptr;check(libusb_init(&raw),"libusb_init");s.context.reset(raw);
        DeviceList devices;
        const auto n=libusb_get_device_list(s.context.get(),&devices.items);
        if(n<0)check(static_cast<int>(n),"list devices");
        libusb_device* selected=nullptr;unsigned count=0;
        for(decltype(+n) i=0;i<n;++i) {
            libusb_device_descriptor d{};
            if(libusb_get_device_descriptor(devices.items[i],&d)==0 && d.idVendor==0x699 && d.idProduct==0xa306) {
                selected=devices.items[i];++count;
            }
        }
        if(count!=1)throw Error("device_count","Connect exactly one RSA306B to this Mac; found "+std::to_string(count)+".",4);
        const auto speed=libusb_get_device_speed(selected);
        libusb_device_handle* opened=nullptr;check(libusb_open(selected,&opened),"open RSA306B");s.handle.reset(opened);
        if(speed<LIBUSB_SPEED_SUPER)throw Error("usb_speed","Raw IF acquisition requires a USB 3 SuperSpeed connection.",4);
        int config=0;check(libusb_get_configuration(s.handle.get(),&config),"get configuration");
        if(config!=1)check(libusb_set_configuration(s.handle.get(),1),"select configuration 1");
        check(libusb_claim_interface(s.handle.get(),0),"claim interface 0");s.claimed=true;
        // Preserve the qualified Mac setup: do not redundantly select an already
        // selected alternate, because doing so disturbs the FX3 endpoint state.
        unsigned char alternate=0;
        const auto status=libusb_control_transfer(s.handle.get(),0x81,0x0a,0,0,&alternate,1,1000);
        check(status,"get interface alternate");
        if(status!=1)throw Error("protocol_error","Missing interface alternate response.",4);
        if(alternate!=0)check(libusb_set_interface_alt_setting(s.handle.get(),0,0),"select alternate 0");
    }
    ~LibusbTransport() override {
        if(has_pending()) {try {drain();}catch(...) {}}
        if(has_pending())(void)state_.release(); // Live callback storage survives until process exit.
    }
    std::size_t interrupt(std::uint8_t endpoint,std::span<std::uint8_t> bytes,unsigned timeout_ms) override {
        int actual=0;
        check(libusb_interrupt_transfer(state_->handle.get(),endpoint,bytes.data(),static_cast<int>(bytes.size()),
                                       &actual,timeout_ms),endpoint==0x03?"command OUT":"command IN");
        if(actual<0 || static_cast<std::size_t>(actual)>bytes.size())
            throw Error("usb_transfer_error","Invalid command transfer length.",4);
        return static_cast<std::size_t>(actual);
    }
    bool has_pending() const noexcept override {return state_ && state_->pending!=0;}
    void start_bulk(Consumer consume) override {
        auto& s=*state_;
        if(has_pending())throw Error("usb_busy","Previous bulk transfers have not drained.",4);
        s.consume=std::move(consume);s.order.reset();s.streaming=true;
        for(std::size_t i=0;i<s.slots.size();++i) {
            auto& slot=s.slots[i];slot.owner=&s;slot.index=i;
            if(!slot.transfer)slot.transfer.reset(libusb_alloc_transfer(0));
            if(!slot.transfer)throw std::bad_alloc();
            libusb_fill_bulk_transfer(slot.transfer.get(),s.handle.get(),0x84,slot.bytes.data(),
                                      static_cast<int>(slot.bytes.size()),callback,&slot,3000);
            submit(slot);
        }
    }
    void pump() override {
        auto& s=*state_;
        events();
        // A finite batch allows acquisition cancellation/deadlines to be checked
        // even if the producer is faster than the consumer.
        for(std::size_t n=0;n<bulk_slots && s.streaming && s.order.ready();++n) {
            auto& slot=s.slots[s.order.head()];
            if(slot.transfer->status!=LIBUSB_TRANSFER_COMPLETED)
                throw Error("bulk_transfer_error","Bulk completion status "+std::to_string(slot.transfer->status)+
                            "; partial bytes were not emitted as a spectrum.",4);
            const auto actual=slot.transfer->actual_length;
            if(actual<=0 || static_cast<std::size_t>(actual)>slot.bytes.size())
                throw Error("bulk_transfer_error","Invalid bulk completion length.",4);
            s.streaming=s.consume(std::span(slot.bytes).first(static_cast<std::size_t>(actual)));
            s.order.consume();
            if(s.streaming)submit(slot);
        }
    }
    void drain() override {
        auto& s=*state_;s.streaming=false;
        for(auto& slot:s.slots)if(slot.pending)(void)libusb_cancel_transfer(slot.transfer.get());
        const auto deadline=Clock::now()+std::chrono::seconds(5);
        while(has_pending()) {
            if(Clock::now()>deadline)throw Error("usb_cleanup_timeout","USB callbacks did not drain within five seconds; this session cannot be reused.",4);
            try {events();}catch(const Error&) {std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        }
        s.consume={};
    }
};
}

std::unique_ptr<Transport> open() {
    return std::make_unique<LibusbTransport>();
}

std::vector<Device> discover_devices() {
    libusb_context* raw=nullptr;check(libusb_init(&raw),"libusb_init");
    std::unique_ptr<libusb_context,decltype(&libusb_exit)> context(raw,libusb_exit);
    DeviceList list;
    const auto count=libusb_get_device_list(context.get(),&list.items);
    if(count<0)check(static_cast<int>(count),"libusb_get_device_list");
    std::vector<Device> result;
    for(decltype(+count) i=0;i<count;++i) {
        auto* device=list.items[i];libusb_device_descriptor descriptor{};
        if(libusb_get_device_descriptor(device,&descriptor)<0)continue;
        if(descriptor.idVendor!=0x0699 || descriptor.idProduct!=0xa306)continue;
        Device found{descriptor.idVendor,descriptor.idProduct,descriptor.bcdUSB,
                     libusb_get_bus_number(device),libusb_get_device_address(device),
                     speed_mbps(libusb_get_device_speed(device)),{}};
        for(std::uint8_t c=0;c<descriptor.bNumConfigurations;++c) {
            libusb_config_descriptor* raw_config=nullptr;
            check(libusb_get_config_descriptor(device,c,&raw_config),"read configuration");
            std::unique_ptr<libusb_config_descriptor,decltype(&libusb_free_config_descriptor)> config(raw_config,libusb_free_config_descriptor);
            for(unsigned n=0;n<config->bNumInterfaces;++n) {
                const auto& interface=config->interface[n];
                for(int a=0;a<interface.num_altsetting;++a) {
                    const auto& alt=interface.altsetting[a];
                    Interface info{config->bConfigurationValue,alt.bInterfaceNumber,alt.bAlternateSetting,alt.bInterfaceClass,{}};
                    for(unsigned e=0;e<alt.bNumEndpoints;++e) {
                        const auto& endpoint=alt.endpoint[e];
                        info.endpoints.push_back({endpoint.bEndpointAddress,unsigned(endpoint.bmAttributes&3),endpoint.wMaxPacketSize});
                    }
                    found.interfaces.push_back(std::move(info));
                }
            }
        }
        result.push_back(std::move(found));
    }
    return result;
}
}
