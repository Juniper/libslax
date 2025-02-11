/*
 * Copyright (c) 2010-2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
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
#include <sys/time.h>
#include <sys/resource.h>

#include <libxml/xmlsave.h>
#include <libxml/xmlIO.h>
#include <libxslt/variables.h>
#include <libxslt/transform.h>

#include "slaxinternals.h"
#include <libslax/slax.h>
#include <libpsu/psutime.h>

typedef struct slax_prof_entry_s {
    unsigned long spe_count; /* Number of times we've hit this line */
    unsigned long spe_user; /* Total number of user cycles we've spent */
    unsigned long spe_system; /* Total number of system cycles we've spent */
    psu_time_usecs_t spe_wall; /* Total wall clock time we've spent */
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
    psu_time_usecs_t sp_wall;   /* Saved wall-clock time from entry */
    slax_prof_entry_t sp_data[0]; /* Raw data, indexed by line number */
} slax_prof_t;

slax_prof_t *slax_profile;	/* Profiling data */
psu_time_usecs_t slax_profile_time_user; /* Last user time from getrusage */
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
    unsigned lines = slaxProfCountLines(docp);
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
    unsigned line;
    struct rusage ru;

    slaxLog("profile:enter for %s", inst->name);

    if (spp->sp_inst_line)
	slaxLog("profile: warning: enter while still set");

    /* Zero means no valid data, so don't record */
    slax_profile_time_user = slax_profile_time_system = 0;

    spp->sp_wall = psu_get_time();

    if (inst->doc != spp->sp_docp)
	return;

    line = xmlGetLineNo(inst);
    if (line == 0 || line > spp->sp_lines)
	return;

    if (getrusage(0, &ru) == 0) {
	slax_profile_time_user = psu_timeval_to_usecs(&ru.ru_utime);
	slax_profile_time_system = psu_timeval_to_usecs(&ru.ru_stime);
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
    unsigned line;
    struct rusage ru;
    int rc;

    psu_time_usecs_t now = psu_get_time();

    if (spp->sp_inst_line == 0)
	return;

    /* Zero means no valid data, so don't record */
    if (slax_profile_time_user == 0)
	return;

    rc = getrusage(0, &ru);

    line = spp->sp_inst_line;
    if (line == 0 || line > spp->sp_lines)
	return;

    if (rc == 0) {
	psu_time_usecs_t user = psu_timeval_to_usecs(&ru.ru_utime);
	psu_time_usecs_t syst = psu_timeval_to_usecs(&ru.ru_stime);

	spp->sp_data[line].spe_count += 1;
	user = (user > slax_profile_time_user)
	      ? user - slax_profile_time_user : 0;
	syst = (syst > slax_profile_time_system)
	      ? syst - slax_profile_time_system : 0;

	spp->sp_data[line].spe_user += user;
	spp->sp_data[line].spe_system += syst;
    }

    /* Update the wall-clock number */
    if (spp->sp_wall != 0 && now > spp->sp_wall)
	spp->sp_data[line].spe_wall += now - spp->sp_wall;
    spp->sp_wall = 0;		/* Clear it out */

    slax_profile_time_user = slax_profile_time_system = 0; /* Not valid */
    spp->sp_inst_line = 0;
}

static inline double
doublediv (unsigned long num, unsigned long denom)
{
    double xnum = num, xdenom = denom;
    return xnum / xdenom;
}

static char *
slaxProfTrimTime (char *buf, int bufsiz, psu_time_usecs_t t)
{
    snprintf(buf, bufsiz, "%12llu", t);

    return buf;
}

/**
 * Report the results
 */
