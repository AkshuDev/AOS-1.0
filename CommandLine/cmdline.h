//Command Line Header File
extern int MAX_PATH_BUFFER = 512;
extern char* ignore_command = "commandline";
extern char* built_in_commands[] = {"clear", "color", "cwd", "help", "exit", "write"};
extern char* startup_msg = "AOS Command Line\n";
extern char* exit_msg = "Exiting...\n";
extern int usr_input_buffer = 1024;
extern int command_buffer = 256;
extern int argument_buffer = 768;
extern char* base_prompt = " @-AOS$ : ";
extern int prompt_buffer = 1024;
extern char* DEFAULT_COLOR = "7";
extern char* UnknownOS_MACRO = "!!ERROR!!-UnknownOS";
extern char* availableOS[4] = {"windows", "linux", "aos", "unix"};
extern int availableOS_MACRO_LENGTH = 4;
extern char* availableOS_MACROS[4] = {"_Win32", "__linux__", "AOS", "__unix__"};

extern char* HELP_MSG = "Commands ->\nhelp: Provides most used commands and their definitions.\nclear: Clears the terminal.\ncolor [INT]: Changes the color of the text.\ncwd: Provides the current working directory.\ncd: Changes the current working directory.\nwrite [text]: Prints the specified text.\nexit: Exits the CMDLINE.\nSETOS [STR]: Sets the operating system to the specified value.\n[DEFAULT_PROGRAM] [ARGUMENTS]: Runs the given program with the given arguments.\n\nVARIABLES:\n|[VAR]|: Default Variable.\n|CD|: cwd.\n";
// extern char* startups[] = {""};

extern char* fh_usage_full = "Usage: win_fh [flag] [file/dir]\nFlags:\n-c     Create file and open in win_ati\n-r [file]     Read file\n-br [file]    Read binary file using binio.exe\n-mkdir [dir]  Create directory\n-remdir [dir] Remove directory\n-remf [file]  Remove file\n";

extern int MAX_LINES_ATI = 100;
extern int MAX_VISIBLE_LINES_ATI = 20;