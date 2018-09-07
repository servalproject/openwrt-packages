#include "code_instrumentation.h"

#include <time.h>
#include <stdio.h>
#include <stdarg.h> 
#include <string.h> 

#define BUFFER_SIZE 256

void code_instrumentation_log(const char* fileName, int line, const char* functionName, int logLevel, const char *msg, ...)
{
	if (logLevel <= COMPILE_LOG_LEVEL)
	{
		time_t now = time(0);
		struct tm* localtm = localtime(&now);

		static char timeBuffer[BUFFER_SIZE];
		strcpy(timeBuffer, asctime(localtm));
		*(timeBuffer + strlen(timeBuffer) - 1) = '\0';

		static char formatBuffer[BUFFER_SIZE];
		snprintf(formatBuffer, BUFFER_SIZE, "%s: %s (%d) - %s:\n  %s\n", timeBuffer, fileName, line, functionName, msg);
		va_list args;
		va_start(args, msg);
		vfprintf(stderr, formatBuffer, args);
	}
}

// To do: store usage counts for functions. Also can allow us to find rogue mid-function 'return's: 
// if an entry is not balanced with an exit, we have a rogue return somewhere

void code_instrumentation_entry(const char* functionName)
{
}

void code_instrumentation_exit(const char* functionName)
{
}
