#pragma once

#include <aos_inttypes.h>
#include <stddef.h>
#include <system.h>
#include <uniboot.h>

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))

typedef volatile int spinlock_t;

void* memset(void* s, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
void* memmove(void* dest, const void* src, size_t n);

int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
size_t strlen(const char* s);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
uint32_t str_to_uint(const char* str);

uint64_t spin_lock_irqsave(spinlock_t* lock);
void spin_unlock_irqrestore(spinlock_t* lock, uint64_t flags);

void ktimer_calibrate(void);
uint64_t kget_ms_passed(void);
void kdelay(uint64_t ms);
void kdelay_ns(uint64_t ms);
void kdelay_us(uint64_t us);
uint64_t kget_timestamp_seconds(void);
uint64_t kget_timestamp_ms(void);

uint64_t kcompute_checksum(const uint8_t* data, uint32_t len);

void* kmalloc(size_t size);
void klink(void* ptr1, void* ptr2);
void kfree(void* ptr);
void* kcalloc(size_t nmemb, size_t size);
void* krealloc(void* ptr, size_t new_size);

aos_bool kinit_bootinfo(uniboot_boot_info* boot_info);
uniboot_boot_info* kget_sysinfo(void);
uniboot_smmap* kget_sysmap(void);

aos_bool kc_is_alpha(char c);
aos_bool kc_is_digit(char c);
aos_bool kc_is_alphanum(char c);
aos_bool kc_is_printable(char c);
aos_bool kis_alpha(char* s);
aos_bool kis_digit(char* s);
aos_bool kis_alphanum(char* s);
aos_bool kis_printable(char* s);
aos_bool kis_float(char* s);
int kchar_to_digit(char c);
uint64_t kstr_to_u64(const char* str, int base);
int64_t kstr_to_i64(const char* str, int base);
double kstr_to_double(const char* str);
double kstr_to_double(const char* str);
char* ki64_to_str(int64_t v, char* buf, size_t max_size, int base, aos_bool caps);
char* ku64_to_str(uint64_t v, char* buf, size_t max_size, int base, aos_bool caps);
char* kdouble_to_str(double v, char* buf, size_t max_size, int precision);

const char* kbeautify_memory_size(uint64_t size_in_bytes, double* out_size);

float kceil(float x);
float kround(float x);
uint64_t kmax(uint64_t a, uint64_t b);
uint64_t kmin(uint64_t a, uint64_t b);

uint32_t kget_uid(void);
uint32_t kget_gid(void);
aos_bool kpbfs_rooting_allowed(void);