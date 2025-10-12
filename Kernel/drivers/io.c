#include <inttypes.h>
#include <asm.h>

#include <stdarg.h>

#include <inc/io.h>

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

// Serial print with formatting
void serial_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[32];

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
                case 'd': {
                    int n = va_arg(args, int);
                    int i = 30;
                    buffer[31] = 0;
                    int neg = 0;
                    if (n < 0) { n = -n; neg = 1; }
                    do { buffer[i--] = '0' + (n % 10); n /= 10; } while (n);
                    if (neg) buffer[i--] = '-';
                    serial_print(&buffer[i+1]);
                    break;
                }
                case 'x': {
                    uint32_t n = va_arg(args, uint32_t);
                    int i = 30;
                    buffer[31] = 0;
                    do {
                        int digit = n & 0xF;
                        buffer[i--] = (digit < 10) ? '0'+digit : 'A'+digit-10;
                        n >>= 4;
                    } while(n);
                    serial_print(&buffer[i+1]);
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
