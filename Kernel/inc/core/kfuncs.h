#pragma once

#include <inttypes.h>
#include <stddef.h>
#include <system.h>

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

typedef volatile int spinlock_t;

void* memset(void* s, int c, size_t n) __attribute__((used));
void* memcpy(void* dest, const void* src, size_t n) __attribute__((used));
int memcmp(const void* s1, const void* s2, size_t n) __attribute__((used));

int strcmp(char* s1, char* s2) __attribute__((used));
int strncmp(char* s1, char* s2, size_t n) __attribute__((used));
size_t strlen(char* s) __attribute__((used));
char* strcpy(char* dest, char* src) __attribute__((used));
char* strncpy(char* dest, char* src, size_t n) __attribute__((used));
uint32_t str_to_uint(const char* str) __attribute__((used));

void spin_lock(spinlock_t* lock) __attribute__((used));
void spin_unlock(spinlock_t* lock) __attribute__((used));
uint64_t spin_lock_irqsave(spinlock_t* lock) __attribute__((used));
void spin_unlock_irqrestore(spinlock_t* lock, uint64_t flags) __attribute__((used));

void ktimer_calibrate(void) __attribute__((used));
uint64_t kget_ms_passed(void) __attribute__((used));
void kdelay(uint32_t ms) __attribute__((used));
uint64_t kget_timestamp_seconds(void) __attribute__((used));
uint64_t kget_timestamp_ms(void) __attribute__((used));


uint8_t kcompute_checksum(const uint8_t* data, uint32_t len) __attribute__((used));

void* kmalloc(size_t size) __attribute__((used));
void klink(void* ptr1, void* ptr2) __attribute__((used));
void kfree(void* ptr) __attribute__((used));
void* kcalloc(size_t nmemb, size_t size) __attribute__((used));
void* krealloc(void* ptr, size_t new_size) __attribute__((used));

aos_sysinfo_t* kget_sysinfo(void) __attribute__((used));