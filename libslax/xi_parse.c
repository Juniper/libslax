/*
 * $Id$
 *
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) June 2016
 *
 * Parsing input means three distinct areas of work: parsing input, deciding
 * what to do with that input, and then doing it.  Our "xi_source" module
 * does the parsing, giving us back a "token" of input, which we pass to the
 * "rules" code to determine what needs done.  
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>

#include "slaxconfig.h"
#include <libslax/slaxdef.h>
#include <libslax/slax.h>
#include <libslax/pa_common.h>
#include <libslax/pa_config.h>
#include <libslax/pa_mmap.h>
#include <libslax/pa_fixed.h>
#include <libslax/pa_arb.h>
#include <libslax/pa_istr.h>
#include <libslax/pa_pat.h>
#include <libslax/pa_bitmap.h>
#include <libslax/xi_common.h>
#include <libslax/xi_source.h>
#include <libslax/xi_rules.h>
#include <libslax/xi_tree.h>
#include <libslax/xi_workspace.h>
#include <libslax/xi_parse.h>

xi_parse_t *
xi_parse_open (pa_mmap_t *pmp, xi_workspace_t *workp, const char *name,
	       const char *input, xi_source_flags_t flags)
{
    xi_source_t *srcp = NULL;
    xi_parse_t *parsep = NULL;
    xi_insert_t *xip = NULL;
    xi_tree_t *xtp = NULL;
    xi_node_t *nodep = NULL;
    pa_atom_t node_atom;
    char namebuf[PA_MMAP_HEADER_NAME_LEN];

    /*
     * XXX okay, so this is crap, just a bunch of initialization that
     * needs to be broken out in distinct functions.
     */

    srcp = xi_source_open(input, flags);
    if (srcp == NULL)
	goto fail;

    /* The xi_tree_t is the tree we'll be inserting into */
    xtp = calloc(1, sizeof(*xtp));
    if (xtp == NULL)
	goto fail;

    xtp->xt_infop = pa_mmap_header(pmp, xi_mk_name(namebuf, name, "tree"),
				   PA_TYPE_TREE, 0, sizeof(*xtp->xt_infop));
    xtp->xt_max_depth = 0;
    xtp->xt_workspace = workp;

    /* The xi_insert_t is the point in the tree at which we are inserting */
    xip = calloc(1, sizeof(*xip));
    if (xip == NULL)
	goto fail;
    xip->xi_tree = xtp;
    xip->xi_depth = 0;
    xip->xi_relation = XIR_CHILD;

    /* And finally, fill in the parse structure */
    parsep = calloc(1, sizeof(*parsep));
    if (parsep == NULL)
	goto fail;
    parsep->xp_srcp = srcp;
    parsep->xp_insert = xip;

    /* Fill in the default default rule */
    parsep->xp_default_rule.xr_flags = XRF_MATCH_ALL;
    parsep->xp_default_rule.xr_action = XIA_SAVE;

    nodep = xi_node_alloc(workp, &node_atom);
    if (nodep == NULL)
	goto fail;
    nodep->xn_type = XI_TYPE_ROOT;
    nodep->xn_depth = 0;
    nodep->xn_ns = PA_NULL_ATOM;
    nodep->xn_name = PA_NULL_ATOM;
    nodep->xn_next = PA_NULL_ATOM;
    nodep->xn_contents = PA_NULL_ATOM;

    xtp->xt_root = node_atom;
    xip->xi_stack[xip->xi_depth].xs_atom = node_atom;
    xip->xi_stack[xip->xi_depth].xs_node = nodep;

    return parsep;

 fail:
    if (xip)
	free(xip);
    if (xtp)
	free(xtp);
    if (parsep)
	free(parsep);
    if (srcp)
	xi_source_destroy(srcp);
    return NULL;
}

void
xi_parse_destroy (xi_parse_t *parsep UNUSED)
{
    return;
}

