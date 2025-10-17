#include <inttypes.h>
#include <asm.h>

#include <stdarg.h>

#include <inc/io.h>

// Returns n / d
static uint64_t udiv64(uint64_t n, uint64_t d) {
    uint64_t q = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        q <<= 1;
        if ((n >> i) >= d) {
            n -= d << i;
            q |= 1;
        }
    }
    return q;
}

// Returns n % d
static uint64_t umod64(uint64_t n, uint64_t d) {
    int i;
    for (i = 63; i >= 0; i--) {
        if ((n >> i) >= d) {
            n -= d << i;
        }
    }
    return n;
}

void serial_init(void) {
    asm_outb(SERIAL_PORT + 1, 0x00); // Disable interrupts
    asm_outb(SERIAL_PORT + 3, 0x80); // Enable DLAB
    asm_outb(SERIAL_PORT + 0, 0x03); // Set baud rate divisor to 3 (38400)
    asm_outb(SERIAL_PORT + 1, 0x00); 
    asm_outb(SERIAL_PORT + 3, 0x03); // 8 bits, no parity, one stop bit
    asm_outb(SERIAL_PORT + 2, 0xC7); // FIFO: enable, clear, 14-byte threshold
    asm_outb(SERIAL_PORT + 4, 0x0B); // IRQs enabled, RTS/DSR set
}

// Check if transmit buffer is empty
int serial_is_transmit_empty(void) {
    return asm_inb(SERIAL_PORT + 5) & 0x20;
}

// Print a single character
void serial_printc(char c) {
    while (!serial_is_transmit_empty());
    asm_outb(SERIAL_PORT, c);
}

// Print a null-terminated string
void serial_print(const char* str) {
    while (*str) {
        if (*str == '\n') serial_printc('\r'); // Carriage return for terminals
        serial_printc(*str++);
    }
}

static void print_integer(uint64_t val, int is_signed, int base, int uppercase) {
    char buffer[32];
    int i = 30;
    buffer[31] = 0;
    int neg = 0;

    if (is_signed && ((int64_t)val) < 0) {
        neg = 1;
        val = -(int64_t)val;
    }

    // Convert digits
    do {
        uint64_t digit = umod64(val, base);
        buffer[i--] = (digit < 10) ? '0' + digit : (uppercase ? 'A' : 'a') + digit - 10;
        val = udiv64(val, base);
    } while (val);

    if (neg) buffer[i--] = '-';
    serial_print(&buffer[i+1]);
}

// Serial print with formatting
void serial_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    serial_printc(c);
                    break;
                }
                case 's': {
                    const char* s = va_arg(args, const char*);
                    serial_print(s);
                    break;
                }
                case 'd': { // signed 32-bit
                    int n = va_arg(args, int);
                    print_integer((uint64_t)n, 1, 10, 0);
                    break;
                }
                case 'u': { // unsigned 32-bit
                    uint32_t n = va_arg(args, uint32_t);
                    print_integer(n, 0, 10, 0);
                    break;
                }
                case 'l': { // long / 64-bit
                    fmt++;
                    switch (*fmt) {
                        case 'd': { // signed 64-bit
                            int64_t n = va_arg(args, int64_t);
                            print_integer((uint64_t)n, 1, 10, 0);
                            break;
                        }
                        case 'u': { // unsigned 64-bit
                            uint64_t n = va_arg(args, uint64_t);
                            print_integer(n, 0, 10, 0);
                            break;
                        }
                        case 'x': { // unsigned 64-bit hex
                            uint64_t n = va_arg(args, uint64_t);
                            print_integer(n, 0, 16, 1);
                            break;
                        }
                        default:
                            serial_printc(*fmt);
                            break;
                    }
                    break;
                }
                case 'x': { // unsigned 32-bit hex
                    uint32_t n = va_arg(args, uint32_t);
                    print_integer(n, 0, 16, 1);
                    break;
                }
                default:
                    serial_printc(*fmt);
                    break;
            }
        } else {
            serial_printc(*fmt);
        }
        fmt++;
    }

    va_end(args);
}

