// Offline numerical oracle interface; never constructs a USB Session.
#include "rsa/calibration.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Supply directory containing block-2.bin through block-6.bin.");
        std::vector<rsa::wire::StoredBlock> blocks;
        for(std::uint16_t i=2;i<=6;++i) {
            auto path=std::filesystem::path(argv[1])/("block-"+std::to_string(i)+".bin");
            auto size=std::filesystem::file_size(path);
            if(size>65536)throw std::runtime_error("Oversized block.");
            rsa::wire::Bytes b(static_cast<std::size_t>(size));std::ifstream in(path,std::ios::binary);
            in.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));
            if(!in)throw std::runtime_error("Cannot read block.");blocks.push_back({i,std::move(b)});
        }
        rsa::FactoryCalibration calibration(blocks,"offline-reference");
        double requested,tuned,atten,gain,first_if;unsigned bits,high;
        std::cout<<std::setprecision(17);
        std::string line;
        while(std::getline(std::cin,line)) {
            std::istringstream input(line);unsigned lf_code=rsa::wire::lf_gain_code;
            if(!(input>>requested>>tuned>>bits>>atten>>gain>>first_if>>high))throw std::runtime_error("Invalid oracle input.");
            if(!(input>>lf_code))lf_code=rsa::wire::lf_gain_code;
            auto t=rsa::wire::tuning(requested);t.tuned_hz=tuned;t.first_if_hz=first_if;t.high_side=high!=0;
            auto g=calibration.gain(requested,bits,atten,gain,lf_code);auto eq=calibration.eq(t,bits,lf_code);
            std::cout<<"{\"volts_per_adc_count\":"<<std::pow(10,-g/20)/32767<<",\"gain_db\":"<<g<<",\"eq_amplitude_db\":[";
            for(std::size_t i=0;i<eq.size();++i){if(i)std::cout<<',';std::cout<<eq[i];}
            std::cout<<"]";
            if((bits==0x71 && requested>=22.5e6 && requested<=6199.5e6) || (bits==0x50 && requested>=509000 && requested<=21.5e6 && lf_code==rsa::wire::lf_gain_code)) {
                rsa::wire::NativeSettings s{{requested,1e6,10000,0},atten,gain,true};
                rsa::Spectrum spec{requested,1,1,{0,-6.020599913279624}};
                auto levels=calibration.levels(spec,s,t);
                std::cout<<",\"test_levels_dbm\":["<<levels[0]<<','<<levels[1]<<']';
            }
            std::cout<<"}\n";
        }
        if(std::cin.bad())throw std::runtime_error("Oracle input failed.");
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