pa_atom_t
xi_parse_namepool_atom (xi_parse_t *parsep, const char *name)
{
    return xi_namepool_atom(xi_parse_workspace(parsep), name, TRUE);
}

const char *
xi_parse_namepool_string (xi_parse_t *parsep, pa_atom_t atom)
{
    return xi_namepool_string(xi_parse_workspace(parsep), atom);
}

static void
xi_insert_push (xi_insert_t *xip, pa_atom_t atom, xi_node_t *nodep)
{
    xi_rstate_t *statep = xip->xi_stack[xip->xi_depth].xs_statep;

    xip->xi_depth += 1;
    xip->xi_stack[xip->xi_depth].xs_atom = atom;
    xip->xi_stack[xip->xi_depth].xs_node = nodep;
    xip->xi_stack[xip->xi_depth].xs_statep = statep;
}

static void
xi_insert_pop (xi_insert_t *xip)
{
    xip->xi_stack[xip->xi_depth].xs_atom = PA_NULL_ATOM;
    xip->xi_stack[xip->xi_depth].xs_node = NULL;
    xip->xi_depth -= 1;
}

/*
 * Insert a node into the insertion point
 */
static pa_atom_t
xi_insert_node (xi_insert_t *xip, const char *msg,
		const char *data, size_t len,
		xi_node_type_t type, pa_atom_t name_atom, pa_atom_t contents)
{
    pa_atom_t node_atom;
    xi_node_t *nodep = xi_node_alloc(xip->xi_tree->xt_workspace, &node_atom);
    if (nodep == NULL)
	return PA_NULL_ATOM;

    /* Initialize our fields */
    nodep->xn_type = type;
    nodep->xn_ns = PA_NULL_ATOM;
    nodep->xn_name = name_atom;
    nodep->xn_contents = contents;

    slaxLog("%s: [%.*s] %u / %u (depth %u)", msg, len, data,
	    name_atom, contents, xip->xi_depth + 1);

    /*
     * If we don't have a child, make one.  Otherwise append it.
     */
    xi_istack_t *xsp = &xip->xi_stack[xip->xi_depth];
    if (xsp->xs_node->xn_contents == PA_NULL_ATOM) {
	/* Record us as the child of the current stack node */
	xsp->xs_node->xn_contents = node_atom;

	/* Set our "parent" as the current node */
	nodep->xn_next = xsp->xs_atom;

    } else {
	/* Append our node as the next child */
	nodep->xn_next = xsp->xs_atom;
	xsp->xs_last_node->xn_next = node_atom;
    }

    /* Mark the "last" as us */
    xsp->xs_last_atom = node_atom;
    xsp->xs_last_node = nodep;

    /* Set our depth */
    nodep->xn_depth = xip->xi_depth + 1;

    /* Update xi_maxdepth */
    if (nodep->xn_depth > xip->xi_maxdepth)
	xip->xi_maxdepth = nodep->xn_depth;

    return node_atom;
}

static void
xi_insert_attribs (xi_parse_t *parsep, xi_node_t *nodep, const char *data)
{
    xi_insert_t *xip = parsep->xp_insert;
    pa_arb_t *prp = xip->xi_tree->xt_workspace->xw_textpool;
    size_t len = strlen(data);
    pa_atom_t data_atom = pa_arb_alloc(prp, len + 1);
    char *cp = pa_arb_atom_addr(prp, data_atom);

    if (cp == NULL)
	return;

    memcpy(cp, data, len);
    cp[len] = '\0';

    pa_atom_t node_atom;
    node_atom = xi_insert_node(xip, "xi_insert_attribs", data, len,
			       XI_TYPE_ATSTR, PA_NULL_ATOM, data_atom);
    if (node_atom == PA_NULL_ATOM) {
	pa_arb_free_atom(prp, data_atom);
	return;
    }

    /* Mark the attributes as present (but not extracted) */
    nodep->xn_flags |= XNF_ATTRIBS_PRESENT;
}

