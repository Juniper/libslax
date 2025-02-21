
#include "bamain.h"

void
do_test (void)
{
    size_t size = 32;
    char *buf = malloc(size);

    snprintf(buf, size, "%08d", 12345);
    printf("%zu %s\n", strlcat(buf, "abcdefg", size), buf);
    printf("%zu %s\n", strlcat(buf, "hijklmno", size), buf);
    printf("%zu %s\n", strlcat(buf, "pqrstuvwxxyz", size), buf);
    printf("%zu %s\n", strlcat(buf, "12341234", size), buf);

    snprintf(buf, size, "%08d", 12345);
    printf("%zu %s\n", strlcat(buf, "abcdefg", size), buf);
    printf("%zu %s\n", strlcat(buf, "hijklmno", size), buf);
    printf("%zu %s\n", strlcat(buf, "pqrstuvw", size), buf);
    printf("%zu %s\n", strlcat(buf, "12341234", size), buf);
}
