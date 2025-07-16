#ifndef WIN_HANDLER_H
#define WIN_HANDLER_H

// Function declaration (implemented in the DLL)
int get_current_file_location(char *path, size_t size);
int remove_last_slash_dir(char* path);

#endif