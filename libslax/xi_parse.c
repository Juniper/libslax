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
    nodep->xn_ns_map = PA_NULL_ATOM;
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
    /* We reuse the current rule state */
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
    nodep->xn_ns_map = PA_NULL_ATOM;
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

/*
 * Insert a namespace node after the given "last" position
 */
static pa_atom_t *
xi_insert_ns_node (xi_insert_t *xip, const char *msg,
		   const char *data, size_t len, pa_atom_t parent_atom,
		   pa_atom_t *lastp,
		   xi_node_type_t type, pa_atom_t name_atom,
		   pa_atom_t contents)
{
    pa_atom_t node_atom;
    xi_node_t *nodep = xi_node_alloc(xip->xi_tree->xt_workspace, &node_atom);
    if (nodep == NULL)
	return NULL;

    /* Initialize our fields */
    nodep->xn_type = type;
    nodep->xn_ns_map = PA_NULL_ATOM;
    nodep->xn_name = name_atom;
    nodep->xn_contents = contents;

    slaxLog("%s: [%.*s] %u / %u (depth %u)", msg, len, data,
	    name_atom, contents, xip->xi_depth + 1);

    nodep->xn_next = (*lastp == PA_NULL_ATOM) ? parent_atom : *lastp;
    *lastp = node_atom;
    lastp = &nodep->xn_next;

    /* Mark the "last" as us */
    xi_istack_t *xsp = &xip->xi_stack[xip->xi_depth];
    xsp->xs_last_atom = node_atom;
    xsp->xs_last_node = nodep;

    /* Set our depth */
    nodep->xn_depth = xip->xi_depth + 1;

    /* Update xi_maxdepth */
    if (nodep->xn_depth > xip->xi_maxdepth)
	xip->xi_maxdepth = nodep->xn_depth;

    return lastp;
}

/*
 * Find the parent of a node; not cheap.  Search the list of "xn_next"
 * until we find a node of differing depth.
 */
static inline xi_node_t *
xi_node_parent (xi_workspace_t *xwp, xi_node_t *nodep)
{
    xi_node_t *nextp;
    xi_depth_t depth = nodep->xn_depth;

    for (; nodep->xn_next != PA_NULL_ATOM; nodep = nextp) {
	nextp = xi_node_addr(xwp, nodep->xn_next);
	if (nextp == NULL)
	    break;		/* Should not occur */

	if (nextp->xn_depth < depth)
	    return nextp;
    }

    return NULL;
}

/*
 * We follow each node up the hierarchy, looking at each child.  When
 * we're past the namespace nodes, we move on.  Then we have follow
 * the chain of siblings to find our parent.  If we get to the root,
 * we're done.
 */
static pa_atom_t
xi_parse_find_ns_atom (xi_parse_t *parsep, xi_node_t *nodep,
		       pa_atom_t pref_atom)
{
    xi_workspace_t *xwp = parsep->xp_insert->xi_tree->xt_workspace;
    xi_node_t *curp, *childp;
    xi_ns_map_t *ns_map;

    for (curp = nodep; curp; curp = xi_node_parent(xwp, curp)) {
	for (childp = xi_node_addr(xwp, curp->xn_contents); childp;
	     childp = xi_node_addr(xwp, childp->xn_next)) {
	    if (childp->xn_type != XI_TYPE_NS)
		break;		/* Done with namespaces */

	    /* The namespace mapping number is in the node's contents */
	    ns_map = xi_ns_map_addr(xwp, childp->xn_contents);
	    if (ns_map != NULL && ns_map->xnm_prefix == pref_atom)
		return childp->xn_contents; /* Match! */
 	}
    }

    return PA_NULL_ATOM;
}

/*
 * Find a namespace mapping for the given prefix and return it.  We are
 * forced to search upward thru the hierarchy to find the mapping, which
 * is expensive, but this operation will mostly naturally be done as the
 * tree is being built, so the number of trailing subling nodes should be
 * very low.
 */
