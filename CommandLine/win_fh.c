#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>

#include "../headers/commandline.h"
#include "cmdline.h"
#include "../Handlers/Windows/win_handler.h"

void listFiles(char* path){
    char command[command_buffer];

    if(path == NULL){
        system("dir");
    } else {
        snprintf(command, sizeof(command), "dir %s", path);
        system(command);
    }
}

void createFile(char *CPath){
    char command[command_buffer];
    snprintf(command, sizeof(command), "\"%s\\win_ati.exe\"", CPath);
    system(command);
}

void readFile(char *path, char *CPath){
    char command[command_buffer];
    snprintf(command, sizeof(command), "\"%s\\binio.exe\" %s -ascii -r", CPath, path);
    system(command);
}

void readBinaryFile(char* path, char* CPath){
    char command[command_buffer];
    snprintf(command, sizeof(command), "\"%s\\binio.exe\" %s -r", CPath, path);
    system(command);
}

void createDir(char *path){
    if(_mkdir(path) == 0){
        printf("Directory created successfully\n");
    } else {
        perror("Error creating directory!\n");
    }
}

void removeDir(char *dirName) {
    if (_rmdir(dirName) == 0) {
        printf("Directory removed successfully.\n");
    } else {
        perror("Error removing directory");
    }
}

void removeFile(char *fileName) {
    if (remove(fileName) == 0) {
        printf("File removed successfully.\n");
    } else {
        perror("Error removing file");
    }
}

int main(int argc, char **argv){
    char CFileLocation[MAX_PATH_BUFFER];
    char *path = "";

    if(get_current_file_location(CFileLocation, sizeof(CFileLocation)) == 1){
        perror("Error getting current file location");
        return 1;
    } else {
        if (remove_last_slash_dir(CFileLocation) == 1){
            perror("Issue resolving dir!");
        }
    }

    if (argc < 2) {
        listFiles(NULL);  // No arguments, list files
    } else if (strcmp(argv[1], "-c") == 0 && argc == 2) {
        createFile(CFileLocation);  // -c flag to create file
    } else if (strcmp(argv[1], "-r") == 0 && argc == 3) {
        readFile(argv[2], CFileLocation);  // -r flag to read file
    } else if (strcmp(argv[1], "-br") == 0 && argc == 3) {
        readBinaryFile(argv[2], CFileLocation);  // -br flag to read binary file
    } else if (strcmp(argv[1], "-mkdir") == 0 && argc == 3) {
        createDir(argv[2]);  // -mkdir flag to create directory
    } else if (strcmp(argv[1], "-remdir") == 0 && argc == 3) {
        removeDir(argv[2]);  // -remdir flag to remove directory
    } else if (strcmp(argv[1], "-remf") == 0 && argc == 3) {
        removeFile(argv[2]);  // -remf flag to remove file
    } else {
        printf(fh_usage_full);
    }

    return 0;
}