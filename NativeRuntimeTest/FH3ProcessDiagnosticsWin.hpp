#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <processthreadsapi.h>
#include <securitybaseapi.h>
#include <appmodel.h>
#include <string>
#include <vector>
#include <cstdio>

// Read-only process policy report. Windows query failures are reported
// separately and are not interpreted as evidence of protection/anti-tamper.
inline void FH3PrintProcessDiagnostics(DWORD pid) {
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process) {
        std::wprintf(L"DIAG OpenProcess query failed: %lu\n",GetLastError());
        return;
    }
    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY sig{};
    if(GetProcessMitigationPolicy(process,ProcessSignaturePolicy,&sig,sizeof(sig))) {
        std::wprintf(L"DIAG BinarySignature: MicrosoftSignedOnly=%u StoreSignedOnly=%u MitigationOptIn=%u\n",
                     sig.MicrosoftSignedOnly,sig.StoreSignedOnly,sig.MitigationOptIn);
    } else {
        std::wprintf(L"DIAG BinarySignature query failed: %lu\n",GetLastError());
    }
    PROCESS_MITIGATION_IMAGE_LOAD_POLICY images{};
    if(GetProcessMitigationPolicy(process,ProcessImageLoadPolicy,&images,sizeof(images))) {
        std::wprintf(L"DIAG ImageLoad: NoRemoteImages=%u NoLowMandatoryLabelImages=%u PreferSystem32Images=%u\n",
                     images.NoRemoteImages,images.NoLowMandatoryLabelImages,images.PreferSystem32Images);
    } else {
        std::wprintf(L"DIAG ImageLoad query failed: %lu\n",GetLastError());
    }
    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamic{};
    if(GetProcessMitigationPolicy(process,ProcessDynamicCodePolicy,&dynamic,sizeof(dynamic))) {
        std::wprintf(L"DIAG DynamicCode: ProhibitDynamicCode=%u AllowThreadOptOut=%u AllowRemoteDowngrade=%u\n",
                     dynamic.ProhibitDynamicCode,dynamic.AllowThreadOptOut,dynamic.AllowRemoteDowngrade);
    } else {
        std::wprintf(L"DIAG DynamicCode query failed: %lu\n",GetLastError());
    }

    HANDLE token=nullptr;
    if(OpenProcessToken(process,TOKEN_QUERY,&token)) {
        DWORD container=0,returned=0;
        if(GetTokenInformation(token,TokenIsAppContainer,&container,sizeof(container),&returned)) {
            std::wprintf(L"DIAG TokenIsAppContainer=%lu\n",container);
        } else {
            std::wprintf(L"DIAG TokenIsAppContainer query failed: %lu\n",GetLastError());
        }
        CloseHandle(token);
    } else {
        std::wprintf(L"DIAG OpenProcessToken failed: %lu\n",GetLastError());
    }
    UINT32 packageLength=0;
    LONG packageStatus=GetPackageFullName(process,&packageLength,nullptr);
    if(packageStatus==ERROR_INSUFFICIENT_BUFFER && packageLength>0 && packageLength<4096) {
        std::vector<wchar_t> package(packageLength,0);
        packageStatus=GetPackageFullName(process,&packageLength,package.data());
        if(packageStatus==ERROR_SUCCESS)
            std::wprintf(L"DIAG PackageFullName=%ls\n",package.data());
        else
            std::wprintf(L"DIAG GetPackageFullName failed: %ld\n",packageStatus);
    } else if(packageStatus==APPMODEL_ERROR_NO_PACKAGE) {
        std::wprintf(L"DIAG Package identity: none\n");
    } else {
        std::wprintf(L"DIAG Package identity query failed: %ld\n",packageStatus);
    }
    CloseHandle(process);
}
#endif
