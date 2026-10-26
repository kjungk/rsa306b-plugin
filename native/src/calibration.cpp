#include "rsa/calibration.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <tuple>

namespace rsa {
namespace {
using V = std::vector<double>;
using B = wire::Bytes;
[[noreturn]] void bad(const std::string& s) { throw Error("invalid_calibration",s,3); }
void require(bool ok,const std::string& s) { if(!ok)bad(s); }
void axis(const V& x,const V& y) {
    require(x.size()==y.size() && x.size()>=2,"Invalid calibration interpolation arrays.");
    for(std::size_t i=0;i<x.size();++i)
        require(std::isfinite(x[i]) && std::isfinite(y[i]) && (!i || x[i]>x[i-1]),"Invalid calibration axis/value.");
}
std::size_t interval(const V& x,double v) {
    require(std::isfinite(v),"Non-finite interpolation target.");
    auto i=std::upper_bound(x.begin(),x.end(),v)-x.begin();
    return std::min(i>0?static_cast<std::size_t>(i-1):0,x.size()-2);
}
V sub(const V& a,std::size_t left,std::size_t end) {
    require(left<end && end<=a.size(),"Invalid calibration slice.");
    return V(a.begin()+static_cast<std::ptrdiff_t>(left),a.begin()+static_cast<std::ptrdiff_t>(end));
}
V pchip(const V& x,const V& y,const V& targets) {
    axis(x,y); const auto n=x.size(); V h(n-1),s(n-1),d(n);
    for(std::size_t i=0;i<n-1;++i) {h[i]=x[i+1]-x[i];s[i]=(y[i+1]-y[i])/h[i];}
    if(n==2) d[0]=d[1]=s[0];
    else {
        auto end=[](double a,double b,double u,double v) {double q=((2*a+b)*u-a*v)/(a+b);return q*u<=0?0:(u*v<0 && std::abs(q)>3*std::abs(u)?3*u:q);};
        d[0]=end(h[0],h[1],s[0],s[1]);d[n-1]=end(h[n-2],h[n-3],s[n-2],s[n-3]);
        for(std::size_t i=1;i<n-1;++i)if(s[i-1]*s[i]>0) {double a=2*h[i]+h[i-1],b=h[i]+2*h[i-1];d[i]=(a+b)/(a/s[i-1]+b/s[i]);}
    }
    V result;result.reserve(targets.size());
    for(double v:targets) {auto i=interval(x,v);double t=(v-x[i])/h[i],t2=t*t,t3=t2*t;
        result.push_back((2*t3-3*t2+1)*y[i]+(t3-2*t2+t)*h[i]*d[i]+(-2*t3+3*t2)*y[i+1]+(t3-t2)*h[i]*d[i+1]);}
    return result;
}
V spline(const V& x,const V& y,const V& targets) {
    axis(x,y);const auto n=x.size();V second(n),rhs(n),upper(n);
    for(std::size_t i=1;i<n-1;++i) {double a=x[i]-x[i-1],b=x[i+1]-x[i],diagonal=2*(a+b)-a*upper[i-1];
        upper[i]=b/diagonal;rhs[i]=(6*((y[i+1]-y[i])/b-(y[i]-y[i-1])/a)-a*rhs[i-1])/diagonal;}
    for(std::size_t i=n-2;i>0;--i)second[i]=rhs[i]-upper[i]*second[i+1];
    V result;result.reserve(targets.size());
    for(double v:targets) {auto i=interval(x,v);double h=x[i+1]-x[i],a=(x[i+1]-v)/h,b=(v-x[i])/h;
        result.push_back(a*y[i]+b*y[i+1]+((a*a*a-a)*second[i]+(b*b*b-b)*second[i+1])*h*h/6);}
    return result;
}
double linear(const V& x,const V& y,double v) {
    require(std::isfinite(v) && v>=x.front() && v<=x.back(),"Setting outside stored factory calibration.");
    auto i=interval(x,v);return y[i]+(y[i+1]-y[i])*(v-x[i])/(x[i+1]-x[i]);
}
V array(const B& b,std::size_t offset,std::size_t count,bool wide,std::size_t header) {
    const std::size_t width=wide?8:4;
    require(count>=2 && count<=4096 && offset>=header && offset<=b.size() && count<=(b.size()-offset)/width,"Calibration array outside payload.");
    V result;result.reserve(count);
    for(std::size_t i=0;i<count;++i) {double v=wide?std::bit_cast<double>(wire::u64(b,offset+width*i)):static_cast<double>(std::bit_cast<float>(wire::u32(b,offset+width*i)));
        require(std::isfinite(v) && std::abs(v)<1e12,"Invalid stored calibration value.");result.push_back(v);}
    return result;
}
unsigned config(unsigned bits) {switch(bits) {case 0x50:return 0;case 0x51:return 1;case 0x71:return 2;case 0x79:return 3;default:bad("RF route has no factory gain row.");}}
}
struct FactoryCalibration::Impl {
    std::string identity,serial;
    V f,atten_f,atten_settings,if_settings,if_values,lf_f,lf_settings,lf_values;
    std::vector<V> lf_response;
    std::vector<V> gains,atten,if_response;
    std::vector<std::pair<double,double>> bands;
    using Key=std::tuple<double,double,double,double,double,double,double,std::size_t,bool,bool,unsigned>;
    std::map<Key,V> cache;
    std::size_t cached_points=0;
    std::pair<std::size_t,std::size_t> band(double center) const {
        for(auto [low,high]:bands)if(low<=center && center<high) {
            auto left=std::max<std::size_t>(1,static_cast<std::size_t>(std::lower_bound(f.begin(),f.end(),low)-f.begin()));
            auto end=static_cast<std::size_t>(std::lower_bound(f.begin(),f.end(),high)-f.begin());
            require(end>left && left<f.size(),"Empty RF calibration band.");
            return {left,std::min(f.size()-1,end-1)};
        }
        bad("No factory calibration frequency band.");
    }
};
FactoryCalibration::FactoryCalibration(const std::vector<wire::StoredBlock>& blocks,std::string serial):impl_(std::make_unique<Impl>()) {
    auto& m=*impl_;m.serial=std::move(serial);require(!m.serial.empty() && m.serial.size()<=256,"Missing calibration serial.");
    std::map<unsigned,const B*> data;
    std::ostringstream identity;identity<<"factory-fullrange-v1:"<<m.serial;
    for(const auto& block:blocks) {
        require(data.emplace(block.index,&block.payload).second,"Duplicate calibration block.");
        if(block.index>=2 && block.index<=6)identity<<':'<<block.index<<'-'<<block.payload.size()<<'-'<<std::hex<<wire::crc16(block.payload)<<std::dec;
    }
    auto get=[&](unsigned i,unsigned revision,std::size_t size)->const B& {
        require(data.contains(i),"Missing factory calibration block.");const auto& b=*data.at(i);
        require(b.size()==size && wire::u32(b,0)==revision,"Unsupported factory calibration shape/revision.");return b;
    };
    const auto& l=get(2,0,3420);
    require(wire::u32(l,4)==46 && wire::u32(l,8)==2 && wire::u32(l,12)==188,"Unsupported LF calibration grid.");
    m.lf_settings=array(l,wire::u32(l,16),46,false,36);
    m.lf_values=array(l,wire::u32(l,20),46,false,36);
    const auto ids=wire::u32(l,24);
    require(ids>=36 && ids<=l.size()-8 && wire::u32(l,ids)==6 && wire::u32(l,ids+4)==35,"Unsupported LF VGA rows.");
    m.lf_f=array(l,wire::u32(l,28),188,true,36);axis(m.lf_f,m.lf_f);
    require(m.lf_f[0]==9000 && m.lf_f[1]==10000 && m.lf_f[2]==15000 && m.lf_f.back()==45e6,"Unsupported LF response axis.");
    for(unsigned row=0;row<2;++row)m.lf_response.push_back(array(l,wire::u32(l,32)+row*188*4,188,false,36));
    const auto& i=get(3,0,272);require(wire::u32(i,4)==32,"Unsupported IF gain grid.");
    m.if_settings=array(i,wire::u32(i,8),32,false,16);m.if_values=array(i,wire::u32(i,12),32,false,16);axis(m.if_settings,m.if_values);
    auto zero=std::find(m.if_settings.begin(),m.if_settings.end(),0);require(zero!=m.if_settings.end(),"IF calibration has no zero reference.");
    double z=m.if_values[static_cast<std::size_t>(zero-m.if_settings.begin())];
    for(auto& v:m.if_values)v=static_cast<float>(v-z);
    const auto& a=get(4,0,55676);require(wire::u32(a,4)==9 && wire::u32(a,8)==1264,"Unsupported RF attenuation grid.");
    m.atten_settings=array(a,wire::u32(a,12),9,false,24);m.atten_f=array(a,wire::u32(a,16),1264,true,24);
    axis(m.atten_settings,m.atten_settings);axis(m.atten_f,m.atten_f);
    for(unsigned row=0;row<9;++row)m.atten.push_back(array(a,wire::u32(a,20)+row*1264*4,1264,false,24));
    const V baseline=m.atten[0];for(auto& row:m.atten)for(std::size_t j=0;j<row.size();++j)row[j]=static_cast<float>(row[j]-baseline[j]);
    const auto& g=get(5,1,30856);
    require(wire::u32(g,8)==12 && wire::u32(g,16)==1264 && wire::u32(g,24)==4 && wire::u32(g,28)==1264 && wire::u32(g,36)==g.size(),"Unsupported RF path grid.");
    m.f=array(g,wire::u32(g,12),1264,true,40);axis(m.f,m.f);
    auto descriptors=wire::u32(g,4);require(descriptors>=40 && descriptors<=g.size()-12*40,"Invalid RF band descriptors.");
    for(unsigned row=0;row<12;++row) {double low=std::bit_cast<double>(wire::u64(g,descriptors+row*40)),high=std::bit_cast<double>(wire::u64(g,descriptors+row*40+8));
        require(std::isfinite(low) && std::isfinite(high) && low<high && (m.bands.empty() || low==m.bands.back().second),"Invalid RF band limits.");m.bands.emplace_back(low,high);}
    for(unsigned row=0;row<4;++row)m.gains.push_back(array(g,wire::u32(g,20)+row*1264*4,1264,false,40));
    const auto& e=get(6,1,4108);require(wire::u32(e,4)==2 && wire::u32(e,8)==501,"Unsupported IF response grid.");
    auto desc=wire::u32(e,12);require(desc>=20 && desc<=e.size()-80,"Invalid IF descriptors.");
    for(unsigned row=0;row<2;++row)m.if_response.push_back(array(e,wire::u32(e,16)+row*501*4,501,false,20));
    m.identity=identity.str();
}
FactoryCalibration::~FactoryCalibration()=default;
const std::string& FactoryCalibration::id()const{return impl_->identity;}
const std::string& FactoryCalibration::serial()const{return impl_->serial;}
double FactoryCalibration::gain(double center,unsigned bits,double attenuation,double if_gain,unsigned lf_code)const {
    const auto& m=*impl_;require(std::isfinite(center) && center>=9000 && center<=6.2e9,"Factory dBm requires centers from 9 kHz to 6.2 GHz.");
    if(center<wire::lf_limit_hz) {
        require(bits==0x50 && lf_code>=1 && lf_code<=255 && lf_code!=128,"Unsupported LF gain route.");
        const double setting=static_cast<float>(20*std::log10((1+(lf_code>>7)*6.079458)*0.055744*(lf_code&127)));
        const auto found=std::find_if(m.lf_settings.begin(),m.lf_settings.end(),[&](double v){return std::abs(v-setting)<=0.001;});
        require(found!=m.lf_settings.end(),"LF VGA code has no measured factory setting.");
        const auto index=static_cast<std::size_t>(found-m.lf_settings.begin());
        const double relative=static_cast<float>(m.lf_values[index]-m.lf_values[11]);
        const auto k=static_cast<std::size_t>(std::upper_bound(m.lf_f.begin(),m.lf_f.end(),center)-m.lf_f.begin()-1);
        const auto left=k?k-1:0,end=std::min(m.lf_f.size(),k+3);
        return relative+pchip(sub(m.lf_f,left,end),sub(m.lf_response[lf_code>>7],left,end),{center})[0];
    }
    require(center>=m.f.front() && center<=m.f.back(),"RF center outside stored gain axis.");
    auto row=config(bits);auto [left,right]=m.band(center);
    auto k=static_cast<std::size_t>(std::upper_bound(m.f.begin(),m.f.end(),center)-m.f.begin()-1);
    left=std::max(left,k?k-1:0);right=std::min(right,k+2);
    double path=right<=left?m.gains[row][left]:pchip(sub(m.f,left,right+1),sub(m.gains[row],left,right+1),{center})[0];
    V values;for(const auto& a:m.atten)values.push_back(linear(m.atten_f,a,center));
    return path+linear(m.atten_settings,values,attenuation)+linear(m.if_settings,m.if_values,if_gain);
}
std::vector<double> FactoryCalibration::lf_axis()const {
    V positive;for(unsigned i=1;i<=15;++i)positive.push_back(i*1000.0);
    positive.insert(positive.end(),impl_->lf_f.begin()+3,impl_->lf_f.end());
    V result;for(auto i=positive.rbegin();i!=positive.rend();++i)result.push_back(-*i);
    result.push_back(0);result.insert(result.end(),positive.begin(),positive.end());return result;
}
std::vector<double> FactoryCalibration::eq(const wire::Tuning& t,unsigned bits,unsigned lf_code)const {
    const auto& m=*impl_;
    if(t.lf_path) {
        require(bits==0x50 && t.requested_hz>=9000 && t.requested_hz<wire::lf_limit_hz && lf_code>=1 && lf_code<=255 && lf_code!=128,"Unsupported factory LF response path.");
        const auto& g=m.lf_response[lf_code>>7];const auto grid=lf_axis();
        V near_x,near_y;
        for(std::size_t i=3;i>0;--i){near_x.push_back(-m.lf_f[i-1]);near_y.push_back(g[i-1]);}
        near_x.push_back(0);near_y.push_back(g[0]-0.5);
        for(std::size_t i=0;i<3;++i){near_x.push_back(m.lf_f[i]);near_y.push_back(g[i]);}
        V targets;for(double hz:grid)targets.push_back(std::max(std::abs(hz),m.lf_f.front()));
        auto response=pchip(m.lf_f,g,targets);
        for(std::size_t i=0;i<grid.size();++i)if(std::abs(grid[i])<=15000)response[i]=spline(near_x,near_y,{grid[i]})[0];
        // Off-grid API references distinguish this natural-spline normalization
        // from PCHIP: the latter leaves a ~1.46e-4 dB constant offset.
        const double reference=spline(grid,response,{t.requested_hz})[0];
        for(auto& value:response)value=reference-value;return response;
    }
    auto row=config(bits);
    require(!t.lf_path && t.tuned_hz>=22e6 && t.tuned_hz<=6.2e9 && (t.first_if_hz==1190e6 || t.first_if_hz==2440e6),"Unsupported factory RF response path.");
    V offset;for(unsigned j=0;j<501;++j)offset.push_back(-25e6+j*100000.0);
    auto [lo,hi]=m.band(t.requested_hz);V response;
    if(t.tuned_hz<35e6) {
        V x,y;double step=m.f[1]-m.f[0],first=m.f[0]-t.tuned_hz;
        double count_value=std::max(0.0,std::ceil((first+25e6)/step))+1;
        require(std::isfinite(count_value) && count_value<10000,"Unbounded RF response extension.");
        auto count=static_cast<std::size_t>(count_value);double start=first-count_value*step;
        for(std::size_t j=0;j<count;++j) {double v=start+static_cast<double>(j)*step;x.push_back(v);y.push_back(m.gains[row][0]+(m.gains[row][1]-m.gains[row][0])*(v-first)/step);}
        for(std::size_t j=0;j<m.f.size();++j){x.push_back(m.f[j]-t.tuned_hz);y.push_back(m.gains[row][j]);}
        response=spline(x,y,offset);
    } else if(t.tuned_hz-25e6<m.f[lo] || t.tuned_hz+25e6>m.f[hi]) {
        if(hi==m.f.size()-1)--hi;
        std::size_t near=lo;for(auto j=lo;j<=hi;++j)if(std::abs(m.f[j]-t.tuned_hz)<std::abs(m.f[near]-t.tuned_hz))near=j;
        std::size_t left=0;
        if(t.tuned_hz-25e6<m.f[lo])left=std::max(lo,near>1?near-2:0);
        else {auto end=std::min(hi,near+2);require(end>=4,"Insufficient RF fit points.");left=end-4;}
        require(left>=lo && left+4<=hi,"Insufficient band-local RF fit points.");
        double mx=0,my=0;for(auto j=left;j<left+5;++j){mx+=m.f[j];my+=m.gains[row][j];}mx/=5;my/=5;
        double num=0,den=0;for(auto j=left;j<left+5;++j){num+=(m.f[j]-mx)*(m.gains[row][j]-my);den+=(m.f[j]-mx)*(m.f[j]-mx);}
        for(double v:offset)response.push_back(num/den*v);
    } else {V targets;for(double v:offset)targets.push_back(t.tuned_hz+v);response=pchip(sub(m.f,lo,hi+1),sub(m.gains[row],lo,hi+1),targets);}
    V a=m.if_response[t.first_if_hz==2440e6?1:0];if(!t.high_side)std::reverse(a.begin(),a.end());
    V result;for(std::size_t j=0;j<501;++j)result.push_back(-(a[j]-a[250]+response[j]-response[250]));return result;
}
std::vector<double> FactoryCalibration::levels(const Spectrum& spec,const wire::NativeSettings& settings,const wire::Tuning& tune) {
    require(settings.factory_dbm,"Factory conversion requires an explicitly configured route.");settings.validate();
    auto& m=*impl_;Impl::Key key{tune.requested_hz,tune.tuned_hz,settings.attenuation_db,settings.if_gain_db,tune.first_if_hz,spec.start_hz,spec.step_hz,spec.levels_dbfs.size(),tune.high_side,tune.lf_path,wire::lf_gain_code};
    auto it=m.cache.find(key);
    if(it==m.cache.end()) {
        const unsigned bits=tune.lf_path?0x50:0x71;
        V offsets;auto correction=eq(tune,bits);V grid;
        if(tune.lf_path)grid=lf_axis();else for(unsigned i=0;i<501;++i)grid.push_back(3e6+i*100000.0);
        const double base=10+20*std::log10(32768.0/32767.0)-gain(tune.requested_hz,bits,settings.attenuation_db,settings.if_gain_db);
        V targets;for(std::size_t i=0;i<spec.levels_dbfs.size();++i) {
            double hz=spec.start_hz+static_cast<double>(i)*spec.step_hz;
            targets.push_back(tune.lf_path?hz:hz-tune.tuned_hz+wire::digital_if);
        }
        if(tune.lf_path) {offsets=spline(grid,correction,targets);for(auto& v:offsets)v+=base;}
        else for(double hz:targets)offsets.push_back(base+linear(grid,correction,hz));
        if(m.cached_points+offsets.size()>wire::max_trace_points){m.cache.clear();m.cached_points=0;}
        m.cached_points+=offsets.size();it=m.cache.emplace(key,std::move(offsets)).first;
    }
    V levels;levels.reserve(spec.levels_dbfs.size());
    for(std::size_t i=0;i<spec.levels_dbfs.size();++i){double v=spec.levels_dbfs[i]+it->second[i];require(std::isfinite(v),"Non-finite corrected spectrum.");levels.push_back(v);}return levels;
}
}
