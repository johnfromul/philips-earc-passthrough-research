#include "../native/iec_mat_carrier.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace tcl_iec;
int main(int argc,char** argv) {
    std::vector<uint8_t> b(kBurstBytes);
    const uint8_t hdr[]={0x72,0xf8,0x1f,0x4e,0x16,0x00,0xf0,0xef,0x9e,0x07,0x03,0x00};
    std::copy(std::begin(hdr),std::end(hdr),b.begin());
    assert(valid_mat_burst(b.data(),b.size()));
    assert(!valid_mat_burst(b.data(),b.size()-1));
    for (auto i:{0,2,4,6,8,61439}) {
        auto bad=b; bad[i]^=1;
        assert(!valid_mat_burst(bad.data(),bad.size()));
    }
    std::vector<uint8_t> input=b; input.insert(input.end(),b.begin(),b.end());
    if(argc==2) {
        std::ifstream f(argv[1],std::ios::binary);
        assert(f);
        input.assign(std::istreambuf_iterator<char>(f),{});
        assert(input.size() && input.size()%kBurstBytes==0);
    }
    for(std::size_t chunk:{1,7,8,4093,61440,100000}) {
        MatFramer framer; std::vector<uint8_t> output;
        auto emit=[&](const uint8_t* p,std::size_t n){output.insert(output.end(),p,p+n);return true;};
        for(std::size_t pos=0;pos<input.size();pos+=chunk)
            assert(framer.feed(input.data()+pos,std::min(chunk,input.size()-pos),emit));
        assert(framer.complete() && output==input);
    }
    MatFramer framer; auto reject=[](const uint8_t*,std::size_t){return false;};
    auto accept=[](const uint8_t*,std::size_t){return true;};
    assert(!framer.feed(b.data(),b.size(),reject));
    assert(!framer.feed(b.data(),b.size(),accept));
    framer.reset(); assert(framer.feed(b.data(),7,accept)); assert(!framer.complete());
    framer.reset(); assert(framer.feed(b.data(),b.size(),accept)); assert(framer.complete());
    std::cout<<"PASS: "<<input.size()/kBurstBytes<<" bursts, unchanged, all fragmentation sizes; invalid inputs rejected.\n";
}
