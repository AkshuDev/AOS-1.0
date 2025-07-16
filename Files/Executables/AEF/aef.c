#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "../../../headers/aos.h"

void Write_Headers(char* filePath, int debugLevel);
void Append_Machine_Code(char* filePath, int debugLevel, char* bin_filePath);
long getFileSize(char* filePath, int debugLevel);
void UpdateFileSize(char* filePath, int debugLevel, long fileSize);

char get_path_separator(char *path);
char* replace_last_component(char *array, char *new_filename);

char* replace_last_component(char *array, char *new) {
    // Copy the path to a mutable string
    char *path_copy = strdup(array);
    if (path_copy == NULL) {
        return NULL; // Check for allocation failure
    }

    // Get the path separator
    char sep = get_path_separator(array);

    // Find the last occurrence of the separator
    char *last_sep = strrchr(path_copy, sep);

    if (last_sep != NULL) {
        // Move the pointer after the last separator
        *(last_sep + 1) = '\0'; // Truncate the string at the last separator
    } else {
        path_copy[0] = '\0'; // If no separator found, the path is empty
    }

    // Concatenate the new filename to the path
    strcat(path_copy, new);

    return path_copy; // Return the modified path
}

char get_path_separator(char *path) {
    if (strchr(path, '\\') != NULL) {
        return '\\';
    }
    return '/';
}

int main(int argc, char **argv){
    int debugLevel = 0;
    char* debugLevel_str = "0";

    char* aef_filePath = "";
    char* bin_filePath = "";
    char* format = Default_Convertion_Format;

    char* path2 = "";

    printf("Checking Options...\n");

    if (argc < 2){
        printf("Error: No input files provided.\n");
        return 1;
    }

    else if (argc == 2){
        if(strstr(argv[3], "-d")){
            int i = 0;
            char *splits[3];
            char *p = strtok (argv[3], ":");

            while (p != NULL){
                splits[i++] = p;
                p = strtok(NULL, ":");
            }

            debugLevel = atoi(splits[1]);
            debugLevel_str = splits[1];
            printf("Debug Level set to - %s \n", splits[1]);
        } else{
            printf("Usage: No binary/raw machine code/object file provided.\n");
            return 2;
        }
    }
    else{
        aef_filePath = argv[1];
        bin_filePath = argv[2];

        for (int i = 4; i < argc; i++){
            if(strstr(argv[i], "-d")){
                int i = 0;
                char *splits[3];
                char *p = strtok (argv[i], ":");

                while (p != NULL){
                    splits[i++] = p;
                    p = strtok(NULL, ":");
                }

                debugLevel = atoi(splits[1]);
                debugLevel_str = splits[1];
                printf("Debug Level set to - %s \n", splits[1]);
            }
        }
    }

    if(debugLevel >= 4){printf("Writing Headers!\n");}
    Write_Headers(aef_filePath, debugLevel);
    if(debugLevel >= 4){printf("Appending Machine Code!\n");}
    Append_Machine_Code(aef_filePath, debugLevel, bin_filePath);
    if(debugLevel >= 4){printf("Getting FileSize Done!\n");}
    long fileSize = getFileSize(aef_filePath, debugLevel);
    if(debugLevel >= 4){printf("Updating FileSize Done!\n");}
    UpdateFileSize(aef_filePath, debugLevel, fileSize);

    return 0;
}

long getFileSize(char* filePath, int debugLevel){
    FILE *fptr = fopen(filePath, "rb");
    if (!fptr){
        if (debugLevel >= 1){printf("Error: Cannot open file [%s]\n", filePath);}
        return -1;
    }

    fseek(fptr, 0, SEEK_END);

    if(debugLevel >= 4){printf("Seek Done!\n");}

    long fileSize = ftell(fptr);

    if(debugLevel >= 4){printf("Writing of fileSize done!\n");}

    fclose(fptr);

    if(debugLevel >= 4){printf("File Closed!\n");}

    if(debugLevel >= 4){printf("File Size -> %ld\n", fileSize);}

    return fileSize;
}

void Write_Headers(char* filePath, int debugLevel){
    FILE *fptr = fopen(filePath, "wb");
    if (!fptr){
        if(debugLevel >= 1){printf("Error opening file!\n");}
    }

    //Write
    //Sign
    fwrite(Sign_WNL, sizeof(char), Sign_Size_WNL, fptr);
    if(debugLevel >= 4){printf("Written -> [%s], Size - [%i], Size of data - [%i]\n", Sign_WNL, sizeof(char), Sign_Size_WNL);}

    //Version
    if(debugLevel >= 4){printf("Version -> [%hu]\n", Version);}
    fwrite(&Version, sizeof(unsigned short), Version_Size, fptr);
    if(debugLevel >= 4){printf("Written -> [%s], Size - [%i], Size of data - [%i]\n", &Version, sizeof(unsigned char), Version_Size);}

    //File Size (reserve space for now)
    int placeholder = 0;
    fwrite(&placeholder, sizeof(int), 1, fptr);
    if(debugLevel >= 4){printf("Written -> [%s], Size - [%i], Size of data - [1]\n", &placeholder, sizeof(int));}

    //Get machine code offset
    long machine_code_offset = ftell(fptr);
    if(debugLevel >= 4){printf("Machine code offset -> [%ld]\n", machine_code_offset);}

    //Machine Code Entry Point
    fwrite(&machine_code_offset, sizeof(int), 1, fptr);
    if(debugLevel >= 4){printf("Written -> [%s], Size - [%i], Size of data - [1]\n", &machine_code_offset, sizeof(int));}

    if (debugLevel >= 1){printf("Headers written to file!\n");}

    fclose(fptr);
}

void UpdateFileSize(char* filePath, int debugLevel, long fileSize){
    FILE *fptr = fopen(filePath, "ab");
    if(debugLevel >= 4){printf("File Opened!\n");}

    //Go to file size in headers
    fseek(fptr, strlen(Sign_WNL) + sizeof(unsigned short), SEEK_SET);
    if(debugLevel >= 4){printf("File Seek at -> [%s], Size - [%i], Mode - [SEEK_SET]\n", Sign_WNL, sizeof(unsigned short));}

    //Overwrite
    fwrite(&fileSize, sizeof(int), 1, fptr);
    if(debugLevel >= 4){printf("File Write -> [%s], Size - [%i], Size of data - [1]\n", &fileSize, sizeof(int));}

    fclose(fptr);

    if(debugLevel >= 4){printf("File Closed!\n");}
}

void Append_Machine_Code(char* aef_path, int debugLevel, char* bin_filePath){
    FILE *bin_ptr = fopen(bin_filePath, "rb");
    FILE *aef_ptr = fopen(aef_path, "ab");

    if(debugLevel >= 4){printf("Files Opened!\n");}

    if (!aef_ptr || !bin_ptr){
        if (debugLevel >= 1){printf("Error: Cannot open file.-> [%s], [%s]\n", aef_path, bin_filePath);return;}
    }else{
        //Read machine code from .bin and write it to .aef
        char buffer[1024];
        size_t bytesRead;
        while((bytesRead = fread(buffer, 1, sizeof(buffer), bin_ptr)) > 0){
            fwrite(buffer, 1, bytesRead, aef_ptr);
            if(debugLevel >= 4){printf("Write -> [%s], Size - [1], Size of data - [%i]\n", buffer, bytesRead);}
        }

        fclose(bin_ptr);
        fclose(aef_ptr);

        if (debugLevel >= 1){printf("Machine Code appended.\n");}
    }
}