static pa_atom_t
xi_parse_find_ns (xi_parse_t *parsep, xi_node_t *nodep, const char *prefix)
{
    xi_workspace_t *xwp = parsep->xp_insert->xi_tree->xt_workspace;
    pa_atom_t pref_atom;

    if (prefix == NULL) {
	/* If the prefix is NULL, we're looking for the default prefix */
	pref_atom = PA_NULL_ATOM;

    } else {
	/*
	 * Find the atom for the prefix; if there isn't one, then it
	 * cannot have been defined, which is likely a syntax error.
	 */
	pref_atom = xi_namepool_atom(xwp, prefix, FALSE);
	if (pref_atom == PA_NULL_ATOM)
	    return PA_NULL_ATOM;
    }

    return xi_parse_find_ns_atom(parsep, nodep, pref_atom);
}

static inline xi_boolean_t
xi_parse_is_attrib (xi_node_type_t type)
{
    if (type == XI_TYPE_ATSTR || type == XI_TYPE_ATTRIB
	|| type == XI_TYPE_NS)
	return TRUE;
    return FALSE;
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
		      char **namep, size_t *namelenp,
		      char **valuep, size_t *valuelenp)
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

    /* Trim space off end of attribute name */
    size_t namelen = cp - name;
    char *sp = cp - 1;
    sp = xi_skipws(sp, sp - name, -1); /* Trim trailing ws */
    if (sp != NULL)
	namelen = &sp[1] - name;

    cp += 1;			/* Move over '=' */

    char *value = xi_skipws(cp, cp - endp, 1);
    if (value == NULL || value[1] == '\0')
	return "invalid attribute; missing value";

    char quote = *value++; /* Record and skip leading quote character */
    cp = memchr(value, quote, value - endp);
    if (cp == NULL)
	return "invalid attribute; missing trailing quote";

    /* Fill in the caller's value */
    *valuelenp = cp - value;
    *valuep = value;
    *content = cp + 1;		/* Move over the closing quote */
    *namep = name;
    *namelenp = namelen;

    return NULL;
}

/*
 * Extract attributes into proper nodes.  Loop through the input
 * string, parsing out attributes (name=value), and generating
 * namespace and attribute nodes.  Namespaces (XI_TYPE_NS) are handled
 * distinctly from other attributes (XI_TYPE_ATTRIB).  For namespaces,
 * the xn_name is the PA_NULL_ATOM and the xn_contents is the prefix
 * mapping, which is an index into the prefix mapping table, providing
 * some reuse of prefix-to-uri relationships.  For attributes, the
 * xn_name is the name (an index into the namepool) and the
 * xn_contents is the value (an index into the string table).
 *
 * Namespaces are a pain, but a necessary one; they are handled
 * differently from other attributes, in that they use the namepool
 * for both their prefixes and their values, since we assume the
 * strings will continue to appear.  It also allows us to compare
 * namespace URIs by comparing atom numbers, rather than strcmp.  We
 * record the prefix-to-namespace mapping in the ns_map, and then
 * record that mapping as the value (xn_contents) of the XI_TYPE_NS
 * node.
 *
 * For attributes, our "name" can be a prefix:local-name so we need to
 * look for a ':' to know.  If we find one, we find an atom number for
 * that prefix and record it in the node.  We'll come back later and
 * turn this into a proper prefix mapping, but at this point, we might
 * not have seen the namespace definition for this prefix.  This can
 * occur legally, since XML attributes are defined as unordered:
 *
 *    <a b:foo="x" xmlns:b="b.org"/>
 *
 * So we're forced to whiffle thru the attributes twice, once to build
 * them and once to ns_map them.  Note: This is sad, since it means that
 * we have to allocate a node just to hold our prefix atom until we have
 * processed all attributes and can safely perform the prefix mapping.
 *
 * With this long a comment, you're sure to realize this is a tricky
 * part, right?
 */
