/* Owned native input: reuse checked window/loading handling; Smart uses RMB. */
#define main wc3_order_input_main
#include "../wc3_ui_input.c"
#undef main
int main(int argc, char **argv) {
    int smart = argc >= 5 && !lstrcmpA(argv[argc-1], "smart");
    if (argc == 3 && !lstrcmpA(argv[2], "continue")) {
        target_pid = strtoul(argv[1], NULL, 10);
        EnumWindows(find_owned_window, 0);
        if (!target_pid || matching_windows != 1) return 3;
        SetForegroundWindow(target_window);
        if (GetForegroundWindow() != target_window) return 4;
        INPUT key = {0}; key.type = INPUT_KEYBOARD; key.ki.wVk = VK_SPACE;
        if (SendInput(1, &key, sizeof(key)) != 1) return 5;
        Sleep(50); key.ki.dwFlags = KEYEVENTF_KEYUP;
        return SendInput(1, &key, sizeof(key)) == 1 ? 0 : 6;
    }
    if (!smart) return wc3_order_input_main(argc, argv);
    BOOL shift = FALSE;
    if (argc != 5 && argc != 6) return 2;
    if (argc == 6) { if (lstrcmpA(argv[4], "shift")) return 2; shift = TRUE; }
    target_pid = strtoul(argv[1], NULL, 10);
    POINT point = {strtol(argv[2], NULL, 10), strtol(argv[3], NULL, 10)};
    EnumWindows(find_owned_window, 0);
    if (!target_pid || matching_windows != 1) return 3;
    SetForegroundWindow(target_window);
    if (GetForegroundWindow() != target_window) return 4;
    if (!ClientToScreen(target_window, &point) || !SetCursorPos(point.x, point.y)) return 5;
    INPUT key = {0}; key.type = INPUT_KEYBOARD; key.ki.wVk = VK_LSHIFT;
    Sleep(300);
    if (shift && SendInput(1, &key, sizeof(key)) != 1) return 6;
    INPUT input = {0}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    if (SendInput(1, &input, sizeof(input)) != 1) return 7;
    Sleep(250); input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    if (SendInput(1, &input, sizeof(input)) != 1) return 8;
    key.ki.dwFlags = KEYEVENTF_KEYUP;
    if (shift && SendInput(1, &key, sizeof(key)) != 1) return 9;
    printf("owned PID %lu Smart RMB down/up accepted\n", (unsigned long)target_pid);
    return 0;
}
