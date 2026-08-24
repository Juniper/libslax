/*
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
 * what to do with that input, and then doing it.  Our "pin_source" module
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
#include <libpsu/psulog.h>
#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>
#include <parrotdb/paarb.h>
#include <parrotdb/paistr.h>
#include <parrotdb/papat.h>
#include <parrotdb/pabitmap.h>

#include <libpin/pin_common.h>
#include <libpin/pin_source.h>
#include <libpin/pin_rules.h>
#include <libpin/pin_body.h>
#include <libpin/pin_tree.h>
#include <libpin/pin_workspace.h>
#include <libpin/pin_parse.h>

/* Forward declaration: defined after pin_insert_text */
static void pin_body_exec_advance(pin_parse_t *parsep);

#include <libxo/xo.h>
#include "xo_filter.h"
#include <libpin/pin_filter.h>

pin_parse_t *
pin_parse_open (pa_mmap_t *pmp, pin_workspace_t *workp, const char *name,
	       const char *input, pin_source_flags_t flags)
{
    pin_source_t *srcp = NULL;
    pin_parse_t *parsep = NULL;
    pin_insert_t *pip = NULL;
    pin_tree_t *ptp = NULL;
    pin_node_t *nodep = NULL;
    char namebuf[PA_MMAP_HEADER_NAME_LEN];

    /*
     * XXX okay, so this is crap, just a bunch of initialization that
     * needs to be broken out in distinct functions.
     */

    srcp = pin_source_open(input, flags);
    if (srcp == NULL)
	goto fail;

    /* The pin_tree_t is the tree we'll be inserting into */
    ptp = calloc(1, sizeof(*ptp));
    if (ptp == NULL)
	goto fail;

    ptp->pt_infop = pa_mmap_header(pmp, pin_mk_name(namebuf, name, "tree"),
				   PA_TYPE_TREE, 0, sizeof(*ptp->pt_infop));
    ptp->pt_max_depth = 0;
    ptp->pt_workspace = workp;

    /* The pin_insert_t is the point in the tree at which we are inserting */
    pip = calloc(1, sizeof(*pip));
    if (pip == NULL)
	goto fail;
    pip->pin_tree = ptp;
    pip->pin_depth = 0;
    pip->pin_relation = PIR_CHILD;

    /* And finally, fill in the parse structure */
    parsep = calloc(1, sizeof(*parsep));
    if (parsep == NULL)
	goto fail;
    parsep->pp_srcp = srcp;
    parsep->pp_insert = pip;

    /* Fill in the default default rule */
    parsep->pp_default_rule.pr_flags = PRF_MATCH_ALL;
    parsep->pp_default_rule.pr_action = PIA_SAVE;

    pin_node_id_t node_atom;
    nodep = pin_node_alloc(workp, &node_atom);
    if (nodep == NULL)
	goto fail;
    nodep->pn_type = PIN_TYPE_ROOT;
    nodep->pn_depth = 0;
    nodep->pn_ns_map = PA_NULL_ATOM;
    nodep->pn_name = pin_name_id_null_atom();
    nodep->pn_next = pin_node_id_null_atom();
    nodep->pn_contents = PA_NULL_ATOM;

    ptp->pt_root = node_atom;
    pip->pin_stack[pip->pin_depth].ps_atom = node_atom;
    pip->pin_stack[pip->pin_depth].ps_node = nodep;

    return parsep;

 fail:
    if (pip)
	free(pip);
    if (ptp)
	free(ptp);
    if (parsep)
	free(parsep);
    if (srcp)
	pin_source_destroy(srcp);
    return NULL;
}

void
pin_parse_destroy (pin_parse_t *parsep)
{
    pin_source_destroy(parsep->pp_srcp);
}

pin_name_id_t
pin_parse_namepool_atom (pin_parse_t *parsep, const char *name)
{
    return pin_namepool_atom(pin_parse_workspace(parsep), name, TRUE);
}

const char *
pin_parse_namepool_string (pin_parse_t *parsep, pin_name_id_t name_id)
{
    return pin_namepool_string(pin_parse_workspace(parsep), name_id);
}

static void
pin_insert_push (pin_insert_t *pip, pin_node_id_t atom, pin_node_t *nodep)
{
    /* We reuse the current rule state */
    pin_rstate_t *statep = pip->pin_stack[pip->pin_depth].ps_statep;

    pip->pin_depth += 1;
    pin_istack_t *frame = &pip->pin_stack[pip->pin_depth];
    frame->ps_atom = atom;
    frame->ps_node = nodep;
    frame->ps_statep = statep;
    frame->ps_action = PIA_NONE;
    frame->ps_old_name = pin_name_id_null_atom();
}

static void
pin_insert_pop (pin_insert_t *pip)
{
    pip->pin_stack[pip->pin_depth].ps_atom = pin_node_id_null_atom();
    pip->pin_stack[pip->pin_depth].ps_node = NULL;
    pip->pin_depth -= 1;
}

/*
 * Insert a node into the insertion point
 */
static pin_node_id_t
pin_insert_node (pin_insert_t *pip, const char *msg,
		const char *data, size_t len,
		pin_node_type_t type, pin_name_id_t name_id, pa_atom_t contents)
{
    /* Inside a phantom discard frame — don't insert anything */
    pin_istack_t *cur = &pip->pin_stack[pip->pin_depth];
    if (cur->ps_node == NULL && cur->ps_action == PIA_DISCARD)
	return pin_node_id_null_atom();

    pin_node_id_t node_atom;
    pin_node_t *nodep = pin_node_alloc(pip->pin_tree->pt_workspace, &node_atom);
    if (nodep == NULL)
	return pin_node_id_null_atom();

    /* Initialize our fields */
    nodep->pn_type = type;
    nodep->pn_ns_map = PA_NULL_ATOM;
    nodep->pn_name = name_id;
    nodep->pn_contents = contents;

    psu_log("%s: [%.*s] %u / %u (depth %u)", msg, (int) len, data,
	    pin_name_id_atom_of(name_id), contents, pip->pin_depth + 1);

    /*
     * If we don't have a child, make one.  Otherwise append it.
     */
    pin_istack_t *psp = &pip->pin_stack[pip->pin_depth];
    if (psp->ps_node->pn_contents == PA_NULL_ATOM) {
	/* Record us as the child of the current stack node */
	pin_node_set_child(psp->ps_node, node_atom);

	/* Set our "parent" as the current node */
	nodep->pn_next = psp->ps_atom;

    } else {
	/* Append our node as the next child */
	nodep->pn_next = psp->ps_atom;
	psp->ps_last_node->pn_next = node_atom;
    }

    /* Mark the "last" as us */
    psp->ps_last_atom = node_atom;
    psp->ps_last_node = nodep;

    /* Set our depth */
    nodep->pn_depth = pip->pin_depth + 1;

    /* Update pin_maxdepth */
    if (nodep->pn_depth > pip->pin_maxdepth)
	pip->pin_maxdepth = nodep->pn_depth;

    return node_atom;
}

/*
 * Insert a namespace node as the next in the NS chain for parent_atom.
 * prev_ns_nodep is NULL for the first NS node (sets parent's pn_contents),
 * or points to the previous NS node (updates its pn_next).
 * Returns the newly allocated NS node so the caller can pass it as prev on
 * the next call.
 */
static pin_node_t *
pin_insert_ns_node (pin_insert_t *pip, const char *msg,
		   const char *data, size_t len, pin_node_id_t parent_atom,
		   pin_node_t *prev_ns_nodep,
		   pin_node_type_t type, pin_name_id_t name_id,
		   pa_atom_t contents)
{
    pin_node_id_t node_atom;
    pin_node_t *nodep = pin_node_alloc(pip->pin_tree->pt_workspace, &node_atom);
    if (nodep == NULL)
	return NULL;

    /* Initialize our fields */
    nodep->pn_type = type;
    nodep->pn_ns_map = PA_NULL_ATOM;
    nodep->pn_name = name_id;
    nodep->pn_contents = contents;

    psu_log("%s: [%.*s] %u / %u (depth %u)", msg, (int) len, data,
	    pin_name_id_atom_of(name_id), contents, pip->pin_depth + 1);

    /* New NS node's pn_next always points to parent (last-sibling sentinel) */
    nodep->pn_next = parent_atom;

    /* Wire into the chain */
    pin_istack_t *psp = &pip->pin_stack[pip->pin_depth];
    if (prev_ns_nodep == NULL)
	pin_node_set_child(psp->ps_node, node_atom); /* first NS: set parent's child */
    else
	prev_ns_nodep->pn_next = node_atom;          /* subsequent: chain from prev */

    /* Mark the "last" as us */
    psp->ps_last_atom = node_atom;
    psp->ps_last_node = nodep;

    /* Set our depth */
    nodep->pn_depth = pip->pin_depth + 1;

    /* Update pin_maxdepth */
    if (nodep->pn_depth > pip->pin_maxdepth)
	pip->pin_maxdepth = nodep->pn_depth;

    return nodep;
}