static void
xi_insert_attribs_extract (xi_parse_t *parsep, pa_atom_t node_atom,
			   xi_node_t *nodep, char *attrib,
			   xi_boolean_t only_ns)
{
    xi_insert_t *xip = parsep->xp_insert;
    xi_workspace_t *xwp = xip->xi_tree->xt_workspace;
    pa_arb_t *prp = xwp->xw_textpool;
    size_t len = strlen(attrib);
    char *content = attrib, *endp = content + len, *name, *value;
    size_t namelen, valuelen;
    pa_atom_t name_atom, value_atom, attrib_atom, stash_atom;
    int hit = FALSE;
    const char *msg;
    pa_atom_t *last_nsp = &nodep->xn_contents; /* XXX For freshly made node */

    for (;;) {
	msg = xi_parse_next_attrib(&content, endp, &name, &namelen,
				   &value, &valuelen);
	if (msg) {
	    xi_source_failure(parsep->xp_srcp, 0, msg);
	    break;
	}
	if (content == NULL)
	    break;		/* Normal end-of-attributes detected */

	if (name == NULL || value == NULL)
	    break;		/* Should not occur */

	name[namelen] = '\0'; /* NUL-terminate our name */
	value[valuelen] = '\0'; /* NUL-terminate our value */

	/* Namespace attributes start with "xmlns" */
	static const char xmlns[] = "xmlns";
	size_t xmlns_len = sizeof(xmlns) - 1;

	/*
	 * Is it a namespace?  Does it start with the magic leading "xmlns"
	 * string?
	 */
	if (name != NULL && strncmp(name, xmlns, xmlns_len) == 0) {
	    /* Skip the "xmlns:?" leading string */
	    name += xmlns_len;
	    if (*name == ':')
		name += 1;	/* Skip over the ':' */
	    if (*name == '\0')
		name = NULL;	/* Empty prefix == the "default" namespace */
	    if (*value == '\0')
		value = NULL;	/* Empty value == the "null" namespace */

	    pa_atom_t ns_atom = xi_ns_find(xwp, name, value, TRUE);
	    if (ns_atom == PA_NULL_ATOM) {
		xi_source_failure(parsep->xp_srcp, 0,
				  "namespace create/find failed");
		break;
	    }

	    last_nsp = xi_insert_ns_node(xip, "xi_insert_attribs_extract(ns)",
					 name, name ? strlen(name) : 0,
					 node_atom, last_nsp,
					 XI_TYPE_NS, PA_NULL_ATOM, ns_atom);
	    if (last_nsp == PA_NULL_ATOM) {
		xi_source_failure(parsep->xp_srcp, 0,
				  "attribute insert (ns) failed");
		break;
	    }

	} else if (only_ns) {
	    continue;		/* Skip other attributes */

	} else {
	    char *prefix;
	    pa_atom_t pref_atom;
	    char *localp = strchr(name, ':');
	    if (localp) {
		*localp++ = '\0';
		prefix = name;
		pref_atom = xi_namepool_atom(xwp, prefix, TRUE);
	    } else {
		localp = name;
		prefix = NULL;
		pref_atom = PA_NULL_ATOM;
	    }

	    /* Normal attribute */
	    name_atom = xi_namepool_atom(xwp, localp, TRUE);
	    if (name_atom == PA_NULL_ATOM)
		break;

	    value_atom = pa_arb_alloc_string(prp, value);
	    if (value_atom == PA_NULL_ATOM)
		break;

	    attrib_atom = xi_insert_node(xip, "xi_insert_attribs_extract",
				 name, strlen(name),
				 XI_TYPE_ATTRIB, name_atom, value_atom);
	    if (attrib_atom == PA_NULL_ATOM) {
		xi_source_failure(parsep->xp_srcp, 0,
				  "attribute insert failed");
		pa_arb_free_atom(prp, value_atom);
		break;
	    }

	    if (pref_atom != PA_NULL_ATOM) {
		/*
		 * We have to stash our prefix atom in a special
		 * temporary node of type XI_TYPE_NSPREF.  After all
		 * the attributes are processed and all namespaces
		 * have been defined, we'll loop thru and set
		 * real ns_map values.
		 */
		stash_atom = xi_insert_node(xip,
				 "xi_insert_attribs_extract (stash)",
				 name, strlen(name),
				 XI_TYPE_NSPREF, PA_NULL_ATOM, pref_atom);
		if (stash_atom == PA_NULL_ATOM) {
		    xi_source_failure(parsep->xp_srcp, 0,
				      "attribute (stash) insert failed");
		    pa_arb_free_atom(prp, value_atom);
		    break;
		}
	    }
	}

	hit = TRUE;
    }

    /*
     * We've parse namespaces as part of the attribute handling, so
     * now we have to use them.  When we find an XI_TYPE_NSPREF
     * attribute, the xn_contents are the atom of a prefix string.  We
     * finish that off, finding the real mapping and recording it,
     * discarding the NSPREF node.
     */
    xi_node_t *childp, *prev = NULL;
    pa_atom_t ns_atom;

    for (childp = xi_node_addr(xwp, nodep->xn_contents); childp;
	 childp = xi_node_addr(xwp, childp->xn_next)) {
	if (nodep->xn_type == XI_TYPE_NS) {
	    /* Skip namespace defs */

	} else if (!xi_parse_is_attrib(nodep->xn_type)) {
	    break;		/* End of attributes == done */

	} else if (prev == NULL) {
	    /* Can't handle not having a previous node */

	} else if (childp->xn_type == XI_TYPE_NSPREF) {
	    /*
	     * An XI_TYPE_NSPREF node means the previous node needs an
	     * accurate name mapping.  We'll find one and discard the
	     * current node.
	     */
	    ns_atom = xi_parse_find_ns_atom(parsep, nodep, childp->xn_contents);
	    if (ns_atom == PA_NULL_ATOM) {
		const char *prefix = xi_namepool_string(xwp, childp->xn_contents);
		xi_source_failure(parsep->xp_srcp, 0,
				  "namespace mapping not found for %s:%s",
				  prefix ?: "", name);
	    }

	    /* Set the namespace mapping */
	    prev->xn_ns_map = ns_atom; /* Assign mapping */
	    prev->xn_next = childp->xn_next; /* Remove node from list */

	    xi_node_free(xwp, prev->xn_next); /* Free node */
	    childp = prev;		   /* childp is dead; resume logic */
	}

	prev = childp;
    }

    /* Mark the attributes as present and extracted */
    if (hit)
	nodep->xn_flags |= XNF_ATTRIBS_PRESENT | XNF_ATTRIBS_EXTRACTED;
}

