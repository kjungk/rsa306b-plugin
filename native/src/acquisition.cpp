#include "rsa/protocol.hpp"
#include "rsa/device_lock.hpp"
#include <libusb.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <exception>
#include <thread>

namespace rsa::wire {
namespace {
using Clock=std::chrono::steady_clock;
void usb_check(int code,const char* action) {
    if (code<0) throw Error("usb_error",std::string(action)+": "+libusb_error_name(code),4);
}
void protocol_need(bool ok,const char* message) {
    if (!ok) throw Error("protocol_error",message,4);
}
double wall_time() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string ascii(std::span<const std::uint8_t> bytes) {
    const auto end=std::find(bytes.begin(),bytes.end(),0);
    protocol_need(end!=bytes.end(),"Unterminated device identity.");
    for (auto it=bytes.begin();it!=end;++it) protocol_need(*it>=32 && *it<127,"Invalid device identity.");
    return {bytes.begin(),end};
}
}
struct Session::Impl {
    std::unique_ptr<DeviceLock> ownership; // Held through USB cleanup, including exceptional teardown.
    std::unique_ptr<libusb_context,decltype(&libusb_exit)> context{nullptr,libusb_exit};
    std::unique_ptr<libusb_device_handle,decltype(&libusb_close)> handle{nullptr,libusb_close};
    std::function<bool()> cancelled;
    std::function<void(bool, std::span<const std::uint8_t>)> trace;
    bool claimed=false, communicating=false, configured=false, failed=false;
    std::uint16_t sequence=0, tag=0;
    Identity id;
    explicit Impl(std::function<bool()> stop):cancelled(std::move(stop)) {}
    void check_cancel() const {
        if (cancelled()) throw Error("interrupted","Native acquisition cancelled.",130);
    }
    Bytes command(std::uint16_t op,std::span<const std::uint8_t> payload={}) {
        // Reserve a sequence for each attempt. A failed logger or uncertain USB
        // completion must not cause cleanup Stop to reuse a previous Run's ID.
        const auto current=sequence++;
        auto out=request(current,op,payload);
        if(trace) trace(true,out);
        int actual=0;
        usb_check(libusb_interrupt_transfer(handle.get(),0x03,out.data(),static_cast<int>(out.size()),&actual,1000),"command OUT");
        protocol_need(actual==static_cast<int>(out.size()),"Partial command write; command was not retried.");
        std::array<std::uint8_t,1024> in{};
        usb_check(libusb_interrupt_transfer(handle.get(),0x83,in.data(),static_cast<int>(in.size()),&actual,1000),"command IN");
        if(trace) trace(false,std::span(in).first(static_cast<std::size_t>(actual)));
        auto result=response(std::span(in).first(static_cast<std::size_t>(actual)),current,op);
        return result;
    }
    Bytes cmd_words(std::uint16_t op,std::initializer_list<std::uint16_t> values) {return command(op,words(values));}
    void cleanup_command(std::uint16_t op,std::span<const std::uint8_t> payload={}) noexcept {
        // A full/broken diagnostic log must not prevent Stop or restoring read mode.
        auto saved_trace=std::move(trace);
        trace={}; // A moved-from std::function is not guaranteed to be empty.
        try {command(op,payload);} catch(...) {}
        trace=std::move(saved_trace);
    }
    std::uint16_t read(std::uint16_t address) {
        auto b=cmd_words(4,{1,address});
        protocol_need(b.size()==4 && u16(b,0)==1,"Register read returned an invalid count.");
        return u16(b,2);
    }
    void write(std::span<const Register> values) {command(5,registers(values));}
    void write(std::initializer_list<Register> values) {write(std::span(values.begin(),values.size()));}
    std::uint16_t next_tag() { do {++tag;} while(tag==0xffff || tag==0); return tag; }
    Bytes block(std::uint16_t index) {
        cmd_words(11,{1});
        try {
            auto header=cmd_words(17,{index});
            const auto length=stored_block_size(index,header);
            Bytes bytes;
            for (std::uint16_t page=0;bytes.size()<length;++page) {
                check_cancel();
                auto data=cmd_words(19,{index,page});
                protocol_need(data.size()==256,"Unexpected calibration page length.");
                bytes.insert(bytes.end(),data.begin(),data.end());
            }
            bytes.resize(length);
            protocol_need(crc16(bytes)==u16(header,0),"Calibration block CRC mismatch; refusing initialization.");
            cmd_words(11,{0}); return bytes;
        } catch (...) {const auto normal=words(std::array<std::uint16_t,1>{0});cleanup_command(11,normal);throw;}
    }
    void open() {
        check_cancel();
        ownership=std::make_unique<DeviceLock>();
        libusb_context* raw=nullptr; usb_check(libusb_init(&raw),"libusb_init"); context.reset(raw);
        libusb_device** devices=nullptr;
        const auto n=libusb_get_device_list(context.get(),&devices);
        if(n<0) usb_check(static_cast<int>(n),"list devices");
        libusb_device* selected=nullptr; unsigned count=0;
        for (decltype(+n) i=0;i<n;++i) {
            libusb_device_descriptor d{};
            if(libusb_get_device_descriptor(devices[i],&d)==0 && d.idVendor==0x699 && d.idProduct==0xa306) {selected=devices[i];++count;}
        }
        if(count!=1) {libusb_free_device_list(devices,1);throw Error("device_count","Connect exactly one RSA306B to this Mac; found "+std::to_string(count)+".",4);}
        const auto speed=libusb_get_device_speed(selected);
        libusb_device_handle* opened=nullptr;
        const auto status=libusb_open(selected,&opened);
        libusb_free_device_list(devices,1); usb_check(status,"open RSA306B");handle.reset(opened);
        if(speed<LIBUSB_SPEED_SUPER) throw Error("usb_speed","Raw IF acquisition requires a USB 3 SuperSpeed connection.",4);
        int config=0;usb_check(libusb_get_configuration(handle.get(),&config),"get configuration");
        if(config!=1) usb_check(libusb_set_configuration(handle.get(),1),"select configuration 1");
        usb_check(libusb_claim_interface(handle.get(),0),"claim interface 0");claimed=true;
        // Do not resend SET_INTERFACE for an already-selected alternate. The
        // FX3 endpoint state can be disturbed by redundant interface selection.
        unsigned char alternate=0;
        const auto alt_status=libusb_control_transfer(handle.get(),0x81,0x0a,0,0,&alternate,1,1000);
        usb_check(alt_status,"get interface alternate");
        protocol_need(alt_status==1,"Missing interface alternate response.");
        if(alternate!=0)usb_check(libusb_set_interface_alt_setting(handle.get(),0,0),"select alternate 0");
        command(22); communicating=true;
        check_post(command(23)); // Read stored startup results before RF/register access.
        command(2);
        const auto fw=command(3);protocol_need(fw.size()==2,"Unexpected firmware response.");
        id.firmware=u16(fw,0);
        // Windows reads identity in mode 1 and returns to mode 0 before its
        // first FPGA register read. Preserve that observed ordering; it does
        // not repair a failed power-on self-test.
        const auto identity=block(0);
        protocol_need(identity.size()>=512,"Device identity block is too short.");
        id.model=ascii(std::span(identity).first(256));id.serial=ascii(std::span(identity).subspan(256,256));
        id.hardware=read(1);id.fpga=read(2);
        if(id.model!="RSA306B" || id.hardware!=7 || id.firmware!=0x0701 || id.fpga!=0x0201)
            throw Error("unsupported_device_revision","This implementation is qualified only for RSA306B HW7 / firmware 1.7 / FPGA 2.1.",4);
        const auto trim=block(1);
        protocol_need((trim.size()==8 && u32(trim,0)==0) || (trim.size()==16 && u32(trim,0)==1),"Unsupported oscillator calibration format.");
        protocol_need(u32(trim,4)<=1023,"Oscillator trim outside its 10-bit range.");
        id.oscillator_dac=static_cast<std::uint16_t>(u32(trim,4));
        tag=read(0x43);
        initialize();
    }
    void initialize() {
        check_cancel();
        // Volatile HW7 power and RF-path enables, named in docs/USBProtocol.md.
        write({{0x8012,1},{0x8035,0x10},{0x4030,1},{0x8030,0x10},{0x8030,0x40},
               {0x4033,7},{0x8033,1},{0x8034,1},{0x8034,2},{0x8034,8},
               {0x8035,2},{0x8035,1},{0x8035,4},{0x8015,4},{0x8015,2},{0x8015,1}});
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::vector<Register> regs;
        append_spi(regs,0x100,0x80f8); // DS1722 temperature monitor configuration.
        append_spi(regs,0x20,static_cast<std::uint32_t>(0xf000U | (std::uint32_t(id.oscillator_dac)<<2)));
        for(auto value:{0x168196U,0x100014U,0x3801U}) append_spi(regs,0x40,value); // Reference PLL.
        for(auto value:{0x240071U,0x0d5128U,0x471eU}) append_spi(regs,0x08,value); // Second LO.
        const auto initial=tuning(600e6);
        for(int i=5;i>=0;--i) append_spi(regs,0x04,initial.pll[static_cast<std::size_t>(i)]);
        append_spi(regs,0x10,6); // IF VGA +17 dB.
        append_spi(regs,0x01,0x14); // LF VGA default (RF path selected below).
        append_spi(regs,0x02,0xffffffc0); // 31.5 dB attenuation during startup.
        regs.insert(regs.end(),{{0x4032,1},{0x8035,0x10},{0x8034,4},
                    {0x4040,0xff},{0x8040,9},{0x47,79},{0x8041,0x8000}});
        // DATA_INV=1 yields increasing RF frequency with increasing positive digital IF.
        write(regs);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        lock();
    }
    void lock() {
        const auto deadline=Clock::now()+std::chrono::seconds(2);
        do {
            check_cancel();if((read(0x16)&7)==7) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while(Clock::now()<deadline);
        throw Error("lo_unlock","Reference/LO lock did not settle within two seconds.",4);
    }
    Tuning configure(const NativeSettings& settings) {
        settings.validate();check_cancel();
        const auto t=tuning(settings.spectrum.center_hz);
        write(configuration_registers(settings,next_tag()));lock();configured=true;return t;
    }
    struct Slot {
        Impl* owner=nullptr;
        std::unique_ptr<libusb_transfer,decltype(&libusb_free_transfer)> transfer{nullptr,libusb_free_transfer};
        std::array<unsigned char,262144> bytes{};
        bool pending=false;
    };
    std::array<Slot,16> slots;
    std::size_t pending=0;
    bool draining=false;
    std::unique_ptr<FrameCollector> collector;
    std::exception_ptr failure;
    static void callback(libusb_transfer* transfer) noexcept {
        auto& slot=*static_cast<Slot*>(transfer->user_data);auto& self=*slot.owner;
        slot.pending=false;--self.pending;
        if(self.draining) return;
        try {
            if(transfer->status!=LIBUSB_TRANSFER_COMPLETED)
                throw Error("bulk_transfer_error","Bulk completion status "+std::to_string(transfer->status)+"; partial bytes were not emitted as a spectrum.",4);
            self.collector->push(std::span(slot.bytes).first(static_cast<std::size_t>(transfer->actual_length)));
            if(!self.collector->complete() && !self.failure) {
                usb_check(libusb_submit_transfer(transfer),"resubmit bulk transfer");slot.pending=true;++self.pending;
            }
        } catch(...) {self.failure=std::current_exception();}
    }
    void pump() {
        timeval wait{0,10000};
        const auto status=libusb_handle_events_timeout(context.get(),&wait);
        if(status!=LIBUSB_ERROR_INTERRUPTED) usb_check(status,"handle USB events");
    }
    void drain() {
        draining=true;
        for(auto& slot:slots) if(slot.pending) {
            const auto status=libusb_cancel_transfer(slot.transfer.get());
            if(status!=0 && status!=LIBUSB_ERROR_NOT_FOUND) {
                // Completion callbacks still own the buffers; never free an in-flight transfer.
            }
        }
        const auto deadline=Clock::now()+std::chrono::seconds(5);
        while(pending) {
            if(Clock::now()>deadline)
                throw Error("usb_cleanup_timeout","USB callbacks did not drain within five seconds; this session cannot be reused.",4);
            try {pump();} catch(const Error&) {
                // Cancellation completion may still arrive after an event-loop error.
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    Capture acquire(std::size_t n) {
        protocol_need(configured,"Configure before acquiring.");check_cancel();
        if(n<8 || n>(1U<<20) || !std::has_single_bit(n)) throw Error("invalid_fft","Invalid native sample count.",2);
        command(2);write({{0x43,0xffff},{0x43,next_tag()}});
        collector=std::make_unique<FrameCollector>(tag,n);
        failure=nullptr;draining=false;
        try {
            for(auto& slot:slots) {
                slot.owner=this;
                if(!slot.transfer) slot.transfer.reset(libusb_alloc_transfer(0));
                if(!slot.transfer) throw std::bad_alloc();
                libusb_fill_bulk_transfer(slot.transfer.get(),handle.get(),0x84,slot.bytes.data(),
                                          static_cast<int>(slot.bytes.size()),callback,&slot,3000);
                usb_check(libusb_submit_transfer(slot.transfer.get()),"submit bulk transfer");slot.pending=true;++pending;
            }
            collector->result.host_start=wall_time();command(1);
            const auto deadline=Clock::now()+std::chrono::seconds(3);
            while(!collector->complete()) {
                check_cancel();if(failure) std::rethrow_exception(failure);
                if(Clock::now()>deadline) throw Error("acquisition_timeout","No complete contiguous IF block with the current frame tag within three seconds.",4);
                pump();
            }
            if(failure) std::rethrow_exception(failure);
            collector->result.host_end=wall_time();command(2);drain();return std::move(collector->result);
        } catch(...) {
            const auto error=std::current_exception();
            cleanup_command(2);
            drain();std::rethrow_exception(error);
        }
    }
    ~Impl() {
        if(communicating) cleanup_command(2);
        if(pending) {try {drain();} catch(...) {}}
        if(claimed) libusb_release_interface(handle.get(),0);
    }
};
Session::Session(std::function<bool()> cancelled, std::function<void(bool, std::span<const std::uint8_t>)> trace)
{
    // Recreating several USB sessions in one process reproduced a silent bulk
    // endpoint after process exit. Keep the proven CLI worker lifetime explicit
    // until that host/device teardown interaction is understood.
    static std::atomic_flag attempted=ATOMIC_FLAG_INIT;
    if(attempted.test_and_set(std::memory_order_relaxed))
        throw Error("session_reuse_unsupported","Only one native session attempt is supported per process. Start a fresh acquisition worker to reconnect.",4);
    impl_=std::make_unique<Impl>(std::move(cancelled));impl_->trace=std::move(trace);impl_->open();
}
Session::~Session() {
    if(impl_ && impl_->pending) {
        try {impl_->drain();} catch(...) {
            // The USB backend still owns callback pointers and buffers. Quarantine
            // the entire session until process exit rather than free live memory.
            // Successful callback drain avoids this exceptional path.
            if(impl_->pending) (void)impl_.release();
        }
    }
}
const Identity& Session::identity() const {return impl_->id;}
std::vector<StoredBlock> Session::read_calibration() {
    if(impl_->failed)throw Error("session_failed","Start a fresh worker to read stored calibration.",4);
    if(impl_->configured)throw Error("invalid_session_state","Read stored calibration before acquisition configuration.",4);
    try {
        std::vector<StoredBlock> result;
        for(const auto index:std::array<std::uint16_t,8>{0,1,2,3,4,5,6,8}) {
            impl_->check_cancel();
            result.push_back({index,impl_->block(index)});
        }
        return result;
    } catch(...) {impl_->failed=true;throw;}
}
Tuning Session::configure(const NativeSettings& settings) {
    if(impl_->failed)throw Error("session_failed","The native session has failed. Start a fresh acquisition worker.",4);
    try {return impl_->configure(settings);} catch(...) {impl_->failed=true;throw;}
}
Capture Session::acquire(std::size_t n) {
    if(impl_->failed)throw Error("session_failed","The native session has failed. Start a fresh acquisition worker.",4);
    try {return impl_->acquire(n);} catch(...) {impl_->failed=true;throw;}
}
} // namespace rsa::wire