/*
 * Returns NULL for success, or static error message text
 */
static const char *
xi_parse_next_attrib (char **content, char *endp,
		      char **namep, char **valuep)
{
    char *cp = *content;

    if (cp == NULL)
	return NULL;		/* Should not occur */

    char *name = xi_skipws(cp, endp - cp, 1);
    if (name == NULL) {		/* End of attributes */
	*content = NULL;	/* Mark end of attributes */
	return NULL;
    }

    cp = memchr(name, '=', endp - name);
    if (cp == NULL)
	return "invalid attribute; missing '='";

    /*
     * If valuep is NULL, we're not looking to carve up the buffer,
     * just scanning it for particular attributes, namely for namespace
     * attributes, which always start with "xmlns".
     */
    if (valuep == NULL) {
	cp += 1;		/* Skip equals sign */
    } else {
	/* Trim space off end of attribute name */
	char *sp = cp - 1;
	sp = xi_skipws(sp, sp - name, -1); /* Trim trailing ws */
	if (sp != NULL)
	    sp[1] = '\0';
	*cp++ = '\0';		/* NUL-terminate name */
    }

    char *value = xi_skipws(cp, cp - endp, 1);
    if (value == NULL || value[1] == '\0')
	return "invalid attribute; missing name";

    char quote = *value++; /* Record and skip leading quote character */
    cp = memchr(value, quote, value - endp);
    if (cp == NULL)
	return "invalid attribute; missing trailing quote";

    if (valuep != NULL) {	/* Don't touch buffer if valuep is NULL */
	*cp++ = '\0';		/* NUL-terminate value */
	*valuep = value;
    }

    *content = cp;		/* Record next starting point */
    *namep = name;
    return NULL;
}

static void
xi_insert_attribs_extract (xi_parse_t *parsep, xi_node_t *nodep, char *attrib)
{
    xi_insert_t *xip = parsep->xp_insert;
    pa_arb_t *prp = xip->xi_tree->xt_workspace->xw_textpool;
    size_t len = strlen(attrib);
    char *content = attrib, *endp = content + len, *name, *value;
    pa_atom_t name_atom, value_atom, attrib_atom;
    int hit = FALSE;
    const char *msg;

    for (;;) {
	msg = xi_parse_next_attrib(&content, endp, &name, &value);
	if (msg) {
	    xi_source_failure(parsep->xp_srcp, 0, msg);
	    break;
	}
	if (content == NULL)
	    break;		/* Normal end-of-attributes detected */

	name_atom = xi_namepool_atom(xip->xi_tree->xt_workspace, name, TRUE);
	if (name_atom == PA_NULL_ATOM)
	    break;

	value_atom = pa_arb_alloc_string(prp, value);
	if (value_atom == PA_NULL_ATOM)
	    break;

	attrib_atom = xi_insert_node(xip, "xi_insert_attribs_extract",
				     name, strlen(name),
				     XI_TYPE_ATTRIB, name_atom, value_atom);
	if (attrib_atom == PA_NULL_ATOM) {
	    xi_source_failure(parsep->xp_srcp, 0, "attribute insert failed");
	    pa_arb_free_atom(prp, value_atom);
	    break;
	}

	hit = TRUE;
    }

    /* Mark the attributes as present and extracted */
    if (hit)
	nodep->xn_flags |= XNF_ATTRIBS_PRESENT | XNF_ATTRIBS_EXTRACTED;
}