static void
xi_insert_open (xi_parse_t *parsep, pa_atom_t name_atom,
		const char *prefix, const char *name, char *attribs,
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
	enum { SAVE_NONE, SAVE_NS, SAVE_STRING, SAVE_FULL } save = SAVE_NONE;

	/*
	 * Do we need only namespaces?  Should we do full attributes?
	 *
	 * Use a trivial strstr() test for namespaces; might not be
	 * true, but it's dirt cheap.  We know that namespaces are
	 * either "xmlns:pref='url'" or "xmlns='url'", so we're sure
	 * not to miss one.
	 *
	 * Past that, it depends on the "act", but if we're asked
	 * to save-attributes-as-string (XIA_SAVE_ATSTR) and we
	 * see a namespace, we force the full save.
	 */
	if (type == XIA_SAVE_ATTRIB) {
	    save = SAVE_FULL;
	} else if (strstr(attribs, XI_XMLNS_LEADER) == NULL) {
	    if (type == XIA_SAVE_ATSTR)
		save = SAVE_STRING;
	} else if (type == XIA_SAVE_ATSTR) {
	    save = SAVE_FULL;
	} else {
	    save = SAVE_NS;
	}

	if (save == SAVE_STRING) /* Save as string */
	    xi_insert_attribs(parsep, nodep, attribs);
	else if (save != SAVE_NONE) /* Save as parsed attributes */
	    xi_insert_attribs_extract(parsep, node_atom, nodep, attribs,
				      (save == SAVE_NS) ? TRUE : FALSE);
    }

    if (prefix != NULL) {
	nodep->xn_ns_map = xi_parse_find_ns(parsep, nodep, prefix);
	if (nodep->xn_ns_map == PA_NULL_ATOM)
	    xi_source_failure(parsep->xp_srcp, 0,
			      "namespace mapping not found for %s:%s",
			      prefix, name);
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

    if (xip->xi_depth == 0 || xsp->xs_node == NULL) {
	xi_source_failure(parsep->xp_srcp, 0,
			  "close for open that doesn't exist: %s", name);
	return;
    } else if (xsp->xs_old_name != 0) {
	if (xsp->xs_old_name != name_atom) {
	    xi_source_failure(parsep->xp_srcp, 0,
			      "close doesn't match original: %s", name);
	    return;
	}
    } else if (xsp->xs_node->xn_name != name_atom) {
	xi_source_failure(parsep->xp_srcp, 0, "close doesn't match: %s", name);
	return;
    }

    bzero(xsp, sizeof(*xsp));
    xi_insert_pop(xip);
}

