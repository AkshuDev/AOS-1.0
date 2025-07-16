#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "../Extentions/Windows/hyper_str.h"

int main(int argc, char **argv){
    if (argc < 3){
        printf("Usage: No Commands specified <input_file>/<-all <dir>> <extention_file>\n");
    }

    printf("DONT SPECIFY EXTENTION IN INPUT FILE!\n");

    int skipI = 3;
    char *path = "";
    char* extention = "exe";
    char* ext_path = "";

    if (strcmp(argv[1], "-ext") == 0){
        skipI = 5;
        path = argv[3];
        ext_path = argv[4];
        extention = argv[2];
    } else {
        skipI = 3;
        path = argv[1];
        ext_path = argv[2];
    }


    printf("Data:\nDir/File: %s\nExtention: %s\nOutput_Extention: %s\n\nStarting Operation...\nOperating Mode: Windows\n\n", path, ext_path, extention);
    printf("REQUIREMENTS: gcc\n");
    char fullcommand[512] = "";

    sprintf(fullcommand, "gcc -o %s.%s %s.c %s", path, extention, path, ext_path);

    char* fullcommand_str = convert_CHAR_BUFFER_to_CHAR_POINTER(fullcommand, 512);

    printf("PRE COMMAND: %s\n", fullcommand_str);

    for (int i = skipI; i < argc; i++){
        sprintf(fullcommand, "%s %s", fullcommand_str, argv[i]);
        fullcommand_str = convert_CHAR_BUFFER_to_CHAR_POINTER(fullcommand, 512);
        printf("PRE-NEW COMMAND: %s\n", fullcommand_str);
    }

    printf("FULL COMMAND: %s\n", fullcommand_str);

    printf("RETURNED VALUE OF OPERATION: %d\n", system(fullcommand_str));

    printf("\n--------------------------------\n");

    return 0;
}