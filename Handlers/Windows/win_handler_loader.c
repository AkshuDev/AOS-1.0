#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "win_handler.h"

typedef int (*GetCurrentFileLocation)(char *, size_t);
typedef int (*RemLastSlashDir)(char *);

static HMODULE handlerLib = NULL;
static GetCurrentFileLocation gcfl = NULL;
static RemLastSlashDir rlsd = NULL;

void load_win_handler() {
    char path[512];

    GetModuleFileName(NULL, path, sizeof(path));

    char *last_slash = strrchr(path, '\\');
    if (last_slash != NULL) {
        *last_slash = '\0';  // Replace the backslash with a null terminator to get the directory path
    }

    char dll_path[512];

    snprintf(dll_path, sizeof(dll_path), "%s\\..\\Handlers\\Windows\\win_handler.dll", path);

    if (!handlerLib) {
        handlerLib = LoadLibrary(dll_path);
        if (handlerLib == NULL) {
            printf("Failed to load win_handler.dll\n");
            return;
        }
    }

    if (!gcfl) {
        gcfl = (GetCurrentFileLocation)GetProcAddress(handlerLib, "get_current_file_location");
        if (gcfl == NULL) {
            printf("Failed to find gcfltion\n");
            FreeLibrary(handlerLib);
            handlerLib = NULL;
        }
    }

    if (!rlsd){
        rlsd = (RemLastSlashDir)GetProcAddress(handlerLib, "remove_last_slash_dir");
        if (rlsd == NULL) {
            printf("Failed to find gcfltion\n");
            FreeLibrary(handlerLib);
            handlerLib = NULL;
        }
    }
}

void unload_win_handler() {
    if (handlerLib) {
        FreeLibrary(handlerLib);
        handlerLib = NULL;
        gcfl = NULL;
        rlsd = NULL;
    }
}

int get_current_file_location(char *path, size_t size) {
    load_win_handler(); // Ensure the DLL is loaded

    if (gcfl) {
        return gcfl(path, size);  // Call the gcfltion from the DLL
    } else {
        snprintf(path, size, "Error: DLL not loaded");
        return 1;
    }
}

int remove_last_slash_dir(char *path){
    load_win_handler();

    if(rlsd) {
        return rlsd(path);
    } else {
        snprintf(path, sizeof(path), "Error: DLL not loaded");
        return 1;
    }
}