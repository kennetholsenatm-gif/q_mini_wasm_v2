#include <windows.h>

// Kernel32 functions - manually declare to avoid imports
extern HANDLE __stdcall GetStdHandle(DWORD nStdHandle);
extern BOOL __stdcall WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
extern HANDLE __stdcall CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
extern BOOL __stdcall CloseHandle(HANDLE hObject);
extern void __stdcall ExitProcess(UINT uExitCode);
extern DWORD __stdcall GetCurrentProcessId(void);

#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define GENERIC_WRITE ((DWORD)0x40000000)
#define CREATE_ALWAYS 2
#define FILE_ATTRIBUTE_NORMAL 0x00000080

void WriteString(HANDLE hFile, const char* str) {
    DWORD len = 0;
    while (str[len]) len++;
    DWORD written;
    WriteFile(hFile, str, len, &written, 0);
}

void IntToString(char* buf, DWORD val) {
    int i = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }
    char temp[16];
    int j = 0;
    while (val > 0) {
        temp[j++] = '0' + (val % 10);
        val /= 10;
    }
    while (j > 0) {
        buf[i++] = temp[--j];
    }
    buf[i] = 0;
}

void EntryPoint(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    
    WriteString(hOut, "[STANDALONE] Starting\n");
    
    HANDLE hFile = CreateFileA("C:/q_mini_data/trainer_standalone.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (hFile != (HANDLE)-1) {
        WriteString(hFile, "[STANDALONE] File created successfully\n");
        char pidBuf[32] = "PID: ";
        IntToString(pidBuf + 5, GetCurrentProcessId());
        int i = 5;
        while (pidBuf[i]) i++;
        pidBuf[i] = '\n';
        pidBuf[i+1] = 0;
        WriteString(hFile, pidBuf);
        CloseHandle(hFile);
        WriteString(hOut, "[STANDALONE] File written\n");
    } else {
        WriteString(hOut, "[STANDALONE] Failed to create file\n");
    }
    
    ExitProcess(0);
}
