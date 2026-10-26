#include "rsa/protocol.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace rsa::wire {
namespace {
void need(bool valid, const char* message) {
    if (!valid) throw Error("protocol_error", message, 4);
}
void put16(Bytes& bytes, std::uint16_t v) {
    bytes.push_back(static_cast<std::uint8_t>(v));
    bytes.push_back(static_cast<std::uint8_t>(v >> 8));
}
}
std::uint16_t u16(std::span<const std::uint8_t> b, std::size_t p) {
    need(p <= b.size() && b.size() - p >= 2, "Truncated 16-bit field.");
    return static_cast<std::uint16_t>(unsigned(b[p]) | (unsigned(b[p+1]) << 8));
}
std::uint32_t u32(std::span<const std::uint8_t> b, std::size_t p) {
    return std::uint32_t(u16(b,p)) | (std::uint32_t(u16(b,p+2)) << 16);
}
std::uint64_t u64(std::span<const std::uint8_t> b, std::size_t p) {
    return std::uint64_t(u32(b,p)) | (std::uint64_t(u32(b,p+4)) << 32);
}
Bytes words(std::span<const std::uint16_t> values) {
    Bytes bytes; for (auto v : values) put16(bytes,v); return bytes;
}
Bytes request(std::uint16_t seq, std::uint16_t op, std::span<const std::uint8_t> payload) {
    // Explicit allowlist: no firmware, erase, flash-write, or block-write commands.
    constexpr std::array<unsigned, 10> allowed{1,2,3,4,5,11,17,19,22,23};
    need(std::find(allowed.begin(),allowed.end(),op) != allowed.end(), "Command outside the native allowlist.");
    need(payload.size() % 2 == 0 && payload.size() <= 1016, "Invalid command payload size.");
    Bytes result; put16(result,seq); put16(result,static_cast<std::uint16_t>(payload.size()+8));
    put16(result,op); result.insert(result.end(),payload.begin(),payload.end()); put16(result,0xffff);
    return result;
}
Bytes response(std::span<const std::uint8_t> b, std::uint16_t seq, std::uint16_t op) {
    need(b.size() >= 12 && b.size() % 2 == 0, "Malformed command response.");
    need(u16(b,0)==seq && u16(b,2)==b.size() && u16(b,4)==op && u16(b,b.size()-2)==0xffff,
         "Response sequence, opcode, length, or terminator mismatch.");
    if (u16(b,6)!=0 || u16(b,8)!=0)
        throw Error("protocol_error", "Device rejected command " + std::to_string(op) +
                    " (sequence " + std::to_string(seq) + ", status " + std::to_string(u16(b,6)) +
                    ", auxiliary " + std::to_string(u16(b,8)) + ").",4);
    return {b.begin()+10,b.end()-2};
}
void check_post(std::span<const std::uint8_t> b) {
    need(b.size()>=2 && b.size()%2==0, "Malformed startup-test response.");
    const auto count=u16(b,0);
    need(b.size()==2+std::size_t(count)*2, "Startup-test error count/length mismatch.");
    if(!count) return;
    std::string message="Analyzer startup self-test failed:";
    bool speed_failure=false;
    for(std::size_t i=0;i<count;++i) {
        const auto code=u16(b,2+2*i);
        const char* name="unknown";
        switch(code) {
        case 1:name="FPGA load";break;
        case 2:name="high power";break;
        case 3:name="I2C";break;
        case 4:name="GPIF";break;
        case 5:name="USB speed";speed_failure=true;break;
        }
        message+=" "+std::to_string(code)+" ("+name+")";
    }
    message+=". Acquisition was not started.";
    if(speed_failure)
        message+=" Unplug for ten seconds and reconnect through a USB 3 cable/port; try another port or adapter if it recurs. Current host link speed does not clear a stored startup failure.";
    throw Error("device_post_failure",message,4);
}
Bytes registers(std::span<const Register> pairs) {
    need(!pairs.empty() && pairs.size()<=253, "Invalid register-write count.");
    Bytes b; put16(b,static_cast<std::uint16_t>(pairs.size()));
    for (const auto& [address,value] : pairs) {
        const unsigned reg=address & 0x3fff;
        constexpr std::array<unsigned,14> allowed{0x12,0x15,0x20,0x22,0x23,0x30,0x32,0x33,0x34,0x35,0x40,0x41,0x43,0x47};
        need((address & 0xc000)!=0xc000 && std::find(allowed.begin(),allowed.end(),reg)!=allowed.end(),
             "Register outside the volatile acquisition allowlist.");
        put16(b,address); put16(b,value);
    }
    return b;
}
std::uint16_t crc16(std::span<const std::uint8_t> bytes) {
    std::uint16_t crc=0;
    for (auto v:bytes) {
        crc ^= v;
        for (int i=0;i<8;++i) crc=static_cast<std::uint16_t>((crc>>1)^((crc&1)?0xa001:0));
    }
    return crc;
}
std::size_t stored_block_size(std::uint16_t index, std::span<const std::uint8_t> header) {
    need(header.size()==8,"Unexpected stored-block header length.");
    // Per-block limits cover the captured HW7 formats without accepting an
    // arbitrary flash index or allowing device-supplied unbounded allocation.
    std::size_t limit=0;
    switch(index) {
    case 0:case 1:case 2:case 3:limit=4096;break;
    case 4:limit=65536;break;
    case 5:limit=32768;break;
    case 6:case 8:limit=8192;break;
    default:throw Error("unsupported_calibration_block","Stored block is outside the read allowlist.",4);
    }
    const auto size=u32(header,2);
    need(size>0 && size<=limit,"Unsupported stored-block length.");
    return size;
}
Footer footer(std::span<const std::uint8_t> frame) {
    need(frame.size()==frame_bytes,"An IF frame must contain exactly 16384 bytes.");
    return {u16(frame,16362),u16(frame,16374),u32(frame,16364),u64(frame,16376)};
}
Tuning tuning(double center) {
    // HW7 frequency plan, checked against paired API/USB captures on 2026-10-26.
    // The vendor's internally named "RevB" table does not describe this HW7 unit.
    if (!std::isfinite(center) || center<min_center_hz || center>max_frequency_hz)
        throw Error("unsupported_native_band","Native HW7 centers must be within 9 kHz–6.2 GHz.",2);
    if(center<lf_limit_hz) return {center,center,1,{},true,false,0,0};
    const unsigned filter=center<700e6?1U:center<1850e6?2U:center<2700e6?3U:
                          center<3700e6?4U:center<4960e6?5U:center<5700e6?6U:7U;
    const bool alternate=center>=1850e6 && !(center>=4175e6 && center<4225e6);
    const bool high_side=center<4175e6 || (center>=4225e6 && center<4750e6);
    const double first_if=alternate?1190e6:2440e6;
    const double desired_lo=high_side?center+first_if:center-first_if;
    const unsigned div=desired_lo<3e9 ? 2U : 1U;
    const unsigned modulus=div==2 ? 28U : 56U;
    const double ratio=desired_lo*div/28e6;
    unsigned integer=static_cast<unsigned>(std::floor(ratio));
    unsigned fraction=static_cast<unsigned>(std::floor((ratio-integer)*modulus+0.5));
    if (fraction==modulus) {++integer;fraction=0;}
    const double actual_lo=(integer+static_cast<double>(fraction)/modulus)*28e6/div;
    const double vco=desired_lo*div;
    // The API emits charge-pump code 15 exactly at 3 GHz VCO; two independent
    // 560 MHz captures establish this otherwise discontinuous boundary case.
    const auto cp=vco==3e9?15U:static_cast<unsigned>(std::clamp(std::ceil((vco-3e9)/375e6)+6,7.0,14.0));
    const std::uint32_t r4=0x62830184U | (div==2?0x100000U:0x200U);
    return {center,high_side?actual_lo-first_if:actual_lo+first_if,filter,
            {(integer<<15)|(fraction<<3),0x20008001U|(modulus<<3),
             0x400121c2U|(cp<<9),0x0000000bU,r4,0x01400005U},false,high_side,first_if,
             alternate?0x404116U:0x471eU};
}
void append_spi(std::vector<Register>& pairs, std::uint16_t mask, std::uint32_t value) {
    pairs.emplace_back(0x22,static_cast<std::uint16_t>(value));
    pairs.emplace_back(0x23,static_cast<std::uint16_t>(value>>16));
    pairs.emplace_back(0x20,mask);
}
std::size_t fft_size(double rbw) {
    if (!std::isfinite(rbw) || rbw<=0) throw Error("invalid_settings","RBW must be positive.",2);
    std::size_t n=8;
    while (1.5*adc_rate/static_cast<double>(n)>rbw && n<(1U<<20)) n*=2;
    if (1.5*adc_rate/static_cast<double>(n)>rbw)
        throw Error("unsupported_rbw","Native Hann ENBW cannot be below 160.2173 Hz (1048576 samples).",2);
    return n;
}
void NativeSettings::validate() const {
    spectrum.validate(); const auto t=tuning(spectrum.center_hz);
    const double bin_hz=adc_rate/static_cast<double>(fft_size(spectrum.rbw_hz));
    if(t.lf_path && spectrum.center_hz+spectrum.span_hz/2>lf_limit_hz)
        throw Error("unsupported_native_band","A direct LF band must end at or below 22 MHz. Use wide sweep to cross the LF/RF boundary.",2);
    if(spectrum.span_hz/2+std::abs(t.tuned_hz-t.requested_hz)>20e6)
        throw Error("unsupported_native_band","Reduce span to keep the requested band inside the tuned 40 MHz passband.",2);
    const double offset=t.lf_path?0:t.tuned_hz-digital_if;
    if(std::ceil((spectrum.center_hz-spectrum.span_hz/2-offset)/bin_hz)>=
       std::floor((spectrum.center_hz+spectrum.span_hz/2-offset)/bin_hz))
        throw Error("invalid_settings","Band must contain at least two FFT bins. Reduce RBW or widen the band.",2);
    if (!std::isfinite(attenuation_db) || attenuation_db<0 || attenuation_db>31.5 ||
        std::round(attenuation_db*2)!=attenuation_db*2 || !std::isfinite(if_gain_db) ||
        if_gain_db < -11.5 || if_gain_db > 20 || std::round(if_gain_db*2)!=if_gain_db*2)
        throw Error("invalid_gain","RF attenuation must be 0…31.5 dB and IF gain -11.5…20 dB, both in 0.5 dB steps.",2);
    if(factory_dbm && ((!t.lf_path && spectrum.center_hz-spectrum.span_hz/2<lf_limit_hz) || attenuation_db>31 || if_gain_db < -11))
        throw Error("unsupported_calibration","Factory dBm bands must stay within one LF/RF domain; RF controls require attenuation 0…31 dB and IF gain -11…20 dB.",2);
}
std::vector<Register> configuration_registers(const NativeSettings& settings,std::uint16_t tag) {
    settings.validate();
    if(tag==0xffff) throw Error("invalid_settings","Reserved transition descriptor.",2);
    const auto t=tuning(settings.spectrum.center_hz);
    std::vector<Register> regs{{0x43,0xffff}};
    if(t.lf_path) {
        // Direct ADC input, fixed observed LF VGA code. RF controls do not apply.
        regs.insert(regs.end(),{{0x4030,0x2f},{0x8032,1},{0x4035,0x10},{0x4040,1}});
        append_spi(regs,1,lf_gain_code);
        regs.emplace_back(0x47,19);
    } else {
        regs.insert(regs.end(),{{0x4032,1},{0x8035,0x10},{0x4033,7},
                               {0x8033,static_cast<std::uint16_t>(t.filter)}});
        for(int i=5;i>=0;--i) append_spi(regs,4,t.pll[static_cast<std::size_t>(i)]);
        regs.emplace_back(t.first_if_hz==2440e6?0x8034:0x4034,4);
        append_spi(regs,8,t.second_lo_word);
        // Hardware DATA_INV compensates high-side mixing; RF bins increase in
        // positive digital frequency for both observed IF routes.
        regs.emplace_back(t.high_side?0x8040:0x4040,1);
        // Retain the explicit manual RF amplification route from the original
        // backend, independent of the vendor API's automatic reference level.
        regs.insert(regs.end(),{{0x4030,0x0f},{0x8030,static_cast<std::uint16_t>(settings.factory_dbm?0x31:0x30)}});
        append_spi(regs,2,static_cast<std::uint32_t>(static_cast<std::int32_t>(-2*settings.attenuation_db-1)));
        append_spi(regs,0x10,static_cast<std::uint32_t>(40-2*settings.if_gain_db));
        regs.emplace_back(0x47,t.first_if_hz==2440e6?79:78);
    }
    regs.emplace_back(0x43,tag);
    return regs;
}
std::vector<NativeSettings> sweep_plan(double start,double stop,double rbw,double attenuation,double gain,bool factory_dbm) {
    if(!std::isfinite(start) || !std::isfinite(stop) || start<min_sweep_hz || stop>max_frequency_hz || stop<=start)
        throw Error("unsupported_sweep_range","Sweep edges must satisfy 9 kHz <= start < stop <= 6.2 GHz.",2);
    if(!std::isfinite(rbw) || rbw<=0 || rbw>std::min(stop-start,20e6))
        throw Error("invalid_settings","Sweep RBW must be positive and no larger than the range or 20 MHz.",2);
    if(factory_dbm) {
        std::vector<NativeSettings> joined;
        auto append=[&](double low,double high) {
            if(low>=high)return;
            // A narrow fragment next to 22 MHz can contain fewer than two FFT
            // bins even when the requested overall sweep is valid. Acquire a
            // wider band inside this same calibration domain; the consumer
            // filters it to the original sweep edges, never filling samples.
            const bool lf=low<lf_limit_hz;
            const double domain_low=lf?min_sweep_hz:lf_limit_hz;
            const double domain_high=lf?lf_limit_hz:max_frequency_hz;
            const double minimum=std::min(domain_high-domain_low,4.5*adc_rate/static_cast<double>(fft_size(rbw)));
            if(high-low<minimum) {
                high=std::min(domain_high,low+minimum);
                low=std::max(domain_low,high-minimum);
            }
            auto part=sweep_plan(low,high,rbw,attenuation,gain,false);
            for(auto& settings:part){settings.factory_dbm=true;settings.validate();joined.push_back(settings);}
        };
        append(start,std::min(stop,lf_limit_hz));append(std::max(start,lf_limit_hz),stop);
        std::size_t points=0;for(const auto& settings:joined)points+=static_cast<std::size_t>(std::ceil(settings.spectrum.span_hz*static_cast<double>(fft_size(rbw))/adc_rate))+1;
        if(points>max_trace_points || joined.size()>max_sweep_segments)
            throw Error("sweep_too_large","Sweep exceeds 4,194,304 samples or 512 segments. Increase RBW or reduce the range.",2);
        return joined;
    }
    std::vector<NativeSettings> result;
    std::size_t points=0;
    // Two MHz overlap avoids treating FFT edge rounding as unobserved segments.
    // Preserve per-segment axes/times; the display combines overlapping bins.
    const double span=std::min(stop-start,20e6);
    double left=start;
    while(true) {
        double center=std::min(left,stop-span)+span/2;
        if(center<lf_limit_hz && center+span/2>lf_limit_hz)
            center=left<lf_limit_hz-span/2?lf_limit_hz-span/2:lf_limit_hz;
        const double right=center+span/2;
        NativeSettings settings{{center,span,rbw,0},attenuation,gain};settings.validate();
        // Conservative bin bound; reject excessive memory before any USB access.
        points+=static_cast<std::size_t>(std::ceil(span*static_cast<double>(fft_size(rbw))/adc_rate))+1;
        if(points>max_trace_points || result.size()>=max_sweep_segments)
            throw Error("sweep_too_large","Sweep exceeds 4,194,304 samples or 512 segments. Increase RBW or reduce the range.",2);
        result.push_back(settings);
        if(right>=stop)break;
        left=right-std::min(2e6,span/10);
    }
    return result;
}
FrameCollector::FrameCollector(std::uint16_t descriptor, std::size_t target):target_(target) {
    if (target<8 || target>(1U<<20) || !std::has_single_bit(target) || descriptor==0xffff)
        throw Error("invalid_frame_collector","Invalid target or descriptor.",2);
    result.descriptor=descriptor;result.samples.reserve(target);
}
bool FrameCollector::complete() const {return result.samples.size()>=target_ && contiguous_frames_>=2;}
void FrameCollector::push(std::span<const std::uint8_t> bytes) {
        remainder_.insert(remainder_.end(),bytes.begin(),bytes.end());
        std::size_t offset=0;
        for(;remainder_.size()-offset>=frame_bytes;offset+=frame_bytes) {
            auto f=std::span(remainder_).subspan(offset,frame_bytes);const auto meta=footer(f);
            if(meta.descriptor!=result.descriptor) {++result.discarded_frames;continue;}
            if(complete()) continue;
            if(previous_ && (meta.counter!=std::uint32_t(previous_->counter+1) || meta.timestamp!=previous_->timestamp+samples_per_frame)) {
                ++result.gap_events;result.samples.clear();result.status=0;contiguous_frames_=0;
            }
            previous_=meta;
            ++contiguous_frames_;
            if(result.samples.empty()) {result.first_tick=meta.timestamp;result.first_counter=meta.counter;}
            result.last_tick=meta.timestamp;result.last_counter=meta.counter;result.status|=meta.status;
            for(std::size_t i=0;i<samples_per_frame && result.samples.size()<target_;++i)
                result.samples.push_back(std::bit_cast<std::int16_t>(u16(f,2*i)));
        }
        remainder_.erase(remainder_.begin(),remainder_.begin()+static_cast<std::ptrdiff_t>(offset));
    }
} // namespace rsa::wire