static void
xi_insert_text (xi_parse_t *parsep, const char *data, size_t len,
		xi_node_type_t type)
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
			       type, PA_NULL_ATOM, data_atom);
    if (node_atom == PA_NULL_ATOM) {
	pa_arb_free_atom(prp, data_atom);
	return;
    }
}

static void
xi_parse_handle_rule (xi_parse_t *parsep, pa_atom_t name_atom,
		      const char *prefix UNUSED, const char *name,
		      char *attribs, xi_rule_t *xrp)
{
    xi_insert_t *xip = parsep->xp_insert;
    xi_action_type_t act = xrp->xr_action;
    pa_atom_t use_tag = xrp->xr_use_tag;
    pa_atom_t save_name_atom = name_atom;

    /* Use a different tag is directed */
    if (use_tag)
	name_atom = use_tag;

    switch (act) {
    case XIA_SAVE:
    case XIA_SAVE_ATSTR:
    case XIA_SAVE_ATTRIB:
	xi_insert_open(parsep, name_atom, prefix, name, attribs, act);
	break;

    case XIA_EMIT:
    case XIA_DISCARD:
	/* XXX */
	break;
    }

    if (use_tag) {
	xi_istack_t *xsp = &xip->xi_stack[xip->xi_depth];
	xsp->xs_old_name = save_name_atom;
    }
}

int
xi_parse (xi_parse_t *parsep)
{
    xi_source_t *srcp = parsep->xp_srcp;
    char *data, *rest, *localp;
    xi_node_type_t type;
    xi_boolean_t opt_quiet = XI_BIT_TEST(parsep->xp_flags, XI_PF_DEBUG);
    xi_boolean_t opt_unescape = 0;
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
	    type = XI_TYPE_UNESC; /* UNESC (aka CDATA) is unescaped text */
	    if (!opt_quiet) {
		int len;
		if (opt_unescape && data && rest) {
		    len = xi_source_unescape(srcp, data, rest - data);
		    type = XI_TYPE_TEXT; /* TEXT is escaped */
		} else {
		    len = rest - data;
		}
		slaxLog("text [%.*s] (%u)", len, data, type);
	    }
	    xi_insert_text(parsep, data, rest - data, type);
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

	    /*
	     * No rule (or no rulebook) means use the default rule, which
	     * will likely make us save everything, just in case.
	     */
	    if (rulep == NULL)
		rulep = &parsep->xp_default_rule;

	    /*
	     * This is where the real work is done, performing any
	     * action described in the rule.
	     */
	    xi_parse_handle_rule(parsep, name_atom, data, localp,
				 rest, rulep);

	    /*
	     * An empty tag is an open and a close, since we've already
	     * done the parsing, we can't just "fallthru" to the close
	     * logic, so we call it directly ourselves.
	     */
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

	case XI_TYPE_UNESC:	/* unescaped/cdata */
	    if (!opt_quiet)
		slaxLog("cdata [%.*s]", (int)(rest - data), data);
	    break;
	}
    }

    return 0;
}

static const char *xi_type_names[] = {
    "NONE",
    "EOF",
    "SKIP",
    "FAIL",
    "ROOT",
    "TEXT",
    "UNESC",
    "OPEN",
    "CLOSE",
    "EMPTY",
    "PI",
    "DTD",
    "COMMENT",
    "ATSTR",
    "ATTRIB",
    "EOL_ATTRIB",
    "EOL_EMPTY",
    "NS",
    "NSPREF",
    NULL
};

