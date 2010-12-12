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

    spp = xmlMalloc(sizeof(*spp) + sizeof(spp->sp_data[0]) * (lines + 1));
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
 * Called when we enter an instruction.
 *
 * @docp document pointer
 * @inst instruction (slax/xslt code) pointer
 */
void
slaxProfEnter (xmlNodePtr inst)
{
    slax_prof_t *spp = slax_profile;
    long line;
    struct rusage ru;

    slaxLog("profile:enter for %s", inst->name);

    if (spp->sp_inst_line)
	slaxLog("profile: warning: enter while still set");

    /* Zero means no valid data, so don't record */
    slax_profile_time_user = slax_profile_time_system = 0;

    if (inst->doc != spp->sp_docp)
	return;

    line = xmlGetLineNo(inst);
    if (line <= 0 || line > spp->sp_lines)
	return;

    if (getrusage(0, &ru) == 0) {
	slax_profile_time_user = timeval_to_usecs(&ru.ru_utime);
	slax_profile_time_system = timeval_to_usecs(&ru.ru_stime);
	spp->sp_inst_line = line;	/* Save instruct line number */
    }
}

/**
 * Called when we exit an instruction
 */
void
slaxProfExit (void)
{
    slax_prof_t *spp = slax_profile;
    long line;
    struct rusage ru;
    int rc;

    if (spp->sp_inst_line == 0)
	return;

    /* Zero means no valid data, so don't record */
    if (slax_profile_time_user == 0)
	return;

    rc = getrusage(0, &ru);

    line = spp->sp_inst_line;
    if (line <= 0 || line > spp->sp_lines)
	return;

    if (rc == 0) {
	time_usecs_t user = timeval_to_usecs(&ru.ru_utime);
	time_usecs_t syst = timeval_to_usecs(&ru.ru_stime);

	spp->sp_data[line].spe_count += 1;
	user = (user > slax_profile_time_user)
	      ? user - slax_profile_time_user : 0;
	syst = (syst > slax_profile_time_system)
	      ? syst - slax_profile_time_system : 0;

	spp->sp_data[line].spe_user += user;
	spp->sp_data[line].spe_system += syst;
    }

    slax_profile_time_user = slax_profile_time_system = 0; /* Not valid */
    spp->sp_inst_line = 0;
}

static inline double
doublediv (unsigned long num, unsigned long denom)
{
    double xnum = num, xdenom = denom;
    return xnum / xdenom;
}

/**
 * Report the results
 */
void
slaxProfReport (int brief)
{
    slax_prof_t *spp = slax_profile;
    const char *filename = (const char *) spp->sp_docp->URL;
    FILE *fp;
    unsigned num = 0, count;
    char line[BUFSIZ];
    unsigned long tot_count = 0;
    time_usecs_t tot_user = 0, tot_system = 0;

    fp = fopen(filename, "r");
    if (fp == NULL) {
	slaxOutput("could not open file: %s", filename);
	return;
    }

    slaxOutput("%5s %8s %8s %8s %8s %8s %s",
	       "Line", "Hits", "User", "U/Hit", "System", "S/Hit", "Source");

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
	    line[strlen(line) - 1] = '\0';
	    num += 1;

	    count = spp->sp_data[num].spe_count;

	    if (num <= spp->sp_lines && spp->sp_data[num].spe_count) {
		slaxOutput("%5u %8u %8lu %8.2f %8lu %8.2f %s",
			   num, count,
			   spp->sp_data[num].spe_user,
			   doublediv(spp->sp_data[num].spe_user, count),
			   spp->sp_data[num].spe_system,
			   doublediv(spp->sp_data[num].spe_system, count),
			   line);

		tot_count += spp->sp_data[num].spe_count;
		tot_user += spp->sp_data[num].spe_user;
		tot_system += spp->sp_data[num].spe_system;

	    } else if (!brief) {
		slaxOutput("%5u %8s %8s %8s %8s %8s %s",
			   num, "-", "-", "-", "-", "-", line);
	    }
	} else if (!brief) {
	    slaxOutput("%5s %8s %8s %8s %8s %8s %s",
		       "-", "-", "-", "-", "-", "-", line);
	}
    }

    slaxOutput("%5s %8lu %8lu %8lu %s", "Total", tot_count,
	       tot_user, tot_system, "Totals");

}


/**
 * Clear all values
 */
void
slaxProfClear (void)
{
    slax_prof_t *spp = slax_profile;

    bzero(spp->sp_data, (spp->sp_lines + 1) * sizeof(spp->sp_data[0]));
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
