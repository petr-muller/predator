/*
 * Global variables
 */

#include <stdlib.h>

struct T
{
	struct T* next;
};

struct T* x;

int main()
{
	if (!x)
	{
		// the analyzer should report a bug here
		*(int*)NULL = 0;
	}

	return 0;
}
