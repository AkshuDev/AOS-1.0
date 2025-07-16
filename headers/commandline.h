void setCursorPos(int x, int y);
void clearScreen_win();
void drawStatusBar(int screen_height, char *message);
void textBox(char* message, char* buffer, int line, int column);
void clearLine_win(int y);
int getConsoleHeight();
int winAti_loop(int argc, char **argv);

char* DF_StatusBar_msg = "Exit - ^X | Write Out - ^O";

