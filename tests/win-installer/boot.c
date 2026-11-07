#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
static DWORD child_pid;
static HWND child_window;
static BOOL CALLBACK find_window(HWND window,LPARAM unused) {
    DWORD pid;wchar_t title[256];(void)unused;
    GetWindowThreadProcessId(window,&pid);
    if(pid!=child_pid||!IsWindowVisible(window))return TRUE;
    GetWindowTextW(window,title,256);
    if(wcsstr(title,L"ARAnyM")){child_window=window;return FALSE;}
    return TRUE;
}
int wmain(int argc,wchar_t **argv) {
    wchar_t command[2048];STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};DWORD code;
    if(argc!=3)return 2;
    swprintf(command,2048,L"\"%ls\" -c \"%ls\"",argv[1],argv[2]);si.cb=sizeof(si);
    if(!CreateProcessW(NULL,command,NULL,NULL,TRUE,0,NULL,NULL,&si,&pi))return 3;
    child_pid=pi.dwProcessId;
    if(WaitForSingleObject(pi.hProcess,45000)==WAIT_OBJECT_0){GetExitCodeProcess(pi.hProcess,&code);printf("Early ARAnyM exit %lu\n",code);return 4;}
    EnumWindows(find_window,0);
    if(child_window){
        SetForegroundWindow(child_window);Sleep(500);
        keybd_event(VK_SNAPSHOT,0x37,KEYEVENTF_EXTENDEDKEY,0);
        Sleep(100);keybd_event(VK_SNAPSHOT,0x37,KEYEVENTF_EXTENDEDKEY|KEYEVENTF_KEYUP,0);
        printf("Screenshot key sent to ARAnyM window\n");fflush(stdout);
    }
    Sleep(5000);TerminateProcess(pi.hProcess,0);WaitForSingleObject(pi.hProcess,10000);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    return child_window?0:5;
}
