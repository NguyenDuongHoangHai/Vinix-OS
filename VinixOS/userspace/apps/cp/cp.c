/* cp — copy bytes from src file to dst file */

#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: cp <src> <dst>\n");
        return 1;
    }

    int src = open(argv[1], O_RDONLY);
    if (src < 0) { printf("cp: %s: open failed\n", argv[1]); return 1; }

    int dst = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if (dst < 0) {
        printf("cp: %s: open failed\n", argv[2]);
        close(src);
        return 1;
    }

    char buf[256];
    ssize_t n;
    int rc = 0;
    while ((n = read(src, buf, sizeof(buf))) > 0) {
        ssize_t w = write(dst, buf, (size_t)n);
        if (w != n) { rc = 1; break; }
    }
    if (n < 0) rc = 1;

    close(src);
    close(dst);
    return rc;
}
