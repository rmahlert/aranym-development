// SPDX-License-Identifier: GPL-2.0-or-later
// Read shortcut metadata only. Never resolve or execute a shortcut target.
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <filesystem>
#include <string>
#include <vector>
namespace fs = std::filesystem;
static bool file(const std::wstring &p) {
    DWORD a=GetFileAttributesW(p.c_str());
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
}
static std::wstring full(const std::wstring &p) {
    std::vector<wchar_t> b(32768);DWORD n=GetFullPathNameW(p.c_str(),DWORD(b.size()),b.data(),nullptr);
    return n&&n<b.size()?std::wstring(b.data(),n):std::wstring();
}
static bool same(const std::wstring &a,const std::wstring &b) {
    return !a.empty()&&!b.empty()&&!_wcsicmp(a.c_str(),b.c_str());
}
struct Link {std::wstring path,config;bool matches=false,explicit_config=false,stale_default=false;};
static Link read_link(const std::wstring &path,const std::wstring &exe,const std::wstring &profile,const std::wstring &app) {
    Link result;result.path=path;if(!file(path))return result;
    IShellLinkW *link=nullptr;IPersistFile *persist=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&link));
    if(FAILED(hr))return result;
    hr=link->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&persist));
    if(SUCCEEDED(hr))hr=persist->Load(path.c_str(),STGM_READ);
    std::vector<wchar_t> target(32768),args(32768),cwd(32768);
    if(SUCCEEDED(hr))hr=link->GetPath(target.data(),int(target.size()),nullptr,SLGP_RAWPATH);
    if(SUCCEEDED(hr))hr=link->GetArguments(args.data(),int(args.size()));
    if(SUCCEEDED(hr))hr=link->GetWorkingDirectory(cwd.data(),int(cwd.size()));
    if(SUCCEEDED(hr)&&same(full(target.data()),exe)) {
        result.matches=true;int count=0;
        std::wstring command=L"aranym.exe "+std::wstring(args.data());
        LPWSTR *argv=CommandLineToArgvW(command.c_str(),&count);std::wstring config;
        if(argv) {
            for(int i=1;i<count;++i) {
                std::wstring arg=argv[i];
                if((arg==L"-c"||arg==L"--config")&&i+1<count){config=argv[++i];result.explicit_config=true;}
                else if(arg.rfind(L"--config=",0)==0){config=arg.substr(9);result.explicit_config=true;}
                else if(arg.rfind(L"-c",0)==0&&arg.size()>2){config=arg.substr(2);result.explicit_config=true;}
            }
            LocalFree(argv);
        }
        if(!config.empty()) {
            fs::path candidate(config);
            if(candidate.is_absolute())result.config=full(config);
            else if(fs::path(cwd.data()).is_absolute())result.config=full((fs::path(cwd.data())/candidate).wstring());
            // A relative path without a working directory is ambiguous.
            if(!file(result.config)) {
                result.stale_default=same(result.config,profile)||same(result.config,app);
                result.config.clear();
            }
        }
    }
    if(persist)persist->Release();
    link->Release();return result;
}
static void append_links(std::vector<std::wstring>&paths,const fs::path &dir) {
    WIN32_FIND_DATAW data{};HANDLE h=FindFirstFileW((dir/L"*.lnk").c_str(),&data);
    if(h==INVALID_HANDLE_VALUE)return;
    do {if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))paths.push_back((dir/data.cFileName).wstring());}while(FindNextFileW(h,&data));
    FindClose(h);
}
int wmain(int argc,wchar_t **argv) {
    if(argc!=6)return 2;
    const std::wstring exe=full(argv[1]),profile=full(argv[2]);
    const fs::path desktop(argv[3]),menu(argv[4]),output(argv[5]);
    const std::wstring app=full((fs::path(exe).parent_path()/L"aranym"/L"config").wstring());
    HRESULT init=CoInitialize(nullptr);if(FAILED(init))return 3;
    const std::wstring standard_desktop=(desktop/L"ARAnyM (Unofficial).lnk").wstring();
    const std::wstring standard_menu=(menu/L"ARAnyM (Unofficial).lnk").wstring();
    std::vector<std::wstring> paths{standard_desktop,standard_menu,(desktop/L"ARAnyM.lnk").wstring(),(menu/L"ARAnyM.lnk").wstring()};
    append_links(paths,desktop);append_links(paths,menu);
    std::vector<Link> links;for(const auto &p:paths)links.push_back(read_link(p,exe,profile,app));
    std::wstring config,shortcut;bool existing=file(exe);
    for(const auto &l:links)if(l.matches&&!l.config.empty()){config=l.config;shortcut=l.path;break;}
    if(config.empty()) {
        if(file(profile))config=profile;else if(file(app))config=app;
        if(!config.empty())for(const auto &l:links)if(l.matches&&!l.explicit_config){shortcut=l.path;break;}
    }
    if(!existing&&config.empty())config=profile;
    bool keep_desktop=file(standard_desktop)&&!links[0].stale_default;
    bool keep_menu=file(standard_menu)&&!links[1].stale_default;
    HANDLE h=CreateFileW(output.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){CoUninitialize();return 4;}
    const wchar_t bom=0xfeff;DWORD written=0;bool ok=WriteFile(h,&bom,2,&written,nullptr)&&written==2;CloseHandle(h);
    auto put=[&](const wchar_t *key,const std::wstring &value){ok=WritePrivateProfileStringW(L"ARAnyM",key,value.c_str(),output.c_str())&&ok;};
    put(L"Config",config);put(L"Shortcut",shortcut);put(L"Existing",existing?L"1":L"0");
    put(L"KeepDesktop",keep_desktop?L"1":L"0");put(L"KeepMenu",keep_menu?L"1":L"0");
    CoUninitialize();return ok?0:5;
}
#ifdef CONFIG_LOCATOR_GUI
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int) {
    int argc=0;LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv)return 2;
    int result=wmain(argc,argv);LocalFree(argv);return result;
}
#endif
