#pragma once
#include <inttypes.h>

// Output a single byte to a port
static inline void asm_outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__ (
        "outb %0, %1"
        :
        : "a"(value),"Nd"(port)
    );
}

// Take input of 1 byte value from port
static inline uint8_t asm_inb(uint16_t port) {
    uint8_t result;
    __asm__ __volatile__ (
        "inb %1, %0"
        : "=a"(result)
        : "dN"(port)
    );
    return result;
}

// Take input of 2 byte value from port
static inline uint16_t asm_inw(uint16_t port) {
    uint16_t ret;
    __asm__ __volatile__(
        "inw %1, %0"
        : "=a"(ret) 
        : "dN"(port)
    );
    return ret;
}

// Output a 2 byte value to port
static inline void asm_outw(uint16_t port, uint16_t val) {
    __asm__ __volatile__(
        "outw %0, %1" 
        : 
        : "a"(val),"dN"(port)
    );
}

// Take input from port
static inline void asm_insw(uint16_t port, void* buffer, uint32_t count) {
    __asm__ __volatile__(
        "rep insw" 
        : "+D"(buffer), "+c"(count) 
        : "d"(port) 
        : "memory"
    );
}

// Output buffer in port
static inline void asm_outsw(uint16_t port, const void* buffer, uint32_t count) {
    __asm__ __volatile__(
        "rep outsw" 
        : "+S"(buffer), "+c"(count) 
        : "d"(port)
    );
}

static inline void asm_outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t asm_inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
