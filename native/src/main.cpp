#include "rsa/core.hpp"
#include "rsa/protocol.hpp"
#include "rsa/device_lock.hpp"
#include "rsa/calibration.hpp"
#include "rsa/mock.hpp"
#include <chrono>
#include <thread>
#include <cstdio>
#include <memory>
#include <bit>
#include <charconv>
#include <cmath>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <poll.h>
#include <unistd.h>
#include <cerrno>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void on_interrupt(int) { interrupted = 1; }
bool mock_mode=false,stdin_control=false,control_stop=false;
std::string expected_serial;
bool cancelled() {
    if(interrupted || control_stop)return true;
    if(!stdin_control)return false;
    static std::string buffer;
    pollfd fd{STDIN_FILENO,POLLIN|POLLHUP,0};
    if(::poll(&fd,1,0)>0 && (fd.revents&(POLLIN|POLLHUP|POLLERR))) {
        char bytes[256];const auto n=::read(STDIN_FILENO,bytes,sizeof(bytes));
        if(n==0 || (n<0 && errno!=EINTR && errno!=EAGAIN))control_stop=true;
        else if(n>0) {
            buffer.append(bytes,static_cast<std::size_t>(n));
            if(buffer.size()>4096)throw rsa::Error("invalid_control","Control input exceeds limit.",2);
            if(auto end=buffer.find('\n');end!=std::string::npos) {
                const auto line=buffer.substr(0,end);
                if(line!="stop" && line!="stop\r")throw rsa::Error("invalid_control","Expected stop command.",2);
                control_stop=true;
            }
        }
    }
    return control_stop;
}
class DriverSession {
    std::unique_ptr<rsa::wire::Session> real_;
    rsa::wire::Identity identity_{"RSA306B","MOCK-RSA306B",0x701,0x201,7,0};
    rsa::wire::NativeSettings settings_{{600e6,20e6,10000,0}};
    rsa::wire::Tuning tuning_=rsa::wire::tuning(600e6);
    std::uint64_t tick_=1000000;
public:
    explicit DriverSession(std::function<void(bool,std::span<const std::uint8_t>)> trace={}) {
        if(cancelled())throw rsa::Error("interrupted","Acquisition cancelled.",130);
        if(!mock_mode)real_=std::make_unique<rsa::wire::Session>(cancelled,std::move(trace));
        if(!expected_serial.empty() && identity().serial!=expected_serial)throw rsa::Error("wrong_device","Connected analyzer does not match the selected serial.",4);
    }
    const rsa::wire::Identity& identity()const{return real_?real_->identity():identity_;}
    std::vector<rsa::wire::StoredBlock> read_calibration(){return real_?real_->read_calibration():rsa::mock::calibration();}
    rsa::wire::Tuning configure(const rsa::wire::NativeSettings& settings){
        if(real_)return real_->configure(settings);settings.validate();settings_=settings;tuning_=rsa::wire::tuning(settings.spectrum.center_hz);return tuning_;
    }
    rsa::wire::Capture acquire(std::size_t count){
        if(real_)return real_->acquire(count);
        if(cancelled())throw rsa::Error("interrupted","Synthetic acquisition cancelled.",130);
        auto result=rsa::mock::capture(settings_,tuning_,count,tick_);tick_+=count+112000;return result;
    }
};

