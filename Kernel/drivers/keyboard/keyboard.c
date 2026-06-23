#include <aos_inttypes.h>

#include <inc/core/kfuncs.h>
#include <inc/drivers/io/io.h>
#include <inc/drivers/keyboard/keyboard.h>

#define isascii(c) ((c) >= 0 && (c) < 128)

struct keyboard_state {
    aos_bool lshift;
    aos_bool rshift;
    aos_bool capslock;
};

static spinlock_t keyboard_lock = 0;
static struct keyboard_state cur_state = {0};
#define SHIFT_DOWN (cur_state.lshift || cur_state.rshift)

static const char keymap[128] = {
    [0x01] = 27, // ESC

    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',

    [0x0C] = '-',
    [0x0D] = '=',

    [0x0E] = '\b',
    [0x0F] = '\t',

    [0x10] = 'q',
    [0x11] = 'w',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'y',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',

    [0x1A] = '[',
    [0x1B] = ']',

    [0x1C] = '\n',

    [0x1E] = 'a',
    [0x1F] = 's',
    [0x20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',

    [0x27] = ';',
    [0x28] = '\'',
    [0x29] = '`',

    [0x2B] = '\\',

    [0x2C] = 'z',
    [0x2D] = 'x',
    [0x2E] = 'c',
    [0x2F] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = 'm',

    [0x33] = ',',
    [0x34] = '.',
    [0x35] = '/',

    [0x39] = ' ',

	// Keypad/Numpad
    [0x47] = '7',
    [0x48] = '8',
    [0x49] = '9',

    [0x4B] = '4',
    [0x4C] = '5',
    [0x4D] = '6',

    [0x4F] = '1',
    [0x50] = '2',
    [0x51] = '3',

    [0x52] = '0',
    [0x53] = '.',

    [0x37] = '*',
    [0x4A] = '-',
    [0x4E] = '+'
};

static const char keymap_shift[128] = {
    [0x02] = '!',
    [0x03] = '@',
    [0x04] = '#',
    [0x05] = '$',
    [0x06] = '%',
    [0x07] = '^',
    [0x08] = '&',
    [0x09] = '*',
    [0x0A] = '(',
    [0x0B] = ')',

    [0x0C] = '_',
    [0x0D] = '+',

    [0x1A] = '{',
    [0x1B] = '}',

    [0x27] = ':',
    [0x28] = '"',
    [0x29] = '~',

    [0x2B] = '|',

    [0x33] = '<',
    [0x34] = '>',
    [0x35] = '?',

    // KeyPad/NumPad
    [0x37] = '*',
    [0x4A] = '-',
    [0x4E] = '+'
};

static void update_state(uint16_t sc) {
    switch (sc) {
        case 0x2A:
            cur_state.lshift = AOS_TRUE;
            break;
        case 0x36:
            cur_state.rshift = AOS_TRUE;
            break;
        case 0xAA:
            cur_state.lshift = AOS_FALSE;
            break;
        case 0xB6:
            cur_state.rshift = AOS_FALSE;
            break;
        case 0x3A:
            cur_state.capslock = !cur_state.capslock;
            break;
        default: break;
    }
}

static aos_bool is_letter(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

char keyboard_ps2_get_char(void) {
    uint64_t rflags = spin_lock_irqsave(&keyboard_lock);
    int16_t sc_raw = ps2_read_scan();
    if (sc_raw < 0) {spin_unlock_irqrestore(&keyboard_lock, rflags); return 0;}
    uint16_t sc = (uint16_t)sc_raw;
    update_state(sc);

    if (sc & 0x80) {spin_unlock_irqrestore(&keyboard_lock, rflags); return 0;}

	if (!isascii(sc)) {
		spin_unlock_irqrestore(&keyboard_lock, rflags);
    	return (char)sc;
	}

	char ch = keymap[sc];

	if (is_letter(ch)) {
		if (SHIFT_DOWN ^ cur_state.capslock)
			ch -= 32;
	} else if (SHIFT_DOWN && keymap_shift[sc]) {
		ch = keymap_shift[sc];
	}

    spin_unlock_irqrestore(&keyboard_lock, rflags);
    return ch;
}

char keyboard_ps2_try_get_char(void) {
    uint64_t rflags = spin_lock_irqsave(&keyboard_lock);

    int16_t sc_raw = ps2_try_read_scan();
    if (sc_raw < 0) {spin_unlock_irqrestore(&keyboard_lock, rflags); return 0;}
    uint16_t sc = (uint16_t)sc_raw;
    update_state(sc);

    if (sc & 0x80) {spin_unlock_irqrestore(&keyboard_lock, rflags); return 0;}

	if (!isascii(sc)) {
		spin_unlock_irqrestore(&keyboard_lock, rflags);
    	return (char)sc;
	}

    char ch = keymap[sc];

	if (is_letter(ch)) {
		if (SHIFT_DOWN ^ cur_state.capslock)
			ch -= 32;
	} else if (SHIFT_DOWN && keymap_shift[sc]) {
		ch = keymap_shift[sc];
	}

    spin_unlock_irqrestore(&keyboard_lock, rflags);
    return ch;
}