static void
xi_insert_open (xi_parse_t *parsep, pa_atom_t name_atom,
		      const char *prefix UNUSED,
		      const char *name, char *attribs,
		      xi_action_type_t type)
{
    xi_insert_t *xip = parsep->xp_insert;

    if (name_atom == PA_NULL_ATOM)
	return;

    pa_atom_t node_atom;
    node_atom = xi_insert_node(xip, "xi_insert_open",
			       name, strlen(name),
			       XI_TYPE_ELT, name_atom, PA_NULL_ATOM);
    if (node_atom == PA_NULL_ATOM)
	return;

    xi_node_t *nodep = xi_node_addr(xip->xi_tree->xt_workspace, node_atom);

    /* Push our node on the stack */
    xi_insert_push(xip, node_atom, nodep);

    if (attribs) {
	if (type == XIA_SAVE_ATSTR) /* Save as string */
	    xi_insert_attribs(parsep, nodep, attribs);
	else if (type == XIA_SAVE_ATTRIB) /* Save as parsed attributes */
	    xi_insert_attribs_extract(parsep, nodep, attribs);
    }
}

static void
xi_insert_close (xi_parse_t *parsep, const char *prefix UNUSED, const char *name)
{
    xi_insert_t *xip = parsep->xp_insert;
    pa_atom_t name_atom;

    name_atom = xi_namepool_atom(xip->xi_tree->xt_workspace, name, FALSE);
    
    slaxLog("xi_insert_close: [%s] %u (depth %u)", name, name_atom,
	   xip->xi_depth);

    if (name_atom == PA_NULL_ATOM) {
	xi_source_failure(parsep->xp_srcp, 0, "close tag failed: %s", name);
	return;
    }

    xi_istack_t *xsp = &xip->xi_stack[xip->xi_depth];

    if (xip->xi_depth == 0 || xsp->xs_node == NULL
	|| xsp->xs_node->xn_name != name_atom) {
	xi_source_failure(parsep->xp_srcp, 0, "close doesn't match: %s", name);
	return;
    }

    bzero(xsp, sizeof(*xsp));
    xi_insert_pop(xip);
}

static void
xi_insert_text (xi_parse_t *parsep, const char *data, size_t len)
{
    xi_insert_t *xip = parsep->xp_insert;
    pa_arb_t *prp = xip->xi_tree->xt_workspace->xw_textpool;
    pa_atom_t data_atom = pa_arb_alloc(prp, len + 1);
    char *cp = pa_arb_atom_addr(prp, data_atom);

    if (cp == NULL)
	return;

    memcpy(cp, data, len);
    cp[len] = '\0';

    pa_atom_t node_atom;
    node_atom = xi_insert_node(xip, "xi_insert_text", data, len,
			       XI_TYPE_TEXT, PA_NULL_ATOM, data_atom);
    if (node_atom == PA_NULL_ATOM) {
	pa_arb_free_atom(prp, data_atom);
	return;
    }
}

