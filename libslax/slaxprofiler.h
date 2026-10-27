/*
 * Copyright (c) 2010-2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

/**
 * Called from the debugger when we want to profile a script.
 *
 * @docp document pointer for the script
 * @returns TRUE is there was a problem
 */
int
slaxProfOpen (xmlDocPtr docp);

/**
 * Called when we enter a instruction.
 *
 * @docp document pointer
 * @inst instruction (slax/xslt code) pointer
 */
void
slaxProfEnter (xmlNodePtr inst);

/**
 * Called when we exit an instruction
 */
void
slaxProfExit (void);

#if 0
typedef int (*slaxProfCallback_t)(void *, const char *fmt, ...);
#endif

/**
 * Report the results
 */
void
slaxProfReport (slaxDebugFlags_t flags, const char *buffer);

/**
 * Report libbxslt's per-template callgraph profiling output, as
 * captured by xsltSaveProfiling() into an in-memory buffer.  When
 * "filename" is given, the report is written directly to that file
 * (opened/closed here); otherwise it's re-emitted a line at a time
 * via slaxOutput().  Either way, this never uses
 * slaxIoWriteOutputToFileStart(), so nothing but the report data
 * itself ever lands in "filename".
 *
 * @buffer captured text, or NULL if nothing was captured
 * @filename target file for the report, or NULL to use slaxOutput()
 */
void
slaxProfReportCallgraph (const char *buffer, const char *filename);

/**
 * Report sdb's own per-call-path self-time flame-graph data, as
 * accumulated by the debugger's addFrame/dropFrame hooks into a
 * folded-stack hash table, emitting one line per unique path in the
 * standard "folded stack" text format consumed directly by
 * flamegraph.pl/speedscope.app: "frame1;frame2;frame3 <usecs>".
 *
 * @hash accumulated path -> self time (usecs) table, or NULL if
 *       nothing was recorded
 * @filename target file for the report, or NULL to use slaxOutput()
 */
void
slaxProfReportFlamegraph (xmlHashTablePtr hash, const char *filename);

/**
 * Clear all values
 */
void
slaxProfClear (void);

/**
 * Done (free resources)
 */
void
slaxProfClose (void);
