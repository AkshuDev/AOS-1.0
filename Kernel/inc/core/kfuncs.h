#pragma once

#include <stddef.h>

typedef volatile int spinlock_t;

void* memset(void* s, int c, size_t n) __attribute__((used));
void* memcpy(void* dest, const void* src, size_t n) __attribute__((used));
int memcmp(const void* s1, const void* s2, size_t n) __attribute__((used));

int strcmp(char* s1, char* s2) __attribute__((used));
int strncmp(char* s1, char* s2, size_t n) __attribute__((used));
uint32_t str_to_uint(const char* str) __attribute__((used));

void spin_lock(spinlock_t* lock) __attribute__((used));
void spin_unlock(spinlock_t* lock) __attribute__((used));
uint64_t spin_lock_irqsave(spinlock_t* lock) __attribute__((used));
void spin_unlock_irqrestore(spinlock_t* lock, uint64_t flags) __attribute__((used));

void ktimer_calibrate(void) __attribute__((used));
void kdelay(uint32_t ms) __attribute__((used));
