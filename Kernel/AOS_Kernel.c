// Main kernel
void kernel_main() __attribute__((naked));

void aos_print(const char* str, char* video) {
    for (int i = 0; str[i] != '\0'; i++) {
        video[i * 2] = str[i];
        video[i * 2 + 1] = 0x07; // White on black
    }
}

void kernel_main() {
    const char* str = "AOS Kernel Started, CODE : 0 (SUCCESS)!\nCredits to Limine for bootloader\n";
    char* video = (char*)0xB8000;

    aos_print(str, video);

    while(1);
}