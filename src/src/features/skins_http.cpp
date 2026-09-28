#include <w1re/features/skins.hpp>
#include <windows.h>
#include <winhttp.h>
#include <stdexcept>
#pragma comment(lib, "winhttp.lib")
namespace Skins {
namespace {
struct Internet {
    HINTERNET h;
    explicit Internet(HINTERNET value):h(value) { if(!h) throw std::runtime_error("Could not open the CSFloat connection."); }
    ~Internet(){ WinHttpCloseHandle(h); }
    Internet(const Internet&)=delete;
};
}
Preset FetchListing(const std::string& input) {
    const auto id=ListingId(input);
    Internet session(WinHttpOpen(L"W1RE/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0));
    WinHttpSetTimeouts(session.h,5000,5000,5000,5000);
    Internet connection(WinHttpConnect(session.h,L"csfloat.com",INTERNET_DEFAULT_HTTPS_PORT,0));
    const auto path=L"/api/v1/listings/"+std::wstring(id.begin(),id.end());
    Internet request(WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
    std::wstring headers=L"Accept: application/json\r\n";
    wchar_t key[1024]{};
    const DWORD keyLength=GetEnvironmentVariableW(L"CSFLOAT_API_KEY",key,1024);
    if(keyLength && keyLength<1024) {
        const std::wstring token(key,keyLength);
        if(token.find_first_of(L"\r\n")!=std::wstring::npos) throw std::runtime_error("Invalid CSFloat API key format.");
        headers+=L"Authorization: "+token+L"\r\n";
    }
    if(!WinHttpSendRequest(request.h,headers.c_str(),static_cast<DWORD>(headers.size()),nullptr,0,0,0) ||
       !WinHttpReceiveResponse(request.h,nullptr)) throw std::runtime_error("CSFloat request failed. Check your connection or import listing JSON.");
    DWORD status=0, size=sizeof(status);
    if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr))
        throw std::runtime_error("Could not read the CSFloat response.");
    if(status==401 || status==403) throw std::runtime_error("CSFloat requires access authorization. Set CSFLOAT_API_KEY before launching CS2, or paste listing JSON.");
    if(status==429) throw std::runtime_error("CSFloat rate limit reached. Try again later.");
    if(status!=200) throw std::runtime_error("CSFloat returned HTTP "+std::to_string(status)+". Check that the listing exists.");
    std::string body;
    const auto deadline=GetTickCount64()+15000;
    for(;;) {
        char buffer[8192]; DWORD received=0;
        if(GetTickCount64()>deadline) throw std::runtime_error("CSFloat response timed out.");
        if(!WinHttpReadData(request.h,buffer,sizeof(buffer),&received)) throw std::runtime_error("CSFloat response was interrupted.");
        if(!received) break;
        if(body.size()+received>1024*1024) throw std::runtime_error("CSFloat response exceeds 1 MB.");
        body.append(buffer,received);
    }
    auto preset=ParseListing(body); preset.listing=id; return preset;
}
}
