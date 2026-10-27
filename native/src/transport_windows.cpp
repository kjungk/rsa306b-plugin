#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <setupapi.h>
#include "rsa/transport.hpp"
#include "rsa/cypress_abi.hpp"
#include "rsa/device_lock.hpp"
#include <chrono>
#include <cwctype>
#include <thread>

namespace rsa::usb {
namespace {
using Clock=std::chrono::steady_clock;
namespace abi=cypress;
// The interface installed by RSA3USB3.inf on the qualified RSA306B Windows host.
constexpr GUID interface_guid{0x85c528ee,0xc558,0x4c31,{0x92,0x77,0xa7,0xf5,0xdc,0xf9,0x58,0xdc}};

[[noreturn]] void windows_error(const char* action,DWORD error) {
    const auto code=(error==ERROR_SHARING_VIOLATION || error==ERROR_ACCESS_DENIED)?"device_busy":"usb_error";
    auto message=std::string(action)+" failed (Windows error "+std::to_string(error)+").";
    if(error==ERROR_SHARING_VIOLATION || error==ERROR_ACCESS_DENIED)
        message+=" Close SignalVu, RSA Monitor, and any other analyzer session before retrying.";
    throw Error(code,std::move(message),4);
}
class Handle {
public:
    HANDLE value=INVALID_HANDLE_VALUE;
    Handle()=default;
    explicit Handle(HANDLE handle):value(handle) {}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    ~Handle() {if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct DeviceSet {
    HDEVINFO value=INVALID_HANDLE_VALUE;
    ~DeviceSet() {if(value!=INVALID_HANDLE_VALUE)SetupDiDestroyDeviceInfoList(value);}
};

std::vector<std::wstring> device_paths() {
    DeviceSet devices;
    devices.value=SetupDiGetClassDevsW(&interface_guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(devices.value==INVALID_HANDLE_VALUE)windows_error("Enumerate RSA3USB3 interfaces",GetLastError());
    std::vector<std::wstring> paths;
    for(DWORD index=0;index<256;++index) {
        SP_DEVICE_INTERFACE_DATA info{};info.cbSize=sizeof(info);
        if(!SetupDiEnumDeviceInterfaces(devices.value,nullptr,&interface_guid,index,&info)) {
            const auto error=GetLastError();
            if(error==ERROR_NO_MORE_ITEMS)return paths;
            windows_error("Read RSA3USB3 interface",error);
        }
        DWORD needed=0;
        const auto first=SetupDiGetDeviceInterfaceDetailW(devices.value,&info,nullptr,0,&needed,nullptr);
        if(first || GetLastError()!=ERROR_INSUFFICIENT_BUFFER || needed<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || needed>32768)
            throw Error("usb_error","Unexpected RSA3USB3 interface detail size.",4);
        std::vector<std::uint8_t> storage(needed);
        auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if(!SetupDiGetDeviceInterfaceDetailW(devices.value,&info,detail,needed,nullptr,nullptr))
            windows_error("Read RSA3USB3 interface path",GetLastError());
        const auto capacity=(storage.size()-offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W,DevicePath))/sizeof(wchar_t);
        const auto end=std::find(detail->DevicePath,detail->DevicePath+capacity,L'\0');
        if(end==detail->DevicePath+capacity)throw Error("usb_error","Unterminated RSA3USB3 interface path.",4);
        std::wstring path(detail->DevicePath,end),lower=path;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
        constexpr std::wstring_view identity=L"vid_0699&pid_a306";
        const auto pos=lower.find(identity);
        if(pos!=std::wstring::npos && pos+identity.size()<lower.size() &&
           (lower[pos+identity.size()]==L'&' || lower[pos+identity.size()]==L'#'))paths.push_back(std::move(path));
    }
    throw Error("usb_error","Too many RSA3USB3 device interfaces; refusing ambiguous enumeration.",4);
}

// Every field passed to DeviceIoControl lives in Request until completion is
// confirmed. This matters particularly for METHOD_NEITHER direct transfers:
// both the packed input header and payload remain driver-owned while pending.
struct Request {
    Handle event;
    OVERLAPPED overlapped{};
    abi::Header header{};
    std::vector<std::uint8_t> bytes;
    DWORD transferred=0,error=0;
    bool pending=false,direct=false;
    explicit Request(std::size_t size):event(CreateEventW(nullptr,TRUE,FALSE,nullptr)),bytes(size) {
        if(!event.value)windows_error("Create USB completion event",GetLastError());
        overlapped.hEvent=event.value;
    }
};

class WindowsTransport final : public Transport {
    struct State {
        std::unique_ptr<DeviceLock> discovery_ownership;
        Handle device;
        std::unique_ptr<Request> command;
        std::array<std::unique_ptr<Request>,bulk_slots> slots;
        CompletionOrder order;
        Consumer consume;
        bool streaming=false;
    };
    std::unique_ptr<State> state_=std::make_unique<State>();

    void submit(Request& request,DWORD ioctl) {
        if(request.pending)throw Error("usb_busy","Attempted to reuse a pending USB transfer.",4);
        if(!ResetEvent(request.event.value))windows_error("Reset USB completion event",GetLastError());
        request.overlapped={};request.overlapped.hEvent=request.event.value;
        request.error=0;request.transferred=0;
        auto* input=request.direct?request.header.data():request.bytes.data();
        const auto input_size=request.direct?request.header.size():request.bytes.size();
        const auto ok=DeviceIoControl(state_->device.value,ioctl,input,static_cast<DWORD>(input_size),
                                     request.bytes.data(),static_cast<DWORD>(request.bytes.size()),nullptr,&request.overlapped);
        if(!ok) {
            const auto error=GetLastError();
            if(error!=ERROR_IO_PENDING)windows_error("Submit USB transfer",error);
        }
        request.pending=true;
    }
    bool poll(Request& request) noexcept {
        if(!request.pending)return true;
        DWORD transferred=0;
        const auto ok=GetOverlappedResult(state_->device.value,&request.overlapped,&transferred,FALSE);
        if(!ok) {
            const auto error=GetLastError();
            if(error==ERROR_IO_INCOMPLETE)return false;
            request.error=error;
        }
        request.transferred=transferred;request.pending=false;return true;
    }
    void await_command(unsigned timeout_ms) {
        auto& request=*state_->command;
        const auto deadline=Clock::now()+std::chrono::milliseconds(timeout_ms);
        while(!poll(request) && Clock::now()<deadline) {
            const auto waited=WaitForSingleObject(request.event.value,10);
            if(waited==WAIT_FAILED)break;
        }
        if(request.pending) {
            // Cancel only this request. Acquisition's error path still attempts
            // Stop and then cancels/drains the outstanding sample transfers.
            (void)CancelIoEx(state_->device.value,&request.overlapped);
            const auto cancel_deadline=Clock::now()+std::chrono::seconds(5);
            while(!poll(request) && Clock::now()<cancel_deadline)
                (void)WaitForSingleObject(request.event.value,10);
            if(request.pending)
                throw Error("usb_cleanup_timeout","USB command cancellation did not drain within five seconds; storage is retained until process exit.",4);
            throw Error("usb_timeout","USB command timed out; the transfer was cancelled and drained.",4);
        }
        if(request.error)windows_error("Complete USB transfer",request.error);
        if(request.transferred>request.bytes.size())throw Error("usb_transfer_error","Driver returned an invalid USB transfer length.",4);
    }
    Request& new_command(std::size_t bytes) {
        if(state_->command && state_->command->pending)
            throw Error("usb_cleanup_timeout","A previous USB command is still pending; start a fresh worker.",4);
        state_->command=std::make_unique<Request>(bytes);
        return *state_->command;
    }
    std::vector<std::uint8_t> query(DWORD ioctl,std::size_t bytes) {
        auto& request=new_command(bytes);submit(request,ioctl);await_command(1000);
        if(request.transferred!=bytes)throw Error("usb_transfer_error","Unexpected driver query response length.",4);
        return request.bytes;
    }
    std::vector<std::uint8_t> control(std::uint8_t type,std::uint8_t request_code,
                                      std::uint16_t value,std::uint16_t index,std::uint16_t bytes) {
        auto& request=new_command(abi::header_bytes+bytes);
        const auto header=abi::control_header(type,request_code,value,index,bytes,1000);
        std::copy(header.begin(),header.end(),request.bytes.begin());
        submit(request,abi::ep0_transfer);await_command(1000);abi::check_status(request.bytes);
        if(request.transferred<abi::header_bytes)throw Error("usb_transfer_error","Missing USB control response header.",4);
        return {request.bytes.begin()+abi::header_bytes,request.bytes.begin()+request.transferred};
    }
    void submit_bulk(Request& request) {
        request.direct=true;request.header=abi::direct_header(0x84);submit(request,abi::direct_transfer);
    }
public:
    void own_discovery_lock() {state_->discovery_ownership=std::make_unique<DeviceLock>();}
    void attach(const std::wstring& path) {
        // share=0 rejects a conflicting file handle when the installed driver
        // honors Windows share access. The cross-backend mutex is held by Session.
        state_->device.value=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,
                                        OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(state_->device.value==INVALID_HANDLE_VALUE)windows_error("Open RSA306B driver exclusively",GetLastError());
    }
    Device inspect() {
        const auto descriptor=control(0x80,6,0x0100,0,18);
        abi::need(descriptor.size()==18 && descriptor[0]==18 && descriptor[1]==1,"Invalid USB device descriptor.");
        abi::need(descriptor[17]==1,"RSA306B must expose exactly one USB configuration.");
        Device device{abi::get16(descriptor,8),abi::get16(descriptor,10),abi::get16(descriptor,2),0,0,0,{}};
        abi::need(device.vendor==0x0699 && device.product==0xa306,"Driver interface does not belong to an RSA306B.");
        const auto config=control(0x80,6,0x0200,0,9);
        abi::need(config.size()==9 && config[0]==9 && config[1]==2,"Invalid USB configuration header.");
        const auto length=abi::get16(config,2);
        abi::need(length>=9 && length<=4096,"USB configuration descriptor is too large or truncated.");
        device.interfaces=abi::parse_configuration(control(0x80,6,0x0200,0,length));
        const auto speed=abi::get32(query(abi::get_speed,4),0);
        device.speed_mbps=speed==4?5000:speed==2?480:0; // The driver cannot distinguish low/full speed.
        device.address=query(abi::get_address,1).front();
        return device;
    }
    void initialize() override {
        const auto paths=device_paths();
        if(paths.size()!=1)throw Error("device_count","Connect exactly one RSA306B with its RSA3USB3 driver installed; found "+std::to_string(paths.size())+".",4);
        attach(paths.front());
        const auto device=inspect();abi::check_topology(device);
        if(device.speed_mbps<5000)throw Error("usb_speed","Raw IF acquisition requires a USB 3 SuperSpeed connection.",4);
        // Read current settings without touching endpoint state. The vendor
        // driver owns configuration selection; reset/SET_INTERFACE are forbidden.
        const auto config=control(0x80,8,0,0,1),alternate=control(0x81,10,0,0,1);
        if(config.size()!=1 || config[0]!=1 || alternate.size()!=1 || alternate[0]!=0)
            throw Error("usb_configuration","RSA306B must already use configuration 1 / interface 0 / alternate 0. Reconnect it before retrying.",4);
    }
    ~WindowsTransport() override {
        if(has_pending()) {try {drain();}catch(...) {}}
        if(has_pending())(void)state_.release(); // Includes handle, event, OVERLAPPED, header, and payload.
    }
    std::size_t interrupt(std::uint8_t endpoint,std::span<std::uint8_t> bytes,unsigned timeout_ms) override {
        if((endpoint!=0x03 && endpoint!=0x83) || bytes.empty() || bytes.size()>1024)
            throw Error("usb_transfer_error","Invalid RSA306B interrupt request.",4);
        auto& request=new_command(bytes.size());request.direct=true;request.header=abi::direct_header(endpoint);
        if(endpoint==0x03)std::copy(bytes.begin(),bytes.end(),request.bytes.begin());
        submit(request,abi::direct_transfer);await_command(timeout_ms);abi::check_status(request.header);
        if(endpoint==0x83)std::copy_n(request.bytes.begin(),request.transferred,bytes.begin());
        return request.transferred;
    }
    bool has_pending() const noexcept override {
        if(!state_)return false;
        if(state_->command && state_->command->pending)return true;
        for(const auto& slot:state_->slots)if(slot && slot->pending)return true;
        return false;
    }
    void start_bulk(Consumer consume) override {
        if(has_pending())throw Error("usb_busy","Previous USB transfers have not drained.",4);
        auto& s=*state_;s.consume=std::move(consume);s.streaming=true;s.order.reset();
        for(auto& slot:s.slots) {
            if(!slot)slot=std::make_unique<Request>(bulk_bytes);
            submit_bulk(*slot);
        }
    }
    void pump() override {
        auto& s=*state_;
        auto& next=*s.slots[s.order.head()];
        if(next.pending)(void)WaitForSingleObject(next.event.value,10);
        for(std::size_t i=0;i<s.slots.size();++i) {
            auto& slot=*s.slots[i];
            if(slot.pending && poll(slot))s.order.complete(i);
        }
        for(std::size_t n=0;n<bulk_slots && s.streaming && s.order.ready();++n) {
            auto& slot=*s.slots[s.order.head()];
            if(slot.error)windows_error("Complete bulk transfer",slot.error);
            abi::check_status(slot.header);
            if(slot.transferred==0 || slot.transferred>slot.bytes.size())
                throw Error("bulk_transfer_error","Invalid bulk completion length; partial data was not emitted as a spectrum.",4);
            s.streaming=s.consume(std::span(slot.bytes).first(slot.transferred));
            s.order.consume();
            if(s.streaming)submit_bulk(slot);
        }
    }
    void drain() override {
        auto& s=*state_;s.streaming=false;
        const auto cancel=[&](std::unique_ptr<Request>& request) {
            if(request && request->pending)(void)CancelIoEx(s.device.value,&request->overlapped);
        };
        cancel(s.command);for(auto& slot:s.slots)cancel(slot);
        const auto deadline=Clock::now()+std::chrono::seconds(5);
        while(has_pending()) {
            if(s.command && s.command->pending)(void)poll(*s.command);
            for(auto& slot:s.slots)if(slot && slot->pending)(void)poll(*slot);
            if(!has_pending())break;
            if(Clock::now()>deadline)
                throw Error("usb_cleanup_timeout","USB cancellation did not drain within five seconds; driver-owned storage is retained until process exit.",4);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        s.consume={};
    }
};
}

std::unique_ptr<Transport> open() {
    return std::make_unique<WindowsTransport>();
}
std::vector<Device> discover_devices() {
    std::vector<Device> devices;
    for(const auto& path:device_paths()) {
        auto transport=std::make_unique<WindowsTransport>();transport->own_discovery_lock();transport->attach(path);
        devices.push_back(transport->inspect());
    }
    return devices;
}
}
