#include "rsa/mock.hpp"
#include <bit>
#include <chrono>
#include <cmath>
#include <numbers>

namespace rsa::mock {
namespace {
void put(wire::Bytes& b,std::size_t at,std::uint64_t v,std::size_t n=4) {
    for(std::size_t i=0;i<n;++i)b.at(at+i)=static_cast<std::uint8_t>(v>>(8*i));
}
void f32(wire::Bytes& b,std::size_t at,double v){put(b,at,std::bit_cast<std::uint32_t>(static_cast<float>(v)));}
void f64(wire::Bytes& b,std::size_t at,double v){put(b,at,std::bit_cast<std::uint64_t>(v),8);}
}
std::vector<wire::StoredBlock> calibration() {
    std::vector<wire::StoredBlock> result;
    wire::Bytes l(3420);
    const unsigned header[]{0,46,2,188,36,220,404,412,1916};
    for(unsigned i=0;i<9;++i)put(l,4*i,header[i]);
    for(unsigned i=0;i<46;++i){f32(l,36+4*i,static_cast<double>(i)-11);f32(l,220+4*i,static_cast<double>(i)-11);}
    const double setting=20*std::log10((1+6.079458)*0.055744*(wire::lf_gain_code&127));
    f32(l,36+40*4,setting);f32(l,220+40*4,setting);
    put(l,404,6);put(l,408,35);
    for(unsigned i=0;i<188;++i){double hz=i==0?9000:(i==1?10000:(i==2?15000:20000+(i-3)*(45e6-20000)/184));f64(l,412+8*i,hz);for(unsigned row=0;row<2;++row)f32(l,1916+4*(row*188+i),-8);}
    result.push_back({2,std::move(l)});
    wire::Bytes v(272);const unsigned vh[]{0,32,16,144};for(unsigned i=0;i<4;++i)put(v,4*i,vh[i]);
    for(unsigned i=0;i<32;++i){f32(v,16+4*i,static_cast<double>(i)-11);f32(v,144+4*i,static_cast<double>(i)-11);}
    result.push_back({3,std::move(v)});
    wire::Bytes a(55676);const unsigned ah[]{0,9,1264,24,60,10172};for(unsigned i=0;i<6;++i)put(a,4*i,ah[i]);
    const double atten[]{0,2,6,10,14,18,22,26,31};
    for(unsigned i=0;i<1264;++i)f64(a,60+8*i,1e6+i*5e6);
    for(unsigned row=0;row<9;++row){f32(a,24+4*row,atten[row]);for(unsigned i=0;i<1264;++i)f32(a,10172+4*(row*1264+i),-atten[row]);}
    result.push_back({4,std::move(a)});
    wire::Bytes g(30856);const unsigned gh[]{1,40,12,520,1264,10632,4,1264,0,30856};for(unsigned i=0;i<10;++i)put(g,4*i,gh[i]);
    for(unsigned i=0;i<12;++i){f64(g,40+40*i,i*600e6);f64(g,48+40*i,(i+1)*600e6);}
    for(unsigned i=0;i<1264;++i){f64(g,520+8*i,1e6+i*5e6);for(unsigned row=0;row<4;++row)f32(g,10632+4*(row*1264+i),row*5);}
    result.push_back({5,std::move(g)});
    wire::Bytes e(4108);const unsigned eh[]{1,2,501,20,100};for(unsigned i=0;i<5;++i)put(e,4*i,eh[i]);result.push_back({6,std::move(e)});
    return result;
}
wire::Capture capture(const wire::NativeSettings& settings,const wire::Tuning& tune,std::size_t count,std::uint64_t tick) {
    wire::Capture result;result.first_tick=tick;result.last_tick=tick+count-1;result.descriptor=1;
    result.host_start=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    result.samples.reserve(count);
    const double hz=tune.lf_path?settings.spectrum.center_hz:wire::digital_if+settings.spectrum.center_hz-tune.tuned_hz;
    const double bin=std::round(hz*static_cast<double>(count)/wire::adc_rate);
    std::uint32_t state=12345;
    for(std::size_t i=0;i<count;++i){state=1664525*state+1013904223;const double noise=static_cast<double>(state&255)-127.5;
        result.samples.push_back(static_cast<std::int16_t>(std::round(32*std::cos(2*std::numbers::pi*bin*static_cast<double>(i)/static_cast<double>(count))+noise)));}
    result.host_end=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();return result;
}
}
