/* ORDER-02.2: reuse the owned-window order helper; add loading-screen Space and
 * hotkey-only immediate commands (S Stop, H Hold Position). Owned PID/window only. */
#define main wc3_order_input_main
#include "order0110_ui_input.c"
#undef main
static int owned_key(WORD vk) {
    EnumWindows(find_owned_window,0);
    if(!target_pid || matching_windows!=1)return 3;
    SetForegroundWindow(target_window);
    if(GetForegroundWindow()!=target_window)return 4;
    INPUT key={0};key.type=INPUT_KEYBOARD;key.ki.wVk=vk;
    if(SendInput(1,&key,sizeof(key))!=1)return 5;
    Sleep(50);key.ki.dwFlags=KEYEVENTF_KEYUP;
    if(SendInput(1,&key,sizeof(key))!=1)return 6;
    return 0;
}
int main(int argc, char **argv) {
    if(argc==3 && !lstrcmpA(argv[2],"continue")) {
        target_pid=strtoul(argv[1],NULL,10);
        int rc=owned_key(VK_SPACE);
        if(!rc)printf("owned PID %lu loading Space accepted\n",(unsigned long)target_pid);
        return rc;
    }
    if(argc==4 && !lstrcmpA(argv[2],"key") && (!lstrcmpA(argv[3],"S") || !lstrcmpA(argv[3],"H"))) {
        target_pid=strtoul(argv[1],NULL,10);
        int rc=owned_key(argv[3][0]);
        if(!rc)printf("owned PID %lu hotkey %s accepted\n",(unsigned long)target_pid,argv[3]);
        return rc;
    }
    return wc3_order_input_main(argc,argv);
}
