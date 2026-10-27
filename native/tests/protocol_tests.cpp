#include "rsa/protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <numbers>
#include <sstream>
#include <map>
using namespace rsa::wire;
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
const char* fixture(const char* name,const char* fallback) {
    const auto value=std::getenv(name);return value && *value?value:fallback;
}
void rejected(const std::function<void()>& fn) {
    try {fn();} catch(const rsa::Error&) {return;} throw std::runtime_error("Invalid protocol data accepted");
}
void put(Bytes& b,std::size_t p,std::uint64_t value,std::size_t width) {for(std::size_t i=0;i<width;++i)b[p+i]=static_cast<std::uint8_t>(value>>(8*i));}
Bytes frame(std::uint16_t tag,std::uint32_t counter,std::uint64_t tick,std::uint16_t status=0) {
    Bytes b(frame_bytes);put(b,0,0x8000,2);put(b,2,32764,2);put(b,16362,tag,2);put(b,16364,counter,4);put(b,16374,status,2);put(b,16376,tick,8);return b;
}
void commands() {
    require(request(0,22)==Bytes({0,0,8,0,22,0,255,255}),"Echo request encoding mismatch");
    const Bytes ack{0,0,12,0,22,0,0,0,0,0,255,255};require(response(ack,0,22).empty(),"Ack payload");
    auto bad=ack;bad[6]=1;rejected([&]{response(bad,0,22);});
    rejected([&]{response(ack,1,22);});rejected([&]{response(std::span(ack).first(11),0,22);});
    for(auto op:{6,7,9,10,14,16,18,20,21,27})rejected([&]{request(0,static_cast<std::uint16_t>(op));});
    const std::array<Register,1> erase{{{0x10,1}}};rejected([&]{registers(erase);});
    const Bytes trim{1,0,0,0,0x1e,2,0,0,0,0,0,0,0,0x68,0x41,0x40};
    require(crc16(trim)==0x7c52,"Oscillator trim checksum mismatch");
    Bytes header(8);put(header,2,55676,4);
    require(stored_block_size(4,header)==55676,"RF attenuation block size rejected");
    rejected([&]{stored_block_size(0,header);});
    rejected([&]{stored_block_size(7,header);});
    rejected([&]{stored_block_size(4,std::span(header).first(7));});
    put(header,2,65537,4);rejected([&]{stored_block_size(4,header);});
    put(header,2,0,4);rejected([&]{stored_block_size(4,header);});
}
void startup_tests() {
    // POST success and one stored error: raw code 5 means USB speed.
    check_post(Bytes{0,0});
    try {check_post(Bytes{1,0,5,0});throw std::runtime_error("Failed POST accepted");}
    catch(const rsa::Error& e) {
        require(e.code=="device_post_failure" && std::string(e.what()).find("5 (USB speed)")!=std::string::npos,
                "Stored USB-speed failure must be actionable");
    }
    rejected([]{check_post(Bytes{});});
    rejected([]{check_post(Bytes{0,0,5,0});});
    rejected([]{check_post(Bytes{2,0,5,0});});
    rejected([]{check_post(Bytes{1,0,255,255});}); // Unknown errors still fail closed.
    const Bytes failed_register{3,0,12,0,4,0,3,0,74,0,255,255};
    try {response(failed_register,3,4);throw std::runtime_error("Failed register read accepted");}
    catch(const rsa::Error& e) {
        require(std::string(e.what()).find("status 3, auxiliary 74")!=std::string::npos,
                "Command errors must retain device diagnostic codes");
    }
}
void frequencies() {
    // Fixed HW7 register vectors, independent of the implementation under test.
    struct Case {double center;std::uint32_t r0,r2,r4;unsigned filter;};
    for(const auto& c:std::initializer_list<Case>{{100e6,0x005a8060,0x400139c2,0x62930184,1},
         {400e6,0x006500c0,0x40013dc2,0x62930184,1},{600e6,0x00360100,0x40012fc2,0x62830384,1},
         {610e6,0x003601a0,0x40012fc2,0x62830384,1},{1200e6,0x00410000,0x400131c2,0x62830384,2},
         {1490e6,0x004600a0,0x400133c2,0x62830384,2}}) {
        const auto t=tuning(c.center);require(t.pll[0]==c.r0 && t.pll[2]==c.r2 && t.pll[4]==c.r4 && t.filter==c.filter,"PLL encoding mismatch");
        require(std::abs(t.tuned_hz-c.center)<1e-5,"On-grid tune mismatch");
    }
    require(tuning(600123456).tuned_hz==600e6,"Off-grid center must report actual LO grid");
    require(tuning(699750000).filter==1 && tuning(700250000).filter==2,"700 MHz filter boundary");
    rejected([]{tuning(8999);});rejected([]{fft_size(1);});
    rejected([]{NativeSettings{{9180,200,200,0},0,17}.validate();});
}
void full_range_fixtures() {
    // Register-level regression vectors; no recorded RF samples.
    std::ifstream file(fixture("RSA_ROUTE_FIXTURE",RSA_ROUTE_FIXTURE));std::string line;std::getline(file,line);
    std::size_t count=0;
    while(std::getline(file,line)) {
        std::stringstream input(line);std::string item;std::vector<std::string> columns;
        while(std::getline(input,item,','))columns.push_back(item);
        require(columns.size()==14,"Malformed route fixture");
        const auto t=tuning(std::stod(columns[1]));
        auto check=[&](bool ok){if(!ok)throw std::runtime_error("Route differs: "+columns[0]);};
        check(std::abs(t.tuned_hz-std::stod(columns[2]))<1);
        check(t.lf_path==(std::stoi(columns[3])!=0));
        if(!t.lf_path) {
            check(t.filter==std::stoul(columns[4]) && t.first_if_hz==std::stod(columns[5]));
            check(t.high_side==(std::stoi(columns[6])!=0) && t.second_lo_word==std::stoul(columns[7]));
            for(std::size_t i=0;i<6;++i)check(t.pll[i]==std::stoul(columns[8+i]));
        }
        ++count;
    }
    require(count==66,"Missing route cases");
    // The factory route is observed 0x71; restoring dBFS must remove its bit 0.
    unsigned route=0x50;
    for(bool factory:{true,false,true}) {
        for(auto [address,value]:configuration_registers({{594e6,20e6,1e4,0},1,11,factory},42)) {
            if((address&0x3fff)!=0x30)continue;
            if(address&0x8000)route|=value;else if(address&0x4000)route&=~value;else route=value;
        }
        require(route==(factory?0x71U:0x70U),"Factory/manual RF route not restored");
    }
    // Apply complete plans in arbitrary orders, including LF -> alternate RF -> LF.
    std::map<unsigned,unsigned> registers{{0x30,0x70},{0x32,0},{0x34,15},{0x35,23},{0x40,9}};
    for(double center:{600e6,10e6,2425e6,4200e6,6100e6,9500.0,600e6}) {
        const auto t=tuning(center);
        const double span=center==9500?1000:1e6;
        const auto plan=configuration_registers({{center,span,center==9500?200:1e4,0},.5,11},42);
        (void)rsa::wire::registers(plan); // Volatile allowlist remains enforced.
        for(const auto& [a,v]:plan) {
            auto& value=registers[a&0x3fff];
            if(a&0x8000)value|=v;else if(a&0x4000)value&=~v;else value=v;
        }
        require((registers[0x32]&1)==unsigned(t.lf_path),"LF input selection not restored");
        require((registers[0x35]&16)==(t.lf_path?0U:16U),"IF input selection not restored");
        require((registers[0x40]&1)==unsigned(!t.lf_path && t.high_side),"DATA_INV not restored");
        require(registers[0x30]==(t.lf_path?0x50U:0x70U),"Manual gain route not restored");
        if(!t.lf_path) require((registers[0x34]&4)==(t.first_if_hz==2440e6?4U:0U),"First IF route not restored");
        require(registers[0x43]==42,"Acquisition descriptor missing");
    }
}
void collector() {
    FrameCollector c(11,16384);
    auto stale=frame(10,0,0);c.push(stale);
    for(unsigned i=0;i<3;++i) {auto b=frame(11,i,100000+i*samples_per_frame,static_cast<std::uint16_t>(i==2?1:0));for(auto byte:b)c.push(std::span(&byte,1));}
    require(c.complete() && c.result.samples.size()==16384,"Partial USB reassembly");
    require(c.result.samples[0]==-32768 && c.result.samples[1]==32764,"Signed ADC sample decoding");
    require(c.result.discarded_frames==1 && c.result.status==1 && c.result.gap_events==0,"Frame tag/status accounting");
    FrameCollector gap(11,8192);gap.push(frame(11,1,8178));gap.push(frame(11,3,24534));
    require(!gap.complete() && gap.result.gap_events==1 && gap.result.first_tick==24534,"Must discard incomplete FFT at a sample gap");
    gap.push(frame(11,4,32712));require(gap.complete(),"Recovery after sample gap");
    FrameCollector wrap(11,8192);wrap.push(frame(11,0xffffffff,100));wrap.push(frame(11,0,8278));
    require(wrap.complete() && wrap.result.gap_events==0,"32-bit frame counter rollover");
    const auto f=footer(frame(31245,17,13397425594ULL,4));
    require(f.descriptor==31245 && f.counter==17 && f.timestamp==13397425594ULL && f.status==4,"Synthetic footer fields");
}
void real_fft() {
    constexpr std::size_t n=32768;constexpr unsigned bin=7000;
    std::vector<std::int16_t> samples(n);
    for(std::size_t i=0;i<n;++i)samples[i]=static_cast<std::int16_t>(std::lround(16384*std::cos(2*std::numbers::pi*bin*static_cast<double>(i)/n)));
    const auto s=if_spectrum(samples,{610e6,20e6,1e4,0},610e6);
    const auto peak=std::max_element(s.levels_dbfs.begin(),s.levels_dbfs.end());
    require(std::abs(*peak+6.020599913)<0.001,"Real ADC sine amplitude convention");
    require(std::abs(s.start_hz+static_cast<double>(peak-s.levels_dbfs.begin())*s.step_hz-(610e6-28e6+112e6*bin/n))<1e-6,"Real IF to RF mapping");
    for(double center:{2425e6,4200e6,6100e6}) {
        const auto high=if_spectrum(samples,{center,20e6,1e4,0},center);
        const auto p=std::max_element(high.levels_dbfs.begin(),high.levels_dbfs.end());
        require(std::abs(high.start_hz+static_cast<double>(p-high.levels_dbfs.begin())*high.step_hz-(center-28e6+112e6*bin/n))<1e-6,"RF path orientation");
    }
    // LF is direct sampled: its axis has no 28 MHz translation.
    constexpr unsigned lf_bin=2900;
    for(std::size_t i=0;i<n;++i)samples[i]=static_cast<std::int16_t>(std::lround(16384*std::cos(2*std::numbers::pi*lf_bin*static_cast<double>(i)/n)));
    const auto lf=if_spectrum(samples,{10e6,1e6,1e4,0},10e6);
    const auto lp=std::max_element(lf.levels_dbfs.begin(),lf.levels_dbfs.end());
    require(std::abs(*lp+6.020599913)<0.001,"Direct LF sine scaling");
    require(std::abs(lf.start_hz+static_cast<double>(lp-lf.levels_dbfs.begin())*lf.step_hz-112e6*lf_bin/n)<1e-6,"Direct LF axis");
    std::fill(samples.begin(),samples.end(),0);
    const auto silence=if_spectrum(samples,{610e6,20e6,1e4,0},610e6);
    require(std::all_of(silence.levels_dbfs.begin(),silence.levels_dbfs.end(),[](double x){return x==-300.0;}),"Real IF silence floor");
}
void worker_lifetime() {
    // Cancellation is checked before libusb_init: this test never opens USB.
    try {Session cancelled([]{return true;});throw std::runtime_error("Cancelled startup accepted");}
    catch(const rsa::Error& e) {require(e.code=="interrupted","Pre-USB cancellation");}
    try {Session second([]{return true;});throw std::runtime_error("Second session attempt accepted");}
    catch(const rsa::Error& e) {require(e.code=="session_reuse_unsupported","Worker lifetime guard must precede any USB access");}
}
void sweeps() {
    for(const auto bounds: {std::pair{9000.0,6.2e9},std::pair{470e6,694e6},
                            std::pair{11e6,12e6},std::pair{600123456.0,640123457.0},
                            std::pair{3e6,23e6},std::pair{21e6,22.1e6},std::pair{21.999e6,22.001e6},std::pair{9000.0,10000.0}}) {
        const auto plan=sweep_plan(bounds.first,bounds.second,std::min(1e4,(bounds.second-bounds.first)/5),0,17);
        double covered=bounds.first;
        for(const auto& s:plan) {
            s.validate();
            require(s.spectrum.span_hz<=20e6,"Sweep band width");
            require(s.spectrum.center_hz-s.spectrum.span_hz/2<=covered,"Sweep coverage hole");
            covered=s.spectrum.center_hz+s.spectrum.span_hz/2;
        }
        require(covered>=bounds.second,"Sweep must reach requested stop");
    }
    require(sweep_plan(470e6,510e6,20e6,0,17).size()==3,"Last band retains full RBW capacity");
    rejected([]{sweep_plan(8999,20e6,1e4,0,17);});
    rejected([]{sweep_plan(470e6,6201e6,1e4,0,17);});
    rejected([]{sweep_plan(470e6,470e6,1e4,0,17);});
    rejected([]{sweep_plan(470e6,694e6,1e4,0.25,17);});
}
}
int main() {try {commands();startup_tests();frequencies();full_range_fixtures();sweeps();collector();real_fft();worker_lifetime();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}std::cout<<"Protocol, full-range plans, 66 routes, startup faults, worker lifetime, partial transfers, gaps, rollover and LF/RF FFT passed.\n";}