int
xi_parse (xi_parse_t *parsep)
{
    xi_source_t *srcp = parsep->xp_srcp;
    char *data, *rest, *localp;
    xi_node_type_t type;
    int opt_quiet = 0;
    int opt_unescape = 0;
    pa_atom_t name_atom;
    xi_rule_t *rulep;
    xi_insert_t *xip = parsep->xp_insert;

    for (;;) {

	type = xi_source_next_token(srcp, &data, &rest);

	switch (type) {
	case XI_TYPE_NONE:	/* Unknown type */
	    return 1;

	case XI_TYPE_EOF:	/* End of file */
	    return 0;

	case XI_TYPE_FAIL:	/* Failure mode */
	    return -1;

	case XI_TYPE_TEXT:	/* Text content */
	    if (!opt_quiet) {
		int len;
		if (opt_unescape && data && rest)
		    len = xi_source_unescape(srcp, data, rest - data);
		else len = rest - data;
		slaxLog("text [%.*s]", len, data);
	    }
	    xi_insert_text(parsep, data, rest - data);
	    break;

	case XI_TYPE_OPEN:	/* Open tag */
	case XI_TYPE_EMPTY:	/* Empty tag */
	    if (!opt_quiet)
		slaxLog("open tag [%s] [%s]", data ?: "", rest ?: "");
	    localp = strchr(data, ':');
	    if (localp)
		*localp++ = '\0';
	    else {
		localp = data;
		data = NULL;
	    }

	    /* We need an atom to do the indexing to find rules */
	    name_atom = xi_namepool_atom(xip->xi_tree->xt_workspace,
					 localp, TRUE);

	    /*
	     * We've got incoming data; find out what to do with it
	     */
	    xi_rstate_t *statep = xi_parse_stack_state(parsep);
	    rulep = xi_rulebook_find(parsep, parsep->xp_rulebook,
				     statep,
				     name_atom, data, localp, rest);
	    if (rulep == NULL)    /* No rule means use the default rule */
		rulep = &parsep->xp_default_rule;

	    xi_action_type_t act = rulep->xr_action;

	    switch (act) {
	    case XIA_SAVE:
		xi_insert_open(parsep, name_atom, data, localp, rest, act);
		break;

	    case XIA_SAVE_ATSTR:
		xi_insert_open(parsep, name_atom, data, localp, rest, act);
		break;

	    case XIA_SAVE_ATTRIB:
		xi_insert_open(parsep, name_atom, data, localp, rest, act);
		break;

	    case XIA_DISCARD:
		/* XXX */
		break;
	    }

	    /* An empty tag is an open and a close */
	    if (type == XI_TYPE_EMPTY)
		xi_insert_close(parsep, data, localp);
	    break;

	case XI_TYPE_CLOSE:	/* Close tag */
	    if (!opt_quiet)
		slaxLog("close tag [%s] [%s]", data ?: "", rest ?: "");
	    localp = strchr(data, ':');
	    if (localp)
		*localp++ = '\0';
	    else {
		localp = data;
		data = NULL;
	    }
	    xi_insert_close(parsep, data, localp);
	    break;

	case XI_TYPE_PI:	/* Processing instruction */
	    if (!opt_quiet)
		slaxLog("pi [%s] [%s]", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_DTD:	/* DTD nonsense */
	    if (!opt_quiet)
		slaxLog("dtd [%s] [%s]", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_COMMENT:	/* Comment */
	    if (!opt_quiet)
		slaxLog("comment [%s] [%s]", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_CDATA:	/* cdata */
	    if (!opt_quiet)
		slaxLog("cdata [%.*s]", (int)(rest - data), data);
	    break;
	}
    }

    return 0;
}

static int
xi_parse_dump_cb (xi_parse_t *parsep UNUSED, xi_node_type_t type,
		  xi_node_t *nodep UNUSED,
		  const char *data, void *opaque UNUSED)
{
    slaxLog("node: [%p] type %u, depth %u",
	    nodep, nodep ? nodep->xn_type : 0, nodep ? nodep->xn_depth : 0);

    switch (type) {
    case XI_TYPE_ROOT:
	slaxLog("(root)");
	break;

    case XI_TYPE_ELT:
	slaxLog("element: %s", data ?: "[error]");
	break;

    case XI_TYPE_TEXT:
	slaxLog("text: %s", data ?: "[error]");
	break;

    case XI_TYPE_ATTRIB:
	slaxLog("[attrib %s]\n", data ?: "[error]");
	break;
    }

    return 0;
}

void
xi_parse_dump (xi_parse_t *parsep)
{
    xi_parse_emit(parsep, xi_parse_dump_cb, NULL);
}

static int
xi_parse_emit_xml_cb (xi_parse_t *parsep UNUSED, xi_node_type_t type,
		      xi_node_t *nodep UNUSED,
		      const char *data, void *opaque)
{
    FILE *out = opaque;
    const char *cp;

    switch (type) {
    case XI_TYPE_ROOT:
	fprintf(out, "<!-- start of output>\n");
	break;

    case XI_TYPE_OPEN:
	fprintf(out, "<%s>", data);
	break;

    case XI_TYPE_CLOSE:
	if (data)
	    fprintf(out, "</%s>\n", data);
	break;

    case XI_TYPE_TEXT:
	fprintf(out, "%s", data);
	break;

    case XI_TYPE_ATSTR:
	fprintf(out, "<attrib>%s</>", data);
	break;

    case XI_TYPE_ATTRIB:
	cp = xi_namepool_string(parsep->xp_insert->xi_tree->xt_workspace,
				     nodep->xn_name);
	fprintf(out, "[attrib %s=%s]\n", cp, data);
	break;

    case XI_TYPE_EOF:
	fprintf(out, "<!-- end of output>\n");
	break;
    }

    return 0;
}

void
xi_parse_emit_xml (xi_parse_t *parsep, FILE *out)
{
    xi_parse_emit(parsep, xi_parse_emit_xml_cb, out);
}

void
xi_parse_emit (xi_parse_t *parsep, xi_parse_emit_fn func, void *opaque)
{
    xi_insert_t *xip = parsep->xp_insert;
    xi_tree_t *xtp = xip->xi_tree;
    xi_workspace_t *xwp = xtp->xt_workspace;
    const char *cp;
    pa_atom_t node_atom = xtp->xt_root;
    pa_atom_t next_node_atom;
    xi_node_t *nodep;
    xi_depth_t last_depth = 0;

    while (node_atom != PA_NULL_ATOM) {
	nodep = xi_node_addr(xwp, node_atom);
	if (nodep == NULL) {
	    slaxLog("null atom");
	    break;
	}

	if (last_depth && last_depth > nodep->xn_depth) {
	    cp = xi_namepool_string(xwp, nodep->xn_name);
	    func(parsep, XI_TYPE_CLOSE, nodep, cp, opaque);
	    node_atom = nodep->xn_next;
	    last_depth = nodep->xn_depth;
	    continue;
	}

	if (nodep->xn_type == XI_TYPE_ROOT) {
	    next_node_atom = nodep->xn_contents ?: nodep->xn_next;
	    func(parsep, nodep->xn_type, nodep, NULL, opaque);

	} else if (nodep->xn_type == XI_TYPE_ELT) {
	    cp = xi_namepool_string(xwp, nodep->xn_name);
	    next_node_atom = nodep->xn_contents ?: nodep->xn_next;
	    func(parsep, nodep->xn_type, nodep, cp, opaque);

	} else if (nodep->xn_type == XI_TYPE_TEXT) {
	    cp = xi_textpool_string(xwp, nodep->xn_contents);
	    next_node_atom = nodep->xn_next;
	    func(parsep, nodep->xn_type, nodep, cp, opaque);

	} else if (nodep->xn_type == XI_TYPE_ATSTR) {
	    cp = pa_arb_atom_addr(xwp->xw_textpool, nodep->xn_contents);
	    next_node_atom = nodep->xn_next;
	    func(parsep, nodep->xn_type, nodep, cp, opaque);

	} else if (nodep->xn_type == XI_TYPE_ATTRIB) {
	    cp = pa_arb_atom_addr(xwp->xw_textpool, nodep->xn_contents);
	    next_node_atom = nodep->xn_next;
	    func(parsep, nodep->xn_type, nodep, cp, opaque);

	} else {
	    slaxLog("unhandled node: %u", nodep->xn_type);
	    next_node_atom = PA_NULL_ATOM;
	}

	last_depth = nodep->xn_depth;
	node_atom = next_node_atom;
    }

    func(parsep, XI_TYPE_EOF, NULL, NULL, opaque);
}


void
xi_parse_set_rulebook (xi_parse_t *parsep, xi_rulebook_t *rulebook)
{
    parsep->xp_rulebook = rulebook;

    xi_insert_t *xip = parsep->xp_insert;
    xip->xi_stack[xip->xi_depth].xs_statep = rulebook
	? xi_rulebook_state(rulebook, XI_STATE_INITIAL) : NULL;
}

void
xi_parse_set_default_rule (xi_parse_t *parsep, xi_action_type_t type)
{
    parsep->xp_default_rule.xr_flags = XRF_MATCH_ALL;
    parsep->xp_default_rule.xr_action = type;
}
