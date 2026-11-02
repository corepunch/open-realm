/* Owned window only: resume loading, or toggle the public Pause key. */
#define main wc3_clock275_base_main
#include "order0110_ui_input.c"
#undef main
int main(int argc,char **argv) {
    if (argc!=3 || (lstrcmpA(argv[2],"unpause") && lstrcmpA(argv[2],"continue"))) return wc3_clock275_base_main(argc,argv);
    target_pid=strtoul(argv[1],NULL,10);
    EnumWindows(find_owned_window,0);
    if (!target_pid || matching_windows!=1) return 3;
    SetForegroundWindow(target_window);
    if (GetForegroundWindow()!=target_window) return 4;
    INPUT key={0};key.type=INPUT_KEYBOARD;key.ki.wVk=lstrcmpA(argv[2],"continue") ? VK_F10 : VK_SPACE;
    if (SendInput(1,&key,sizeof(key))!=1) return 5;
    Sleep(50);key.ki.dwFlags=KEYEVENTF_KEYUP;
    if (SendInput(1,&key,sizeof(key))!=1) return 6;
    if (lstrcmpA(argv[2],"continue")) {
        Sleep(2000);key.ki.dwFlags=0;key.ki.wVk=VK_ESCAPE;
        if (SendInput(1,&key,sizeof(key))!=1) return 7;
        Sleep(50);key.ki.dwFlags=KEYEVENTF_KEYUP;
        if (SendInput(1,&key,sizeof(key))!=1) return 8;
    }
    printf("owned PID %lu Pause accepted\n",(unsigned long)target_pid);
    return 0;
}
