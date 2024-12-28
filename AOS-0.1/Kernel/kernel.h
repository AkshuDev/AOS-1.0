//////////////////////////////////////////////
// Originally created for osdever.net,
// Distrobuted with OS Creator, legal notice follows:
// This code was made for the tutorial:
// "Making a Simple C kernel with Basic printf and clearscreen Functions"
//
// This code comes with absolutly
// NO WARRANTY
// you can not hold me(KJ), nor
// anyone else responsible for what
// this code does.
//
// This code is in the public domain,
// you may use it however you want
//////////////////////////////////////////////


#define WHITE_TXT 0x07 // white on black text

void k_clear_screen();
unsigned int k_printf(char *message, unsigned int line);

