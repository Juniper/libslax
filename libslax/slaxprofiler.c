/*
 * $Id: debugger.c 384736 2010-06-16 04:41:59Z deo $
 *
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 *
 * A profiler for SLAX scripts
 *
 * We are starting simple here, but might expand a some later time.  For
 * now, the profiler allocates an
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>

#include <libxml/xmlsave.h>
#include <libxml/xmlIO.h>
#include <libxslt/variables.h>
#include <libxslt/transform.h>

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "config.h"

typedef struct slax_prof_entry_s {
    unsigned long spe_count; /* Number of times we've hit this line */
    unsigned long spe_user; /* Total number of user cycles we've spent */
    unsigned long spe_system; /* Total number of system cycles we've spent */
} slax_prof_entry_t;

/*
 * The profile information for the document we are profiling.
 * Note: If/when we want to support multiple files, a TAILQ_ENTRY-based
 * linked list can be added.  But not yet.....
 */
typedef struct slax_prof_s {
    xmlDocPtr sp_docp;		/* Document pointer */
    unsigned sp_inst_line;	/* Current instruction line number*/
    unsigned sp_lines;		/* Number of lines in the file */
    slax_prof_entry_t sp_data[0]; /* Raw data, indexed by line number */
} slax_prof_t;

slax_prof_t *slax_profile;	/* Profiling data */
time_usecs_t slax_profile_time_user; /* Last user time from getrusage */
unsigned long slax_profile_time_system; /* Last system time from getrusage */

static unsigned
slaxProfCountLines (xmlDocPtr docp)
{
    xmlNodePtr cur;
    xmlNodePtr next;
    xmlNodePtr prev = NULL;

    for (cur = docp->children; cur; prev = cur, cur = next) {
	next = cur->next;
	if (next == NULL)
	    next = cur->children;
    }

    return prev ? xmlGetLineNo(prev) : 1;
}

/**
 * Called from the debugger when we want to profile a script.
 *
 * @docp document pointer for the script
 * @returns TRUE is there was a problem
 */
int
slaxProfOpen (xmlDocPtr docp)
{
    long lines = slaxProfCountLines(docp);
    slax_prof_t *spp;

    if (lines == 0)
	return TRUE;

    spp = xmlMalloc(sizeof(*spp) + sizeof(spp->sp_data[0]) * lines);
    if (spp == NULL) {
	slaxOutput("out of memory");
	return TRUE;
    }

    bzero(spp, sizeof(*spp) + sizeof(spp->sp_data[0]) * (lines + 1));

    spp->sp_docp = docp;
    spp->sp_lines = lines;

    slax_profile = spp;		/* Record as current document */
    return FALSE;
}

/**
 * Called when we enter a instruction.
 *
 * @docp document pointer
 * @inst instruction (slax/xslt code) pointer
 */
void
slaxProfEnter (xmlDocPtr docp, xmlNodePtr inst)
{
    slax_prof_t *spp = slax_profile;
    long line;
    struct rusage ru;

    /* Zero means no valid data, so don't record */
    slax_profile_time_user = slax_profile_time_system = 0;

    if (docp != spp->sp_docp)
	return;

    line = xmlGetLineNo(inst);
    if (line <= 0 || line > spp->sp_lines)
	return;

    if (getrusage(0, &ru) == 0) {
	slax_profile_time_user = timeval_to_usecs(&ru.ru_utime);
	slax_profile_time_system = timeval_to_usecs(&ru.ru_utime);
	spp->sp_inst_line = line;	/* Save instruct line number */
    }
}

/**
 * Called when we exit an instruction
 *
 * @docp document pointer
 * @inst instruction (slax/xslt code) pointer
 */
void
slaxProfExit (xmlDocPtr docp, xmlNodePtr inst UNUSED)
{
    slax_prof_t *spp = slax_profile;
    long line;
    struct rusage ru;
    int rc;

    /* Zero means no valid data, so don't record */
    if (slax_profile_time_user == 0)
	return;

    if (docp != spp->sp_docp)
	return;

    rc = getrusage(0, &ru);

    line = spp->sp_inst_line;
    if (line <= 0 || line > spp->sp_lines)
	return;

    if (rc == 0) {
	time_usecs_t user = timeval_to_usecs(&ru.ru_utime);
	time_usecs_t syst = timeval_to_usecs(&ru.ru_utime);

	spp->sp_data[line].spe_count += 1;
	spp->sp_data[line].spe_user += user - slax_profile_time_user;
	spp->sp_data[line].spe_system += syst - slax_profile_time_system;
    }

    slax_profile_time_user = slax_profile_time_system = 0; /* Not valid */
}

/**
 * Print the results
 */
void
slaxProfPrint (slaxProfCallback_t func, void *data)
{
    slax_prof_t *spp = slax_profile;
    const char *filename = (const char *) spp->sp_docp->URL;
    FILE *fp;
    unsigned num = 0;
    char line[BUFSIZ];

    fp = fopen(filename, "r");
    if (fp == NULL) {
	func(data, "could not open file: %s", filename);
	return;
    }

    for (;;) {
	if (fgets(line, sizeof(line), fp) == NULL) 
	    break;

	/*
	 * Since we are only reading line_size per iteration, if the current 
	 * line length is more than line_size then we may read same line in
	 * more than one iteration, so increment the count only when the last 
	 * character is '\n'
	 */
	if (line[strlen(line) - 1] == '\n') {
	    num += 1;

	    if (num < spp->sp_lines && spp->sp_data[num].spe_count) {
		func(data, "%5d %5d %5d %5d%s", num,
		   spp->sp_data[num].spe_count,
		   spp->sp_data[num].spe_user / spp->sp_data[num].spe_count,
		   spp->sp_data[num].spe_system / spp->sp_data[num].spe_count,
		   line);
	    } else {
		func(data, "%5d %17s%s", num, "", line);
	    }
	} else {
	    func(data, "%23s%s", "", line);
	}
    }

}

/**
 * Done (free resources)
 */
void
slaxProfClose (void)
{
    slax_prof_t *spp = slax_profile;

    xmlFree(spp);

    slax_profile_time_user = slax_profile_time_system = 0; /* Not valid */
}
