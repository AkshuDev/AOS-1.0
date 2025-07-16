#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ctype.h>

#include "../headers/commandline.h"

char* Read_File(char* filePath, int debugLevel, size_t *buffer_size, bool line1, char* mode);
void print_buffer_hex_and_ascii(const char *buffer, size_t buffer_size);

int main(int argc, char **argv){
    char* filePath = "";
    bool non_ascii = true;

    if (argc < 2){
        printf("Usage: No commands given.\n");
        return 1;
    } else{
        filePath = argv[1];

        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "-write") == 0){
                // Write to file
                char command[256]; // Buffer
                sprintf(command, "%s\\win_ati.exe", argv[1]);

                #ifdef _WIN32
                    if(argc > 2){
                        printf("Expecting AOS_windows extention installed via paci (Pheonix App Community Installer)\n");
                        sprintf(command, "%s\\win_ati.exe", argv[1]);
                        system(command);
                    } else{
                        printf("Current File path not defined!\n");
                    }
                #elif __linux__
                    if(argc > 2){
                        printf("Expecting AOS_linux extention installed via paci (Pheonix App Community Installer)\n");
                        sprintf(command, "%s\\win_ati.elf", argv[1]);
                        system(command);
                    } else{
                        printf("Current File path not defined!\n");
                    }
                #elif __unix__
                    if(argc > 2){
                        printf("Expecting AOS_unix extention installed via paci (Pheonix App Community Installer)\n");
                        sprintf(command, "%s\\win_ati.elf", argv[1]);
                        system(command);
                    } else{
                        printf("Current File path not defined!\n");
                    }
                #elif AOS
                    system("win_ati");
                #else
                    printf("Expecting AOS++/AOS\n");
                    system("win_ati");
                #endif
            }

            else if (argc < 3){
                printf("Usage: No commands given.\n");
                return 1;
            } else {
                if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "-read") == 0) {
                    size_t file_size;
                    char* buffer = Read_File(filePath, 0, &file_size, false, "rb");

                    // Print output depending on whether non-ASCII or ASCII is requested
                    if (buffer != NULL) {
                        if (non_ascii) {
                            print_buffer_hex_and_ascii(buffer, file_size);
                        } else {
                            printf("%s\n", buffer);
                        }
                        free(buffer);  // Free the allocated memory
                    }
                } else if (strcmp(argv[i], "-ascii") == 0 || strcmp(argv[i], "-printable") == 0) {
                    non_ascii = false; // Disable non-ASCII printing
                }
            }
        }
    }
}

char* Read_File(char* filePath, int debugLevel, size_t *buffer_size, bool line1, char* mode){
    //Read
    FILE *fptr;

    //Open in read mode
    fptr = fopen(filePath, mode);

    if (fptr == NULL) {
        printf("Error: Could not open file [%s]\n", filePath);
        return NULL;
    }
    if (!line1)
    {
        // Seek to the end of the file to determine its size
        fseek(fptr, 0, SEEK_END);
        long size = ftell(fptr);
        if (size == -1L) {
            perror("ftell failed\n");
            fclose(fptr);
            return NULL;
        }
        *buffer_size = (size_t)size;

        // Allocate buffer to hold the entire file contents
        char *buffer = (char*)malloc(*buffer_size + 1);  // +1 for null terminator
        if (buffer == NULL) {
            perror("Memory allocation failed\n");
            fclose(fptr);
            return NULL;
        }

         // Go back to the start of the file and read it into the buffer
        rewind(fptr);
        size_t read_size = fread(buffer, 1, *buffer_size, fptr);
        if (read_size != *buffer_size) {
            perror("fread failed\n");
            free(buffer);
            fclose(fptr);
            return NULL;
        }

        buffer[*buffer_size] = '\0';

        fclose(fptr);

        return buffer;
    } else{
        fclose(fptr);
        return NULL;
    }

    fclose(fptr);

    return NULL;
}

void print_buffer_hex_and_ascii(const char *buffer, size_t buffer_size) {
    // Print the file contents both as ASCII and hexadecimal
    for (size_t i = 0; i < buffer_size; ++i) {
        // Print the hexadecimal byte representation
        printf("%02X ", (unsigned char)buffer[i]);

        // Print ASCII representation if printable, otherwise print a dot '.'
        if (isprint((unsigned char)buffer[i])) {
            printf("(%c)", buffer[i]);
        } else {
            printf("(NON-ASCII)");
        }

        // Print a new line after 16 bytes for better readability
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    printf("\n");  // Final newline after printing all data
}