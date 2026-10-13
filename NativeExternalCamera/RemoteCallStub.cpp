#include "RemoteCallStub.hpp"
#include <cstddef>

namespace FH3External {
namespace {
void Put32(std::vector<std::uint8_t>& code,std::uint32_t value) {
    for(unsigned i=0;i<4;++i) code.push_back(static_cast<std::uint8_t>(value>>(i*8)));
}
void Put64(std::vector<std::uint8_t>& code,std::uint64_t value) {
    for(unsigned i=0;i<8;++i) code.push_back(static_cast<std::uint8_t>(value>>(i*8)));
}
}
bool SupportedDeveloperMode(std::uint32_t mode) noexcept {
    return mode==0 || mode==6 || mode==9 || mode==12;
}
std::vector<std::uint8_t> BuildSentinelStub(std::uint32_t marker) {
    // mov eax, imm32; ret. No imports, no camera/game access.
    std::vector<std::uint8_t> code{0xB8};
    Put32(code,marker);
    code.push_back(0xC3);
    return code;
}
std::vector<std::uint8_t> BuildSetModeStub(std::uintptr_t cameraInterface,
                                           std::uintptr_t setter,
                                           std::uint32_t mode,
                                           std::uint32_t marker) {
    if(!cameraInterface || !setter || !SupportedDeveloperMode(mode))return {};
    // MS x64 remote thread entry: RSP is 8 mod 16. Align and reserve the
    // required 32-byte shadow space. Calls the existing ForzaMultiCam method.
    // No stolen instructions, no executable patch, no third-party DLL.
    //
    // sub rsp,0x28; mov rcx,cameraInterface; mov edx,mode;
    // mov rax,setter; call rax; mov eax,marker; add rsp,0x28; ret.
    std::vector<std::uint8_t> code{0x48,0x83,0xEC,0x28,0x48,0xB9};
    Put64(code,static_cast<std::uint64_t>(cameraInterface));
    code.push_back(0xBA); Put32(code,mode);
    code.push_back(0x48); code.push_back(0xB8);
    Put64(code,static_cast<std::uint64_t>(setter));
    code.push_back(0xFF); code.push_back(0xD0);
    code.push_back(0xB8); Put32(code,marker);
    code.insert(code.end(),{0x48,0x83,0xC4,0x28,0xC3});
    return code;
}
} // namespace FH3External
