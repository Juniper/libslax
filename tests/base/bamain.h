#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "slaxconfig.h"
#include <libpsu/psulog.h>
#include <libpsu/psualloc.h>
#include <libpsu/psustring.h>

static int opt_dump;

void do_test(void);

int
main (int argc, char **argv)
{
    psu_log_enable(TRUE);

    for (argc = 1; argv[argc]; argc++) {
	if (argv[argc][0] == '#') /* Ignore comments */
	    continue;

	if (strcmp(argv[argc], "dump") == 0) {
	    opt_dump = 1;
	}
    }

    do_test();

    return 0;
}