void
xi_node_dump (xi_workspace_t *xwp, xi_node_type_t op,
	      xi_node_t *nodep, pa_atom_t atom)
{
    if (atom != PA_NULL_ATOM)
	nodep = xi_node_addr(xwp, atom);
    if (nodep == NULL)
	return;

    const char *name = xi_namepool_string(xwp, nodep->xn_name);
    xi_ns_map_t *ns_map = xi_ns_map_addr(xwp, nodep->xn_ns_map);
    const char *pref = ns_map ?
	xi_namepool_string(xwp, ns_map->xnm_prefix) : NULL;
    const char *uri = ns_map ? xi_namepool_string(xwp, ns_map->xnm_uri) : NULL;
    const char *type = (nodep->xn_type < XI_NUM_ELTS(xi_type_names) - 1)
	? xi_type_names[nodep->xn_type] : "unknown";
    const char *opname = (op < XI_NUM_ELTS(xi_type_names) - 1)
	? xi_type_names[op] : "unknown";

    slaxLog("%s%s%snode %u [%p]: type %u(%s), name %u [%s], "
	    "depth %u, flags %#x, "
	    "ns-map %u [%s]=[%s], next %u, contents %u",
	    (op > 0) ? "Op: " : "", (op > 0) ? opname : "",
	    (op > 0) ? ", " : "",
	    atom, nodep, nodep->xn_type, type, nodep->xn_name, name ?: "",
	    nodep->xn_depth, nodep->xn_flags,
	    nodep->xn_ns_map, pref ?: "", uri ?: "", 
	    nodep->xn_next, nodep->xn_contents);
}

static int
xi_parse_dump_cb (xi_parse_t *parsep, xi_node_type_t type,
		  pa_atom_t node_atom, xi_node_t *nodep,
		  const char *data, void *opaque UNUSED)
{
    xi_workspace_t *xwp = parsep->xp_insert->xi_tree->xt_workspace;
    const char *cp;
    xi_ns_map_t *ns_map;

    xi_node_dump(xwp, type, nodep, node_atom);

    switch (type) {
    case XI_TYPE_ROOT:
	slaxLog("(root)");
	break;

    case XI_TYPE_ELT:
	slaxLog("element: [%s]", data ?: "[error]");
	if (nodep->xn_ns_map != PA_NULL_ATOM) {
	    ns_map = xi_ns_map_addr(xwp, nodep->xn_ns_map);
	    if (ns_map != NULL) {
		const char *pref = xi_namepool_string(xwp, ns_map->xnm_prefix);
		const char *uri = xi_namepool_string(xwp, ns_map->xnm_uri);

		slaxLog("element nsmap: [%s]=[%s]", pref ?: "", uri ?: "");
	    } else {
		slaxLog("element nsmap: null");
	    }
	}
	break;

    case XI_TYPE_TEXT:
	slaxLog("text: [%s]", data ?: "[error]");
	break;

    case XI_TYPE_UNESC:		/* Unescaped/cdata */
	slaxLog("cdata: [%s]", data ?: "[error]");
	break;

    case XI_TYPE_ATTRIB:
	cp = xi_parse_namepool_string(parsep, nodep->xn_name);
	slaxLog("attrib: [%s=\"%s\"]", cp, data);
	break;

    case XI_TYPE_NS:
	ns_map = xi_ns_map_addr(xwp, nodep->xn_contents);
	if (ns_map != NULL) {
	    const char *pref = xi_namepool_string(xwp, ns_map->xnm_prefix);
	    const char *uri = xi_namepool_string(xwp, ns_map->xnm_uri);

	    slaxLog("namespace: [%s]=[%s]", pref ?: "", uri ?: "");
	} else {
	    slaxLog("namespace: null");
	}
	break;

    case XI_TYPE_ATSTR:
	slaxLog("atrstr: [%s]", data ?: "[error]");
	break;

    case XI_TYPE_EOL_ATTRIB:
	slaxLog("eol-attrib: %p", nodep);
	break;

    case XI_TYPE_EOL_EMPTY:
	slaxLog("eol-empty: %p", nodep);
	break;

    case XI_TYPE_CLOSE:
	slaxLog("close: [%s]", data ?: "[error]");
	break;
    }

    return 0;
}

void
xi_parse_dump (xi_parse_t *parsep)
{
    xi_parse_emit(parsep, xi_parse_dump_cb, NULL);
}

