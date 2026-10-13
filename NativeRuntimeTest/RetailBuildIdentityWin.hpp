#pragma once
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <string>
#pragma comment(lib,"bcrypt.lib")
namespace FH3Stage5 {
inline constexpr wchar_t kGameExe[] = L"forza_x64_release_final.exe";
inline constexpr wchar_t kExpectedSha256[] = L"7FC4821640BAF5E6F6199D0BEA19861F70550AA4A7E58C0634D7D96CDEA92C14";
inline bool VerifyRetailFile(const std::wstring& path) noexcept {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool good = false;
    do {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) break;
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) break;
        std::array<UCHAR, 65536> bytes{};
        bool ok = true;
        for (;;) {
            DWORD count = 0;
            if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)) {ok=false;break;}
            if (!count) break;
            if (BCryptHashData(hash, bytes.data(), count, 0) < 0) {ok=false;break;}
        }
        if (!ok) break;
        std::array<UCHAR, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) break;
        constexpr wchar_t hex[] = L"0123456789ABCDEF";
        wchar_t out[65]{};
        for (size_t i=0;i<digest.size();++i) {out[2*i]=hex[digest[i]>>4];out[2*i+1]=hex[digest[i]&15];}
        good = std::wstring(out) == kExpectedSha256;
    } while (false);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return good;
}
}
#endif
