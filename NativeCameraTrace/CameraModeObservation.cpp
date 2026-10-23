#include "CameraModeObservation.hpp"
#include <array>
#include <cstring>
#include <limits>

namespace FH3Trace {
namespace {
constexpr std::uintptr_t kMaxUser=0x00007FFFFFFFFFFFULL;
bool Range(std::uintptr_t at,std::size_t size) {
    return at>=0x10000 && size && at<=kMaxUser && size-1<=kMaxUser-at;
}
bool Pointer(std::uintptr_t at) { return Range(at,8) && (at&7)==0; }
template<class T> bool Read(const FH3Native::MemoryReader& r,std::uintptr_t at,T& v) {
    return Range(at,sizeof(v)) && r.Read(at,&v,sizeof(v));
}
struct Snapshot {
    // table begin/end/capacity, allocator/count, then five deque words.
    std::array<std::uint64_t,10> header{};
    std::array<std::uint64_t,2> selected{}, entry{};
    std::uint64_t proxyOwner=0, block=0;
    std::uint32_t mode=0;
    bool operator==(const Snapshot&) const = default;
};
ModeReadStatus Once(const FH3Native::MemoryReader& r,std::uintptr_t owner,
                    Snapshot& s,ModeObservation& out) {
    if(!Pointer(owner)||!Range(owner,0x240))return ModeReadStatus::InvalidLayout;
    if(!Read(r,owner+0x1A8,s.header)||!Read(r,owner+0x228,s.selected))
        return ModeReadStatus::ReadFailed;
    out.owner=owner;out.tableBegin=s.header[0];out.tableEnd=s.header[1];
    out.proxy=s.header[5];out.map=s.header[6];out.mapSize=s.header[7];
    out.start=s.header[8];out.count=s.header[9];
    out.handler=s.selected[0];out.reference=s.selected[1];
    // Conservative bounds for this diagnostic, not general container support.
    if(!Pointer(out.tableBegin)||out.tableEnd<out.tableBegin||
       (out.tableEnd-out.tableBegin)%0x40!=0||
       (out.tableEnd-out.tableBegin)/0x40>1024||
       !Pointer(out.proxy)||!Pointer(out.map)||out.mapSize<1||out.mapSize>4096||
       (out.mapSize&(out.mapSize-1))!=0) return ModeReadStatus::InvalidLayout;
    const auto capacity=out.mapSize*4;
    if(out.start>=capacity||out.count>capacity)return ModeReadStatus::InvalidLayout;
    if(!out.count)return ModeReadStatus::Empty;
    if(!Read(r,out.proxy,s.proxyOwner))return ModeReadStatus::ReadFailed;
    // Retail getter helper chain reduces to this container only when its proxy
    // points back to owner+0x1D0. Reject other layouts rather than guessing.
    if(s.proxyOwner!=owner+0x1D0)return ModeReadStatus::InvalidLayout;
    const auto position=out.start+out.count-1;
    const auto blockIndex=(position>>2)&(out.mapSize-1);
    if(!Range(out.map,out.mapSize*8))return ModeReadStatus::InvalidLayout;
    if(!Read(r,out.map+blockIndex*8,s.block))return ModeReadStatus::ReadFailed;
    if(!Pointer(s.block)||!Range(s.block,16))return ModeReadStatus::InvalidLayout;
    out.modeAddress=s.block+(position&3)*4;
    if(!Read(r,out.modeAddress,s.mode))return ModeReadStatus::ReadFailed;
    out.mode=s.mode;
    if(out.mode>=(out.tableEnd-out.tableBegin)/0x40)return ModeReadStatus::InvalidMode;
    const auto entry=out.tableBegin+static_cast<std::uintptr_t>(out.mode)*0x40+0x30;
    if(!Read(r,entry,s.entry))return ModeReadStatus::ReadFailed;
    out.tableHandler=s.entry[0];out.tableReference=s.entry[1];
    if(!Pointer(out.handler)||!Pointer(out.reference)||s.selected!=s.entry)
        return ModeReadStatus::HandlerMismatch;
    return ModeReadStatus::Consistent;
}
}
ModeObservation ObserveCameraMode(const FH3Native::MemoryReader& reader,
                                  std::uintptr_t owner) noexcept {
    ModeObservation first{},second{}; Snapshot a{},b{};
    first.status=Once(reader,owner,a,first);
    if(first.status!=ModeReadStatus::Consistent)return first;
    second.status=Once(reader,owner,b,second);
    if(second.status!=ModeReadStatus::Consistent) {first.status=second.status;return first;}
    if(a!=b)first.status=ModeReadStatus::Changed;
    return first;
}
const char* ModeReadStatusName(ModeReadStatus status) noexcept {
    switch(status) {
    case ModeReadStatus::Consistent:return "CONSISTENT";
    case ModeReadStatus::ReadFailed:return "READ_FAILED";
    case ModeReadStatus::InvalidLayout:return "INVALID_LAYOUT";
    case ModeReadStatus::Empty:return "EMPTY";
    case ModeReadStatus::InvalidMode:return "INVALID_MODE";
    case ModeReadStatus::HandlerMismatch:return "HANDLER_MISMATCH";
    case ModeReadStatus::Changed:return "CHANGED";
    }
    return "UNKNOWN";
}
} // namespace FH3Trace
