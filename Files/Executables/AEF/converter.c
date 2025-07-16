#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "../../../headers/aos.h"

int check_ext(char* filepath, int debugLevel);
char* convert_ext(char* filepath, int debugLevel, char new_ext[50]);
int check_sign(char* filePath, int debugLevel);
char* Read_File(char* filePath, int debugLevel, int buffer_size, bool line1, char* mode);
int ConvertMain(int argc, char **argv);

int main(int argc, char **argv){
    return ConvertMain(argc, argv);
}

int ConvertMain(int argc, char **argv){
    printf("Starting Conversion of file- %s\n", argv[1]);
    printf("Checking options\n");

    int debugLevel = 0;
    char* filePath = argv[1];
    char* format = Default_Convertion_Format;

    for (int i = 2; i < argc; i++){
        if(strstr(argv[i], "-d")){
            int i = 0;
            char *splits[3];
            char *p = strtok (argv[i], ":");

            while (p != NULL){
                splits[i++] = p;
                p = strtok(NULL, ":");
            }

            debugLevel = atoi(splits[1]);
            printf("Debug Level set to - %s \n", splits[1]);
        } else if (strstr(argv[i], "-f")){
            int i = 0;
            char *splits[3];
            char *p = strtok (argv[i], ":");

            while (p != NULL){
                splits[i++] = p;
                p = strtok(NULL, ":");
            }

            format = splits[1];
            printf("Format set to - %s \n", splits[1]);
        }
    }
    //Check Ext

    int result = check_ext(filePath, debugLevel);

    if (result == 1){
        return 1;
    }

    // Check Sign
    result = check_sign(filePath, debugLevel);
    if (result == 1){
        return 1;
    }

    return 0;
}

char* Read_File(char* filePath, int debugLevel, int buffer_size, bool line1, char* mode){
    //Read
    FILE *fptr;

    //Open in read mode
    fptr = fopen(filePath, mode);

    if (fptr == NULL) {
        printf("Error: Could not open file [%s]\n", filePath);
        return NULL;
    }

    char* data = (char*)malloc(buffer_size);

    if(!data){
        if (debugLevel >= 1){printf("Error: Could not allocate memory for data\n");}
        fclose(fptr);
        return NULL;
    }

    if (debugLevel >= 1){
        printf("Opened file in mode [READ] at [%s]. Buffer size set to [%i]\n", filePath, buffer_size);
    }

    if (!line1)
    {
        size_t bytesREAD = fread(data, 1, buffer_size, fptr);
        if (bytesREAD != buffer_size){
            if (feof(fptr)){
                if (debugLevel >= 1){
                    printf("REACHED THE END OF FILE");
                }
                if (ferror(fptr)){
                    if (debugLevel >= 1){printf("Error: Could not read file.\n");}
                }
            }
        }
    } else{
        fgets(data, buffer_size, fptr);
    }

    if (debugLevel >= 3){
        printf ("File Data -> %s\n", data);
    }

    fclose(fptr);

    return data;
}

int check_sign(char* filePath, int debugLevel){
    if (debugLevel >= 3){
        printf("RECIEVED FILEPATH - %s\n", filePath);
    }
    //Read
    char* result = Read_File(filePath, debugLevel, 1024, true, "rb");

    if (result[strlen(result) - 1] == '\n') {
        result[strlen(result) - 1] = '\0'; // Remove the trailing newline
    }

    if (debugLevel >= 4){
        printf("Required Sign -> [%s]\n", result);
    }

    if (strncmp(result, Sign, strlen(Sign)) == 0){
        if (debugLevel >= 1){
            printf("Sign Approved!\n");
        }
        return 0;
    } else {
        printf("Sign Failed! [%s]\n", result);
        return 1;
    }
}

char* convert_ext(char* filepath, int debugLevel, char new_ext[50]){
    int i = 0;
    char *splits[3];
    char *p = strtok (filepath, ".");

    while (p != NULL){
        splits[i++] = p;
        p = strtok(NULL, ".");
    }

    char* path_without_ext = splits[0];

    char* full_path = strcat(path_without_ext, new_ext);
    if (debugLevel >= 1){
        printf("New Path: %s \n", full_path);
    }
    return full_path;
}

int check_ext(char* filepath, int debugLevel){
    int i = 0;
    char *splits[3];
    char *p = strtok (filepath, ".");

    while (p != NULL){
        splits[i++] = p;
        p = strtok(NULL, ".");
    }

    char *ext = splits[1];

    if (strstr(ext, "aef")){
        if (debugLevel >= 0){
            printf("Correct extention - %s \n", ".aef");
        }
        return 0;
    } else{
        if (debugLevel >= 0){
            printf("Wrong extention - %s \n", ext[1]);
        }
        printf("Quitting... \n");
        return 1;
    }
}
