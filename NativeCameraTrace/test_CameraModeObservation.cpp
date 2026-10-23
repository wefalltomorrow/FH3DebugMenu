#include "CameraModeObservation.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <functional>
#include <vector>
using namespace FH3Trace;
namespace {
constexpr std::uintptr_t owner=0x11000,proxy=0x12000,map=0x13000,table=0x14000;
struct Memory:FH3Native::MemoryReader {
    mutable std::vector<unsigned char> data=std::vector<unsigned char>(0x20000);
    mutable unsigned reads=0;
    unsigned failAt=0,mutateAt=0;
    std::function<void()> mutation;
    template<class T> void Put(std::uintptr_t at,T value) {
        assert(at>=0x10000 && at-0x10000+sizeof(value)<=data.size());
        std::memcpy(data.data()+at-0x10000,&value,sizeof(value));
    }
    void Q(std::uintptr_t at,std::uint64_t v) {Put(at,v);}
    bool Read(std::uintptr_t at,void* dest,std::size_t bytes)const noexcept override {
        ++reads;
        if(reads==mutateAt)mutation();
        if(reads==failAt||at<0x10000||at-0x10000>data.size()||
           bytes>data.size()-(at-0x10000))return false;
        std::memcpy(dest,data.data()+at-0x10000,bytes);return true;
    }
    void Select(std::uint32_t mode) {
        Q(owner+0x228,0x20000+mode*0x100);
        Q(owner+0x230,0x21000+mode*0x100);
    }
    Memory() {
        Q(owner+0x1A8,table);Q(owner+0x1B0,table+22*0x40);
        Q(owner+0x1D0,proxy);Q(proxy,owner+0x1D0);
        Q(owner+0x1D8,map);Q(owner+0x1E0,8);
        Q(owner+0x1E8,0);Q(owner+0x1F0,1);
        for(unsigned block=0;block<8;++block) {
            Q(map+8*block,0x16000+block*0x100);
            for(unsigned slot=0;slot<4;++slot)
                Put<std::uint32_t>(0x16000+block*0x100+slot*4,(block*4+slot)%22);
        }
        for(unsigned mode=0;mode<22;++mode) {
            Q(table+mode*0x40+0x30,0x20000+mode*0x100);
            Q(table+mode*0x40+0x38,0x21000+mode*0x100);
        }
        Select(0);
    }
};
void Expect(Memory& m,ModeReadStatus status) {
    assert(ObserveCameraMode(m,owner).status==status);
}
}
int main() {
    // End element across every block, element slot and ring wrap.
    for(unsigned start=0;start<32;++start)for(unsigned count=1;count<=32;++count) {
        Memory m;const auto index=(start+count-1)%32;
        m.Q(owner+0x1E8,start);m.Q(owner+0x1F0,count);m.Select(index%22);
        const auto result=ObserveCameraMode(m,owner);
        assert(result.status==ModeReadStatus::Consistent && result.mode==index%22);
        assert(result.modeAddress==0x16000+(index/4)*0x100+(index%4)*4);
        assert(m.reads==12);
    }
    for(unsigned fail=1;fail<=12;++fail) {Memory m;m.failAt=fail;Expect(m,ModeReadStatus::ReadFailed);}
    {Memory m;m.Q(owner+0x1F0,0);Expect(m,ModeReadStatus::Empty);}
    for(auto size:{0ULL,3ULL,4097ULL,~0ULL}) {Memory m;m.Q(owner+0x1E0,size);Expect(m,ModeReadStatus::InvalidLayout);}
    {Memory m;m.Q(owner+0x1E8,~0ULL);Expect(m,ModeReadStatus::InvalidLayout);}
    {Memory m;m.Q(owner+0x1F0,33);Expect(m,ModeReadStatus::InvalidLayout);}
    {Memory m;m.Q(proxy,owner);Expect(m,ModeReadStatus::InvalidLayout);}
    {Memory m;m.Q(map,0);Expect(m,ModeReadStatus::InvalidLayout);}
    {Memory m;m.Q(map,0x7FFFFFFFFFF8ULL);Expect(m,ModeReadStatus::InvalidLayout);}
    {Memory m;m.Q(owner+0x1B0,table+22*0x40+1);Expect(m,ModeReadStatus::InvalidLayout);}
    {Memory m;m.Q(owner+0x1B0,table+1025*0x40);Expect(m,ModeReadStatus::InvalidLayout);}
    for(auto mode:{22U,0xFFFFFFFFU}) {Memory m;m.Put<std::uint32_t>(0x16000,mode);Expect(m,ModeReadStatus::InvalidMode);}
    {Memory m;m.Select(1);Expect(m,ModeReadStatus::HandlerMismatch);}
    {Memory m;m.Q(owner+0x230,0);Expect(m,ModeReadStatus::HandlerMismatch);}
    // A fully coherent change between the two passes must not be accepted.
    {Memory m;m.mutateAt=7;m.mutation=[&]{m.Put<std::uint32_t>(0x16000,1);m.Select(1);};
        Expect(m,ModeReadStatus::Changed);}
    // A partially completed selection is also rejected.
    {Memory m;m.mutateAt=7;m.mutation=[&]{m.Select(1);};Expect(m,ModeReadStatus::HandlerMismatch);}
    // Even unchanged mode/handler values cannot hide a moved storage block.
    {Memory m;m.mutateAt=7;m.mutation=[&]{m.Q(map,0x18000);m.Put<std::uint32_t>(0x18000,0);};
        Expect(m,ModeReadStatus::Changed);}
    {Memory m;assert(ObserveCameraMode(m,0).status==ModeReadStatus::InvalidLayout);assert(m.reads==0);}
    return 0;
}
