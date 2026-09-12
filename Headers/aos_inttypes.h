#include <stdint.h>

#ifndef NULL
	#define NULL (void*)0
#endif

typedef uint8_t aos_bool;
typedef aos_bool BOOL;

#ifndef TRUE
	#define TRUE 1
#endif

#ifndef FALSE
	#define FALSE 0
#endif

#define AOS_TRUE TRUE
#define AOS_FALSE FALSE
