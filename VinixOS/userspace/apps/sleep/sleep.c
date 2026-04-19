/* sleep — surrender CPU for approximately N seconds */

#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sleep <seconds>\n");
        return 1;
    }
    unsigned int seconds = (unsigned int)atoi(argv[1]);
    sleep(seconds);
    return 0;
}
