#pragma once

#include <aos_inttypes.h>
#include <stddef.h>
#include <system.h>
#include <uniboot.h>

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))

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
void kdelay(uint64_t ms) __attribute__((used));
void kdelay_ns(uint64_t ms) __attribute__((used));
void kdelay_us(uint64_t us) __attribute__((used));
uint64_t kget_timestamp_seconds(void) __attribute__((used));
uint64_t kget_timestamp_ms(void) __attribute__((used));

uint64_t kcompute_checksum(const uint8_t* data, uint32_t len) __attribute__((used));

void* kmalloc(size_t size) __attribute__((used));
void klink(void* ptr1, void* ptr2) __attribute__((used));
void kfree(void* ptr) __attribute__((used));
void* kcalloc(size_t nmemb, size_t size) __attribute__((used));
void* krealloc(void* ptr, size_t new_size) __attribute__((used));

aos_bool kinit_bootinfo(uniboot_boot_info* boot_info) __attribute__((used));
uniboot_boot_info* kget_sysinfo(void) __attribute__((used));
uniboot_smmap* kget_sysmap(void) __attribute__((used));

aos_bool kc_is_alpha(char c) __attribute__((used));
aos_bool kc_is_digit(char c) __attribute__((used));
aos_bool kc_is_alphanum(char c) __attribute__((used));
aos_bool kc_is_printable(char c) __attribute__((used));
aos_bool kis_alpha(char* s) __attribute__((used));
aos_bool kis_digit(char* s) __attribute__((used));
aos_bool kis_alphanum(char* s) __attribute__((used));
aos_bool kis_printable(char* s) __attribute__((used));
aos_bool kis_float(char* s) __attribute__((used));
int kchar_to_digit(char c) __attribute__((used));
uint64_t kstr_to_u64(const char* str, int base) __attribute__((used));
int64_t kstr_to_i64(const char* str, int base) __attribute__((used));
double kstr_to_double(const char* str) __attribute__((used));
double kstr_to_double(const char* str) __attribute__((used));
char* ki64_to_str(int64_t v, char* buf, int base, aos_bool caps) __attribute__((used));
char* ku64_to_str(uint64_t v, char* buf, int base, aos_bool caps) __attribute__((used));
char* kdouble_to_str(double v, char* buf, int precision) __attribute__((used));