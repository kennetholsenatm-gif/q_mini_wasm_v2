#include <windows.h>

// Win32 only - no C runtime
void WriteToFile(const char* path, const char* data) {
    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, data, lstrlenA(data), &written, NULL);
        CloseHandle(hFile);
    }
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    WriteToFile("C:/q_mini_data/trainer_win32.txt", "[WIN32] Trainer running\r\n");
    
    char msg[256];
    wsprintfA(msg, "[WIN32] PID: %lu\r\n", GetCurrentProcessId());
    WriteToFile("C:/q_mini_data/trainer_win32_details.txt", msg);
    
    // Write SSE output
    WriteToFile("C:/q_mini_data/trainer_sse_output.txt", "{\"status\": \"init\", \"message\": \"Win32 trainer running\"}\r\n");
    
    return 0;
}
