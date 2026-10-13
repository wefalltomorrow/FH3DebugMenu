#include "RemoteCallStub.hpp"
#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    using namespace FH3External;
    assert(BuildSentinelStub()==(std::vector<std::uint8_t>{0xB8,0x36,0x33,0x48,0x46,0xC3}));
    assert(!SupportedDeveloperMode(1));
    assert(SupportedDeveloperMode(0)&&SupportedDeveloperMode(6)&&SupportedDeveloperMode(9)&&SupportedDeveloperMode(12));
    assert(BuildSetModeStub(0,1,6).empty());
    assert(BuildSetModeStub(1,0,6).empty());
    assert(BuildSetModeStub(1,1,22).empty());
    auto code=BuildSetModeStub(0x0102030405060708ULL,0x1112131415161718ULL,6);
    assert(code.size()==41);
    assert(code[0]==0x48&&code[1]==0x83&&code[2]==0xEC&&code[3]==0x28);
    assert(code[4]==0x48&&code[5]==0xB9);
    for(int i=0;i<8;++i)assert(code[6+i]==static_cast<std::uint8_t>(0x08-i));
    assert(code[14]==0xBA&&code[15]==6&&code[16]==0&&code[17]==0&&code[18]==0);
    assert(code[19]==0x48&&code[20]==0xB8);
    for(int i=0;i<8;++i)assert(code[21+i]==static_cast<std::uint8_t>(0x18-i));
    assert(code[29]==0xFF&&code[30]==0xD0);
    assert(code[31]==0xB8&&code[32]==0x36&&code[33]==0x33&&code[34]==0x48&&code[35]==0x46);
    assert(code[36]==0x48&&code[37]==0x83&&code[38]==0xC4&&code[39]==0x28&&code[40]==0xC3);
    return 0;
}