std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else out << c;
    }
    return out.str() + '"';
}
using Options = std::map<std::string, std::string>;
Options options(int argc, char** argv, const std::set<std::string>& allowed) {
    Options result;
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc || !allowed.contains(argv[i]) || !result.emplace(argv[i], argv[i+1]).second)
            throw rsa::Error("invalid_arguments", "Unknown, duplicate, or incomplete option: " + std::string(argv[i]), 2);
    }
    return result;
}
std::string required(const Options& opts, const std::string& name) {
    if (!opts.contains(name)) throw rsa::Error("invalid_arguments", "Required option: " + name, 2);
    return opts.at(name);
}
double number(const std::string& value) {
    double result{};
    // Floating-point from_chars in Apple's libc++ requires macOS 26. Keep the
    // decimal/exponent CLI grammar locale-independent on older deployment targets.
    std::istringstream input(value);
    input.imbue(std::locale::classic());
    input >> std::noskipws >> result;
    const bool nonzero_significand = value.substr(0, value.find_first_of("eE")).find_first_of("123456789") != std::string::npos;
    if (value.empty() || value.front() == '+' || value.find_first_not_of("0123456789.eE+-") != std::string::npos ||
        input.fail() || !input.eof() || !std::isfinite(result) || (result == 0 && nonzero_significand))
        throw rsa::Error("invalid_arguments", "Expected finite number: " + value, 2);
    return result;
}
std::size_t integer(const std::string& value, std::size_t minimum, std::size_t maximum) {
    std::size_t result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result < minimum || result > maximum)
        throw rsa::Error("invalid_arguments", "Integer outside permitted range: " + value, 2);
    return result;
}
void discover() {
    if(mock_mode){std::cout<<"{\"schema_version\":1,\"type\":\"discovery\",\"synthetic\":true,\"devices\":[{\"vendor_id\":1689,\"product_id\":41734,\"bus\":0,\"address\":1,\"speed_mbps\":5000}]}\n";return;}
    const auto devices = rsa::discover();
    std::cout << "{\"schema_version\":1,\"type\":\"discovery\",\"native_acquisition\":true,"
                 "\"reason\":\"Prototype backend implemented; see docs/rsa306b-validation.md for hardware evidence\",\"devices\":[";
    bool first = true;
    for (const auto& d : devices) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"vendor_id\":" << d.vendor << ",\"product_id\":" << d.product
                  << ",\"usb_version_bcd\":" << d.usb_version_bcd << ",\"bus\":" << d.bus
                  << ",\"address\":" << d.address << ",\"speed_mbps\":" << d.speed_mbps
                  << ",\"interfaces\":[";
        bool first_interface = true;
        for (const auto& i : d.interfaces) {
            if (!first_interface) std::cout << ',';
            first_interface = false;
            std::cout << "{\"configuration\":" << i.configuration << ",\"number\":" << i.number
                      << ",\"alternate\":" << i.alternate << ",\"class\":" << i.class_code << ",\"endpoints\":[";
            bool first_endpoint = true;
            for (const auto& e : i.endpoints) {
                if (!first_endpoint) std::cout << ',';
                first_endpoint = false;
                std::cout << "{\"address\":" << e.address << ",\"transfer_type\":" << e.transfer_type
                          << ",\"max_packet_bytes\":" << e.max_packet_bytes << '}';
            }
            std::cout << "]}";
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}
bool level_unit(const Options& opts);
void capabilities() {
    std::cout<<std::setprecision(17)
      <<"{\"schema_version\":1,\"type\":\"capabilities\",\"backend\":\"rsa306b-native-usb\",\"hardware_min_hz\":9000,\"hardware_max_hz\":6200000000"
      <<",\"min_frequency_hz\":"<<rsa::min_frequency_hz<<",\"max_frequency_hz\":"<<rsa::max_frequency_hz
      <<",\"min_center_hz\":"<<rsa::wire::min_center_hz<<",\"min_sweep_hz\":"<<rsa::wire::min_sweep_hz
      <<",\"lf_limit_hz\":"<<rsa::wire::lf_limit_hz<<",\"lf_gain_code\":"<<rsa::wire::lf_gain_code
      <<",\"max_bandwidth_hz\":"<<rsa::max_bandwidth_hz<<",\"min_rbw_hz\":"<<1.5*rsa::wire::adc_rate/(1U<<20)
      <<",\"sweep_segment_hz\":20000000,\"max_sweep_segments\":"<<rsa::wire::max_sweep_segments
      <<",\"max_trace_points\":"<<rsa::wire::max_trace_points
      <<",\"level_units\":[\"dBFS\",\"dBm\"],\"calibrated\":true,\"spectrum_schema_versions\":[1,2],\"calibration_basis\":\"factory_tables\",\"algorithm\":\"hw7-fullrange-v1\",\"fullrange_hardware_validated\":false,\"absolute_accuracy_verified\":false,\"calibrated_min_hz\":9000,\"calibrated_max_hz\":6200000000,\"exclusive_device\":true,\"lock_path\":"<<quote(rsa::DeviceLock::path())
      <<",\"limitation\":\"Factory model dBm: 9 kHz–6.2 GHz, LF VGA199 / RF route0x71. Full-range hardware acceptance pending; absolute accuracy unverified. Sequential bursts leave RF gaps.\"}\n";
}
void probe(const Options& opts) {
    const bool dbm=level_unit(opts);
    DriverSession device;
    const auto& id=device.identity();
    std::unique_ptr<rsa::FactoryCalibration> calibration;
    if(dbm)calibration=std::make_unique<rsa::FactoryCalibration>(device.read_calibration(),id.serial);
    std::cout<<"{\"schema_version\":1,\"type\":\"device\",\"model\":"<<quote(id.model)<<",\"serial\":"<<quote(id.serial)
      <<",\"firmware_word\":"<<id.firmware<<",\"fpga_word\":"<<id.fpga<<",\"hardware\":"<<id.hardware
      <<",\"synthetic\":"<<(mock_mode?"true":"false")<<",\"calibrated\":"<<(dbm?"true":"false")<<",\"absolute_accuracy_verified\":false,\"acquisition_mode\":\"probe\"}\n";
}
void read_calibration() {
    DriverSession device;
    const auto blocks=device.read_calibration();
    const auto& id=device.identity();
    std::cout<<"{\"schema_version\":1,\"type\":\"calibration_dump\",\"calibrated\":false,\"model\":"<<quote(id.model)
      <<",\"serial\":"<<quote(id.serial)<<",\"firmware_word\":"<<id.firmware<<",\"fpga_word\":"<<id.fpga
      <<",\"hardware\":"<<id.hardware<<",\"blocks\":[";
    constexpr char digits[]="0123456789abcdef";
    for(std::size_t i=0;i<blocks.size();++i) {
        if(i)std::cout<<',';
        const auto& block=blocks[i];
        std::string hex;hex.reserve(block.payload.size()*2);
        for(auto byte:block.payload) {hex.push_back(digits[byte>>4]);hex.push_back(digits[byte&15]);}
        std::cout<<"{\"index\":"<<block.index<<",\"bytes\":"<<block.payload.size()
          <<",\"crc16\":"<<rsa::wire::crc16(block.payload)<<",\"payload_hex\":"<<quote(hex)<<'}';
    }
    std::cout<<"]}\n";
    if(!std::cout)throw rsa::Error("output_error","Cannot write stored calibration dump.",5);
}
bool level_unit(const Options& opts) {
    if(!opts.contains("--level-unit") || opts.at("--level-unit")=="dbfs")return false;
    if(opts.at("--level-unit")=="dbm")return true;
    throw rsa::Error("invalid_arguments","Level unit must be dbfs or dbm.",2);
}
void emit_native(const rsa::wire::Capture& frame, const rsa::wire::NativeSettings& settings,
                 const rsa::wire::Tuning& tune, std::size_t sequence, const std::string& source,
                 std::optional<std::uint64_t> previous_end={},
                 std::optional<std::size_t> sweep_id={},std::size_t segment_index=0, rsa::FactoryCalibration* calibration=nullptr) {
    const bool live=source=="rsa306b-native-usb";
    const auto spec=rsa::wire::if_spectrum(frame.samples,settings.spectrum,tune.tuned_hz);
    const auto corrected=calibration?calibration->levels(spec,settings,tune):std::vector<double>{};
    const auto& levels=calibration?corrected:spec.levels_dbfs;
    std::cout<<std::setprecision(17)<<"{\"schema_version\":"<<(calibration?2:1)<<",\"type\":\"spectrum\",\"source\":"<<quote(mock_mode?"synthetic-mock":source)<<",\"synthetic\":"<<(mock_mode?"true":"false")
      <<",\"sequence\":"<<sequence<<",\"calibrated\":"<<(calibration?"true":"false")<<",\"level_unit\":"<<quote(calibration?"dBm":"dBFS")<<",\"window\":\"periodic-hann\""
      <<",\"timestamp_basis\":"<<quote(live?"device_ticks_and_host_call_bounds":"unknown")<<",\"acquisition_start_unix_seconds\":null"
      <<",\"device_timestamp_ticks\":"<<(live?quote(std::to_string(frame.first_tick)):"null")
      <<",\"host_acquire_begin_unix_seconds\":"<<(live?std::to_string(frame.host_start):"null")<<",\"host_read_end_unix_seconds\":"<<(live?std::to_string(frame.host_end):"null")
      <<",\"sample_rate_hz\":"<<rsa::wire::adc_rate<<",\"sample_count\":"<<frame.samples.size()
      <<",\"duration_seconds\":"<<static_cast<double>(frame.samples.size())/rsa::wire::adc_rate
      <<",\"center_hz\":"<<settings.spectrum.center_hz<<",\"tuned_center_hz\":"<<tune.tuned_hz
      <<",\"span_hz\":"<<settings.spectrum.span_hz<<",\"requested_rbw_hz\":"<<settings.spectrum.rbw_hz
      <<",\"bin_width_hz\":"<<spec.step_hz<<",\"window_enbw_hz\":"<<spec.enbw_hz
      <<",\"rf_attenuation_db\":"<<(live&&!tune.lf_path?std::to_string(settings.attenuation_db):"null")<<",\"if_gain_db\":"<<(live&&!tune.lf_path?std::to_string(settings.if_gain_db):"null")
      <<",\"requested_rf_attenuation_db\":"<<(live?std::to_string(settings.attenuation_db):"null")
      <<",\"requested_if_gain_db\":"<<(live?std::to_string(settings.if_gain_db):"null")
      <<",\"receiver_path\":"<<quote(tune.lf_path?"direct-lf":"heterodyne-rf")
      <<",\"lf_gain_code\":"<<(live&&tune.lf_path?std::to_string(rsa::wire::lf_gain_code):"null")
      <<",\"rf_path_bits\":"<<(live?(tune.lf_path?"80":(settings.factory_dbm?"113":"112")):"null")
      <<",\"first_if_hz\":"<<tune.first_if_hz<<",\"data_inverted\":"<<(!tune.lf_path&&tune.high_side?"true":"false")
      <<",\"reference_dbm\":null,\"frame_descriptor\":"<<(live?std::to_string(frame.descriptor):"null")
      <<",\"first_frame_counter\":"<<(live?std::to_string(frame.first_counter):"null")
      <<",\"last_frame_counter\":"<<(live?std::to_string(frame.last_counter):"null");
    if(calibration)std::cout<<",\"calibration\":{\"id\":"<<quote(calibration->id())<<",\"synthetic\":"<<(mock_mode?"true":"false")<<",\"device_serial\":"<<quote(calibration->serial())
      <<",\"basis\":\"factory_tables\",\"algorithm\":\"hw7-fullrange-v1\",\"absolute_accuracy_verified\":false,\"impedance_ohms\":50}";
    if(sweep_id)std::cout<<",\"sweep_id\":"<<*sweep_id<<",\"segment_index\":"<<segment_index;
    if(live) std::cout
      <<",\"quality\":{\"gap_events_before_contiguous_block\":"<<frame.gap_events
      <<",\"discarded_stale_frames\":"<<frame.discarded_frames<<",\"raw_status\":"<<frame.status
      <<",\"overload\":"<<((frame.status&1)?"true":"false")
      <<",\"unlocked\":"<<((frame.status&0xc0e)?"true":"false")
      <<",\"power_failure\":"<<((frame.status&0x10)?"true":"false")
      <<",\"device_frame_drop\":"<<((frame.status&0x20)?"true":"false")
      <<",\"timestamp_regressed\":"<<((previous_end && frame.first_tick<*previous_end)?"true":"false")
      <<",\"between_capture_gap_seconds\":";
    if(live) { if(previous_end && frame.first_tick>=*previous_end) std::cout<<static_cast<double>(frame.first_tick-*previous_end)/rsa::wire::adc_rate;
    else std::cout<<"null"; }
    else std::cout<<",\"quality\":{\"gap_status\":\"unknown\",\"overload\":null";
    std::cout<<"},\"frequency_hz\":[";
    for(std::size_t i=0;i<spec.levels_dbfs.size();++i) {if(i)std::cout<<',';std::cout<<spec.start_hz+static_cast<double>(i)*spec.step_hz;}
    std::cout<<"],\"levels\":[";
    for(std::size_t i=0;i<spec.levels_dbfs.size();++i) {if(i)std::cout<<',';std::cout<<levels[i];}
    std::cout<<"]}\n";std::cout.flush();
    if(!std::cout)throw rsa::Error("output_error","Cannot write spectrum output.",5);
}
void sweep(const Options& opts,bool plan_only) {
    const bool dbm=level_unit(opts);
    const double start=number(required(opts,"--start-hz")),stop=number(required(opts,"--stop-hz"));
    auto plan=rsa::wire::sweep_plan(start,stop,number(required(opts,"--rbw-hz")),
        opts.contains("--attenuation-db")?number(opts.at("--attenuation-db")):0,
        opts.contains("--if-gain-db")?number(opts.at("--if-gain-db")):17,dbm);
    for(auto& settings:plan){settings.factory_dbm=dbm;settings.validate();}
    if(plan_only) {
        std::cout<<std::setprecision(17)<<"{\"schema_version\":1,\"type\":\"sweep_plan\",\"start_hz\":"<<start<<",\"stop_hz\":"<<stop<<",\"segments\":[";
        for(std::size_t i=0;i<plan.size();++i) {if(i)std::cout<<',';std::cout<<"{\"center_hz\":"<<plan[i].spectrum.center_hz<<",\"span_hz\":"<<plan[i].spectrum.span_hz<<"}";}
        std::cout<<"]}\n";return;
    }
    const auto count=integer(required(opts,"--count"),1,1'000'000);
    const double interval=opts.contains("--interval-ms")?number(opts.at("--interval-ms")):100;
    if(interval<0 || interval>10000)throw rsa::Error("invalid_arguments","Interval must be 0…10000 ms.",2);
    const double duration=opts.contains("--duration-seconds")?number(opts.at("--duration-seconds")):86400;
    if(duration<=0 || duration>86400)throw rsa::Error("invalid_arguments","Duration must be >0 and <=86400 seconds.",2);
    DriverSession device;
    const auto& id=device.identity();
    std::unique_ptr<rsa::FactoryCalibration> calibration;
    if(dbm)calibration=std::make_unique<rsa::FactoryCalibration>(device.read_calibration(),id.serial);
    std::cout<<"{\"schema_version\":1,\"type\":\"device\",\"model\":"<<quote(id.model)<<",\"serial\":"<<quote(id.serial)<<",\"synthetic\":"<<(mock_mode?"true":"false")<<",\"calibrated\":"<<(dbm?"true":"false")<<",\"acquisition_mode\":\"sequential-sweep\"}\n";
    std::optional<std::uint64_t> previous_end;std::size_t sequence=0;
    const auto started=std::chrono::steady_clock::now();
    for(std::size_t pass=0;pass<count;++pass) {
        if(cancelled())throw rsa::Error("interrupted","Sweep cancelled.",130);
        if(pass && opts.contains("--duration-seconds") && std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>=duration)break;
        const auto begin=std::chrono::steady_clock::now();
        const double wall=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::cout<<std::setprecision(17)<<"{\"schema_version\":1,\"type\":\"sweep_begin\",\"sweep_id\":"<<pass<<",\"start_hz\":"<<start<<",\"stop_hz\":"<<stop<<",\"segment_count\":"<<plan.size()<<",\"host_begin_unix_seconds\":"<<wall<<"}\n"<<std::flush;
        std::size_t completed=0;
        auto end_record=[&](bool success) {
            std::cout<<"{\"schema_version\":1,\"type\":\"sweep_end\",\"sweep_id\":"<<pass<<",\"complete\":"<<(success?"true":"false")<<",\"segments_completed\":"<<completed<<",\"duration_seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<"}\n"<<std::flush;
        };
        try {
            for(std::size_t i=0;i<plan.size();++i) {
                const auto tune=device.configure(plan[i]);
                const auto frame=device.acquire(rsa::wire::fft_size(plan[i].spectrum.rbw_hz));
                emit_native(frame,plan[i],tune,sequence++,"rsa306b-native-usb",previous_end,pass,i,calibration.get());
                previous_end=frame.first_tick+frame.samples.size();++completed;
            }
        } catch(...) {end_record(false);throw;}
        end_record(true);
        if(!std::cout)throw rsa::Error("output_error","Cannot write sweep output.",5);
        if(pass+1<count) {
            const auto until=std::chrono::steady_clock::now()+std::chrono::duration<double,std::milli>(interval);
            while(std::chrono::steady_clock::now()<until && !cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}
rsa::wire::NativeSettings native_settings(const Options& opts) {
    const bool dbm=level_unit(opts);
    rsa::wire::NativeSettings s{{number(required(opts,"--center-hz")),number(required(opts,"--span-hz")),number(required(opts,"--rbw-hz")),0},
        opts.contains("--attenuation-db")?number(opts.at("--attenuation-db")):0,
        opts.contains("--if-gain-db")?number(opts.at("--if-gain-db")):17};
    s.factory_dbm=dbm;
    s.validate();
    if(opts.contains("--reference-dbm"))
        throw rsa::Error("native_reference_uncalibrated","Automatic reference-level gain selection is unavailable. Use explicit --attenuation-db and --if-gain-db; --level-unit selects dBFS or factory LF/RF dBm.",3);
    return s;
}
void capture(const Options& opts, bool repeat) {
    const auto count=repeat?integer(required(opts,"--count"),1,1'000'000):1;
    const auto settings=native_settings(opts);
    const double interval=opts.contains("--interval-ms")?number(opts.at("--interval-ms")):100;
    const double duration=opts.contains("--duration-seconds")?number(opts.at("--duration-seconds")):86400;
    if(interval<0 || interval>10000 || duration<=0 || duration>86400)
        throw rsa::Error("invalid_arguments","Interval must be 0…10000 ms; duration must be >0 and <=86400 seconds.",2);
    std::unique_ptr<FILE,decltype(&fclose)> raw(nullptr,fclose);
    if(opts.contains("--raw-output")) {
        raw.reset(fopen(opts.at("--raw-output").c_str(),"wbx"));
        if(!raw)throw rsa::Error("output_error","Raw output must be a new writable file.",5);
    }
    std::unique_ptr<FILE,decltype(&fclose)> log(nullptr,fclose);
    if(opts.contains("--usb-log")) {
        log.reset(fopen(opts.at("--usb-log").c_str(),"wx"));
        if(!log)throw rsa::Error("output_error","USB log must be a new writable file.",5);
    }
    DriverSession device([&](bool out,std::span<const std::uint8_t> bytes){
        if(!log)return;
        fprintf(log.get(),"%s ",out?"OUT":"IN");for(auto b:bytes)fprintf(log.get(),"%02x",unsigned(b));
        fputc('\n',log.get());
        if(fflush(log.get())!=0 || ferror(log.get()))throw rsa::Error("output_error","Cannot write USB command log.",5);
    });
    const auto& id=device.identity();
    std::unique_ptr<rsa::FactoryCalibration> calibration;
    if(settings.factory_dbm)calibration=std::make_unique<rsa::FactoryCalibration>(device.read_calibration(),id.serial);
    const auto tune=device.configure(settings);
    std::cout<<"{\"schema_version\":1,\"type\":\"device\",\"model\":"<<quote(id.model)<<",\"serial\":"<<quote(id.serial)
      <<",\"firmware_word\":"<<id.firmware<<",\"fpga_word\":"<<id.fpga<<",\"hardware\":"<<id.hardware
      <<",\"oscillator_dac\":"<<id.oscillator_dac<<",\"synthetic\":"<<(mock_mode?"true":"false")<<",\"calibrated\":"<<(calibration?"true":"false")<<",\"acquisition_mode\":\"repeated-burst\"}\n";
    const auto begin=std::chrono::steady_clock::now();std::optional<std::uint64_t> previous_end;
    for(std::size_t i=0;i<count;++i) {
        if(cancelled())throw rsa::Error("interrupted","Native acquisition cancelled.",130);
        if(i && std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()>=duration)break;
        const auto frame=device.acquire(rsa::wire::fft_size(settings.spectrum.rbw_hz));
        if(raw) {
            for(auto sample:frame.samples) {
                const auto bits=std::bit_cast<std::uint16_t>(sample);
                const unsigned char b[]{static_cast<unsigned char>(bits),static_cast<unsigned char>(bits>>8)};
                if(fwrite(b,1,2,raw.get())!=2)throw rsa::Error("output_error","Cannot save raw ADC samples.",5);
            }
            if(fflush(raw.get())!=0)throw rsa::Error("output_error","Cannot flush raw ADC samples.",5);
        }
        emit_native(frame,settings,tune,i,"rsa306b-native-usb",previous_end,{},0,calibration.get());
        previous_end=frame.first_tick+frame.samples.size();
        if(i+1<count) {
            auto until=std::chrono::steady_clock::now()+std::chrono::duration<double,std::milli>(interval);
            while(std::chrono::steady_clock::now()<until && !cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}
void analyze_if(const Options& opts) {
    const auto settings=native_settings(opts);auto tune=rsa::wire::tuning(settings.spectrum.center_hz);
    if(opts.contains("--tuned-center-hz"))tune.tuned_hz=number(opts.at("--tuned-center-hz"));
    const auto n=opts.contains("--fft-size")?integer(opts.at("--fft-size"),8,1U<<20):rsa::wire::fft_size(settings.spectrum.rbw_hz);
    std::ifstream in(required(opts,"--input"),std::ios::binary);if(!in)throw rsa::Error("input_error","Cannot open raw IF input.",5);
    std::vector<std::uint8_t> bytes(n*2);in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(in.gcount()!=static_cast<std::streamsize>(bytes.size()))throw rsa::Error("incomplete_frame","Raw IF input is shorter than the FFT.",5);
    rsa::wire::Capture frame;
    for(std::size_t i=0;i<n;++i)frame.samples.push_back(std::bit_cast<std::int16_t>(rsa::wire::u16(bytes,2*i)));
    // Offline footer timestamps/status are unavailable in a samples-only file; source remains explicit.
    emit_native(frame,settings,tune,0,"offline-if-samples");
}
void analyze(const Options& opts) {
    const double center = number(required(opts, "--center-hz"));
    const double rate = number(required(opts, "--sample-rate-hz"));
    const auto n = integer(opts.contains("--fft-size") ? opts.at("--fft-size") : "4096", 8, 1U << 20);
    const auto count = integer(opts.contains("--count") ? opts.at("--count") : "1", 1, 1'000'000);
    if (!std::has_single_bit(n)) throw rsa::Error("invalid_arguments", "FFT size must be a power of two.", 2);
    rsa::Settings{center, rate, rate / static_cast<double>(n), 0}.validate();
    if (!std::isfinite(static_cast<double>(count * n) / rate))
        throw rsa::Error("invalid_arguments", "Requested sample duration cannot be represented.", 2);
    std::optional<double> start;
    if (opts.contains("--start-unix-seconds")) {
        start = number(opts.at("--start-unix-seconds"));
        if (*start < 0 || *start > 253402300799.0)
            throw rsa::Error("invalid_arguments", "Start timestamp must be between Unix epoch and year 9999.", 2);
    }
    std::ifstream input(required(opts, "--input"), std::ios::binary);
    if (!input) throw rsa::Error("input_error", "Cannot open offline IQ file.", 5);
    std::vector<std::uint8_t> bytes(n * 8);
    for (std::size_t frame = 0; frame < count; ++frame) {
        if (cancelled()) throw rsa::Error("interrupted", "Offline analysis interrupted.", 130);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size()))
            throw rsa::Error("incomplete_frame", "Offline input ended or failed before requested frame " +
                             std::to_string(frame) + "; no padded or partial spectrum was emitted.", 5);
        const auto samples = rsa::decode_iq_f32le(bytes);
        const auto s = rsa::spectrum(samples, rate, center);
        const double offset = static_cast<double>(frame * n) / rate;
        std::cout << std::setprecision(17)
                  << "{\"schema_version\":1,\"type\":\"spectrum\",\"source\":\"offline-iq\",\"sequence\":" << frame
                  << ",\"calibrated\":false,\"level_unit\":\"dBFS\",\"window\":\"periodic-hann\","
                     "\"timestamp_basis\":" << quote(start ? "provided_start_plus_sample_offset" : "sample_offset_only")
                  << ",\"acquisition_start_unix_seconds\":";
        if (start) std::cout << *start + offset; else std::cout << "null";
        std::cout << ",\"sample_offset\":" << frame * n << ",\"duration_seconds\":" << static_cast<double>(n) / rate
                  << ",\"center_hz\":" << center << ",\"sample_rate_hz\":" << rate
                  << ",\"bin_width_hz\":" << s.step_hz << ",\"window_enbw_hz\":" << s.enbw_hz
                  << ",\"quality\":{\"gap_status\":\"unknown\",\"overload\":null},\"frequency_hz\":[";
        for (std::size_t i = 0; i < n; ++i) {
            if (i) std::cout << ',';
            std::cout << s.start_hz + static_cast<double>(i) * s.step_hz;
        }
        std::cout << "],\"levels\":[";
        for (std::size_t i = 0; i < n; ++i) {
            if (i) std::cout << ',';
            std::cout << s.levels_dbfs[i];
        }
        std::cout << "]}\n";
        if (!std::cout) throw rsa::Error("output_error", "Cannot write spectrum output.", 5);
    }
}
void help() {
    std::cout << R"(RSA306B native acquisition prototype 0.4.0
  Common flags: --mock (synthetic tables/samples, no USB), --serial EXPECTED,
                --stdin-control (stop line or EOF; cleanup acknowledged on stdout).
    Full-range factory conversion has numerical validation; hardware acceptance
    and independent absolute RF accuracy remain unverified.
  rsa306 discover
    Read USB descriptors. Detection does not imply measurement support.
  rsa306 capabilities
    Report implemented frequency/level limits without USB access.
  rsa306 probe [--level-unit dbfs|dbm]
    Exclusively open, initialize and identify the analyzer, then stop and close; no samples.
  rsa306 read-calibration
    Initialize and read eight known stored blocks with CRC checks; output a JSON dump.
    No firmware or calibration writes. This diagnostic does not enable dBm output.
  rsa306 capture --center-hz HZ --span-hz HZ --rbw-hz HZ
                 [--attenuation-db 0] [--if-gain-db 17] [--raw-output NEW_FILE] [--usb-log NEW_FILE]
  rsa306 monitor (same band/gain options) --count N [--interval-ms 100] [--duration-seconds SEC]
    Native HW7/FW1.7/FPGA2.1 LF/RF paths, 9 kHz…6.2 GHz. Uncalibrated ADC dBFS.
    LF bands end at 22 MHz and use fixed VGA code 199; RF gain controls do not apply to LF.
    Repeated bursts have gaps; JSONL retains device ticks, host bounds, and status.
    --reference-dbm is rejected; automatic reference-level gain selection is not implemented.
    --level-unit dbfs is the default. dbm applies factory LF/RF tables (9 kHz–6.2 GHz,
    attenuation <=31 dB, IF gain >=-11 dB). Absolute RF accuracy remains unverified.
  rsa306 sweep --start-hz HZ --stop-hz HZ --rbw-hz HZ --count N
               [--attenuation-db 0] [--if-gain-db 17] [--interval-ms 100] [--duration-seconds SEC]
    Sequential overlapping bands, edges 9 kHz…6.2 GHz. Per-segment times and sweep completion.
  rsa306 plan-sweep (same frequency/gain options, without --count/--interval-ms)
    Print the sweep plan without USB access. Live sweeps acquire segments sequentially.
  rsa306 analyze-if --input FILE --center-hz HZ --span-hz HZ --rbw-hz HZ
                   [--tuned-center-hz HZ] [--fft-size N]
    Offline signed int16 LE real ADC samples at 112 MS/s, footers already removed.
  rsa306 analyze-iq --input FILE --center-hz HZ --sample-rate-hz HZ
                    [--fft-size 4096] [--count 1] [--start-unix-seconds SECONDS]
    Offline normalized complex float32 LE pairs only, NOT RSA USB packets. Outputs dBFS JSONL.
    Timestamp is unknown unless supplied; no live acquisition or real-time playback occurs.
)";
}
}

int main(int argc, char** argv) {
    std::vector<char*> filtered;filtered.push_back(argv[0]);
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--mock")mock_mode=true;
        else if(arg=="--stdin-control")stdin_control=true;
        else if(arg=="--serial" && i+1<argc)expected_serial=argv[++i];
        else filtered.push_back(argv[i]);
    }
    argc=static_cast<int>(filtered.size());argv=filtered.data();
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN); // Broken output pipes must unwind and stop acquisition.
#endif
    std::signal(SIGINT, on_interrupt);
    std::signal(SIGTERM, on_interrupt);
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") { help(); return 0; }
        const std::string command = argv[1];
        if (command == "discover") { (void)options(argc, argv, {}); discover(); }
        else if (command == "capabilities") { (void)options(argc, argv, {}); capabilities(); }
        else if (command == "probe") { probe(options(argc, argv, {"--level-unit"})); }
        else if (command == "read-calibration") { (void)options(argc, argv, {}); read_calibration(); }
        else if (command == "capture" || command == "monitor") {
            std::set<std::string> allowed{"--center-hz", "--span-hz", "--rbw-hz", "--reference-dbm", "--attenuation-db", "--if-gain-db", "--raw-output", "--usb-log", "--level-unit"};
            if (command == "monitor") {allowed.insert("--count");allowed.insert("--interval-ms");allowed.insert("--duration-seconds");}
            capture(options(argc, argv, allowed), command == "monitor");
        } else if (command == "sweep" || command == "plan-sweep") {
            std::set<std::string> allowed{"--start-hz","--stop-hz","--rbw-hz","--attenuation-db","--if-gain-db","--level-unit"};
            if(command=="sweep") {allowed.insert("--count");allowed.insert("--interval-ms");allowed.insert("--duration-seconds");}
            sweep(options(argc,argv,allowed),command=="plan-sweep");
        } else if (command == "analyze-if") {
            analyze_if(options(argc,argv,{"--input","--center-hz","--span-hz","--rbw-hz","--tuned-center-hz","--fft-size"}));
        } else if (command == "analyze-iq") {
            analyze(options(argc, argv, {"--input", "--center-hz", "--sample-rate-hz", "--fft-size",
                                        "--count", "--start-unix-seconds"}));
        } else throw rsa::Error("invalid_arguments", "Unknown command: " + command, 2);
        if(stdin_control)std::cout<<"{\"type\":\"stopped\",\"cleanup_complete\":true}\n"<<std::flush;
        return 0;
    } catch (const rsa::Error& e) {
        if(e.code=="interrupted" && stdin_control && control_stop) {
            std::cout<<"{\"type\":\"stopped\",\"cleanup_complete\":true}\n"<<std::flush;return 0;
        }
        std::cerr << "{\"schema_version\":1,\"type\":\"error\",\"code\":" << quote(e.code)
                  << ",\"message\":" << quote(e.what()) << "}\n";
        return e.exit_code;
    } catch (const std::exception& e) {
        std::cerr << "{\"schema_version\":1,\"type\":\"error\",\"code\":\"internal_error\",\"message\":"
                  << quote(e.what()) << "}\n";
        return 1;
    }
}