typedef struct xi_xml_output_s {
    FILE *xx_out;		/* Output file descriptor */
    unsigned xx_indent;		/* Current indent amount */
    unsigned xx_incr;		/* Indent increment */
    xi_node_type_t xx_last_type; /* Last type seen */
} xi_xml_output_t;

static int
xi_parse_emit_xml_cb (xi_parse_t *parsep, xi_node_type_t type,
		      pa_atom_t node_atom UNUSED, xi_node_t *nodep,
		      const char *data, void *opaque)
{
    xi_xml_output_t *xmlp = opaque;
    FILE *out = xmlp->xx_out;
    xi_workspace_t *xwp = parsep->xp_insert->xi_tree->xt_workspace;
    xi_ns_map_t *ns_map;
    const char *cp;
    int indent;
    const char *pref, *uri;

    switch (type) {
    case XI_TYPE_ROOT:
	fprintf(out, "<!-- start of output>\n");
	break;

    case XI_TYPE_OPEN:
	if (xmlp->xx_last_type != XI_TYPE_ROOT
	    && xmlp->xx_last_type != XI_TYPE_CLOSE)
	    fprintf(out, "\n");

	pref = NULL;
	if (nodep->xn_ns_map != PA_NULL_ATOM) {
	    ns_map = xi_ns_map_addr(xwp, nodep->xn_ns_map);
	    if (ns_map != NULL)
		pref = xi_namepool_string(xwp, ns_map->xnm_prefix);
	}

	fprintf(out, "%*s<%s%s%s", xmlp->xx_indent, "",
		pref ?: "", pref ? ":" : "", data);
	xmlp->xx_indent += xmlp->xx_incr;
	break;

    case XI_TYPE_EOL_ATTRIB:
	fprintf(out, ">");
	break;

    case XI_TYPE_EOL_EMPTY:
	fprintf(out, "/>\n");
	break;

    case XI_TYPE_CLOSE:
	xmlp->xx_indent -= xmlp->xx_incr;

	if (data != NULL) {
	    if (xmlp->xx_last_type != XI_TYPE_EOL_EMPTY) {
		pref = NULL;

		if (nodep->xn_ns_map != PA_NULL_ATOM) {
		    ns_map = xi_ns_map_addr(xwp, nodep->xn_ns_map);
		    if (ns_map != NULL)
			pref = xi_namepool_string(xwp, ns_map->xnm_prefix);
		}

		indent = (xmlp->xx_last_type == XI_TYPE_CLOSE)
		    ? xmlp->xx_indent : 0;
		fprintf(out, "%*s</%s%s%s>\n", indent, "",
			pref ?: "", pref ? ":" : "", data);
	    }
	}
	break;

    case XI_TYPE_TEXT:		/* XXX Text needs to be escaped */
    case XI_TYPE_UNESC:
	fprintf(out, "%s", data);
	break;

    case XI_TYPE_ATSTR:
	fprintf(out, " %s", data);
	break;

    case XI_TYPE_ATTRIB:
	cp = xi_namepool_string(parsep->xp_insert->xi_tree->xt_workspace,
				     nodep->xn_name);
	fprintf(out, " %s=\"%s\"", cp, data);
	break;

    case XI_TYPE_NS:
	ns_map = xi_ns_map_addr(xwp, nodep->xn_contents);
	if (ns_map) {
	    pref = xi_namepool_string(xwp, ns_map->xnm_prefix);
	    uri = xi_namepool_string(xwp, ns_map->xnm_uri);
	    fprintf(out, " xmlns%s%s=\"%s\"",
		    pref ? ":" : "", pref ?: "", uri ?: "");
	} else {
	    slaxLog("namespace: [null]");
	}
	break;

    case XI_TYPE_EOF:
	fprintf(out, "<!-- end of output>\n");
	break;
    }

    xmlp->xx_last_type = type;
    return 0;
}