void
slaxProfReport (slaxDebugFlags_t flags, const char *buffer)
{
    slax_prof_t *spp = slax_profile;
    const char *filename = (const char *) spp->sp_docp->URL;
    FILE *fp = NULL;
    unsigned num = 0, count;
    char line[BUFSIZ];
    unsigned long tot_count = 0;
    psu_time_usecs_t tot_user = 0, tot_system = 0;
    psu_time_usecs_t tot_wall = 0;
    const char *xp, *last = NULL;
    size_t len;

    char count_buf[16];
    char user_buf[16];
    char wall_buf[20];
    char wall_val[64];

    int brief = (flags & SDBF_PROFILE_BRIEF) ? 1 : 0;
    int wall  = (flags & SDBF_PROFILE_WALL) ? 1 : 0;

    if (buffer) {
	last = buffer;
    } else {
	fp = fopen(filename, "r");
	if (fp == NULL) {
	    slaxOutput("could not open file: %s", filename);
	    return;
	}
    }

    if (wall)
	slaxOutput("%5s %8s  %% %9s  %% %9s %8s %8s %12s  %% %s",
	       "Line", "Hits", "User", "U/Hit", "System", "S/Hit",
		   "Wall (usecs)", "Source");
    else 
	slaxOutput("%5s %8s  %% %9s  %% %9s %8s %8s %s",
	       "Line", "Hits", "User", "U/Hit", "System", "S/Hit", "Source");

    /* Calculate the "totals" first, so we can emit percentages */
    for (;;) {
	if (buffer) {
	    /* If we were passed an in-memory script, use it */
	    if (last == NULL)
		break;

	    xp = strchr(last, '\n'); /* Break a newlines */
	    len = xp ? (size_t) (xp - last + 1) : strlen(last);
	    if (len > sizeof(line) - 1)
		len = sizeof(line) - 1;
	    memcpy(line, last, len); /* Mimic the fgets functionality */
	    line[len] = '\0';
	    last = xp ? xp + 1 : NULL;

	} else {
	    if (fgets(line, sizeof(line), fp) == NULL) 
		break;
	}

	int line_len = strlen(line);
	if (line[line_len - 1] == '\n') {
	    line_len -= 1;
	    num += 1;

	    count = spp->sp_data[num].spe_count;

	    if (num <= spp->sp_lines && spp->sp_data[num].spe_count) {
		tot_count += spp->sp_data[num].spe_count;
		tot_user += spp->sp_data[num].spe_user;
		tot_system += spp->sp_data[num].spe_system;
		tot_wall += spp->sp_data[num].spe_wall;
	    }
	}
    }

    /* Reset all the loop variables */
    last = NULL;
    num = 0;
    if (buffer) {
	last = buffer;
    } else {
	fseek(fp, 0, SEEK_SET);
    }

    /* Now make output */
    for (;;) {
	if (buffer) {
	    /* If we were passed an in-memory script, use it */
	    if (last == NULL)
		break;

	    xp = strchr(last, '\n'); /* Break a newlines */
	    len = xp ? (size_t) (xp - last + 1) : strlen(last);
	    if (len > sizeof(line) - 1)
		len = sizeof(line) - 1;
	    memcpy(line, last, len); /* Mimic the fgets functionality */
	    line[len] = '\0';
	    last = xp ? xp + 1 : NULL;

	} else {
	    if (fgets(line, sizeof(line), fp) == NULL) 
		break;
	}

	/*
	 * Since we are only reading line_size per iteration, if the current 
	 * line length is more than line_size then we may read same line in
	 * more than one iteration, so increment the count only when the last 
	 * character is '\n'
	 */
	int line_len = strlen(line);
	if (line[line_len - 1] == '\n') {
	    line_len -= 1;
	    num += 1;

	    count = spp->sp_data[num].spe_count;

	    snprintf(count_buf, sizeof(count_buf), "(%lu)", 
		     (count * 100) / tot_count);
	    snprintf(user_buf, sizeof(user_buf), "(%llu)", 
		     (spp->sp_data[num].spe_user * 100) / tot_user);
	    snprintf(wall_buf, sizeof(wall_buf), "(%llu)", 
		     (spp->sp_data[num].spe_wall * 100) / tot_wall);
	    
	    char *start;

	    if (spp->sp_data[num].spe_wall == 0) {
		snprintf(wall_val, sizeof(wall_val), "0");
		start = wall_val;

	    } else {
		/*
		 * We have our wall clock value, but the formatted value
		 * might be too long with lots of trailing nanoseconds
		 * that aren't interesting.  Decide if we can trim it.
		 */
		start = slaxProfTrimTime(wall_val, sizeof(wall_val),
					 spp->sp_data[num].spe_wall);
	    }

	    if (num <= spp->sp_lines && spp->sp_data[num].spe_count) {
		if (wall)
		    slaxOutput("%5u %8u%4s %8lu%4s %8.2f %8lu %8.2f "
			       "%12s%4s %.*s",
			       num, count, count_buf,
			       spp->sp_data[num].spe_user,
			       user_buf,
			       doublediv(spp->sp_data[num].spe_user, count),
			       spp->sp_data[num].spe_system,
			       doublediv(spp->sp_data[num].spe_system, count),
			       start ?: wall_val, wall_buf,
			       line_len, line);
		else
		    slaxOutput("%5u %8u%4s %8lu%4s %8.2f %8lu %8.2f %.*s",
			       num, count, count_buf,
			       spp->sp_data[num].spe_user,
			       user_buf,
			       doublediv(spp->sp_data[num].spe_user, count),
			       spp->sp_data[num].spe_system,
			       doublediv(spp->sp_data[num].spe_system, count),
			       line_len, line);

	    } else if (!brief) {
		if (wall)
		    slaxOutput("%5u %8s %12s %12s %8s %8s %12s%4s %.*s",
			       num, "-", "-", "-", "-", "-", "-", "-",
			       line_len, line);
		else 
		    slaxOutput("%5u %8s %12s %12s %8s %8s %.*s",
			       num, "-", "-", "-", "-", "-", line_len, line);
	    }
	} else if (!brief) {
	    if (wall)
		slaxOutput("%5u %8s %12s %12s %8s %8s %12s%4s %.*s",
			   num, "-", "-", "-", "-", "-", "-", "-",
			   line_len, line);
	    else 
		slaxOutput("%5s %8s %12s %12s %8s %8s %.*s",
			   "-", "-", "-", "-", "-", "-", line_len, line);
	}
    }

    if (wall)
	slaxOutput("%5s %8lu %12llu %12s %8llu %8s %12s%4s  %s",
		   "Total", tot_count, tot_user, " ", tot_system, " ",
		   slaxProfTrimTime(wall_val, sizeof(wall_val), tot_wall),
		   " ", "Total");
    else
	slaxOutput("%5s %8lu %12llu %12s %8s %8llu  %s",
		   "Total", tot_count, tot_user, " ", " ", tot_system, "Total");

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
