#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int failures;
static void check(int ok, const char *what) {
    printf("%s %s (Windows error %lu)\n",ok ? "PASS" : "FAIL",what,GetLastError());
    fflush(stdout);
    if (!ok) failures++;
}
static int exists(const wchar_t *path) { return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES; }
static int run(const wchar_t *exe, const wchar_t *args) {
    wchar_t command[4096]; STARTUPINFOW si={0}; PROCESS_INFORMATION pi={0}; DWORD code=999;
    swprintf(command,4096,L"\"%ls\" %ls",exe,args); si.cb=sizeof(si);
    if (!CreateProcessW(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi)) return 0;
    if (WaitForSingleObject(pi.hProcess,300000)!=WAIT_OBJECT_0) { TerminateProcess(pi.hProcess,111); WaitForSingleObject(pi.hProcess,10000); }
    GetExitCodeProcess(pi.hProcess,&code); CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    printf("Process exit %lu\n",code); return code==0;
}
static void write_text(const wchar_t *path,const char *text) {
    HANDLE h=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);DWORD n=0;
    check(h!=INVALID_HANDLE_VALUE,"write test sentinel");
    if (h!=INVALID_HANDLE_VALUE) { check(WriteFile(h,text,(DWORD)strlen(text),&n,NULL)&&n==strlen(text),"sentinel content");CloseHandle(h); }
}
static int text_equal(const wchar_t *path,const char *text) {
    char data[4096];DWORD n=0;HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(h==INVALID_HANDLE_VALUE)return 0;
    int ok=ReadFile(h,data,sizeof(data),&n,NULL)&&n==strlen(text)&&!memcmp(data,text,n);CloseHandle(h);return ok;
}
static int create_link(const wchar_t *path,const wchar_t *exe,const wchar_t *args) {
    IShellLinkW *link=NULL;IPersistFile *file=NULL;int ok=0;
    if(SUCCEEDED(CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkW,(void**)&link)) &&
       SUCCEEDED(IShellLinkW_SetPath(link,exe)) && SUCCEEDED(IShellLinkW_SetArguments(link,args)) &&
       SUCCEEDED(IShellLinkW_QueryInterface(link,&IID_IPersistFile,(void**)&file)))
        ok=SUCCEEDED(IPersistFile_Save(file,path,TRUE));
    if(file)IPersistFile_Release(file);if(link)IShellLinkW_Release(link);return ok;
}
static int backups_present(const wchar_t *profile,const wchar_t *main_text_path,const char *main_text,const char *app_text) {
    wchar_t pattern[MAX_PATH],name[MAX_PATH],origin[MAX_PATH],original[MAX_PATH];WIN32_FIND_DATAW entry;int main_found=0,app_found=0;
    swprintf(pattern,MAX_PATH,L"%ls\\aranym\\backups\\*.bak",profile);
    HANDLE h=FindFirstFileW(pattern,&entry);if(h==INVALID_HANDLE_VALUE)return 0;
    do {
        swprintf(name,MAX_PATH,L"%ls\\aranym\\backups\\%ls",profile,entry.cFileName);
        swprintf(origin,MAX_PATH,L"%ls.origin",name);
        GetPrivateProfileStringW(L"Backup",L"OriginalPath",L"",original,MAX_PATH,origin);
        if(text_equal(name,main_text)&&!_wcsicmp(original,main_text_path))main_found++;
        if(text_equal(name,app_text))app_found++;
    } while(FindNextFileW(h,&entry));FindClose(h);return main_found>=1&&app_found>=2;
}
static int shortcut(const wchar_t *path,const wchar_t *expected_exe,const wchar_t *expected_args) {
    IShellLinkW *link=NULL;IPersistFile *file=NULL;wchar_t exe[MAX_PATH]={0},args[1024]={0};int ok=0;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkW,(void**)&link)) &&
        SUCCEEDED(IShellLinkW_QueryInterface(link,&IID_IPersistFile,(void**)&file)) &&
        SUCCEEDED(IPersistFile_Load(file,path,STGM_READ)) &&
        SUCCEEDED(IShellLinkW_GetPath(link,exe,MAX_PATH,NULL,SLGP_RAWPATH)) &&
        SUCCEEDED(IShellLinkW_GetArguments(link,args,1024)))
        ok=!_wcsicmp(exe,expected_exe)&&!wcscmp(args,expected_args);
    if (!ok) { wprintf(L"Shortcut expected: %ls -> %ls [%ls]\n",path,expected_exe,expected_args); wprintf(L"Shortcut actual: %ls [%ls]\n",exe,args); }
    if(file)IPersistFile_Release(file);if(link)IShellLinkW_Release(link);return ok;
}
int wmain(int argc,wchar_t **argv) {
    wchar_t profile[MAX_PATH],config[MAX_PATH],data[MAX_PATH],image[MAX_PATH],host[MAX_PATH],app[MAX_PATH],exe[MAX_PATH],uninstall[MAX_PATH],desktop[MAX_PATH],lnk[MAX_PATH],local_config[MAX_PATH],log_path[MAX_PATH],args[2048],value[1024];
    const char *custom="[GLOBAL]\r\nFastRAM=257\r\nEmuTOS=C:\\keep-rom.img\r\n[IDE0]\r\nPath=D:\\Images\\boot.img\r\n[ETH0]\r\nType=none\r\n[HOSTFS]\r\nQ=C:\\keep-host\r\n";
    HANDLE h;DWORD n;unsigned char original,changed,got;
    if(argc!=3)return 2;
    const char *application_config="[GLOBAL]\r\nFastRAM=199\r\n[IDE0]\r\nPath=D:\\local-boot.img\r\n";
    CoInitialize(NULL);
    SHGetFolderPathW(NULL,CSIDL_PROFILE,NULL,SHGFP_TYPE_CURRENT,profile);
    SHGetFolderPathW(NULL,CSIDL_DESKTOPDIRECTORY,NULL,SHGFP_TYPE_CURRENT,desktop);
    swprintf(config,MAX_PATH,L"%ls\\aranym\\config",profile);
    swprintf(data,MAX_PATH,L"%ls\\aranym\\systems\\%ls",profile,argv[2]);
    swprintf(image,MAX_PATH,L"%ls\\EasyMint\\aranym-ide-1.57G.img",data);
    swprintf(host,MAX_PATH,L"%ls\\host_fs\\KEEP.TXT",data);
    wcscpy(app,L"C:\\ARAnyM Installer QA");
    swprintf(exe,MAX_PATH,L"%ls\\aranym.exe",app);
    swprintf(uninstall,MAX_PATH,L"%ls\\uninstall-aranym.exe",app);
    check(!exists(config)&&!exists(image),"disposable profile starts without ARAnyM data");if(failures)return 1;
    CreateDirectoryW(app,NULL);swprintf(local_config,MAX_PATH,L"%ls\\aranym",app);CreateDirectoryW(local_config,NULL);
    swprintf(local_config,MAX_PATH,L"%ls\\aranym\\config",app);write_text(local_config,application_config);
    swprintf(log_path,MAX_PATH,L"%ls\\uninstall.log",app);
    write_text(log_path,"FIL C:\\ARAnyM Installer QA\\aranym\\config\r\n");
    check(run(argv[1],L"/S /D=C:\\ARAnyM Installer QA"),"fresh silent install");
    check(text_equal(local_config,application_config),"application-local config not overwritten by core files");
    check(exists(exe)&&exists(image),"emulator and ready-to-boot image installed");
    GetPrivateProfileStringW(L"GLOBAL",L"EmuTOS",L"",value,1024,config);
    check(exists(value),"main config points to installed ROM");
    GetPrivateProfileStringW(L"IDE0",L"Path",L"",value,1024,config);
    check(!_wcsicmp(value,image),"main config boots installed image");
    GetPrivateProfileStringW(L"ETH0",L"Type",L"",value,1024,config);
    check(!wcscmp(value,L"slirp"),"fresh networking defaults to SLIRP");
    swprintf(lnk,MAX_PATH,L"%ls\\ARAnyM - Separate EasyMiNT system.lnk",desktop);
    swprintf(args,2048,L"-c \"%ls\\config\"",data);
    check(shortcut(lnk,exe,args),"desktop shortcut resolves emulator and quoted config");
    swprintf(lnk,MAX_PATH,L"%ls\\ARAnyM - Updated EasyMiNT.lnk",desktop);
    check(create_link(lnk,exe,args),"simulate prior installer shortcut selecting separate config");
    swprintf(args,2048,L"-c \"%ls\"",config);
    swprintf(lnk,MAX_PATH,L"%ls\\ARAnyM (Unofficial).lnk",desktop);
    check(shortcut(lnk,exe,args),"normal shortcut explicitly selects main user config");
    write_text(config,custom);write_text(host,"Keep user data\n");
    swprintf(value,1024,L"%ls\\config",data);write_text(value,"[GLOBAL]\r\nFastRAM=173\r\n");
    h=CreateFileW(image,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    check(h!=INVALID_HANDLE_VALUE,"open disposable image to simulate guest writes");
    if(h==INVALID_HANDLE_VALUE)return 1;
    check(ReadFile(h,&original,1,&n,NULL)&&n==1,"read original test byte");changed=original^0x5a;
    SetFilePointer(h,0,NULL,FILE_BEGIN);check(WriteFile(h,&changed,1,&n,NULL)&&n==1,"simulate image changes");CloseHandle(h);
    check(run(argv[1],L"/S /D=C:\\ARAnyM Installer QA"),"repeat install");
    check(text_equal(config,custom),"existing main config preserved byte-for-byte");
    swprintf(lnk,MAX_PATH,L"%ls\\ARAnyM - Updated EasyMiNT.lnk",desktop);
    check(shortcut(lnk,exe,args),"old Updated shortcut now selects existing main config");
    check(text_equal(local_config,application_config),"application-local config preserved on upgrade");
    check(backups_present(profile,config,custom,application_config),"unique backups retain original main and application config contents");
    check(text_equal(value,"[GLOBAL]\r\nFastRAM=173\r\n"),"existing system config preserved byte-for-byte");
    check(text_equal(host,"Keep user data\n"),"user host files preserved");
    h=CreateFileW(image,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    check(h!=INVALID_HANDLE_VALUE&&ReadFile(h,&got,1,&n,NULL)&&n==1&&got==changed,"modified image preserved on repeat install");
    if(h!=INVALID_HANDLE_VALUE){SetFilePointer(h,0,NULL,FILE_BEGIN);WriteFile(h,&original,1,&n,NULL);CloseHandle(h);}
    check(run(uninstall,L"/S"),"silent uninstall");
    for(int i=0;i<100&&exists(exe);i++)Sleep(100);
    check(!exists(exe),"installed emulator removed");
    check(exists(image)&&text_equal(host,"Keep user data\n"),"image and guest files survive uninstall");
    check(text_equal(config,custom),"main configuration survives uninstall");
    check(text_equal(local_config,application_config),"old uninstall-log entry cannot remove user application config");
    CoUninitialize();printf("ARAnyM installer lifecycle %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
