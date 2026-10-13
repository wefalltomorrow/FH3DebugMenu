#include "CameraDelta.hpp"
#include <cassert>
#include <cstdint>
#include <vector>

static std::vector<std::uint8_t> Make(std::uint32_t a,std::uint32_t b,std::uint32_t c) {
    std::vector<std::uint8_t> v(12);
    const std::uint32_t vals[]={a,b,c};
    for(std::size_t i=0;i<3;++i)
        for(unsigned j=0;j<4;++j) v[i*4+j]=static_cast<std::uint8_t>(vals[i]>>(j*8));
    return v;
}
int main() {
    FH3Trace::CameraDelta delta(12);
    assert(delta.Samples()==0);
    assert(!delta.Add({0,1}));
    assert(delta.Add(Make(7,6,10)));
    assert(delta.Add(Make(7,6,11)));
    assert(delta.Add(Make(7,0,12)));
    assert(delta.Add(Make(7,0,13)));
    assert(delta.Add(Make(7,6,14)));
    assert(delta.Samples()==5);
    const auto sparse=delta.SparseChanges(2);
    assert(sparse.size()==1);
    assert(sparse[0].offset==4);
    assert(sparse[0].transitions==2);
    assert(sparse[0].distinctValues==2);
    assert(sparse[0].initial==6 && sparse[0].last==6);
    assert(delta.SparseChanges(0).empty());
    const auto all=delta.SparseChanges(100);
    assert(all.size()==2 && all[0].offset==4 && all[1].offset==8);
    FH3Trace::CameraDelta empty(0);
    assert(!empty.Add({}) && empty.Samples()==0);
    return 0;
}
