/*
 * Copyright (c) 2010-2025, Juniper Networks, Inc.
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
#include <errno.h>
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
#include <libxml/hash.h>
#include <libxslt/variables.h>
#include <libxslt/transform.h>

#include "slaxinternals.h"
#include <libslax/slax.h>
#include <libpsu/psutime.h>

#ifdef WITH_SLAX_PROFILER

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

/*
 * Overhead (in microseconds) of a single psu_get_time() call, used to
 * correct the wall-clock deltas we record below for the cost that our
 * own measurement calls add.  Calibrated once, lazily, since the
 * calibration loop itself takes long enough that we don't want to pay
 * for it more than once per profiling run.
 */
static psu_time_usecs_t slax_profile_overhead;

static unsigned
slaxProfCountLines (xmlDocPtr docp)
{
    xmlNodePtr cur;
    xmlNodePtr next;
    xmlNodePtr prev = NULL;

    for (cur = xmlDocGetChildren(docp); cur; prev = cur, cur = next) {
	next = xmlNodeGetNext(cur);
	if (next == NULL)
	    next = xmlNodeGetChildren(cur);
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

    slax_profile_overhead = psu_calibrate_overhead();

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

    slaxLog("profile:enter for %s", xmlNodeGetName(inst));

    if (spp->sp_inst_line)
	slaxLog("profile: warning: enter while still set");

    /* Zero means no valid data, so don't record */
    slax_profile_time_user = slax_profile_time_system = 0;

    spp->sp_wall = psu_get_time();

    if (xmlNodeGetDoc(inst) != spp->sp_docp)
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

    /* Update the wall-clock number, correcting for our own overhead */
    if (spp->sp_wall != 0 && now > spp->sp_wall) {
	psu_time_usecs_t wall = now - spp->sp_wall;

	wall = (wall > slax_profile_overhead)
	      ? wall - slax_profile_overhead : 0;

	spp->sp_data[line].spe_wall += wall;
    }
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
    const char *filename = (const char *) xmlDocGetURL(spp->sp_docp);
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
            if (tot_count)
	        snprintf(count_buf, sizeof(count_buf), "(%lu)", 
		     (count * 100) / tot_count);
            else
                snprintf(count_buf, sizeof(count_buf), "(-)");

            if (tot_user)
	        snprintf(user_buf, sizeof(user_buf), "(%llu)", 
		     (spp->sp_data[num].spe_user * 100) / tot_user);
            else
                snprintf(user_buf, sizeof(user_buf), "(-)");

            if (tot_wall)
	        snprintf(wall_buf, sizeof(wall_buf), "(%llu)", 
		     (spp->sp_data[num].spe_wall * 100) / tot_wall);
            else
                snprintf(wall_buf, sizeof(wall_buf), "(-)");
	    
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

#else /* WITH_SLAX_PROFILER */

/*
 * This build was configured without the slax per-source-line profiler
 * (--without-slax-profiler), so none of the real code above was built.
 * Without these stubs, sdb would either fail to link or crash the first
 * time it tried to call into a profiler that was never built.  Fail
 * cleanly instead.
 */

int
slaxProfOpen (xmlDocPtr docp UNUSED)
{
    return FALSE;
}

void
slaxProfEnter (xmlNodePtr inst UNUSED)
{
    /* nothing to do; profiler support isn't compiled in */
}

void
slaxProfExit (void)
{
    /* nothing to do; profiler support isn't compiled in */
}

void
slaxProfReport (slaxDebugFlags_t flags UNUSED, const char *buffer UNUSED)
{
    slaxOutput("sdb: profiler support was not compiled into this build "
	      "(rebuild with --with-bxslt-components=\"default +slax-profiler\")");
}

void
slaxProfClear (void)
{
    /* nothing to do; profiler support isn't compiled in */
}

void
slaxProfClose (void)
{
    /* nothing to do; profiler support isn't compiled in */
}

#endif /* WITH_SLAX_PROFILER */

/*
 * Emit one report line to either "fp" (the file given to
 * slaxProfReportCallgraph()/slaxProfReportFlamegraph(), opened and
 * closed by the caller) or, when "fp" is NULL, via slaxOutput().
 * This is the only place either report writes a line, so it's also
 * the only place that needs to know the destination differs; neither
 * report function (nor anything they call) ever goes through
 * slaxIoWriteOutputToFileStart(), so a target file never contains
 * anything but these lines.
 */
static void
slaxProfReportLine (FILE *fp, const char *line)
{
    if (fp)
	fprintf(fp, "%s\n", line);
    else
	slaxOutput("%s", line);
}

/*
 * Close out a report written to "fp" (if any), telling the user how
 * much was written and where; when "fp" is NULL (console output via
 * slaxOutput()), there's nothing to close or confirm.
 */
static void
slaxProfReportDone (FILE *fp, const char *filename, int count)
{
    if (fp == NULL)
	return;

    slaxOutput("Wrote %d line%s of data to file '%s'",
	       count, count == 1 ? "" : "s", filename);
    fclose(fp);
}

/**
 * Report libbxslt's per-template callgraph profiling output, as
 * captured by xsltSaveProfiling() into an in-memory buffer.  When
 * "filename" is given, the report is written directly to that file;
 * otherwise it's re-emitted a line at a time via slaxOutput().
 *
 * Unlike the rest of this file, this does not depend on
 * WITH_SLAX_PROFILER (sdb's own per-source-line profiler); it only
 * reformats text produced by libbxslt's independent
 * WITH_XSLT_PROFILER machinery, so it is always compiled in.
 */
void
slaxProfReportCallgraph (const char *buffer, const char *filename)
{
    const char *last, *xp;
    char line[BUFSIZ];
    size_t len;
    FILE *fp = NULL;
    int count = 0;

    if (filename) {
	fp = fopen(filename, "w");
	if (fp == NULL) {
	    slaxOutput("could not open '%s' for writing: %s",
		       filename, strerror(errno));
	    return;
	}
    }

    if (buffer == NULL) {
	slaxProfReportLine(fp, "no callgraph profiling data was recorded");
	slaxProfReportDone(fp, filename, 1);
	return;
    }

    for (last = buffer; last != NULL && *last; ) {
	xp = strchr(last, '\n');
	len = xp ? (size_t) (xp - last) : strlen(last);
	if (len > sizeof(line) - 1)
	    len = sizeof(line) - 1;

	memcpy(line, last, len);
	line[len] = '\0';

	slaxProfReportLine(fp, line);
	count++;

	last = xp ? xp + 1 : NULL;
    }

    slaxProfReportDone(fp, filename, count);
}

/*
 * One (path, self-time) pair pulled out of a flame-graph hash table,
 * for sorting before we print.
 */
typedef struct slax_flamegraph_entry_s {
    const xmlChar *fe_path;
    psu_time_usecs_t fe_usecs;
} slax_flamegraph_entry_t;

/*
 * Scan context for slaxProfFlamegraphScan(): the array we're filling
 * in, and how much of it is used so far.
 */
typedef struct slax_flamegraph_scan_s {
    slax_flamegraph_entry_t *fs_entries;
    unsigned fs_count;
} slax_flamegraph_scan_t;

static void
slaxProfFlamegraphScan (void *payload, void *data, const xmlChar *name)
{
    slax_flamegraph_scan_t *fsp = data;
    psu_time_usecs_t *valp = payload;

    fsp->fs_entries[fsp->fs_count].fe_path = name;
    fsp->fs_entries[fsp->fs_count].fe_usecs = *valp;
    fsp->fs_count += 1;
}

static int
slaxProfFlamegraphCompare (const void *ap, const void *bp)
{
    const slax_flamegraph_entry_t *a = ap;
    const slax_flamegraph_entry_t *b = bp;

    return strcmp((const char *) a->fe_path, (const char *) b->fe_path);
}

/**
 * Report sdb's own per-call-path self-time flame-graph data (see
 * slaxDebugAddFrame()/slaxDebugDropFrame() in slaxdebugger.c, which
 * build "hash" via the debugger's existing call-stack hooks).  When
 * "filename" is given, the report is written directly to that file;
 * otherwise it's emitted a line at a time via slaxOutput().
 *
 * Entries are sorted alphabetically by path before printing, so the
 * report is deterministic without needing a separate normalization
 * pass, unlike slaxProfReportCallgraph()'s captured-text buffer.
 *
 * Like slaxProfReportCallgraph(), this does not depend on
 * WITH_SLAX_PROFILER: it only reformats sdb's own call-path hash
 * table, so it is always compiled in.
 */
void
slaxProfReportFlamegraph (xmlHashTablePtr hash, const char *filename)
{
    int count = hash ? xmlHashSize(hash) : 0;
    slax_flamegraph_scan_t scan;
    int i;
    FILE *fp = NULL;
    char line[BUFSIZ];

    if (filename) {
	fp = fopen(filename, "w");
	if (fp == NULL) {
	    slaxOutput("could not open '%s' for writing: %s",
		       filename, strerror(errno));
	    return;
	}
    }

    if (hash == NULL || count <= 0) {
	slaxProfReportLine(fp, "no flamegraph profiling data was recorded");
	slaxProfReportDone(fp, filename, 1);
	return;
    }

    scan.fs_entries = xmlMalloc(count * sizeof(*scan.fs_entries));
    if (scan.fs_entries == NULL) {
	slaxProfReportLine(fp, "out of memory");
	slaxProfReportDone(fp, filename, 1);
	return;
    }
    scan.fs_count = 0;

    xmlHashScan(hash, slaxProfFlamegraphScan, &scan);

    qsort(scan.fs_entries, scan.fs_count, sizeof(*scan.fs_entries),
	  slaxProfFlamegraphCompare);

    for (i = 0; i < (int) scan.fs_count; i++) {
	snprintf(line, sizeof(line), "%s %llu", scan.fs_entries[i].fe_path,
		 scan.fs_entries[i].fe_usecs);
	slaxProfReportLine(fp, line);
    }

    xmlFree(scan.fs_entries);

    slaxProfReportDone(fp, filename, (int) scan.fs_count);
}
