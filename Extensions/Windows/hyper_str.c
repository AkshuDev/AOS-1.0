#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

bool inList_str(char* str, char* list[], int size_optional);
bool inList_int(int int_, int list[], int size_optional);
int inList_str_index(char* str, char* list[], int size_optional);
int inList_int_index(int int_, int list[], int size_optional);
char* convert_CHAR_BUFFER_to_CHAR_POINTER(char buffer[], int size_);

int inList_str_index(char* str, char* list[], int size_optional){
    if(size_optional == NULL){
        for (int i = 0; i < sizeof(list); i++){
            if (strcmp(str, list[i]) == 0){
                return i;
            }
        }
    } else {
        for (int i = 0; i < size_optional; i++){
            if(strcmp(str, list[i]) == 0){
                return i;
            }
        }
    }

    return -11;
}

int inList_int_index(int int_, int list[], int size_optional){
    if(size_optional == NULL){
        for (int i = 0; i < sizeof(list); i++){
            if (int_ == list[i]){
                return i;
            }
        }
    } else {
        for (int i = 0; i < size_optional; i++){
            if(int_ == list[i]){
                return i;
            }
        }
    }

    return -11;
}

bool inList_int(int int_, int list[], int size_optional){
    bool inList = false;

    if(size_optional == NULL){
        for (int i = 0; i < sizeof(list); i++){
            if (int_ == list[i]){
                inList = true;
            }
        }
    } else {
        for (int i = 0; i < size_optional; i++){
            if(int_ == list[i]){
                inList = true;
            }
        }
    }

    return inList;
}

bool inList_str(char* str, char* list[], int size_optional){
    bool inList = false;

    if(size_optional == NULL){
        for (int i = 0; i < sizeof(list); i++){
            if (strcmp(str, list[i]) == 0){
                inList = true;
            }
        }
    } else {
        for (int i = 0; i < size_optional; i++){
            if(strcmp(str, list[i]) == 0){
                inList = true;
            }
        }
    }

    return inList;
}

char* convert_CHAR_BUFFER_to_CHAR_POINTER(char buffer[], int size_){
    char* str_ = buffer;

    return str_;
}