/*
 * Find the parent of a node; not cheap.  Search the list of "pn_next"
 * until we find a node of differing depth.
 */
static inline pin_node_t *
pin_node_parent (pin_workspace_t *pwp, pin_node_t *nodep)
{
    pin_node_t *nextp;
    pin_depth_t depth = nodep->pn_depth;

    for (; !pin_node_id_is_null(nodep->pn_next); nodep = nextp) {
	nextp = pin_node_addr(pwp, nodep->pn_next);
	if (nextp == NULL)
	    break;		/* Should not occur */

	if (nextp->pn_depth < depth)
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
static pin_ns_map_id_t
pin_parse_find_ns_atom (pin_parse_t *parsep, pin_node_t *nodep,
		       pin_name_id_t pref_id)
{
    pin_workspace_t *pwp = parsep->pp_insert->pin_tree->pt_workspace;
    pin_node_t *curp, *childp;
    pin_ns_map_t *ns_map;

    for (curp = nodep; curp; curp = pin_node_parent(pwp, curp)) {
	for (childp = pin_node_addr(pwp, pin_node_child(curp)); childp;
	     childp = pin_node_addr(pwp, childp->pn_next)) {
	    if (childp->pn_type != PIN_TYPE_NS)
		break;		/* Done with namespaces */

	    /* The namespace mapping number is in the node's contents */
	    pin_ns_map_id_t ns_id = pin_node_ns_contents(childp);
	    ns_map = pin_ns_map_addr(pwp, ns_id);
	    if (ns_map != NULL && pin_name_id_equal(ns_map->pnm_prefix, pref_id))
		return ns_id; /* Match! */
	}
    }

    return pin_ns_map_id_null_atom();
}

/*
 * Find a namespace mapping for the given prefix and return it.  We are
 * forced to search upward thru the hierarchy to find the mapping, which
 * is expensive, but this operation will mostly naturally be done as the
 * tree is being built, so the number of trailing subling nodes should be
 * very low.
 */
static pin_ns_map_id_t
pin_parse_find_ns (pin_parse_t *parsep, pin_node_t *nodep, const char *prefix)
{
    pin_workspace_t *pwp = parsep->pp_insert->pin_tree->pt_workspace;
    pin_name_id_t pref_id;

    if (prefix == NULL) {
	/* If the prefix is NULL, we're looking for the default prefix */
	pref_id = pin_name_id_null_atom();

    } else {
	/*
	 * Find the atom for the prefix; if there isn't one, then it
	 * cannot have been defined, which is likely a syntax error.
	 */
	pref_id = pin_namepool_atom(pwp, prefix, FALSE);
	if (pin_name_id_is_null(pref_id))
	    return pin_ns_map_id_null_atom();
    }

    return pin_parse_find_ns_atom(parsep, nodep, pref_id);
}

static inline pin_boolean_t
pin_parse_is_attrib (pin_node_type_t type)
{
    if (type == PIN_TYPE_ATSTR || type == PIN_TYPE_ATTRIB
	|| type == PIN_TYPE_NS)
	return TRUE;
    return FALSE;
}

static void
pin_insert_attribs (pin_parse_t *parsep, pin_node_t *nodep, const char *data)
{
    pin_insert_t *pip = parsep->pp_insert;
    pa_arb_t *prp = pip->pin_tree->pt_workspace->pw_textpool;
    size_t len = strlen(data);
    pa_arb_atom_t data_atom = pa_arb_alloc(prp, len + 1);
    char *cp = pa_arb_atom_addr(prp, data_atom);

    if (cp == NULL)
	return;

    memcpy(cp, data, len);
    cp[len] = '\0';

    pin_node_id_t node_atom;
    node_atom = pin_insert_node(pip, "pin_insert_attribs", data, len,
			       PIN_TYPE_ATSTR, pin_name_id_null_atom(), pa_arb_atom_of(data_atom));
    if (pin_node_id_is_null(node_atom)) {
	pa_arb_free_atom(prp, data_atom);
	return;
    }

    /* Mark the attributes as present (but not extracted) */
    nodep->pn_flags |= PNF_ATTRIBS_PRESENT;
}

/*
 * Returns NULL for success, or static error message text
 */
static const char *
pin_parse_next_attrib (char **content, char *endp,
		      char **namep, size_t *namelenp,
		      char **valuep, size_t *valuelenp)
{
    char *cp = *content;

    if (cp == NULL)
	return NULL;		/* Should not occur */

    char *name = pin_skipws(cp, endp - cp, 1);
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
    sp = pin_skipws(sp, sp - name, -1); /* Trim trailing ws */
    if (sp != NULL)
	namelen = &sp[1] - name;

    cp += 1;			/* Move over '=' */

    char *value = pin_skipws(cp, cp - endp, 1);
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
 * namespace and attribute nodes.  Namespaces (PIN_TYPE_NS) are handled
 * distinctly from other attributes (PIN_TYPE_ATTRIB).  For namespaces,
 * the pn_name is the PA_NULL_ATOM and the pn_contents is the prefix
 * mapping, which is an index into the prefix mapping table, providing
 * some reuse of prefix-to-uri relationships.  For attributes, the
 * pn_name is the name (an index into the namepool) and the
 * pn_contents is the value (an index into the string table).
 *
 * Namespaces are a pain, but a necessary one; they are handled
 * differently from other attributes, in that they use the namepool
 * for both their prefixes and their values, since we assume the
 * strings will continue to appear.  It also allows us to compare
 * namespace URIs by comparing atom numbers, rather than strcmp.  We
 * record the prefix-to-namespace mapping in the ns_map, and then
 * record that mapping as the value (pn_contents) of the PIN_TYPE_NS
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
pin_insert_attribs_extract (pin_parse_t *parsep, pin_node_id_t node_atom,
			   pin_node_t *nodep, char *attrib,
			   pin_boolean_t only_ns)
{
    pin_insert_t *pip = parsep->pp_insert;
    pin_workspace_t *pwp = pip->pin_tree->pt_workspace;
    pa_arb_t *prp = pwp->pw_textpool;
    size_t len = strlen(attrib);
    char *content = attrib, *endp = content + len, *name, *value;
    size_t namelen, valuelen;
    pin_name_id_t name_id;
    pin_node_id_t attrib_atom;
    pa_arb_atom_t value_atom;
    int hit = FALSE;
    const char *msg;
    pin_node_t *prev_ns_nodep = NULL; /* Previous NS node for chaining */

    for (;;) {
	msg = pin_parse_next_attrib(&content, endp, &name, &namelen,
				   &value, &valuelen);
	if (msg) {
	    pin_source_failure(parsep->pp_srcp, 0, msg);
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

	    pin_ns_map_id_t ns_id = pin_ns_find(pwp, name, value, TRUE);
	    if (pin_ns_map_id_is_null(ns_id)) {
		pin_source_failure(parsep->pp_srcp, 0,
				  "namespace create/find failed");
		break;
	    }

	    prev_ns_nodep = pin_insert_ns_node(pip,
					 "pin_insert_attribs_extract(ns)",
					 name, name ? strlen(name) : 0,
					 node_atom, prev_ns_nodep,
					 PIN_TYPE_NS, pin_name_id_null_atom(),
					 pa_fixed_atom_of(pin_ns_map_id_atom_of(ns_id)));
	    if (prev_ns_nodep == NULL) {
		pin_source_failure(parsep->pp_srcp, 0,
				  "attribute insert (ns) failed");
		break;
	    }

	} else if (only_ns) {
	    continue;		/* Skip other attributes */

	} else {
	    char *prefix;
	    pin_name_id_t pref_id;
	    char *localp = strchr(name, ':');
	    if (localp) {
		*localp++ = '\0';
		prefix = name;
		pref_id = pin_namepool_atom(pwp, prefix, TRUE);
	    } else {
		localp = name;
		prefix = NULL;
		pref_id = pin_name_id_null_atom();
	    }

	    /* Normal attribute */
	    name_id = pin_namepool_atom(pwp, localp, TRUE);
	    if (pin_name_id_is_null(name_id))
		break;

	    value_atom = pa_arb_alloc_string(prp, value);
	    if (pa_arb_is_null(value_atom))
		break;

	    attrib_atom = pin_insert_node(pip, "pin_insert_attribs_extract",
				 name, strlen(name),
				 PIN_TYPE_ATTRIB, name_id, pa_arb_atom_of(value_atom));
	    if (pin_node_id_is_null(attrib_atom)) {
		pin_source_failure(parsep->pp_srcp, 0,
				  "attribute insert failed");
		pa_arb_free_atom(prp, value_atom);
		break;
	    }

	    if (!pin_name_id_is_null(pref_id)) {
		/*
		 * We have to stash our prefix atom in a special
		 * temporary node of type PIN_TYPE_NSPREF.  After all
		 * the attributes are processed and all namespaces
		 * have been defined, we'll loop thru and set
		 * real ns_map values.
		 */
		pin_node_id_t stash_id = pin_insert_node(pip,
				 "pin_insert_attribs_extract (stash)",
				 name, strlen(name),
				 PIN_TYPE_NSPREF, pin_name_id_null_atom(),
				 pin_name_id_atom_of(pref_id));
		if (pin_node_id_is_null(stash_id)) {
		    pin_source_failure(parsep->pp_srcp, 0,
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
     * now we have to use them.  When we find an PIN_TYPE_NSPREF
     * attribute, the pn_contents are the atom of a prefix string.  We
     * finish that off, finding the real mapping and recording it,
     * discarding the NSPREF node.
     */
    pin_node_t *childp, *prev = NULL;

    for (childp = pin_node_addr(pwp, pin_node_child(nodep)); childp;
	 childp = pin_node_addr(pwp, childp->pn_next)) {
	if (nodep->pn_type == PIN_TYPE_NS) {
	    /* Skip namespace defs */

	} else if (!pin_parse_is_attrib(nodep->pn_type)) {
	    break;		/* End of attributes == done */

	} else if (prev == NULL) {
	    /* Can't handle not having a previous node */

	} else if (childp->pn_type == PIN_TYPE_NSPREF) {
	    /*
	     * An PIN_TYPE_NSPREF node means the previous node needs an
	     * accurate name mapping.  We'll find one and discard the
	     * current node.
	     */
	    pin_name_id_t stashed_pref = pin_name_id(childp->pn_contents);
	    pin_ns_map_id_t ns_id = pin_parse_find_ns_atom(parsep, nodep,
						stashed_pref);
	    if (pin_ns_map_id_is_null(ns_id)) {
		const char *prefix = pin_namepool_string(pwp, stashed_pref);
		pin_source_failure(parsep->pp_srcp, 0,
				  "namespace mapping not found for %s:%s",
				  prefix ?: "", name);
	    }

	    /* Set the namespace mapping */
	    pin_node_set_ns_map(prev, ns_id);        /* Assign mapping */
	    prev->pn_next = childp->pn_next;        /* Remove node from list */

	    pin_node_free(pwp, prev->pn_next);       /* Free node */
	    childp = prev;			    /* childp is dead; resume logic */
	}

	prev = childp;
    }

    /* Mark the attributes as present and extracted */
    if (hit)
	nodep->pn_flags |= PNF_ATTRIBS_PRESENT | PNF_ATTRIBS_EXTRACTED;
}

static void
pin_insert_open (pin_parse_t *parsep, pin_name_id_t name_id,
		const char *prefix, const char *name, char *attribs,
		pin_action_type_t type)
{
    pin_insert_t *pip = parsep->pp_insert;

    if (pin_name_id_is_null(name_id))
	return;

    /* If inside a discard frame, push another phantom instead of storing */
    pin_istack_t *cur_frame = &pip->pin_stack[pip->pin_depth];
    if (cur_frame->ps_node == NULL && cur_frame->ps_action == PIA_DISCARD) {
	pin_rstate_t *statep = cur_frame->ps_statep;
	pip->pin_depth += 1;
	pin_istack_t *new_frame = &pip->pin_stack[pip->pin_depth];
	bzero(new_frame, sizeof(*new_frame));
	new_frame->ps_action = PIA_DISCARD;
	new_frame->ps_old_name = name_id;
	new_frame->ps_statep = statep;
	return;
    }

    pin_node_id_t node_atom;
    node_atom = pin_insert_node(pip, "pin_insert_open",
			       name, strlen(name),
			       PIN_TYPE_ELT, name_id, PA_NULL_ATOM);
    if (pin_node_id_is_null(node_atom))
	return;

    pin_node_t *nodep = pin_node_addr(pip->pin_tree->pt_workspace, node_atom);

    /* Push our node on the stack */
    pin_insert_push(pip, node_atom, nodep);

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
	 * to save-attributes-as-string (PIA_SAVE_ATSTR) and we
	 * see a namespace, we force the full save.
	 */
	if (type == PIA_SAVE_ATTRIB) {
	    save = SAVE_FULL;
	} else if (strstr(attribs, PIN_XMLNS_LEADER) == NULL) {
	    if (type == PIA_SAVE_ATSTR)
		save = SAVE_STRING;
	} else if (type == PIA_SAVE_ATSTR) {
	    save = SAVE_FULL;
	} else {
	    save = SAVE_NS;
	}

	if (save == SAVE_STRING) /* Save as string */
	    pin_insert_attribs(parsep, nodep, attribs);
	else if (save != SAVE_NONE) /* Save as parsed attributes */
	    pin_insert_attribs_extract(parsep, node_atom, nodep, attribs,
				      (save == SAVE_NS) ? TRUE : FALSE);
    }

    if (prefix != NULL) {
	pin_ns_map_id_t ns_id = pin_parse_find_ns(parsep, nodep, prefix);
	pin_node_set_ns_map(nodep, ns_id);
	if (pin_ns_map_id_is_null(ns_id))
	    pin_source_failure(parsep->pp_srcp, 0,
			      "namespace mapping not found for %s:%s",
			      prefix, name);
    }
}

static void
pin_insert_close (pin_parse_t *parsep, const char *prefix UNUSED, const char *name)
{
    pin_insert_t *pip = parsep->pp_insert;
    pin_name_id_t name_id;

    name_id = pin_namepool_atom(pip->pin_tree->pt_workspace, name, FALSE);

    psu_log("pin_insert_close: [%s] %u (depth %u)", name,
	   pin_name_id_atom_of(name_id), pip->pin_depth);

    if (pin_name_id_is_null(name_id)) {
	pin_source_failure(parsep->pp_srcp, 0, "close tag failed: %s", name);
	return;
    }

    pin_istack_t *psp = &pip->pin_stack[pip->pin_depth];

    if (pip->pin_depth == 0) {
	pin_source_failure(parsep->pp_srcp, 0,
			  "close for open that doesn't exist: %s", name);
	return;
    }

    /* Phantom frame pushed for PIA_DISCARD — verify name and pop */
    if (psp->ps_node == NULL) {
	if (psp->ps_action != PIA_DISCARD) {
	    pin_source_failure(parsep->pp_srcp, 0,
			      "close for open that doesn't exist: %s", name);
	    return;
	}
	if (!pin_name_id_is_null(name_id)
	        && !pin_name_id_equal(psp->ps_old_name, name_id)) {
	    pin_source_failure(parsep->pp_srcp, 0,
			      "close doesn't match: %s", name);
	    return;
	}
	bzero(psp, sizeof(*psp));
	pin_insert_pop(pip);
	return;
    }

    if (!pin_name_id_is_null(psp->ps_old_name)) {
	if (!pin_name_id_equal(psp->ps_old_name, name_id)) {
	    pin_source_failure(parsep->pp_srcp, 0,
			      "close doesn't match original: %s", name);
	    return;
	}
    } else if (!pin_name_id_equal(psp->ps_node->pn_name, name_id)) {
	pin_source_failure(parsep->pp_srcp, 0, "close doesn't match: %s", name);
	return;
    }

    pin_action_type_t popped_action = psp->ps_action;
    bzero(psp, sizeof(*psp));
    pin_insert_pop(pip);

    /* PIA_WRAP: also close the synthetic wrapper node */
    if (popped_action == PIA_WRAP) {
	psp = &pip->pin_stack[pip->pin_depth];
	bzero(psp, sizeof(*psp));
	pin_insert_pop(pip);
    }

    /*
     * Body FSM: detect when the matched element (opened by BIA_COPY) closes.
     * pbf_copy_depth is the depth AFTER pin_insert_open(match_name); after the
     * pop above depth is pbf_copy_depth - 1.
     */
    pin_body_exec_t *body = &pip->pin_body;
    if (body->pbe_depth > 0) {
	pin_body_frame_t *bfp = &body->pbe_stack[body->pbe_depth - 1];
	if (bfp->pbf_mode == PBMODE_COPY
		&& pip->pin_depth == (pin_depth_t)(bfp->pbf_copy_depth - 1)) {
	    bfp->pbf_mode = PBMODE_EXEC;
	    pin_body_exec_advance(parsep);
	}
    }
}

static void
pin_insert_text (pin_parse_t *parsep, const char *data, size_t len,
		pin_node_type_t type)
{
    pin_insert_t *pip = parsep->pp_insert;
    pa_arb_t *prp = pip->pin_tree->pt_workspace->pw_textpool;
    pa_arb_atom_t data_atom = pa_arb_alloc(prp, len + 1);
    char *cp = pa_arb_atom_addr(prp, data_atom);

    if (cp == NULL)
	return;

    memcpy(cp, data, len);
    cp[len] = '\0';

    pin_node_id_t node_atom;
    node_atom = pin_insert_node(pip, "pin_insert_text", data, len,
			       type, pin_name_id_null_atom(), pa_arb_atom_of(data_atom));
    if (pin_node_id_is_null(node_atom)) {
	pa_arb_free_atom(prp, data_atom);
	return;
    }
}

/*
 * Execute body instructions while in PBMODE_EXEC mode.
 * Returns when the instruction list is exhausted (body frame popped) or
 * when a BIA_COPY instruction is reached (mode switches to PBMODE_COPY).
 */
static void
pin_body_exec_advance (pin_parse_t *parsep)
{
    pin_insert_t *pip = parsep->pp_insert;
    pin_body_exec_t *body = &pip->pin_body;

    while (body->pbe_depth > 0) {
	pin_body_frame_t *bfp = &body->pbe_stack[body->pbe_depth - 1];

	if (bfp->pbf_mode != PBMODE_EXEC)
	    break;

	pin_body_instr_id_t pc = bfp->pbf_pc;
	if (pin_body_instr_id_is_null(pc)) {
	    /* End of instruction list; body complete */
	    body->pbe_depth -= 1;
	    continue;		/* Check for parent body frame */
	}

	pin_body_instr_t *instr = pin_body_instr_addr(parsep->pp_rulebook, pc);
	if (instr == NULL) {
	    body->pbe_depth -= 1;
	    break;
	}

	bfp->pbf_pc = instr->bi_next;	/* Advance PC before executing */

	switch (instr->bi_type) {
	case BIA_EMIT_OPEN: {
	    const char *tag = pin_parse_namepool_string(parsep, instr->bi_tag);
	    if (tag)
		pin_insert_open(parsep, instr->bi_tag, NULL, tag, NULL, PIA_SAVE);
	    break;
	}
	case BIA_EMIT_TEXT: {
	    const char *text = pin_parse_namepool_string(parsep, instr->bi_text);
	    if (text)
		pin_insert_text(parsep, text, strlen(text), PIN_TYPE_TEXT);
	    break;
	}
	case BIA_EMIT_CLOSE: {
	    const char *tag = pin_parse_namepool_string(parsep, instr->bi_tag);
	    if (tag)
		pin_insert_close(parsep, NULL, tag);
	    break;
	}
	case BIA_COPY: {
	    /* Open the matched element in the output now */
	    const char *match_str = pin_parse_namepool_string(parsep,
							      bfp->pbf_match_name);
	    if (match_str) {
		pin_insert_open(parsep, bfp->pbf_match_name,
				bfp->pbf_match_prefix, match_str,
				bfp->pbf_match_attribs, PIA_SAVE_ATTRIB);
		/* Null out the state so children use the parser default (PIA_SAVE) */
		pip->pin_stack[pip->pin_depth].ps_statep = NULL;
		bfp->pbf_copy_depth = pip->pin_depth;
	    }
	    bfp->pbf_mode = PBMODE_COPY;
	    return;		/* Pause; resume on matched element CLOSE */
	}
	default:
	    break;
	}
    }
}

/*
 * Return TRUE if rulep's mode (pr_mode) matches the context's current mode.
 * Both NULL/PA_NULL_ATOM → default-mode match.
 */
static int
pin_parse_mode_matches (pin_parse_t *parsep, pin_rule_t *rulep)
{
    const char *ctx_mode = parsep->pp_context.pctx_mode;
    pin_name_id_t rule_mode = rulep->pr_mode;

    if (ctx_mode == NULL || ctx_mode[0] == '\0') {
	/* Context is default mode: match only default-mode rules */
	return pin_name_id_is_null(rule_mode);
    }

    if (pin_name_id_is_null(rule_mode))
	return FALSE;	/* Rule is default mode; context is not */

    /* Look up context mode in namepool without creating it */
    pin_name_id_t ctx_id = pin_namepool_atom(pin_parse_workspace(parsep),
					     ctx_mode, FALSE);
    return (!pin_name_id_is_null(ctx_id) && pin_name_id_equal(ctx_id, rule_mode));
}

static void
pin_parse_handle_rule (pin_parse_t *parsep, pin_name_id_t name_id,
		      const char *prefix UNUSED, const char *name,
		      char *attribs, pin_rule_t *prp)
{
    pin_insert_t *pip = parsep->pp_insert;
    pin_action_type_t act = prp->pr_action;
    pin_name_id_t use_tag = prp->pr_use_tag;
    pin_name_id_t save_name_id = name_id;

    /*
     * Body FSM path: if the rule has a compiled body instruction list,
     * push a body frame, run the FSM, and return.  The simple pr_action
     * dispatch below is bypassed.
     */
    if (!pin_body_instr_id_is_null(prp->pr_body)) {
	pin_body_exec_t *body = &pip->pin_body;
	int initial_depth = body->pbe_depth;

	if (body->pbe_depth < PIN_BODY_DEPTH_MAX) {
	    pin_body_frame_t *bfp = &body->pbe_stack[body->pbe_depth];
	    bzero(bfp, sizeof(*bfp));
	    bfp->pbf_pc = prp->pr_body;
	    bfp->pbf_mode = PBMODE_EXEC;
	    bfp->pbf_match_name = name_id;
	    bfp->pbf_match_prefix = prefix;
	    bfp->pbf_match_attribs = attribs;
	    body->pbe_depth += 1;
	}

	pin_body_exec_advance(parsep);

	/*
	 * If the body completed without hitting BIA_COPY (e.g. pure
	 * EMIT_* literal body), push a phantom DISCARD frame so the
	 * matched element's content and close tag are absorbed.
	 */
	if (body->pbe_depth == initial_depth) {
	    pin_rstate_t *statep = pip->pin_stack[pip->pin_depth].ps_statep;
	    pip->pin_depth += 1;
	    pin_istack_t *new_frame = &pip->pin_stack[pip->pin_depth];
	    bzero(new_frame, sizeof(*new_frame));
	    new_frame->ps_action = PIA_DISCARD;
	    new_frame->ps_old_name = save_name_id;
	    new_frame->ps_statep = statep;
	}

	return;
    }

    /* Use a different tag if directed */
    if (!pin_name_id_is_null(use_tag))
	name_id = use_tag;

    switch (act) {
    case PIA_SAVE:
    case PIA_SAVE_ATSTR:
    case PIA_SAVE_ATTRIB:
    case PIA_EMIT:
	pin_insert_open(parsep, name_id, prefix, name, attribs, act);

	/* Apply rulebook state transition if directed */
	if (!pin_rstate_id_is_null(prp->pr_new_state) && parsep->pp_rulebook) {
	    pin_istack_t *new_frame = &pip->pin_stack[pip->pin_depth];
	    new_frame->ps_statep = pin_rstate_element(parsep->pp_rulebook,
						     prp->pr_new_state);
	}
	break;

    case PIA_DISCARD: {
	/* Push phantom frame (no node) to track depth and close matching */
	pin_rstate_t *statep = pip->pin_stack[pip->pin_depth].ps_statep;
	pip->pin_depth += 1;
	pin_istack_t *new_frame = &pip->pin_stack[pip->pin_depth];
	bzero(new_frame, sizeof(*new_frame));
	new_frame->ps_action = PIA_DISCARD;
	new_frame->ps_old_name = save_name_id;
	if (!pin_rstate_id_is_null(prp->pr_new_state) && parsep->pp_rulebook) {
	    new_frame->ps_statep = pin_rstate_element(parsep->pp_rulebook,
						     prp->pr_new_state);
	} else {
	    new_frame->ps_statep = statep;
	}
	break;
    }

    case PIA_LITERAL: {
	/*
	 * name_id is already the literal element tag (substituted from
	 * pr_use_tag at the top of this function).  Emit <tag>text</tag>
	 * and then push a phantom DISCARD frame to swallow the matched
	 * element's content and closing tag from the input stream.
	 */
	const char *emit_tag_str = pin_parse_namepool_string(parsep, name_id);
	if (emit_tag_str == NULL)
	    break;

	/* Capture parent state before pin_insert_open increments depth */
	pin_rstate_t *parent_statep = pip->pin_stack[pip->pin_depth].ps_statep;

	pin_insert_open(parsep, name_id, NULL, emit_tag_str, NULL, PIA_SAVE);

	if (!pin_name_id_is_null(prp->pr_literal_text)) {
	    const char *text = pin_parse_namepool_string(parsep, prp->pr_literal_text);
	    if (text && *text)
		pin_insert_text(parsep, text, strlen(text), PIN_TYPE_TEXT);
	}

	pin_insert_close(parsep, NULL, emit_tag_str);

	/* Push phantom DISCARD frame to absorb the matched element's subtree */
	pip->pin_depth += 1;
	pin_istack_t *new_frame = &pip->pin_stack[pip->pin_depth];
	bzero(new_frame, sizeof(*new_frame));
	new_frame->ps_action = PIA_DISCARD;
	new_frame->ps_old_name = save_name_id;
	if (!pin_rstate_id_is_null(prp->pr_new_state) && parsep->pp_rulebook) {
	    new_frame->ps_statep = pin_rstate_element(parsep->pp_rulebook,
						     prp->pr_new_state);
	} else {
	    new_frame->ps_statep = parent_statep;
	}
	break;
    }

    case PIA_WRAP: {
	/*
	 * Open a synthetic wrapper tag, save the matched element inside it,
	 * and mark the element's frame PIA_WRAP so pin_insert_close also
	 * pops the wrapper when the element closes.
	 *
	 * name_id is already pr_use_tag (the wrapper tag name) from above.
	 * save_name_id is the original matched element name.
	 *
	 * If pr_pre_tag is set, emit <pre_tag>pr_literal_text</pre_tag>
	 * before opening the wrapper.
	 */
	const char *wrap_str = pin_parse_namepool_string(parsep, name_id);
	if (wrap_str == NULL)
	    break;

	if (!pin_name_id_is_null(prp->pr_pre_tag)) {
	    const char *pre_str = pin_parse_namepool_string(parsep, prp->pr_pre_tag);
	    if (pre_str) {
		pin_insert_open(parsep, prp->pr_pre_tag, NULL, pre_str, NULL, PIA_SAVE);
		if (!pin_name_id_is_null(prp->pr_literal_text)) {
		    const char *text = pin_parse_namepool_string(parsep, prp->pr_literal_text);
		    if (text && *text)
			pin_insert_text(parsep, text, strlen(text), PIN_TYPE_TEXT);
		}
		pin_insert_close(parsep, NULL, pre_str);
	    }
	}

	/* Open the wrapper node */
	pin_insert_open(parsep, name_id, NULL, wrap_str, NULL, PIA_SAVE);

	/* Open the matched element inside the wrapper */
	pin_insert_open(parsep, save_name_id, prefix, name, attribs, PIA_SAVE);

	/* Mark the element frame so pin_insert_close triggers a wrapper close */
	pip->pin_stack[pip->pin_depth].ps_action = PIA_WRAP;

	if (!pin_rstate_id_is_null(prp->pr_new_state) && parsep->pp_rulebook) {
	    pip->pin_stack[pip->pin_depth].ps_statep =
		pin_rstate_element(parsep->pp_rulebook, prp->pr_new_state);
	}
	break;
    }

    default:
	break;
    }

    if (!pin_name_id_is_null(use_tag) && act != PIA_WRAP) {
	pin_istack_t *psp = &pip->pin_stack[pip->pin_depth];
	psp->ps_old_name = save_name_id;
    }
}

int
pin_parse (pin_parse_t *parsep)
{
    pin_source_t *srcp = parsep->pp_srcp;
    char *data, *rest, *localp;
    pin_node_type_t type;
    pin_boolean_t opt_quiet = PSU_BIT_TEST(parsep->pp_flags, PIN_PF_DEBUG);
    pin_boolean_t opt_unescape = 0;
    pin_name_id_t name_id;
    pin_rule_t *rulep = NULL;
    pin_insert_t *pip = parsep->pp_insert;

    for (;;) {

	type = pin_source_next_token(srcp, &data, &rest);

	switch (type) {
	case PIN_TYPE_NONE:	/* Unknown type */
	    return 1;

	case PIN_TYPE_EOF:	/* End of file */
	    return 0;

	case PIN_TYPE_FAIL:	/* Failure mode */
	    return -1;

	case PIN_TYPE_TEXT:	/* Text content */
	    type = PIN_TYPE_UNESC; /* UNESC (aka CDATA) is unescaped text */
	    {
		size_t len;
		if (opt_unescape && data && rest) {
		    len = pin_source_unescape(srcp, data, rest - data);
		    type = PIN_TYPE_TEXT; /* TEXT is escaped */
		} else {
		    len = rest - data;
		}
		psu_log("text [%.*s] (%u)", (int) len, data, type);
		pin_insert_text(parsep, data, len, type);
	    }
	    break;

	case PIN_TYPE_OPEN:	/* Open tag */
	case PIN_TYPE_EMPTY:	/* Empty tag */
	    if (!opt_quiet)
		psu_log("open tag [%s] [%s]", data ?: "", rest ?: "");
	    localp = strchr(data, ':');
	    if (localp)
		*localp++ = '\0';
	    else {
		localp = data;
		data = NULL;
	    }

	    /* We need an atom to do the indexing to find rules */
	    name_id = pin_namepool_atom(pip->pin_tree->pt_workspace, localp, TRUE);

	    rulep = NULL;		/* Reset for each element */

	    /*
	     * PBMODE_COPY bypass: while a BIA_COPY is consuming a matched
	     * element, all incoming children must be copied to output.
	     * Skip filter/rulebook dispatch; use a local PIA_SAVE_ATTRIB rule.
	     * Still advance the filter FSM to keep its depth counter in sync.
	     */
	    {
		pin_body_exec_t *body = &pip->pin_body;
		if (body->pbe_depth > 0
			&& body->pbe_stack[body->pbe_depth - 1].pbf_mode
			   == PBMODE_COPY) {
		    xo_filter_t *cpy_filter = parsep->pp_filter;
		    if (cpy_filter) {
			pin_filter_set_attribs(cpy_filter, rest);
			xo_filter_walk_open(NULL, cpy_filter, localp, -1);
		    }
		    pin_rule_t copy_rule;
		    bzero(&copy_rule, sizeof(copy_rule));
		    copy_rule.pr_action = PIA_SAVE_ATTRIB;
		    pin_parse_handle_rule(parsep, name_id, data, localp,
					 rest, &copy_rule);
		    if (type == PIN_TYPE_EMPTY) {
			pin_insert_close(parsep, data, localp);
			if (cpy_filter)
			    xo_filter_walk_close(NULL, cpy_filter, localp, -1);
		    }
		    break;
		}
	    }

	    /*
	     * Advance the filter FSM (always, to keep depth in sync).
	     * If the filter says DEAD, the pattern cannot match anywhere
	     * under this subtree; synthesize a discard rule so the
	     * rulebook is not consulted and no tree nodes are allocated.
	     */
	    xo_filter_t *filter = parsep->pp_filter;
	    if (filter) {
		pin_filter_set_attribs(filter, rest);
		xo_filter_walk_open(NULL, filter, localp, -1);
	    }

	    xo_filter_status_t fstatus = filter
		? xo_filter_walk_status(NULL, filter) : XO_STATUS_ZERO;

	    if (filter && fstatus == XO_STATUS_DEAD) {
		/*
		 * Filter says no registered pattern can match in this subtree.
		 * An active rulebook state (e.g. a for-each) may still apply.
		 */
		pin_rstate_t *statep = pin_parse_stack_state(parsep);
		if (statep != NULL && parsep->pp_rulebook != NULL) {
		    rulep = pin_rulebook_find(parsep, parsep->pp_rulebook, statep,
					     name_id, data, localp, rest);
		}
		if (rulep == NULL) {
		    pin_rule_t dead_rule;
		    bzero(&dead_rule, sizeof(dead_rule));
		    dead_rule.pr_action = PIA_DISCARD;
		    pin_parse_handle_rule(parsep, name_id, data, localp,
					 rest, &dead_rule);
		} else {
		    pin_parse_handle_rule(parsep, name_id, data, localp,
					 rest, rulep);
		}
	    } else if (filter && fstatus == XO_STATUS_FULL) {
		/*
		 * Filter says a registered pattern fully matched.  But the
		 * filter may carry a stale action_id from an earlier sibling
		 * match (e.g. "header" action leaking into "item").  Check
		 * the rulebook state machine first: if an active state (for-each,
		 * etc.) has rules, they take precedence over the filter action.
		 */
		pin_rstate_t *statep = pin_parse_stack_state(parsep);
		if (statep != NULL && parsep->pp_rulebook != NULL) {
		    rulep = pin_rulebook_find(parsep, parsep->pp_rulebook, statep,
					     name_id, data, localp, rest);
		}
		if (rulep == NULL) {
		    /*
		     * No rulebook-state rule; use the filter's terminal action_id.
		     * This is the common case: no active for-each context.
		     */
		    uint32_t action_id = xo_filter_walk_get_action(NULL, filter);
		    if (action_id != PA_NULL_ATOM && parsep->pp_rulebook != NULL) {
			pin_rule_id_t rid = pin_rule_id(action_id);
			pin_rule_t *candidate = pin_rulebook_rule(parsep->pp_rulebook, rid);
			if (candidate != NULL && pin_parse_mode_matches(parsep, candidate))
			    rulep = candidate;
		    }
		    if (rulep == NULL)
			rulep = &parsep->pp_default_rule;
		}
		pin_parse_handle_rule(parsep, name_id, data, localp, rest, rulep);
	    } else {
		/*
		 * TRACK/PRED or no filter: walk the rulebook state machine.
		 */
		pin_rstate_t *statep = pin_parse_stack_state(parsep);
		rulep = pin_rulebook_find(parsep, parsep->pp_rulebook,
					 statep, name_id, data, localp, rest);
		if (rulep == NULL)
		    rulep = &parsep->pp_default_rule;
		pin_parse_handle_rule(parsep, name_id, data, localp, rest, rulep);
	    }

	    /*
	     * An empty tag is an open and a close.  Close both the tree
	     * frame and the filter frame.
	     */
	    if (type == PIN_TYPE_EMPTY) {
		pin_insert_close(parsep, data, localp);
		if (filter)
		    xo_filter_walk_close(NULL, filter, localp, -1);
	    }
	    break;

	case PIN_TYPE_CLOSE:	/* Close tag */
	    if (!opt_quiet)
		psu_log("close tag [%s] [%s]", data ?: "", rest ?: "");
	    localp = strchr(data, ':');
	    if (localp)
		*localp++ = '\0';
	    else {
		localp = data;
		data = NULL;
	    }

	    /* Pop the filter frame before popping the tree frame */
	    if (parsep->pp_filter)
		xo_filter_walk_close(NULL, parsep->pp_filter, localp, -1);
	    pin_insert_close(parsep, data, localp);
	    break;

	case PIN_TYPE_PI:	/* Processing instruction */
	    if (!opt_quiet)
		psu_log("pi [%s] [%s]", data ?: "", rest ?: "");
	    break;

	case PIN_TYPE_DTD:	/* DTD nonsense */
	    if (!opt_quiet)
		psu_log("dtd [%s] [%s]", data ?: "", rest ?: "");
	    break;

	case PIN_TYPE_COMMENT:	/* Comment */
	    if (!opt_quiet)
		psu_log("comment [%s] [%s]", data ?: "", rest ?: "");
	    break;

	case PIN_TYPE_UNESC:	/* unescaped/cdata */
	    if (!opt_quiet)
		psu_log("cdata [%.*s]", (int)(rest - data), data);
	    break;
	}
    }

    return 0;
}

static const char *pin_type_names[] = {
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
pin_node_dump (pin_workspace_t *pwp, pin_node_type_t op,
	      pin_node_t *nodep, pin_node_id_t atom)
{
    if (!pin_node_id_is_null(atom))
	nodep = pin_node_addr(pwp, atom);
    if (nodep == NULL)
	return;

    const char *name = pin_namepool_string(pwp, nodep->pn_name);
    pin_ns_map_t *ns_map = pin_ns_map_addr(pwp, pin_ns_map_id(nodep->pn_ns_map));
    const char *pref = ns_map ?
	pin_namepool_string(pwp, ns_map->pnm_prefix) : NULL;
    const char *uri = ns_map ? pin_namepool_string(pwp, ns_map->pnm_uri) : NULL;
    const char *type = (nodep->pn_type < PSU_NUM_ELTS(pin_type_names) - 1)
	? pin_type_names[nodep->pn_type] : "unknown";
    const char *opname = (op < PSU_NUM_ELTS(pin_type_names) - 1)
	? pin_type_names[op] : "unknown";

    psu_log("%s%s%snode %u [%p]: type %u(%s), name %u [%s], "
	    "depth %u, flags %#x, "
	    "ns-map %u [%s]=[%s], next %u, contents %u",
	    (op > 0) ? "Op: " : "", (op > 0) ? opname : "",
	    (op > 0) ? ", " : "",
	    pa_fixed_atom_of(pin_node_id_atom_of(atom)), nodep,
	    nodep->pn_type, type, pin_name_id_atom_of(nodep->pn_name), name ?: "",
	    nodep->pn_depth, nodep->pn_flags,
	    nodep->pn_ns_map, pref ?: "", uri ?: "",
	    pa_fixed_atom_of(pin_node_id_atom_of(nodep->pn_next)),
	    nodep->pn_contents);
}

static int
pin_parse_dump_cb (pin_parse_t *parsep, pin_node_type_t type,
		  pin_node_id_t node_atom, pin_node_t *nodep,
		  const char *data, void *opaque UNUSED)
{
    pin_workspace_t *pwp = parsep->pp_insert->pin_tree->pt_workspace;
    const char *cp;
    pin_ns_map_t *ns_map;

    pin_node_dump(pwp, type, nodep, node_atom);

    switch (type) {
    case PIN_TYPE_ROOT:
	psu_log("(root)");
	break;

    case PIN_TYPE_ELT:
	psu_log("element: [%s]", data ?: "[error]");
	if (nodep->pn_ns_map != PA_NULL_ATOM) {
	    ns_map = pin_ns_map_addr(pwp, pin_ns_map_id(nodep->pn_ns_map));
	    if (ns_map != NULL) {
		const char *pref = pin_namepool_string(pwp, ns_map->pnm_prefix);
		const char *uri = pin_namepool_string(pwp, ns_map->pnm_uri);

		psu_log("element nsmap: [%s]=[%s]", pref ?: "", uri ?: "");
	    } else {
		psu_log("element nsmap: null");
	    }
	}
	break;

    case PIN_TYPE_TEXT:
	psu_log("text: [%s]", data ?: "[error]");
	break;

    case PIN_TYPE_UNESC:		/* Unescaped/cdata */
	psu_log("cdata: [%s]", data ?: "[error]");
	break;

    case PIN_TYPE_ATTRIB:
	cp = pin_parse_namepool_string(parsep, nodep->pn_name);
	psu_log("attrib: [%s=\"%s\"]", cp, data);
	break;

    case PIN_TYPE_NS:
	ns_map = pin_ns_map_addr(pwp, pin_node_ns_contents(nodep));
	if (ns_map != NULL) {
	    const char *pref = pin_namepool_string(pwp, ns_map->pnm_prefix);
	    const char *uri = pin_namepool_string(pwp, ns_map->pnm_uri);

	    psu_log("namespace: [%s]=[%s]", pref ?: "", uri ?: "");
	} else {
	    psu_log("namespace: null");
	}
	break;

    case PIN_TYPE_ATSTR:
	psu_log("atrstr: [%s]", data ?: "[error]");
	break;

    case PIN_TYPE_EOL_ATTRIB:
	psu_log("eol-attrib: %p", nodep);
	break;

    case PIN_TYPE_EOL_EMPTY:
	psu_log("eol-empty: %p", nodep);
	break;

    case PIN_TYPE_CLOSE:
	psu_log("close: [%s]", data ?: "[error]");
	break;
    }

    return 0;
}

void
pin_parse_dump (pin_parse_t *parsep)
{
    pin_parse_emit(parsep, pin_parse_dump_cb, NULL);
}

typedef struct pin_xml_output_s {
    FILE *xx_out;		/* Output file descriptor */
    unsigned xx_indent;		/* Current indent amount */
    unsigned xx_incr;		/* Indent increment */
    pin_node_type_t xx_last_type; /* Last type seen */
} pin_xml_output_t;

static int
pin_parse_is_ws (const char *data)
{
    for (const char *cp = data; cp && *cp; cp++)
	if (!isspace(*cp))
	    return FALSE;
    return TRUE;
}

static int
pin_parse_emit_xml_cb (pin_parse_t *parsep, pin_node_type_t type,
		      pin_node_id_t node_atom UNUSED, pin_node_t *nodep,
		      const char *data, void *opaque)
{
    pin_xml_output_t *xmlp = opaque;
    FILE *out = xmlp->xx_out;
    pin_workspace_t *pwp = parsep->pp_insert->pin_tree->pt_workspace;
    pin_ns_map_t *ns_map;
    const char *cp;
    int indent;
    const char *pref, *uri;
    int is_debug = pin_parse_flags_isset(parsep, PIN_PF_DEBUG);
    int skipped = 0;

    if (is_debug)
	fprintf(out, "<!-- [[%d]%s%s%s] -->", type, data ? "[" : "",
		data ?: "", data ? "]" : "");

    switch (type) {
    case PIN_TYPE_ROOT:
	if (is_debug)
	    fprintf(out, "<!-- start of output>\n");
	break;

    case PIN_TYPE_OPEN:
	if (xmlp->xx_last_type != PIN_TYPE_ROOT
	    && xmlp->xx_last_type != PIN_TYPE_CLOSE
	    && xmlp->xx_last_type != PIN_TYPE_EMPTY)
	    fprintf(out, "\n");

	pref = NULL;
	if (nodep->pn_ns_map != PA_NULL_ATOM) {
	    ns_map = pin_ns_map_addr(pwp, pin_ns_map_id(nodep->pn_ns_map));
	    if (ns_map != NULL)
		pref = pin_namepool_string(pwp, ns_map->pnm_prefix);
	}

	fprintf(out, "%*s<%s%s%s", xmlp->xx_indent, "",
 		pref ?: "", pref ? ":" : "", data);
	xmlp->xx_indent += xmlp->xx_incr;
	break;

    case PIN_TYPE_EOL_ATTRIB:
	fprintf(out, ">");
	break;

    case PIN_TYPE_EOL_EMPTY:
	fprintf(out, "/>\n");
	break;

    case PIN_TYPE_CLOSE:
	xmlp->xx_indent -= xmlp->xx_incr;

	if (data != NULL) {
	    if (xmlp->xx_last_type != PIN_TYPE_EOL_EMPTY) {
		pref = NULL;

		if (nodep->pn_ns_map != PA_NULL_ATOM) {
		    ns_map = pin_ns_map_addr(pwp, pin_ns_map_id(nodep->pn_ns_map));
		    if (ns_map != NULL)
			pref = pin_namepool_string(pwp, ns_map->pnm_prefix);
		}

		if (xmlp->xx_last_type == PIN_TYPE_UNESC
			|| xmlp->xx_last_type == PIN_TYPE_TEXT) {
		    fprintf(out, "</%s%s%s>\n",
			    pref ?: "", pref ? ":" : "", data);
		} else {
		    indent = xmlp->xx_indent;
		    fprintf(out, "%*s</%s%s%s>\n", indent, "",
			    pref ?: "", pref ? ":" : "", data);
		}
	    }
	}
	break;

    case PIN_TYPE_TEXT:		/* XXX Text needs to be escaped */
	fprintf(out, "%s%s%s", is_debug ? "[text]" : "", data,
		is_debug ? "[/text]": "");
	break;

    case PIN_TYPE_UNESC:
	if (pin_parse_is_ws(data)) {
	    skipped = 1;
	} else {
	    fprintf(out, "%s%s%s", is_debug ? "[unes]" : "", data,
		    is_debug ? "[/unes]": "");
	}
	break;

    case PIN_TYPE_ATSTR:
	fprintf(out, " %s", data);
	break;

    case PIN_TYPE_ATTRIB:
	cp = pin_namepool_string(parsep->pp_insert->pin_tree->pt_workspace,
				     nodep->pn_name);
	fprintf(out, " %s=\"%s\"", cp, data);
	break;

    case PIN_TYPE_NS:
	ns_map = pin_ns_map_addr(pwp, pin_node_ns_contents(nodep));
	if (ns_map) {
	    pref = pin_namepool_string(pwp, ns_map->pnm_prefix);
	    uri = pin_namepool_string(pwp, ns_map->pnm_uri);
	    fprintf(out, " xmlns%s%s=\"%s\"",
		    pref ? ":" : "", pref ?: "", uri ?: "");
	} else {
	if (is_debug)
	    fprintf(out, "namespace: [null]\n");
	}
	break;

    case PIN_TYPE_EOF:
	if (is_debug)
	    fprintf(out, "<!-- end of output>\n");
	break;
    }

    if (!skipped)
	xmlp->xx_last_type = type;
    return 0;
}

void
pin_parse_emit_xml (pin_parse_t *parsep, FILE *out)
{
    pin_xml_output_t xml;

    bzero(&xml, sizeof(xml));
    xml.xx_out = out;
    xml.xx_incr = 3;

    pin_parse_emit(parsep, pin_parse_emit_xml_cb, &xml);
}

void
pin_parse_emit (pin_parse_t *parsep, pin_parse_emit_fn func, void *opaque)
{
    pin_insert_t *pip = parsep->pp_insert;
    pin_tree_t *ptp = pip->pin_tree;
    pin_workspace_t *pwp = ptp->pt_workspace;
    const char *cp;
    pin_node_id_t node_atom = ptp->pt_root;
    pin_node_id_t next_node_atom;
    pin_node_t *nodep;
    pin_depth_t last_depth = 0;
    unsigned need_eol_attrib = FALSE;

    while (!pin_node_id_is_null(node_atom)) {
	nodep = pin_node_addr(pwp, node_atom);
	if (nodep == NULL) {
	    psu_log("pin_parse_emit sees a null atom!");
	    break;
	}

	/* If this is the first non-attrib, let the emitter know */
	if (need_eol_attrib && !pin_parse_is_attrib(nodep->pn_type)) {
	    if (last_depth && last_depth > nodep->pn_depth) {
		func(parsep, PIN_TYPE_EOL_EMPTY, node_atom, nodep,
		     NULL, opaque);
	    } else {
		func(parsep, PIN_TYPE_EOL_ATTRIB, node_atom, nodep,
		     NULL, opaque);
	    }

	    /* Clear the need flag in case we hit the "if" below */
	    need_eol_attrib = FALSE;
	}

	/* We're looking at the first step out of layer of hierarchy */
	if (last_depth && last_depth > nodep->pn_depth) {
	    cp = pin_namepool_string(pwp, nodep->pn_name);
	    func(parsep, PIN_TYPE_CLOSE, node_atom, nodep, cp, opaque);
	    node_atom = nodep->pn_next;
	    last_depth = nodep->pn_depth;
	    continue;
	}

	need_eol_attrib = FALSE; /* Don't need it (yet) */

	if (nodep->pn_type == PIN_TYPE_ROOT) {
	    next_node_atom = (nodep->pn_contents != PA_NULL_ATOM)
		? pin_node_id(nodep->pn_contents) : nodep->pn_next;
	    func(parsep, nodep->pn_type, node_atom, nodep, NULL, opaque);

	} else if (nodep->pn_type == PIN_TYPE_ELT) {
	    cp = pin_namepool_string(pwp, nodep->pn_name);
	    func(parsep, nodep->pn_type, node_atom, nodep, cp, opaque);

	    /*
	     * If an ELT's contents are NULL, then this is an empty ELT.
	     * Otherwise we follow them to visit the children.  We have
	     * to handle this case explicitly, since there's not depth
	     * change to trigger the normal EMPTY logic above.
	     */
	    if (nodep->pn_contents == PA_NULL_ATOM) {
		next_node_atom = nodep->pn_next;
		func(parsep, PIN_TYPE_EOL_EMPTY, node_atom, nodep,
		     NULL, opaque);
		func(parsep, PIN_TYPE_CLOSE, node_atom, nodep, NULL, opaque);
	    } else {
		need_eol_attrib = TRUE;
		next_node_atom = pin_node_id(nodep->pn_contents);
	    }

	} else if (nodep->pn_type == PIN_TYPE_TEXT
		   || nodep->pn_type == PIN_TYPE_UNESC) {
	    cp = pin_textpool_string(pwp, nodep->pn_contents);
	    next_node_atom = nodep->pn_next;
	    func(parsep, nodep->pn_type, node_atom, nodep, cp, opaque);

	} else if (nodep->pn_type == PIN_TYPE_ATSTR) {
	    cp = pa_arb_atom_addr(pwp->pw_textpool,
				  pa_arb_atom(nodep->pn_contents));
	    next_node_atom = nodep->pn_next;
	    func(parsep, nodep->pn_type, node_atom, nodep, cp, opaque);
	    need_eol_attrib = TRUE;

	} else if (nodep->pn_type == PIN_TYPE_ATTRIB) {
	    cp = pa_arb_atom_addr(pwp->pw_textpool,
				  pa_arb_atom(nodep->pn_contents));
	    next_node_atom = nodep->pn_next;
	    func(parsep, nodep->pn_type, node_atom, nodep, cp, opaque);
	    need_eol_attrib = TRUE;

	} else if (nodep->pn_type == PIN_TYPE_NS) {
	    next_node_atom = nodep->pn_next;
	    func(parsep, nodep->pn_type, node_atom, nodep, NULL, opaque);
	    need_eol_attrib = TRUE;

	} else {
	    psu_log("unhandled node: %u", nodep->pn_type);
	    next_node_atom = pin_node_id_null_atom();
	}

	node_atom = next_node_atom;
	last_depth = nodep->pn_depth;
    }

    func(parsep, PIN_TYPE_EOF, pin_node_id_null_atom(), NULL, NULL, opaque);
}

#if 0
typedef struct pin_parse_as_source_s {
    pin_node_type_t pps_type;	/* Current type (PIN_TYPE_*) */
    pa_atom_t pps_atom;		/* Current atom number */
    pa_atom_t pps_next_atom;	/* Next atom number */
    pin_node_t *pps_nodep;	/* Current node */
    const char *pps_string;	/* String value */
    pin_depth_t pps_last_depth;	/* Previous depth */
} pin_parse_as_source_t;

{
    pin_parse_as_source_t data;

    for (type = pin_parse_as_source(parsep, pwp, PIN_TYPE_INIT, &data);
	 type != PIN_TYPE_EOF;
	 type = pin_parse_as_source(parsep, pwp, type, &data)) {
	continue;
    }
}

pin_node_type_t
pin_parse_as_source (pin_parse_t *parsep, pin_workspace_t *pwp,
		    pin_node_type_t type, pin_parse_as_source_t *datap)
{
    if (type == PIN_TYPE_INIT) {
	bzero(datap, sizeof(*datap));
	datap->pps_atom = parsep->pp_insert->pin_tree->pt_root;
    }
    
    pa_atom_t node_atom = *datap->pps_atom;

    while (node_atom != PA_NULL_ATOM) {
	nodep = pin_node_addr(pwp, node_atom);
	if (nodep == NULL) {
	    psu_log("pin_parse_emit sees a null atom!");
	    break;
	}

	/* We're looking at the first step out of layer of hierarchy */
	if (last_depth && last_depth > nodep->pn_depth) {
	    cp = pin_namepool_string(pwp, nodep->pn_name);
	    func(parsep, PIN_TYPE_CLOSE, node_atom, nodep, cp, opaque);
	    node_atom = nodep->pn_next;
	    last_depth = nodep->pn_depth;
	    continue;
	}

	need_eol_attrib = FALSE; /* Don't need it (yet) */

	if (nodep->pn_type == PIN_TYPE_ROOT) {
	    next_node_atom = nodep->pn_contents ?: nodep->pn_next;
	    func(parsep, nodep->pn_type, node_atom, nodep, NULL, opaque);

	} else if (nodep->pn_type == PIN_TYPE_ELT) {
	    cp = pin_namepool_string(pwp, nodep->pn_name);
	    func(parsep, nodep->pn_type, node_atom, nodep, cp, opaque);

	    /*
	     * If an ELT's contents are NULL, then this is an empty ELT.
	     * Otherwise we follow them to visit the children.  We have
	     * to handle this case explicitly, since there's not depth
	     * change to trigger the normal EMPTY logic above.
	     */
	    if (nodep->pn_contents == PA_NULL_ATOM) {
		next_node_atom = nodep->pn_next;
		func(parsep, PIN_TYPE_EOL_EMPTY, node_atom, nodep,
		     NULL, opaque);
		func(parsep, PIN_TYPE_CLOSE, node_atom, nodep, NULL, opaque);
	    } else {
		need_eol_attrib = TRUE;
		next_node_atom = nodep->pn_contents;
	    }

	} else if (nodep->pn_type == PIN_TYPE_TEXT
		   || nodep->pn_type == PIN_TYPE_UNESC) {
	    cp = pin_textpool_string(pwp, nodep->pn_contents);
	    next_node_atom = nodep->pn_next;
	    func(parsep, nodep->pn_type, node_atom, nodep, cp, opaque);

	} else if (nodep->pn_type == PIN_TYPE_ATSTR) {
	    cp = pa_arb_atom_addr(pwp->pw_textpool, pa_arb_atom(nodep->pn_contents));
	    next_node_atom = nodep->pn_next;
	    func(parsep, nodep->pn_type, node_atom, nodep, cp, opaque);
	    need_eol_attrib = TRUE;

	} else if (nodep->pn_type == PIN_TYPE_ATTRIB) {
	    cp = pa_arb_atom_addr(pwp->pw_textpool, pa_arb_atom(nodep->pn_contents));
	    next_node_atom = nodep->pn_next;
	    func(parsep, nodep->pn_type, node_atom, nodep, cp, opaque);
	    need_eol_attrib = TRUE;

	} else if (nodep->pn_type == PIN_TYPE_NS) {
	    next_node_atom = nodep->pn_next;
	    func(parsep, nodep->pn_type, node_atom, nodep, NULL, opaque);
	    need_eol_attrib = TRUE;

	} else {
	    psu_log("unhandled node: %u", nodep->pn_type);
	    next_node_atom = PA_NULL_ATOM;
	}

	node_atom = next_node_atom;
	last_depth = nodep->pn_depth;
    }

    func(parsep, PIN_TYPE_EOF, PA_NULL_ATOM, NULL, NULL, opaque);
}
#endif

void
pin_parse_set_rulebook (pin_parse_t *parsep, pin_rulebook_t *rulebook)
{
    parsep->pp_rulebook = rulebook;

    pin_insert_t *pip = parsep->pp_insert;
    pip->pin_stack[pip->pin_depth].ps_statep = rulebook
	? pin_rulebook_state(rulebook, pin_rstate_id(PIN_STATE_INITIAL)) : NULL;
}

void
pin_parse_set_default_rule (pin_parse_t *parsep, pin_action_type_t type)
{
    parsep->pp_default_rule.pr_flags = PRF_MATCH_ALL;
    parsep->pp_default_rule.pr_action = type;
}

void
pin_parse_set_filter (pin_parse_t *parsep, xo_filter_t *xfp)
{
    parsep->pp_filter = xfp;
}

void
pin_parse_set_mode (pin_parse_t *parsep, const char *mode)
{
    parsep->pp_context.pctx_mode = mode;
}
