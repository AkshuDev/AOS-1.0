#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>

#include "cmdline.h"
#include "../headers/aos.h"

#ifdef _WIN32
    #include "../Handlers/Windows/win_handler.h"
    #include "../Extentions/Windows/hyper_str.h"
#endif

char* cw_os;

#ifdef _WIN32
    char* cw_os = "_Win32";
#elif __linux__
    char* cw_os = "__linux__";
#elif __unix__
    char* cw_os = "__unix__";
#elif AOS
    char* cw_os = "AOS";
#else
    char* cw_os = UnknownOS_MACRO;
#endif

// Function prototypes
void handle_command(char* command, char* arguments);
bool file_exists(const char* path);
void execute_external_command(char* command, char* arguments);

void SETOS(char* os);

void SETOS(char *os){
    printf("Expecting AOS_HyperIO Extention installed. [hyper_str.h&c]\n");

    strlwr(os);

    int inOs = inList_str_index(os, availableOS, availableOS_MACRO_LENGTH);

    if(inOs == -11){
        printf("Unknown OS Specified!\n");
        return;
    }

    cw_os = availableOS_MACROS[inOs];

    printf("Os is now set to %s\n", cw_os);
}

int main() {
    char input[usr_input_buffer];  // Buffer for user input
    char command[command_buffer]; // Buffer for the command
    char arguments[argument_buffer]; // Buffer for arguments (assuming command part is 256 max)

    //Start
    printf(startup_msg);

    while (true) {
        // Display prompt
        char* promptBASE = base_prompt;
        char prompt[prompt_buffer];

        getcwd(prompt, sizeof(prompt));

        strcat(prompt, promptBASE);

        printf(prompt);
        fflush(stdout);

        // Get user input
        if (fgets(input, sizeof(input), stdin) == NULL) {
            // End of input, exit loop
            break;
        }

        // Remove newline at the end of input
        input[strcspn(input, "\n")] = '\0';

        // Split input into command and arguments
        sscanf(input, "%s %[^\n]", command, arguments);

        int len = strlen(command);
        for (int i = 0; i < len; i++){
            command[i] = tolower(command[i]);
        }

        // Handle the command
        handle_command(command, arguments);
    }

    return 0;
}

// This function handles the command and checks if it's built-in or external
void handle_command(char* command, char* arguments) {
    char fullcommand[command_buffer];

    snprintf(fullcommand, sizeof(fullcommand), "%s %s", command, arguments);

    // Check for built-in commands
    if (strcmp(command, "exit") == 0) {
        printf(exit_msg);
        exit(0); // Exit the program
    } else if (strcmp(command, "color") == 0) {
        if(strcmp(arguments, "") == 0){
            printf("Arguments required!\nTry using command [help].\n");
            return;
        } else {
            if (strstr(arguments, "DF") == 0){
                char nfullcommand[command_buffer];
                snprintf(nfullcommand, sizeof(nfullcommand), "%s %s", command, DEFAULT_COLOR);
                system(nfullcommand);
                return;
            } else{
                system(fullcommand);
                return;
            }
        }
    } else if (strcmp(command, "help") == 0){
        printf(HELP_MSG);
        return;
    } else if (strcmp(command, "cwd") == 0){
        char path[MAX_PATH_BUFFER];

        getcwd(path, sizeof(path));

        strcat(path, "\n");

        printf(path);
        return;
    } else if (strcmp(command, "write") == 0){
        if (strcmp(arguments, "|cd|") != 0 && strcmp(arguments, "|CD|") != 0) {
            strcat(arguments, "\n");
            printf(arguments);
            return;
        } else {
            char path[MAX_PATH_BUFFER];

            getcwd(path, sizeof(path));

            strcat(path, "\n");

            printf(path);
            return;
        }
    } else if (strcmp(command, "clear") == 0){
        if(strcmp(cw_os, "_Win32") == 0){
            system("cls");
            return;
        } else if (strcmp(cw_os, "__linux__") == 0){
            system("clear");
            return;
        } else if (strcmp(cw_os, "__unix__") == 0){
            system("clear");
            return;
        } else if (strcmp(cw_os, "AOS") == 0){
            printf("NOT YET MADE!\n");
            return;
        } else
            printf("Unknown OS!, trying default command.\n");
            system("clear");
            return;
    } else if (strcmp(command, "title") == 0){
        if(strcmp(arguments, "") == 0){
            printf("Arguments required!\nTry using command [help].\n");
            return;
        }else {
            if(strcmp(cw_os, "_WIN32") == 0){
                system(fullcommand);
                return;
            } else
                printf("Unknown command for this OS!\n");
                return;
        }
    } else if (strcmp(command, "setos") == 0){
        SETOS(arguments);
        return;
    } else if (strcmp(command, "cos") == 0){
        printf("Current OS: %s\n", cw_os);
        return;
    } else if (strcmp(command, "prompt") == 0){
        if(strcmp(arguments, "") == 0){
            printf("Arguments required!\nTry using command [help].\n");
            return;
        }else {
            if(strcmp(cw_os, "_WIN32") == 0){
                system(fullcommand);
                return;
            } else
                printf("Unknown command for this OS!\n");
                return;
        }
    } else if (strcmp(command, "|cd|") == 0){
        char path[MAX_PATH_BUFFER];

        getcwd(path, sizeof(path));

        strcat(path, "\n");

        printf(path);
        return;
    }

    // Check if an external executable exists in the CommandLine folder
    execute_external_command(command, arguments);
}

// Function to check if a file exists at the given path
bool file_exists(const char* path) {
    return access(path, F_OK) == 0;
}

// Function to execute an external command if found
void execute_external_command(char* command, char* arguments) {
    char df_file_path[MAX_PATH_BUFFER];
    bool use_q_dfP = false;
    char quoted_df_file_path[MAX_PATH_BUFFER + 3]; // Buffer to hold quoted path (2 for quotes, 1 for null terminator)

    // Get the full path of the executable
    if (get_current_file_location(df_file_path, MAX_PATH_BUFFER) != 1) {
        if (remove_last_slash_dir(df_file_path) == 1){
            printf("System Error!\nTry using command [help].\n");
        }
    } else{
        printf("System Error!\nTry using command [help].\n");
        return;
    }

    char path_to_command[MAX_PATH_BUFFER];

    if (strcmp(command, ignore_command) == 0){
        printf("Unknown command: %s\nTry using command [help].\n", command);
    }

    snprintf(path_to_command, sizeof(path_to_command), "%s\\%s.exe", df_file_path, command);

    // Check if the file exists in the CommandLine folder
    if (file_exists(path_to_command)) {
        // Build the full command with arguments
        char full_command[command_buffer];
        snprintf(full_command, sizeof(full_command), "\"%s\" %s", path_to_command, arguments);

        // Execute the external command using system()
        int result = system(full_command);
        if (result == -1) {
            printf("Error: Failed to execute %s\nTry using command [help].\n", path_to_command);
        }
    } else {
        // Command not found
        printf("Unknown command: [%s]\nTry using command [help].\n", command);
    }
}
