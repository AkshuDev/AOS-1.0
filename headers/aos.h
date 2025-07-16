// Aos Data
//AOS
// %define version db "0001"
extern char* version = "0001";

// %define AOS 1
#define AOS 1

//Files
//Executables
//AEF (Aos Executable File)
// %define Sign db "AS&AN", 0
// %define Sign_Size equ $ - Sign
extern char* Sign = "AS&AN";
extern char* Sign_WNL = "AS&AN\n";
extern int Sign_Size = sizeof(Sign);
extern int Sign_Size_WNL = sizeof(Sign_WNL);

extern char* Available_Convertion_Formats[2] = {"aef", "exe"};
extern char* Default_Convertion_Format = "exe";

extern unsigned short Version = 0x0001;
extern int Version_Size = sizeof(Version);

extern int MAX_BUFFER = 1024;