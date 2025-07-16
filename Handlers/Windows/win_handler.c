#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <windows.h>

__declspec(dllexport) int get_current_file_location(char *path, size_t size){
    //Get the file path of the running exe
    if (GetModuleFileName(NULL, path, size) == 0){
        snprintf(path, size, "Error retrieving file path");
        return 1;
    }

    // Find the last backslash to isolate the directory
    // char *lastSlash = strrchr(path, '\\');
    // if (lastSlash != NULL) {
    //     *lastSlash = '\0';  // Terminate the string after the last backslash
    // }

    return 0;
}

__declspec(dllexport) int remove_last_slash_dir(char *path){
    //Find the last backslash to isolate the directory
    char *lastSlash = strrchr(path, '\\');
    if (lastSlash != NULL) {
        *lastSlash = '\0';  // Terminate the string after the last backslash
    }

    return 0;
}