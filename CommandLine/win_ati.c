#include <stdio.h>
#include <conio.h>
#include <windows.h>
#include <stdlib.h>

#include "../headers/commandline.h"
#include "cmdline.h"

void setCursorPos(int x, int y);
void clearScreen_win();
void drawStatusBar(int screen_height, char *message);
void textBox(char* message, char* buffer, int line, int column);
void clearLine_win(int y);
int getConsoleHeight();
int winAti_loop(int argc, char **argv);
void scroll_up(int start_line, int total_lines, char lines[MAX_VISIBLE_LINES_ATI][80]);
void scroll_down(int start_line, int total_lines, char lines[MAX_VISIBLE_LINES_ATI][80]);
void draw_text_area(int start_line, int total_lines, char lines[MAX_VISIBLE_LINES_ATI][80]);
void enableMouseInput();
void handleMouseInput(MOUSE_EVENT_RECORD mouseEvent, int start_line, int total_lines, char lines[MAX_VISIBLE_LINES_ATI][80]);

int getConsoleHeight(){
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int height;

    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else {
        height = 24;  // Default height if we can't retrieve the info
    }
    return height;
}

// Function to set the cursor at specific x, y position
void setCursorPos(int x, int y) {
    COORD coord = {x, y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

// Function to clear the screen
void clearScreen_win() {
    system("cls");
}

// Function to clear a specific line (for status bar updates)
void clearLine_win(int y) {
    setCursorPos(0, y);
    printf("%-80s", "");  // Clear the line by printing spaces
    setCursorPos(0, y);   // Return the cursor to the beginning of the cleared line
}

// Function to draw status bar
void drawStatusBar(int screen_height, char *message) { // Parameter screen_height to -11 (use default)
    int screen_height_new = getConsoleHeight();

    if (screen_height != -11){
        screen_height_new = screen_height;
    }

    setCursorPos(0, screen_height_new - 1);
    printf("%-79s", message);
}

void textBox(char *message, char* buffer, int line, int column){
    drawStatusBar(-11, message);
    setCursorPos(line, column);
    gets(buffer);
}

// Function to get file mode (bin or normal)
void getFileMode(char* mode) {
    int height = getConsoleHeight();

    drawStatusBar(-11, "Enter file mode (bin or normal): ");
    setCursorPos(34, height - 1); // Position cursor for input
    gets(mode);

    while (1) {
        if (strcmp(mode, "bin") == 0 || strcmp(mode, "normal") == 0) {
            break; // Valid mode entered
        } else {
            clearLine_win(height - 1); // Clear the line
            drawStatusBar(-11, "Invalid mode. Please enter 'bin' or 'normal': ");
            setCursorPos(48, height - 1);
            gets(mode);
        }
    }

    clearLine_win(height - 1); // The input
}

// Function to get file name input from user
void getFileName(char* filename) {
    int height = getConsoleHeight();

    drawStatusBar(-11, "Enter file name: ");
    setCursorPos(18, height - 1); // Position cursor for input below the main text area
    gets(filename);  // Get file name

    clearLine_win(height - 1); // drawStatusBar does height - 1 automatically
}

// Function to save the content in buffer to the file
void saveToFile(char* filename, char* mode, char buffer[100][80], int maxLine) {
    FILE *file;

    if (strcmp(mode, "bin") == 0) {
        file = fopen(filename, "wb"); // Open in binary mode
    } else {
        file = fopen(filename, "w"); // Open in text mode
    }

    if (file == NULL) {
        drawStatusBar(-11, "Error opening file.");
        return;
    }

    for (int i = 0; i <= maxLine; i++) {
        if (strcmp(mode, "bin") == 0) {
            fwrite(buffer[i], sizeof(char), strlen(buffer[i]), file);
        } else {
            fprintf(file, "%s\n", buffer[i]);  // Save in normal text mode
        }
    }

    fclose(file);
}

void scroll_up(int start_line, int total_lines, char lines[MAX_VISIBLE_LINES_ATI][80]){
    if (start_line > 0){
        start_line --;
        //Redraw
        draw_text_area(start_line, total_lines, lines);
    }
}

void scroll_down(int start_line, int total_lines, char lines[MAX_VISIBLE_LINES_ATI][80]){
    if (start_line + MAX_VISIBLE_LINES_ATI < total_lines){
        start_line++;
        //Redraw
        draw_text_area(start_line, total_lines, lines);
    }
}

void draw_text_area(int start_line, int total_lines, char lines[MAX_VISIBLE_LINES_ATI][80]){
    // Clear the text area and redraw from start_line
    for (int i = 0; i < MAX_VISIBLE_LINES_ATI && (start_line + i) < total_lines; i++) {
        printf("%s\n", lines[start_line + i]); // Display lines from the buffer
    }
}

void enableMouseInput() {
    HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hConsole, ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
}

// Function to handle mouse input
void handleMouseInput(MOUSE_EVENT_RECORD mouseEvent, int start_line, int total_lines, char lines[MAX_VISIBLE_LINES_ATI][80]) {
    if (mouseEvent.dwButtonState == FROM_LEFT_1ST_BUTTON_PRESSED) {
        // Mouse click (if needed)
    } else if (mouseEvent.dwButtonState == RIGHTMOST_BUTTON_PRESSED) {
        // Handle right-click actions if needed
    }

    if (mouseEvent.dwEventFlags == MOUSE_WHEELED) {
        if (mouseEvent.dwButtonState == WHEEL_DELTA) {
            scroll_up(start_line, total_lines, lines);
        } else if (mouseEvent.dwButtonState == -WHEEL_DELTA) {
            scroll_down(start_line, total_lines, lines);
        }
    }
}

// Main loop to handle input and screen drawing
int winAti_loop(int argc, char **argv) {
    int height = getConsoleHeight();
    char buffer[MAX_LINES_ATI][80]; // Buffer to store text (100 lines, 80 chars per line)
    memset(buffer, 0, sizeof(buffer)); // Initialize it with zeros
    int start_line = 0;
    int total_lines = 0;
    int line = 0, column = 0;   // Track current line and column

    clearScreen_win();

    // Draw status bar
    drawStatusBar(-11, DF_StatusBar_msg);

    //enableMouseInput();

    while (1) {
        setCursorPos(column, line);  // Set the cursor to where the user is typing

        // Check for mouse input
        // HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);
        // DWORD mode = 0;
        // GetConsoleMode(hConsole, &mode);
        // SetConsoleMode(hConsole, ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);

        // INPUT_RECORD inputRecord;
        // DWORD events;
        // if (ReadConsoleInput(hConsole, &inputRecord, 1, &events) > 0) {
        //     if (inputRecord.EventType == MOUSE_EVENT) {
        //         handleMouseInput(inputRecord.Event.MouseEvent, start_line, total_lines, buffer);
        //         continue; // Skip the rest of the loop to process mouse input
        //     }
        // }

        if (_kbhit()) {  // Check if a key was pressed
            char ch = _getch();  // Get the character

            if (ch == 24) {  // Ctrl + X to exit
                break;
            } else if (ch == 15){ // Ctrl + O to write
                char mode[50];
                char fileName[150];
                clearLine_win(height - 1);
                getFileName(fileName);
                getFileMode(mode);
                saveToFile(fileName, mode, buffer, line);
                clearScreen_win();
                drawStatusBar(-11, DF_StatusBar_msg);
            } else if (ch == 8) {  // Backspace key
                if (column > 0) {
                    column--;  // Move cursor back
                    buffer[line][column] = '\0';  // Remove the character from buffer
                    printf("\b \b");  // Go back, print space, go back again (visual backspace)
                } else {
                    memset(buffer[line], 0, sizeof(line));
                    column = 0;
                    line--;
                    setCursorPos(column, line);
                    if (total_lines > MAX_VISIBLE_LINES_ATI){
                        scroll_up(start_line, total_lines, buffer);
                    }
                    total_lines--;
                }

            // Handle arrow keys
            } else if(ch == '\033'){
                getch();
                switch(getch()){  // Get the second character of arrow key sequence
                    case 'D':
                        if (column > 0) {  // Left arrow
                            column--;
                            setCursorPos(column, line);
                        }
                    case 'C':
                        if (column < 79) { // Right arrow
                            column++;
                            setCursorPos(column, line);
                        }
                    case 'A':
                        if (line > 0) {    // Up arrow
                            line--;
                            setCursorPos(column, line);
                        }
                    case 'B':
                        if (line < 99) {   // Down arrow
                            line++;
                            setCursorPos(column, line);
                        }
                }
            } else if (ch == 13) {  // Enter key
                line++; column = 0;
                total_lines++;

                if(total_lines > MAX_VISIBLE_LINES_ATI){
                    scroll_down(start_line, total_lines, buffer);
                }
            } else {
                buffer[line][column++] = ch;
                printf("%c", ch);
            }
        }


    }

    clearScreen_win();
    printf("Exiting editor...\n");

    return 0;
}

int main(int argc, char **argv){
    winAti_loop(argc, argv);
    return 0;
}