void
xi_parse_emit_xml (xi_parse_t *parsep, FILE *out)
{
    xi_xml_output_t xml;

    bzero(&xml, sizeof(xml));
    xml.xx_out = out;
    xml.xx_incr = 3;

    xi_parse_emit(parsep, xi_parse_emit_xml_cb, &xml);
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
    unsigned need_eol_attrib = FALSE;

    while (node_atom != PA_NULL_ATOM) {
	nodep = xi_node_addr(xwp, node_atom);
	if (nodep == NULL) {
	    slaxLog("xi_parse_emit sees a null atom!");
	    break;
	}

	/* If this is the first non-attrib, let the emitter know */
	if (need_eol_attrib && !xi_parse_is_attrib(nodep->xn_type)) {
	    if (last_depth && last_depth > nodep->xn_depth) {
		func(parsep, XI_TYPE_EOL_EMPTY, node_atom, nodep,
		     NULL, opaque);
	    } else {
		func(parsep, XI_TYPE_EOL_ATTRIB, node_atom, nodep,
		     NULL, opaque);
	    }

	    /* Clear the need flag in case we hit the "if" below */
	    need_eol_attrib = FALSE;
	}

	/* We're looking at the first step out of layer of hierarchy */
	if (last_depth && last_depth > nodep->xn_depth) {
	    cp = xi_namepool_string(xwp, nodep->xn_name);
	    func(parsep, XI_TYPE_CLOSE, node_atom, nodep, cp, opaque);
	    node_atom = nodep->xn_next;
	    last_depth = nodep->xn_depth;
	    continue;
	}

	need_eol_attrib = FALSE; /* Don't need it (yet) */

	if (nodep->xn_type == XI_TYPE_ROOT) {
	    next_node_atom = nodep->xn_contents ?: nodep->xn_next;
	    func(parsep, nodep->xn_type, node_atom, nodep, NULL, opaque);

	} else if (nodep->xn_type == XI_TYPE_ELT) {
	    cp = xi_namepool_string(xwp, nodep->xn_name);
	    func(parsep, nodep->xn_type, node_atom, nodep, cp, opaque);

	    /*
	     * If an ELT's contents are NULL, then this is an empty ELT.
	     * Otherwise we follow them to visit the children.  We have
	     * to handle this case explicitly, since there's not depth
	     * change to trigger the normal EMPTY logic above.
	     */
	    if (nodep->xn_contents == PA_NULL_ATOM) {
		next_node_atom = nodep->xn_next;
		func(parsep, XI_TYPE_EOL_EMPTY, node_atom, nodep,
		     NULL, opaque);
		func(parsep, XI_TYPE_CLOSE, node_atom, nodep, NULL, opaque);
	    } else {
		need_eol_attrib = TRUE;
		next_node_atom = nodep->xn_contents;
	    }

	} else if (nodep->xn_type == XI_TYPE_TEXT
		   || nodep->xn_type == XI_TYPE_UNESC) {
	    cp = xi_textpool_string(xwp, nodep->xn_contents);
	    next_node_atom = nodep->xn_next;
	    func(parsep, nodep->xn_type, node_atom, nodep, cp, opaque);

	} else if (nodep->xn_type == XI_TYPE_ATSTR) {
	    cp = pa_arb_atom_addr(xwp->xw_textpool, nodep->xn_contents);
	    next_node_atom = nodep->xn_next;
	    func(parsep, nodep->xn_type, node_atom, nodep, cp, opaque);
	    need_eol_attrib = TRUE;

	} else if (nodep->xn_type == XI_TYPE_ATTRIB) {
	    cp = pa_arb_atom_addr(xwp->xw_textpool, nodep->xn_contents);
	    next_node_atom = nodep->xn_next;
	    func(parsep, nodep->xn_type, node_atom, nodep, cp, opaque);
	    need_eol_attrib = TRUE;

	} else if (nodep->xn_type == XI_TYPE_NS) {
	    next_node_atom = nodep->xn_next;
	    func(parsep, nodep->xn_type, node_atom, nodep, NULL, opaque);
	    need_eol_attrib = TRUE;

	} else {
	    slaxLog("unhandled node: %u", nodep->xn_type);
	    next_node_atom = PA_NULL_ATOM;
	}

	node_atom = next_node_atom;
	last_depth = nodep->xn_depth;
    }

    func(parsep, XI_TYPE_EOF, PA_NULL_ATOM, NULL, NULL, opaque);
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
