#pragma once

#include <stddef.h>

void* memset(void* s, int c, size_t n) __attribute__((used));
void* memcpy(void* dest, const void* src, size_t n) __attribute__((used));
int memcmp(const void* s1, const void* s2, size_t n) __attribute__((used));
int strcmp(char* s1, char* s2) __attribute__((used));
int strncmp(char* s1, char* s2, int n) __attribute__((used));
uint32_t str_to_uint(const char* str) __attribute__((used));
