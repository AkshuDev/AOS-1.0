#define WHITE_TXT 0x07 // white on black text
#define __NO_CRT

void k_clear_screen();
void k_printf(char *message, unsigned int line);
void __main();

int main() // like main in a normal C program
{
	k_clear_screen();
	unsigned int line = 0;
	char* message = "Hi, How is this for AOS!";
	k_printf(message, line);

	return 0;
};

void __main(){
	return;
} //A void function that does nothing to prevent from insertion of crt (c runtime) causing issues with linking.

void k_clear_screen() // clear the entire text screen
{
	char *vidmem = (char *) 0xb8000;
	unsigned int i=0;
	while(i < (80*25*2))
	{
		vidmem[i]=' ';
		i++;
		vidmem[i]=WHITE_TXT;
		i++;
	};
};

void k_printf(char *message, unsigned int line) // the message and then the line #
{
	char *vidmem = (char *) 0xb8000;
	unsigned int i=0;

	i=(line*80*2);

	while(*message!=0)
	{
		if(*message=='\n') // check for a new line
		{
			line++;
			i=(line*80*2);
			*message++;
		} else {
			vidmem[i]=*message;
			*message++;
			i++;
			vidmem[i]=WHITE_TXT;
			i++;
		};
	};
};
