/* ORDER-01.10 research copy of the owned Winelib SendInput helper (runtime/payoff124/wc3_ui_input.c) adding the Attack (A) targeting key. Owned PID/window only. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static DWORD target_pid;
static HWND target_window;
static unsigned matching_windows;

static BOOL CALLBACK find_owned_window(HWND window, LPARAM unused) {
    DWORD pid=0;
    GetWindowThreadProcessId(window,&pid);
    if (pid==target_pid && IsWindowVisible(window)) {
        char title[128]={0};
        GetWindowTextA(window,title,sizeof(title));
        if (!lstrcmpA(title,"Warcraft III")) target_window=window,matching_windows++;
    }
    return TRUE;
}

int main(int argc, char **argv) {
    BOOL shift=FALSE,alt=FALSE,move=FALSE,repair=FALSE,patrol=FALSE,attack=FALSE;
    if (argc<4 || argc>7) return fprintf(stderr,"usage: wc3-ui-input pid client-x client-y [shift] [alt] [move|repair|patrol|attack]\n"),2;
    for (int i=4;i<argc;i++) {
        if (!lstrcmpA(argv[i],"shift") && !shift) shift=TRUE;
        else if (!lstrcmpA(argv[i],"alt") && !alt) alt=TRUE;
        else if (!lstrcmpA(argv[i],"move") && !move && !repair && !patrol) move=TRUE;
        else if (!lstrcmpA(argv[i],"repair") && !repair && !move && !patrol) repair=TRUE;
        else if (!lstrcmpA(argv[i],"patrol") && !patrol && !move && !repair && !attack) patrol=TRUE;
        else if (!lstrcmpA(argv[i],"attack") && !attack && !patrol && !move && !repair) attack=TRUE;
        else return fprintf(stderr,"unknown or repeated input option: %s\n",argv[i]),2;
    }
    target_pid=strtoul(argv[1],NULL,10);
    POINT point={strtol(argv[2],NULL,10),strtol(argv[3],NULL,10)};
    EnumWindows(find_owned_window,0);
    if (!target_pid || matching_windows!=1) return fprintf(stderr,"expected one visible Warcraft window for owned PID %lu; found %u\n",(unsigned long)target_pid,matching_windows),3;
    SetForegroundWindow(target_window);
    if (GetForegroundWindow()!=target_window) return fprintf(stderr,"owned window not foreground: %lu\n",(unsigned long)GetLastError()),4;
    if (!ClientToScreen(target_window,&point)) return fprintf(stderr,"owned client transform failed: %lu\n",(unsigned long)GetLastError()),7;
    if (!SetCursorPos(point.x,point.y)) return fprintf(stderr,"owned cursor positioning failed: %lu\n",(unsigned long)GetLastError()),8;
    INPUT key={0}; key.type=INPUT_KEYBOARD;
    if (move || repair || patrol || attack) {
        key.ki.wVk=repair ? 'R' : patrol ? 'P' : attack ? 'A' : 'M';
        if (SendInput(1,&key,sizeof(key))!=1) return fprintf(stderr,"owned order key down failed\n"),11;
        Sleep(50); key.ki.dwFlags=KEYEVENTF_KEYUP;
        if (SendInput(1,&key,sizeof(key))!=1) return fprintf(stderr,"owned order key up failed\n"),12;
    }
    Sleep(300);
    key.ki.wVk=VK_LSHIFT; key.ki.dwFlags=0;
    if (shift && SendInput(1,&key,sizeof(key))!=1) return fprintf(stderr,"owned Shift down failed\n"),9;
    key.ki.wVk=VK_LMENU;
    if (alt && SendInput(1,&key,sizeof(key))!=1) return fprintf(stderr,"owned Alt down failed\n"),13;
    INPUT input={0}; input.type=INPUT_MOUSE; input.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
    if (SendInput(1,&input,sizeof(input))!=1) return fprintf(stderr,"owned mouse down failed: %lu\n",(unsigned long)GetLastError()),5;
    Sleep(250); input.mi.dwFlags=MOUSEEVENTF_LEFTUP;
    if (SendInput(1,&input,sizeof(input))!=1) return fprintf(stderr,"owned mouse up failed: %lu\n",(unsigned long)GetLastError()),6;
    key.ki.dwFlags=KEYEVENTF_KEYUP;
    if (alt && SendInput(1,&key,sizeof(key))!=1) return fprintf(stderr,"owned Alt up failed\n"),14;
    key.ki.wVk=VK_LSHIFT;
    if (shift && SendInput(1,&key,sizeof(key))!=1) return fprintf(stderr,"owned Shift up failed\n"),10;
    printf("owned PID %lu window %p screen %ld,%ld input-size %u: down/up accepted\n",(unsigned long)target_pid,target_window,(long)point.x,(long)point.y,(unsigned)sizeof(input));
    return 0;
}
