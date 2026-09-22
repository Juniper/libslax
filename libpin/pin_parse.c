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
#include <libpsu/psuerror.h>
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
#include <libpin/pin_exec.h>
#include <libpin/pin_sort.h>

/* Forward declarations */
static void pin_body_exec_advance(pin_parse_t *parsep);
static int pin_is_ws_only(const char *data, size_t len);

#include <libxo/xo.h>
#include "xo_filter.h"
#include <libpin/pin_filter.h>


/*
 * Find 'name' in the raw attribute string (format: name="value" ...).
 * Returns a pointer to the value and sets *lenp to its length, or
 * returns NULL if the attribute is not found.  The returned pointer
 * points into 'attribs'; it is NOT NUL-terminated.
 */
static const char *
pin_body_find_attrib (const char *attribs, const char *name, size_t *lenp)
{
    if (attribs == NULL || name == NULL)
	return NULL;

    size_t nlen = strlen(name);
    const char *cp = attribs;

    while (*cp) {
	/* Skip whitespace between attributes */
	while (isspace((unsigned char) *cp))
	    cp += 1;
	if (*cp == '\0')
	    break;

	/* Read attribute name */
	const char *aname = cp;
	while (*cp && *cp != '=' && !isspace((unsigned char) *cp))
	    cp += 1;
	size_t alen = cp - aname;

	/* Skip whitespace and '=' */
	while (isspace((unsigned char) *cp))
	    cp += 1;
	if (*cp != '=')
	    break;
	cp += 1;

	/* Skip whitespace before quoted value */
	while (isspace((unsigned char) *cp))
	    cp += 1;
	if (*cp != '"' && *cp != '\'')
	    break;

	/* Read quoted value */
	char q = *cp;
	cp += 1;
	const char *aval = cp;
	while (*cp && *cp != q)
	    cp += 1;
	size_t vlen = cp - aval;
	if (*cp == q)
	    cp += 1;

	/* Check for a match */
	if (alen == nlen && strncmp(aname, name, nlen) == 0) {
	    if (lenp)
		*lenp = vlen;
	    return aval;
	}
    }
    return NULL;
}

/*
 * Parse the raw XML attribute string and feed each name=value pair to
 * the filter via xo_filter_walk_attr.  Called on demand from BIA_IF
 * evaluation — attributes are only parsed when a condition check needs them.
 */
static void
pin_body_feed_attribs (xo_filter_t *xfp, const char *attribs)
{
    if (xfp == NULL || attribs == NULL || *attribs == '\0')
	return;

    const char *cp = attribs;
    while (*cp) {
	while (isspace((unsigned char) *cp))
	    cp += 1;
	if (*cp == '\0')
	    break;

	const char *name = cp;
	while (*cp && *cp != '=' && !isspace((unsigned char) *cp))
	    cp += 1;
	size_t nlen = cp - name;
	if (nlen == 0)
	    break;

	while (isspace((unsigned char) *cp))
	    cp += 1;
	if (*cp != '=')
	    break;
	cp += 1;

	while (isspace((unsigned char) *cp))
	    cp += 1;
	if (*cp != '"' && *cp != '\'')
	    break;
	char q = *cp++;
	const char *val = cp;
	while (*cp && *cp != q)
	    cp += 1;
	size_t vlen = cp - val;
	if (*cp == q)
	    cp += 1;

	xo_filter_walk_attr(NULL, xfp, name, (ssize_t) nlen,
			    val, (ssize_t) vlen);
    }
}

/*
 * Evaluate a BIA_IF condition by walking the matched element + attributes
 * through its pre-compiled per-instruction filter.
 * Returns 1 if the condition is true, 0 if false.
 */
static int
pin_body_eval_if (pin_parse_t *parsep, pin_body_instr_t *instr,
		  pin_body_frame_t *bfp)
{
    pin_rulebook_t *rb = parsep->pp_rulebook;
    if (rb == NULL || instr->bi_filter_idx >= rb->prb_if_filter_count)
	return 0;

    xo_filter_t *xfp = rb->prb_if_filters[instr->bi_filter_idx];
    if (xfp == NULL)
	return 0;

    const char *elt = pin_parse_namepool_string(parsep, bfp->pbf_match_name);
    if (elt == NULL)
	return 0;

    xo_filter_walk_open(NULL, xfp, elt, -1);
    pin_body_feed_attribs(xfp, bfp->pbf_match_attribs);

    int result = (xo_filter_walk_status(NULL, xfp) == XO_STATUS_FULL);

    xo_filter_walk_close(NULL, xfp, elt, -1);

    return result;
}

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
	goto fail;	/* pin_source_open already reported the error */

    /* The pin_tree_t is the tree we'll be inserting into */
    ptp = calloc(1, sizeof(*ptp));
    if (ptp == NULL) {
	psu_error(input, 0, "out of memory (pin_tree_t)");
	goto fail;
    }

    ptp->pt_infop = pa_mmap_header(pmp, pin_mk_name(namebuf, name, "tree"),
				   PA_TYPE_TREE, 0, sizeof(*ptp->pt_infop));
    ptp->pt_max_depth = 0;
    ptp->pt_workspace = workp;

    /* The pin_insert_t is the point in the tree at which we are inserting */
    pip = calloc(1, sizeof(*pip));
    if (pip == NULL) {
	psu_error(input, 0, "out of memory (pin_insert_t)");
	goto fail;
    }
    pip->pin_tree = ptp;
    pip->pin_depth = 0;
    pip->pin_relation = PIR_CHILD;

    /* And finally, fill in the parse structure */
    parsep = calloc(1, sizeof(*parsep));
    if (parsep == NULL) {
	psu_error(input, 0, "out of memory (pin_parse_t)");
	goto fail;
    }
    parsep->pp_srcp = srcp;
    parsep->pp_insert = pip;

    /* Fill in the default default rule */
    parsep->pp_default_rule.pr_flags = PRF_MATCH_ALL;
    parsep->pp_default_rule.pr_action = PIA_SAVE;

    pin_node_id_t node_atom;
    nodep = pin_node_alloc(workp, &node_atom);
    if (nodep == NULL) {
	psu_error(input, 0, "node pool exhausted");
	goto fail;
    }
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
    if (parsep->pp_ws_pending) {
	free(parsep->pp_ws_pending);
	parsep->pp_ws_pending = NULL;
    }
    if (parsep->pp_strip_stack) {
	free(parsep->pp_strip_stack);
	parsep->pp_strip_stack = NULL;
    }
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
    pin_istack_t *parent = &pip->pin_stack[pip->pin_depth];
    pin_rstate_t *statep = parent->ps_statep;
    int parent_retain = parent->ps_context_retain;

    pip->pin_depth += 1;
    pin_istack_t *frame = &pip->pin_stack[pip->pin_depth];
    frame->ps_atom = atom;
    frame->ps_node = nodep;
    frame->ps_statep = statep;
    frame->ps_action = PIA_NONE;
    frame->ps_old_name = pin_name_id_null_atom();
    frame->ps_context_retain = parent_retain;
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

    if (psp->ps_context_retain)
	nodep->pn_flags |= PNF_TRANSIENT;

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

void
pin_insert_open (pin_parse_t *parsep, pin_name_id_t name_id,
		const char *prefix, const char *name, char *attribs,
		pin_action_type_t type)
{
    if (parsep->pp_capture > 0)
	return;

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

void
pin_insert_close (pin_parse_t *parsep, const char *prefix UNUSED, const char *name)
{
    if (parsep->pp_capture > 0)
	return;

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

    pin_op_id_t close_ops = psp->ps_close_ops;
    pin_node_id_t context_node = psp->ps_atom;
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
     * Op-dispatch: execute the compiled op sequence against the retained
     * element.  Ops emit into the output at the current (post-pop) depth,
     * so emitted nodes are siblings of the retained element and not transient.
     */
    if (!pin_op_id_is_null(close_ops)) {
	pin_exec_run(&parsep->pp_exec, parsep, close_ops, context_node);
	return;
    }

    /*
     * Body FSM: detect when the matched element (opened by BIA_COPY or
     * BIA_APPLY) closes.  pbf_copy_depth is the output depth set when
     * pin_insert_open(match_name) was called; after the pop above it is
     * pbf_copy_depth - 1.
     */
    pin_body_exec_t *body = &pip->pin_body;
    if (body->pbe_depth > 0) {
	pin_body_frame_t *bfp = &body->pbe_stack[body->pbe_depth - 1];
	if ((bfp->pbf_mode == PBMODE_COPY
		    || bfp->pbf_mode == PBMODE_FOR_EACH_WAIT)
		&& pip->pin_depth == (pin_depth_t)(bfp->pbf_copy_depth - 1)) {
	    bfp->pbf_mode = PBMODE_EXEC;
	    pin_body_exec_advance(parsep);
	}
    }
}

void
pin_insert_text (pin_parse_t *parsep, const char *data, size_t len,
		pin_node_type_t type)
{
    if (parsep->pp_capture > 0) {
	/* Redirect text to capture buffer (for comment/PI/message bodies) */
	int newlen = parsep->pp_cap_len + (int) len;
	if (newlen >= parsep->pp_cap_size) {
	    int newsize = parsep->pp_cap_size ? parsep->pp_cap_size * 2 : 64;
	    while (newsize <= newlen)
		newsize *= 2;
	    char *nd = realloc(parsep->pp_cap_data, (size_t) newsize);
	    if (nd == NULL)
		return;
	    parsep->pp_cap_data = nd;
	    parsep->pp_cap_size = newsize;
	}
	memcpy(parsep->pp_cap_data + parsep->pp_cap_len, data, len);
	parsep->pp_cap_len += (int) len;
	parsep->pp_cap_data[parsep->pp_cap_len] = '\0';
	return;
    }

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

void
pin_capture_start (pin_parse_t *parsep, int mode, unsigned name_atom)
{
    parsep->pp_capture = mode;
    parsep->pp_cap_name = name_atom;
    parsep->pp_cap_len = 0;
    /* Re-use existing buffer if already allocated */
}

void
pin_capture_flush (pin_parse_t *parsep)
{
    int mode = parsep->pp_capture;
    const char *data = parsep->pp_cap_data ? parsep->pp_cap_data : "";
    int len = parsep->pp_cap_len;

    parsep->pp_capture = 0;
    parsep->pp_cap_len = 0;

    if (mode == 1) {
	/* xsl:message: write to stderr */
	fprintf(stderr, "%.*s\n", len, data);
    } else if (mode == 2) {
	/* xsl:comment: insert as XML comment node */
	pin_insert_text(parsep, data, (size_t) len, PIN_TYPE_COMMENT);
    } else if (mode == 3) {
	/* xsl:processing-instruction: "target\ncontent" format */
	pin_workspace_t *pwp = pin_parse_workspace(parsep);
	pin_name_id_t nid = pin_name_id(parsep->pp_cap_name);
	const char *target = pin_namepool_string(pwp, nid);
	if (target) {
	    size_t tlen = strlen(target);
	    size_t total = tlen + 1 + (size_t) len;
	    char *buf = malloc(total + 1);
	    if (buf) {
		memcpy(buf, target, tlen);
		buf[tlen] = '\n';
		memcpy(buf + tlen + 1, data, (size_t) len);
		buf[total] = '\0';
		pin_insert_text(parsep, buf, total, PIN_TYPE_PI);
		free(buf);
	    }
	}
    }
}

/*
 * For-each helpers
 */

/*
 * Append one node id to a dynamically-grown array.
 */
static void
pin_body_collect_append (pin_node_id_t nid,
			 pin_node_id_t **outp, int *cntp, int *capp)
{
    if (*cntp >= *capp) {
	int newsize = *capp ? *capp * 2 : 8;
	pin_node_id_t *nd = realloc(*outp, (size_t) newsize * sizeof(**outp));
	if (nd == NULL)
	    return;
	*outp = nd;
	*capp = newsize;
    }
    (*outp)[(*cntp)++] = nid;
}

/*
 * Collect all terminal nodes reachable from start by walking slash-separated
 * path steps.  Each step descends to ALL direct children with that name.
 * Results are appended to *outp / *cntp / *capp (caller must free *outp).
 */
static void
pin_body_collect_path (pin_workspace_t *pwp, pin_node_id_t start,
		       const char *path, size_t plen,
		       pin_node_id_t **outp, int *cntp, int *capp)
{
    const char *slash = memchr(path, '/', plen);
    size_t step_len = slash ? (size_t)(slash - path) : plen;
    int last_step = (slash == NULL);
    const char *rest = slash ? slash + 1 : NULL;
    size_t rest_len = slash ? plen - step_len - 1 : 0;

    if (step_len == 0 || (step_len == 1 && path[0] == '.')) {
	if (last_step)
	    pin_body_collect_append(start, outp, cntp, capp);
	else
	    pin_body_collect_path(pwp, start, rest, rest_len, outp, cntp, capp);
	return;
    }

    char stepbuf[step_len + 1];
    memcpy(stepbuf, path, step_len);
    stepbuf[step_len] = '\0';

    pin_name_id_t step_name = pin_namepool_atom(pwp, stepbuf, FALSE);
    if (pin_name_id_is_null(step_name))
	return;

    pin_node_t *node = pin_node_addr(pwp, start);
    if (node == NULL)
	return;

    pin_depth_t target_depth = node->pn_depth + 1;
    for (pin_node_id_t cid = pin_node_child(node);
	    !pin_node_id_is_null(cid); ) {
	pin_node_t *child = pin_node_addr(pwp, cid);
	if (child == NULL || child->pn_depth < target_depth)
	    break;
	if (child->pn_depth == target_depth
		&& child->pn_type == PIN_TYPE_ELT
		&& pin_name_id_equal(child->pn_name, step_name)) {
	    if (last_step)
		pin_body_collect_append(cid, outp, cntp, capp);
	    else
		pin_body_collect_path(pwp, cid, rest, rest_len,
				      outp, cntp, capp);
	}
	cid = child->pn_next;
    }
}

/*
 * Look up a variable value (as a namepool string atom) from a body frame.
 * name/nlen are the variable name (without leading '$').
 * Returns a string or NULL if not found.
 */
static const char *
pin_body_var_get (pin_parse_t *parsep, pin_body_frame_t *bfp,
		  const char *name, size_t nlen)
{
    char nbuf[nlen + 1];
    memcpy(nbuf, name, nlen);
    nbuf[nlen] = '\0';

    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_name_id_t nid = pin_namepool_atom(pwp, nbuf, FALSE);
    if (pin_name_id_is_null(nid))
	return NULL;

    for (int i = 0; i < bfp->pbf_var_count; i++) {
	if (pin_name_id_equal(bfp->pbf_var_names[i], nid))
	    return pin_namepool_string(pwp, bfp->pbf_var_values[i]);
    }
    /* Fall back to global variables registered in the rulebook */
    pin_rulebook_t *rb = parsep->pp_rulebook;
    if (rb) {
	pin_name_id_t gval = pin_rulebook_global_find(rb, nid);
	if (!pin_name_id_is_null(gval))
	    return pin_namepool_string(pwp, gval);
    }
    return NULL;
}

/*
 * Free heap storage owned by a body frame.  Called before pbe_depth is
 * decremented so the frame can be reused (zeroed) on the next push.
 */
static void
pin_body_frame_cleanup (pin_body_frame_t *bfp)
{
    xo_buf_cleanup(&bfp->pbf_value_cache);
    xo_free(bfp->pbf_var_names);
    xo_free(bfp->pbf_var_values);
    bfp->pbf_var_names = NULL;
    bfp->pbf_var_values = NULL;
    bfp->pbf_var_count = 0;
    bfp->pbf_var_size = 0;
}

/*
 * Store a variable binding in the body frame, growing the arrays as needed.
 */
static void
pin_body_var_set (pin_body_frame_t *bfp, pin_name_id_t name, pin_name_id_t value)
{
    for (int i = 0; i < bfp->pbf_var_count; i++) {
	if (pin_name_id_equal(bfp->pbf_var_names[i], name)) {
	    bfp->pbf_var_values[i] = value;
	    return;
	}
    }
    if (bfp->pbf_var_count >= bfp->pbf_var_size) {
	int newsize = bfp->pbf_var_size ? bfp->pbf_var_size * 2 : 4;
	pin_name_id_t *nn = xo_realloc(bfp->pbf_var_names,
				       newsize * sizeof(*bfp->pbf_var_names));
	pin_name_id_t *nv = xo_realloc(bfp->pbf_var_values,
				       newsize * sizeof(*bfp->pbf_var_values));
	if (nn)
	    bfp->pbf_var_names = nn;
	if (nv)
	    bfp->pbf_var_values = nv;
	if (nn == NULL || nv == NULL)
	    return;
	bfp->pbf_var_size = newsize;
    }
    bfp->pbf_var_names[bfp->pbf_var_count] = name;
    bfp->pbf_var_values[bfp->pbf_var_count] = value;
    bfp->pbf_var_count += 1;
}

/*
 * Expand an attribute value template (AVT) string, substituting {$var}
 * with the bound variable's value.  Returns a newly-allocated string
 * (caller must free), or NULL on allocation failure.
 */
static char *
pin_body_avt_expand (pin_parse_t *parsep, pin_body_frame_t *bfp,
		     const char *tmpl)
{
    char buf[4096];
    size_t pos = 0;
    const char *p = tmpl;

    while (*p && pos < sizeof(buf) - 1) {
	/* Escaped {{ → single '{' */
	if (p[0] == '{' && p[1] == '{') {
	    buf[pos++] = '{';
	    p += 2;
	    continue;
	}
	/* Escaped }} → single '}' */
	if (p[0] == '}' && p[1] == '}') {
	    buf[pos++] = '}';
	    p += 2;
	    continue;
	}
	/* AVT expression: {expr} */
	if (p[0] == '{') {
	    const char *close = strchr(p + 1, '}');
	    if (close) {
		const char *expr = p + 1;
		size_t elen = (size_t)(close - expr);
		if (elen > 0) {
		    char valbuf[256];
		    valbuf[0] = '\0';
		    if (expr[0] == '$') {
			const char *val = pin_body_var_get(parsep, bfp,
							   expr + 1, elen - 1);
			if (val)
			    snprintf(valbuf, sizeof(valbuf), "%s", val);
		    } else if (!pin_node_id_is_null(bfp->pbf_ctx_node)) {
			pin_workspace_t *pwp = pin_parse_workspace(parsep);
			pin_exec_eval_expr_string(pwp, bfp->pbf_ctx_node,
						  expr, elen,
						  valbuf, sizeof(valbuf));
		    }
		    if (valbuf[0] != '\0') {
			size_t vlen = strlen(valbuf);
			if (pos + vlen < sizeof(buf)) {
			    memcpy(buf + pos, valbuf, vlen);
			    pos += vlen;
			}
		    }
		}
		p = close + 1;
		continue;
	    }
	}
	buf[pos++] = *p++;
    }
    buf[pos] = '\0';
    return strdup(buf);
}

/*
 * Evaluate a BIA_IF condition against a specific context node rather than
 * the matched element stored in the body frame.
 */
static int
pin_body_eval_if_ctx (pin_parse_t *parsep, pin_body_instr_t *instr,
		      pin_node_id_t ctx_node)
{
    pin_rulebook_t *rb = parsep->pp_rulebook;
    if (rb == NULL || instr->bi_filter_idx >= rb->prb_if_filter_count)
	return 0;

    xo_filter_t *xfp = rb->prb_if_filters[instr->bi_filter_idx];
    if (xfp == NULL)
	return 0;

    pin_workspace_t *pwp = pin_parse_workspace(parsep);
    pin_node_t *nodep = pin_node_addr(pwp, ctx_node);
    if (nodep == NULL)
	return 0;

    const char *elt = pin_namepool_string(pwp, nodep->pn_name);
    if (elt == NULL)
	return 0;

    /* Build attribute string from ctx_node's extracted attributes */
    char abuf[4096];
    size_t apos = 0;
    pin_depth_t elt_depth = nodep->pn_depth;

    for (pin_node_id_t aid = pin_node_child(nodep);
	    !pin_node_id_is_null(aid); ) {
	pin_node_t *ap = pin_node_addr(pwp, aid);
	if (ap == NULL || ap->pn_depth <= elt_depth)
	    break;
	if (ap->pn_type == PIN_TYPE_ATTRIB) {
	    const char *aname = pin_namepool_string(pwp, ap->pn_name);
	    const char *aval = pin_textpool_string(pwp,
					pa_arb_atom_of(pin_node_text(ap)));
	    if (aname && aval) {
		if (apos > 0 && apos < sizeof(abuf) - 1)
		    abuf[apos++] = ' ';
		int n = snprintf(abuf + apos, sizeof(abuf) - apos,
				 "%s=\"%s\"", aname, aval);
		if (n > 0)
		    apos += (size_t) n;
	    }
	}
	aid = ap->pn_next;
    }
    abuf[apos] = '\0';

    xo_filter_walk_open(NULL, xfp, elt, -1);
    pin_body_feed_attribs(xfp, apos > 0 ? abuf : NULL);
    int result = (xo_filter_walk_status(NULL, xfp) == XO_STATUS_FULL);
    xo_filter_walk_close(NULL, xfp, elt, -1);

    return result;
}

/*
 * Add a single name=value attribute node to the current element in the tree.
 * Must be called while the element is on top of the insert stack (i.e., after
 * pin_insert_open and before any non-attribute children are added).
 */
void
pin_insert_attrib_pair (pin_parse_t *parsep, const char *name, const char *value)
{
    pin_insert_t *pip = parsep->pp_insert;
    pin_workspace_t *pwp = pip->pin_tree->pt_workspace;

    pin_name_id_t name_id = pin_namepool_atom(pwp, name, TRUE);
    if (pin_name_id_is_null(name_id))
	return;

    size_t vlen = strlen(value);
    pa_arb_t *prp = pwp->pw_textpool;
    pa_arb_atom_t val_atom = pa_arb_alloc(prp, vlen + 1);
    char *cp = pa_arb_atom_addr(prp, val_atom);
    if (cp == NULL)
	return;
    memcpy(cp, value, vlen);
    cp[vlen] = '\0';

    pin_node_id_t node_atom = pin_insert_node(pip, "pin_insert_attrib_pair",
					      name, strlen(name),
					      PIN_TYPE_ATTRIB, name_id,
					      pa_arb_atom_of(val_atom));
    if (pin_node_id_is_null(node_atom)) {
	pa_arb_free_atom(prp, val_atom);
	return;
    }

    pin_istack_t *psp = &pip->pin_stack[pip->pin_depth];
    if (psp->ps_node)
	psp->ps_node->pn_flags |= PNF_ATTRIBS_PRESENT;
}

/*
 * Execute a for-each body instruction sub-list synchronously.
 * All data is read from the retained tree; this function never pauses for
 * streaming events.  bfp->pbf_ctx_node / pbf_position / pbf_last must be
 * set by the caller before each invocation.
 */
static void
pin_body_foreach_body (pin_parse_t *parsep, pin_body_instr_id_t head,
		       pin_body_frame_t *bfp)
{
    pin_rulebook_t *rb = parsep->pp_rulebook;
    pin_workspace_t *pwp = pin_parse_workspace(parsep);

    for (pin_body_instr_id_t pc = head; !pin_body_instr_id_is_null(pc); ) {
	pin_body_instr_t *instr = pin_body_instr_addr(rb, pc);
	if (instr == NULL)
	    break;

	pc = instr->bi_next;

	switch (instr->bi_type) {
	case BIA_EMIT_OPEN: {
	    const char *tag = pin_namepool_string(pwp, instr->bi_tag);
	    if (tag) {
		char *attribs = NULL;
		pin_action_type_t act = PIA_SAVE;

		if (!pin_name_id_is_null(instr->bi_text)) {
		    /* AVT template: expand {$var} references */
		    const char *avt = pin_namepool_string(pwp, instr->bi_text);
		    if (avt && *avt) {
			attribs = pin_body_avt_expand(parsep, bfp, avt);
			act = PIA_SAVE_ATTRIB;
		    }
		} else if (!pin_name_id_is_null(instr->bi_select)) {
		    const char *astr = pin_namepool_string(pwp, instr->bi_select);
		    if (astr && *astr) {
			attribs = strdup(astr);
			act = PIA_SAVE_ATTRIB;
		    }
		}
		pin_insert_open(parsep, instr->bi_tag, NULL, tag, attribs, act);
		free(attribs);
	    }
	    break;
	}
	case BIA_EMIT_TEXT: {
	    const char *text = pin_namepool_string(pwp, instr->bi_text);
	    if (text)
		pin_insert_text(parsep, text, strlen(text), PIN_TYPE_TEXT);
	    break;
	}
	case BIA_EMIT_CLOSE: {
	    const char *tag = pin_namepool_string(pwp, instr->bi_tag);
	    if (tag)
		pin_insert_close(parsep, NULL, tag);
	    break;
	}
	case BIA_COPY: {
	    /* Emit the context node from the retained tree (no streaming) */
	    if (!pin_node_id_is_null(bfp->pbf_ctx_node))
		pin_exec_emit_node(parsep, pwp, bfp->pbf_ctx_node);
	    break;
	}
	case BIA_VALUE_OF: {
	    if (!pin_name_id_is_null(instr->bi_text)) {
		/* select="path/or/expr" — evaluate against context node */
		const char *path = pin_namepool_string(pwp, instr->bi_text);
		if (path && !pin_node_id_is_null(bfp->pbf_ctx_node)) {
		    char vbuf[512];
		    pin_exec_eval_expr_string(pwp, bfp->pbf_ctx_node,
					     path, strlen(path),
					     vbuf, sizeof(vbuf));
		    if (vbuf[0])
			pin_insert_text(parsep, vbuf, strlen(vbuf), PIN_TYPE_TEXT);
		}
	    } else if (!pin_name_id_is_null(instr->bi_select)) {
		/* select="@attr": get attribute from context node */
		const char *aname = pin_namepool_string(pwp, instr->bi_select);
		if (aname && !pin_node_id_is_null(bfp->pbf_ctx_node)) {
		    pin_node_t *ctx = pin_node_addr(pwp, bfp->pbf_ctx_node);
		    pin_name_id_t aid = pin_namepool_atom(pwp, aname, FALSE);
		    if (ctx && !pin_name_id_is_null(aid)) {
			const char *val = pin_get_attrib_string(pwp, ctx, aid);
			if (val)
			    pin_insert_text(parsep, val, strlen(val),
					    PIN_TYPE_TEXT);
		    }
		}
	    } else {
		/* select=".": get text of context node */
		if (!pin_node_id_is_null(bfp->pbf_ctx_node)) {
		    pin_name_id_t tid = pin_exec_text_of(pwp, bfp->pbf_ctx_node);
		    const char *text = pin_namepool_string(pwp, tid);
		    if (text)
			pin_insert_text(parsep, text, strlen(text),
					PIN_TYPE_TEXT);
		}
	    }
	    break;
	}
	case BIA_IF: {
	    int truth = pin_body_eval_if_ctx(parsep, instr, bfp->pbf_ctx_node);
	    if (!truth)
		pc = instr->bi_else;
	    break;
	}
	case BIA_IF_POSITION: {
	    uint32_t N = instr->bi_filter_idx;
	    uint32_t op = pin_name_id_atom_of(instr->bi_tag);
	    uint32_t pos = bfp->pbf_position;
	    int truth;
	    switch (op) {
	    case PCMP_LT: truth = (pos <  N); break;
	    case PCMP_LE: truth = (pos <= N); break;
	    case PCMP_EQ: truth = (pos == N); break;
	    case PCMP_NE: truth = (pos != N); break;
	    case PCMP_GT: truth = (pos >  N); break;
	    case PCMP_GE: truth = (pos >= N); break;
	    default:      truth = 0;
	    }
	    if (!truth)
		pc = instr->bi_else;
	    break;
	}
	case BIA_VARIABLE: {
	    /* Evaluate select (path or arithmetic) and store in pbf_vars */
	    const char *sel = !pin_name_id_is_null(instr->bi_select)
			      ? pin_namepool_string(pwp, instr->bi_select) : NULL;
	    pin_name_id_t val = pin_name_id_null_atom();

	    if (sel && sel[0] != '\0' && !pin_node_id_is_null(bfp->pbf_ctx_node)) {
		char vbuf[128];
		pin_exec_eval_expr_string(pwp, bfp->pbf_ctx_node,
					  sel, strlen(sel), vbuf, sizeof(vbuf));
		if (vbuf[0] != '\0')
		    val = pin_namepool_atom(pwp, vbuf, TRUE);
	    }

	    if (!pin_name_id_is_null(instr->bi_tag))
		pin_body_var_set(bfp, instr->bi_tag, val);
	    break;
	}
	case BIA_ELEMENT_OPEN: {
	    const char *name_expr = !pin_name_id_is_null(instr->bi_select)
				    ? pin_namepool_string(pwp, instr->bi_select) : NULL;
	    if (name_expr) {
		char *tagname = pin_body_avt_expand(parsep, bfp, name_expr);
		const char *tag = tagname ? tagname : name_expr;
		pin_name_id_t tag_id = pin_namepool_atom(pwp, tag, TRUE);
		char *attribs = NULL;
		pin_action_type_t act = PIA_SAVE;
		if (!pin_name_id_is_null(instr->bi_text)) {
		    const char *avt = pin_namepool_string(pwp, instr->bi_text);
		    if (avt && *avt) {
			attribs = pin_body_avt_expand(parsep, bfp, avt);
			act = PIA_SAVE_ATTRIB;
		    }
		}
		pin_insert_open(parsep, tag_id, NULL, tag, attribs, act);
		free(attribs);
		free(tagname);
	    }
	    break;
	}
	case BIA_ELEMENT_CLOSE: {
	    pin_insert_t *pip = parsep->pp_insert;
	    pin_istack_t *psp = &pip->pin_stack[pip->pin_depth];
	    if (psp->ps_node != NULL) {
		const char *tag = pin_namepool_string(pwp, psp->ps_node->pn_name);
		if (tag)
		    pin_insert_close(parsep, NULL, tag);
	    }
	    break;
	}
	case BIA_ATTRIB: {
	    const char *aname = !pin_name_id_is_null(instr->bi_tag)
				? pin_namepool_string(pwp, instr->bi_tag) : NULL;
	    if (aname) {
		char valbuf[512];
		valbuf[0] = '\0';
		if (!pin_name_id_is_null(instr->bi_text)) {
		    const char *avt = pin_namepool_string(pwp, instr->bi_text);
		    if (avt && *avt) {
			char *expanded = pin_body_avt_expand(parsep, bfp, avt);
			if (expanded) {
			    snprintf(valbuf, sizeof(valbuf), "%s", expanded);
			    free(expanded);
			}
		    }
		}
		pin_insert_attrib_pair(parsep, aname, valbuf);
	    }
	    break;
	}
	case BIA_FOR_EACH:
	    /* Nested for-each: not yet supported in foreach body */
	    break;
	case BIA_COPY_OPEN: {
	    /* Shallow copy: emit open tag of context node */
	    if (!pin_node_id_is_null(bfp->pbf_ctx_node)) {
		pin_node_t *ctx = pin_node_addr(pwp, bfp->pbf_ctx_node);
		if (ctx != NULL) {
		    const char *tag = pin_namepool_string(pwp, ctx->pn_name);
		    if (tag)
			pin_insert_open(parsep, ctx->pn_name, NULL, tag, NULL, PIA_SAVE);
		}
	    }
	    break;
	}
	case BIA_MESSAGE_OPEN:
	    pin_capture_start(parsep, 1, 0);
	    break;
	case BIA_MESSAGE_CLOSE:
	    pin_capture_flush(parsep);
	    if (!pin_name_id_is_null(instr->bi_tag))
		exit(1);
	    break;
	case BIA_COMMENT_OPEN:
	    pin_capture_start(parsep, 2, 0);
	    break;
	case BIA_COMMENT_CLOSE:
	    pin_capture_flush(parsep);
	    break;
	case BIA_PI_OPEN:
	    pin_capture_start(parsep, 3,
			      (unsigned) pin_name_id_atom_of(instr->bi_tag));
	    break;
	case BIA_PI_CLOSE:
	    pin_capture_flush(parsep);
	    break;
	case BIA_NUMBER: {
	    const char *expr = !pin_name_id_is_null(instr->bi_select)
			       ? pin_namepool_string(pwp, instr->bi_select) : NULL;
	    if (expr && !pin_node_id_is_null(bfp->pbf_ctx_node)) {
		char vbuf[64];
		pin_exec_eval_expr_string(pwp, bfp->pbf_ctx_node,
					  expr, strlen(expr), vbuf, sizeof(vbuf));
		if (vbuf[0]) {
		    long n = strtol(vbuf, NULL, 10);
		    char nbuf[32];
		    snprintf(nbuf, sizeof(nbuf), "%ld", n);
		    pin_insert_text(parsep, nbuf, strlen(nbuf), PIN_TYPE_TEXT);
		}
	    }
	    break;
	}
	case BIA_JUMP:
	    break;
	case BIA_GOTO:
	    pc = instr->bi_else;
	    break;
	default:
	    break;
	}
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
	    pin_body_frame_cleanup(bfp);
	    body->pbe_depth -= 1;
	    continue;		/* Check for parent body frame */
	}

	pin_body_instr_t *instr = pin_body_instr_addr(parsep->pp_rulebook, pc);
	if (instr == NULL) {
	    pin_body_frame_cleanup(bfp);
	    body->pbe_depth -= 1;
	    break;
	}

	bfp->pbf_pc = instr->bi_next;	/* Advance PC before executing */

	switch (instr->bi_type) {
	case BIA_EMIT_OPEN: {
	    const char *tag = pin_parse_namepool_string(parsep, instr->bi_tag);
	    if (tag) {
		char *attribs = NULL;
		pin_action_type_t act = PIA_SAVE;
		if (!pin_name_id_is_null(instr->bi_text)) {
		    /* AVT template: expand {$var} references */
		    const char *avt = pin_parse_namepool_string(parsep, instr->bi_text);
		    if (avt && *avt) {
			attribs = pin_body_avt_expand(parsep, bfp, avt);
			act = PIA_SAVE_ATTRIB;
		    }
		} else if (!pin_name_id_is_null(instr->bi_select)) {
		    const char *astr = pin_parse_namepool_string(parsep,
							    instr->bi_select);
		    if (astr && *astr) {
			attribs = strdup(astr);
			act = PIA_SAVE_ATTRIB;
		    }
		}
		pin_insert_open(parsep, instr->bi_tag, NULL, tag, attribs, act);
		free(attribs);
	    }
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
	case BIA_APPLY: {
	    /*
	     * xsl:apply-templates dispatches the CHILDREN of the current
	     * element, not the element itself.  Do NOT open the matched element
	     * in the output.  Record the current output depth so the CLOSE
	     * bypass in pin_parse can detect when the element closes.
	     *
	     * If bi_mode is set, record the mode atom for mode-aware dispatch.
	     */
	    bfp->pbf_copy_depth = pip->pin_depth;
	    bfp->pbf_mode = PBMODE_APPLY;
	    bfp->pbf_apply_mode_id = instr->bi_mode;
	    return;		/* Pause; resume on matched element CLOSE */
	}
	case BIA_VALUE_OF: {
	    if (!pin_name_id_is_null(instr->bi_select)) {
		/*
		 * select="@attr": emit the named attribute value from the
		 * matched element as text.  No streaming pause needed.
		 */
		const char *aname = pin_parse_namepool_string(parsep,
							      instr->bi_select);
		if (aname) {
		    size_t vlen = 0;
		    const char *aval = pin_body_find_attrib(bfp->pbf_match_attribs,
							    aname, &vlen);
		    if (aval && vlen > 0)
			pin_insert_text(parsep, aval, vlen, PIN_TYPE_TEXT);
		}
		break;		/* Synchronous: no pause */
	    }
	    /*
	     * select=".": if this is not the first use in this body frame,
	     * the matched element's content was already consumed and cached.
	     * Emit the cache synchronously rather than re-streaming.
	     */
	    if (xo_buf_data(&bfp->pbf_value_cache, 0) != NULL) {
		if (!xo_buf_is_empty(&bfp->pbf_value_cache))
		    pin_insert_text(parsep,
				    xo_buf_data(&bfp->pbf_value_cache, 0),
				    xo_buf_offset(&bfp->pbf_value_cache),
				    PIN_TYPE_TEXT);
		break;		/* Synchronous: no pause */
	    }
	    /*
	     * First use of select=".": collect text content of the matched
	     * element and emit it in place.  Child element open/close events
	     * are tracked via pbf_depth_counter so we know when the element ends.
	     * Text is also accumulated into pbf_value_cache for later reuse.
	     */
	    xo_buf_init(&bfp->pbf_value_cache);
	    bfp->pbf_copy_depth = pip->pin_depth;
	    bfp->pbf_depth_counter = 0;
	    bfp->pbf_mode = PBMODE_VALUE_OF;
	    return;		/* Pause; resume on matched element CLOSE */
	}
	case BIA_JUMP:
	    /* Unconditional join point; PC already set to bi_next above. */
	    break;
	case BIA_GOTO:
	    /* Unconditional jump to bi_else (used to skip else-branches). */
	    bfp->pbf_pc = instr->bi_else;
	    break;
	case BIA_IF: {
	    /*
	     * Evaluate the pre-compiled per-instruction filter against the
	     * matched element and its attributes (on demand).  On false, jump
	     * to bi_else (the BIA_JUMP join point after the true-body).
	     */
	    if (!pin_body_eval_if(parsep, instr, bfp))
		bfp->pbf_pc = instr->bi_else;
	    break;
	}
	case BIA_IF_POSITION: {
	    uint32_t N = instr->bi_filter_idx;
	    uint32_t op = pin_name_id_atom_of(instr->bi_tag);
	    uint32_t pos = bfp->pbf_position;
	    int truth;
	    switch (op) {
	    case PCMP_LT: truth = (pos <  N); break;
	    case PCMP_LE: truth = (pos <= N); break;
	    case PCMP_EQ: truth = (pos == N); break;
	    case PCMP_NE: truth = (pos != N); break;
	    case PCMP_GT: truth = (pos >  N); break;
	    case PCMP_GE: truth = (pos >= N); break;
	    default:      truth = 0;
	    }
	    if (!truth)
		bfp->pbf_pc = instr->bi_else;
	    break;
	}
	case BIA_VARIABLE: {
	    /*
	     * Local variable in a streaming template body.  Only constant
	     * select= values can be evaluated here (no context node).
	     * $var references within the same body will find this binding
	     * via pin_body_var_get on the current body frame (bfp).
	     */
	    if (!pin_name_id_is_null(instr->bi_tag)
		    && !pin_name_id_is_null(instr->bi_select)) {
		pin_workspace_t *swp = pin_parse_workspace(parsep);
		const char *sel = pin_parse_namepool_string(parsep,
							    instr->bi_select);
		pin_name_id_t val = pin_name_id_null_atom();
		if (sel && sel[0]) {
		    size_t slen = strlen(sel);
		    /* String literal: 'x' or "x" */
		    if (slen >= 2
			    && ((sel[0] == '\'' && sel[slen - 1] == '\'')
				|| (sel[0] == '"' && sel[slen - 1] == '"'))) {
			char *inner = strndup(sel + 1, slen - 2);
			if (inner) {
			    val = pin_namepool_atom(swp, inner, TRUE);
			    free(inner);
			}
		    } else {
			/* Arithmetic expression (no context node needed) */
				pin_node_id_t ctx = pip->pin_stack[pip->pin_depth].ps_atom;
			char vbuf[128];
			pin_exec_eval_expr_string(swp, ctx, sel, slen,
						  vbuf, sizeof(vbuf));
			if (vbuf[0])
			    val = pin_namepool_atom(swp, vbuf, TRUE);
		    }
		}
		pin_body_var_set(bfp, instr->bi_tag, val);
	    }
	    break;
	}
	case BIA_ELEMENT_OPEN: {
	    const char *name_expr = !pin_name_id_is_null(instr->bi_select)
				    ? pin_parse_namepool_string(parsep, instr->bi_select) : NULL;
	    if (name_expr) {
		pin_body_frame_t *abfp = (body->pbe_depth > 0)
					 ? &body->pbe_stack[body->pbe_depth - 1] : NULL;
		char *tagname = abfp ? pin_body_avt_expand(parsep, abfp, name_expr)
				     : strdup(name_expr);
		const char *tag = tagname ? tagname : name_expr;
		pin_name_id_t tag_id = pin_parse_namepool_atom(parsep, tag);
		char *attribs = NULL;
		pin_action_type_t act = PIA_SAVE;
		if (!pin_name_id_is_null(instr->bi_text)) {
		    const char *avt = pin_parse_namepool_string(parsep, instr->bi_text);
		    if (avt && *avt && abfp) {
			attribs = pin_body_avt_expand(parsep, abfp, avt);
			act = PIA_SAVE_ATTRIB;
		    }
		}
		pin_insert_open(parsep, tag_id, NULL, tag, attribs, act);
		free(attribs);
		free(tagname);
	    }
	    break;
	}
	case BIA_ELEMENT_CLOSE: {
	    pin_istack_t *psp = &pip->pin_stack[pip->pin_depth];
	    if (psp->ps_node != NULL) {
		pin_workspace_t *pwp = pin_parse_workspace(parsep);
		const char *tag = pin_namepool_string(pwp, psp->ps_node->pn_name);
		if (tag)
		    pin_insert_close(parsep, NULL, tag);
	    }
	    break;
	}
	case BIA_ATTRIB: {
	    const char *aname = !pin_name_id_is_null(instr->bi_tag)
				? pin_parse_namepool_string(parsep, instr->bi_tag) : NULL;
	    if (aname) {
		char valbuf[512];
		valbuf[0] = '\0';
		if (!pin_name_id_is_null(instr->bi_text)) {
		    const char *avt = pin_parse_namepool_string(parsep, instr->bi_text);
		    if (avt && *avt) {
			pin_body_frame_t *abfp = (body->pbe_depth > 0)
						 ? &body->pbe_stack[body->pbe_depth - 1] : NULL;
			if (abfp) {
			    char *expanded = pin_body_avt_expand(parsep, abfp, avt);
			    if (expanded) {
				snprintf(valbuf, sizeof(valbuf), "%s", expanded);
				free(expanded);
			    }
			}
		    }
		}
		pin_insert_attrib_pair(parsep, aname, valbuf);
	    }
	    break;
	}
	case BIA_FOR_EACH: {
	    /*
	     * Phase 1 (pbf_depth_counter == 0): The matched element hasn't
	     * been retained yet.  Open it in the tree (to capture children)
	     * and switch to PBMODE_FOR_EACH_WAIT.  Children flow through the
	     * PBMODE_FOR_EACH_WAIT open-bypass and are saved as PIA_SAVE_ATTRIB.
	     * When the matched element closes, pin_insert_close resumes here.
	     *
	     * Phase 2 (pbf_depth_counter == 1): Matched element and all its
	     * children are now in the retained tree; execute the for-each.
	     */
	    if (bfp->pbf_depth_counter == 0) {
		/* Save the current output node so Phase 2 can use it as the
		 * navigation root for absolute paths.  For /authors/author the
		 * retained <authors> is a child of this node, so navigating
		 * authors/author from here finds the right nodes. */
		bfp->pbf_ctx_node = pip->pin_stack[pip->pin_depth].ps_atom;
		const char *match_str = pin_parse_namepool_string(parsep,
							      bfp->pbf_match_name);
		if (match_str) {
		    pin_insert_open(parsep, bfp->pbf_match_name,
				    bfp->pbf_match_prefix, match_str,
				    bfp->pbf_match_attribs, PIA_SAVE_ATTRIB);
		    pip->pin_stack[pip->pin_depth].ps_statep = NULL;
		    bfp->pbf_copy_depth = pip->pin_depth;
		    /* Mark as transient: retained for navigation only, not output */
		    if (pip->pin_stack[pip->pin_depth].ps_node)
			pip->pin_stack[pip->pin_depth].ps_node->pn_flags |= PNF_TRANSIENT;
		}
		bfp->pbf_depth_counter = 1;
		bfp->pbf_pc = pc;		/* Re-run BIA_FOR_EACH on resume */
		bfp->pbf_mode = PBMODE_FOR_EACH_WAIT;
		return;
	    }
	    bfp->pbf_depth_counter = 0;
	    /* fe_root: the output node that contains the retained matched element;
	     * used as the navigation root for absolute paths in Phase 2. */
	    pin_node_id_t fe_root = bfp->pbf_ctx_node;

	    /*
	     * Collect all nodes matching the select path, sort them, then
	     * execute the body sub-list synchronously for each node.
	     */
	    const char *sel = !pin_name_id_is_null(instr->bi_select)
			      ? pin_parse_namepool_string(parsep, instr->bi_select) : NULL;
	    if (sel == NULL || sel[0] == '\0' || pin_body_instr_id_is_null(instr->bi_else))
		break;

	    pin_workspace_t *pwp = pin_parse_workspace(parsep);
	    pin_tree_t *ptp = pip->pin_tree;

	    /* Determine start node for path navigation.
	     * Absolute paths: the retained matched element was opened inside
	     * fe_root (the output node current when Phase 1 ran), so navigating
	     * the stripped path from fe_root finds the right subtree.
	     * Relative paths: start from the current output stack top. */
	    pin_node_id_t start;
	    const char *path = sel;
	    if (sel[0] == '/') {
		start = !pin_node_id_is_null(fe_root) ? fe_root : ptp->pt_root;
		path = sel + 1;		/* strip leading '/' */
	    } else {
		start = pip->pin_stack[pip->pin_depth].ps_atom;
	    }

	    /* Collect matching nodes */
	    pin_node_id_t *nodes = NULL;
	    int node_count = 0, node_size = 0;
	    if (path[0] != '\0')
		pin_body_collect_path(pwp, start, path, strlen(path),
				      &nodes, &node_count, &node_size);

	    /* Sort if a sort spec is present */
	    const char *sort_spec = !pin_name_id_is_null(instr->bi_text)
				    ? pin_parse_namepool_string(parsep, instr->bi_text) : NULL;

	    if (sort_spec && *sort_spec && node_count > 1) {
		bool primary_desc = (*sort_spec == '-');
		pin_stree_t *tree = pin_stree_create();
		if (tree) {
		    for (int i = 0; i < node_count; i++) {
			char keybuf[512];
			uint16_t klen = pin_for_each_build_key(pwp, nodes[i],
							       sort_spec,
							       keybuf, sizeof(keybuf));
			pin_stree_insert(tree, keybuf, klen, nodes[i]);
		    }
		    int k = 0;
		    pin_stree_entry_t *e;
		    if (primary_desc) {
			for (e = pin_stree_last(tree); e && k < node_count;
				e = pin_stree_prev(tree, e))
			    nodes[k++] = e->pse_node;
		    } else {
			for (e = pin_stree_first(tree); e && k < node_count;
				e = pin_stree_next(tree, e))
			    nodes[k++] = e->pse_node;
		    }
		    pin_stree_free(tree);
		}
	    }

	    /* Execute body for each node with updated context */
	    uint32_t save_pos = bfp->pbf_position;
	    uint32_t save_last = bfp->pbf_last;
	    int save_var_count = bfp->pbf_var_count;

	    bfp->pbf_last = (uint32_t) node_count;
	    for (int i = 0; i < node_count; i++) {
		bfp->pbf_ctx_node = nodes[i];
		bfp->pbf_position = (uint32_t)(i + 1);
		bfp->pbf_var_count = save_var_count;
		pin_body_foreach_body(parsep, instr->bi_else, bfp);
	    }

	    bfp->pbf_ctx_node = pin_node_id_null_atom();
	    bfp->pbf_position = save_pos;
	    bfp->pbf_last = save_last;
	    bfp->pbf_var_count = save_var_count;
	    free(nodes);
	    break;
	}
	case BIA_COPY_OPEN: {
	    const char *match_str = pin_parse_namepool_string(parsep, bfp->pbf_match_name);
	    if (match_str)
		pin_insert_open(parsep, bfp->pbf_match_name,
				bfp->pbf_match_prefix, match_str, NULL, PIA_SAVE);
	    break;
	}
	case BIA_MESSAGE_OPEN:
	    pin_capture_start(parsep, 1, 0);
	    break;
	case BIA_MESSAGE_CLOSE:
	    pin_capture_flush(parsep);
	    if (!pin_name_id_is_null(instr->bi_tag))
		exit(1);
	    break;
	case BIA_COMMENT_OPEN:
	    pin_capture_start(parsep, 2, 0);
	    break;
	case BIA_COMMENT_CLOSE:
	    pin_capture_flush(parsep);
	    break;
	case BIA_PI_OPEN:
	    pin_capture_start(parsep, 3,
			      (unsigned) pin_name_id_atom_of(instr->bi_tag));
	    break;
	case BIA_PI_CLOSE:
	    pin_capture_flush(parsep);
	    break;
	case BIA_NUMBER: {
	    const char *expr = !pin_name_id_is_null(instr->bi_select)
			       ? pin_parse_namepool_string(parsep, instr->bi_select) : NULL;
	    if (expr) {
		pin_node_id_t ctx = pip->pin_stack[pip->pin_depth].ps_atom;
		if (!pin_node_id_is_null(ctx)) {
		    pin_workspace_t *pwp = pin_parse_workspace(parsep);
		    char vbuf[64];
		    pin_exec_eval_expr_string(pwp, ctx, expr, strlen(expr),
					     vbuf, sizeof(vbuf));
		    if (vbuf[0]) {
			long n = strtol(vbuf, NULL, 10);
			char nbuf[32];
			snprintf(nbuf, sizeof(nbuf), "%ld", n);
			pin_insert_text(parsep, nbuf, strlen(nbuf), PIN_TYPE_TEXT);
		    }
		}
	    }
	    break;
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
     * Inside a context-retain frame: save the element without template
     * dispatch.  The frame's ps_context_retain propagates to the child
     * frame via pin_insert_push, so all descendants are also retained.
     */
    if (pip->pin_stack[pip->pin_depth].ps_context_retain) {
	pin_insert_open(parsep, name_id, prefix, name, attribs, PIA_SAVE_ATTRIB);
	return;
    }

    /*
     * Op-dispatch path: retain the element as context for the op sequence
     * that executes on CLOSE.  The retained element and all descendants are
     * marked PNF_TRANSIENT so they are suppressed from the emitted output.
     * Op-emitted nodes go at parent depth after the pop and are not transient.
     */
    if (!pin_op_id_is_null(prp->pr_close_ops)
	    && pin_body_instr_id_is_null(prp->pr_body)
	    && pin_rstate_id_is_null(prp->pr_new_state)) {
	pin_insert_open(parsep, name_id, prefix, name, attribs, PIA_SAVE_ATTRIB);
	pin_istack_t *new_frame = &pip->pin_stack[pip->pin_depth];
	new_frame->ps_close_ops = prp->pr_close_ops;
	new_frame->ps_context_retain = 1;

	if (new_frame->ps_node != NULL) {
	    new_frame->ps_node->pn_flags |= PNF_TRANSIENT;

	    /* Attribute children are already inserted by pin_insert_open;
	     * mark them transient now since ps_context_retain was not yet
	     * set when they were created. */
	    pin_workspace_t *pwp = pip->pin_tree->pt_workspace;
	    pin_node_id_t cid = pin_node_child(new_frame->ps_node);
	    while (!pin_node_id_is_null(cid)) {
		pin_node_t *child = pin_node_addr(pwp, cid);
		if (child == NULL || child->pn_depth <= new_frame->ps_node->pn_depth)
		    break;
		child->pn_flags |= PNF_TRANSIENT;
		cid = child->pn_next;
	    }
	}
	return;
    }

    /*
     * Body FSM path: if the rule has a compiled body instruction list,
     * push a body frame, run the FSM, and return.  The simple pr_action
     * dispatch below is bypassed.
     */
    if (!pin_body_instr_id_is_null(prp->pr_body)) {
	pin_body_exec_t *body = &pip->pin_body;
	int initial_depth = body->pbe_depth;

	if (body->pbe_depth >= body->pbe_size) {
	    int newsize = body->pbe_size ? body->pbe_size * 2 : 4;
	    pin_body_frame_t *ns = xo_realloc(body->pbe_stack,
					      newsize * sizeof(*body->pbe_stack));
	    if (ns != NULL) {
		body->pbe_stack = ns;
		body->pbe_size = newsize;
	    }
	}
	if (body->pbe_depth < body->pbe_size) {
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
		{
		    uint8_t smode = parsep->pp_strip_len > 0
				    ? parsep->pp_strip_stack[parsep->pp_strip_len - 1]
				    : PIN_STRIP_NONE;
		    if (pin_is_ws_only(data, len)) {
			if (smode == PIN_STRIP_DEEP)
			    break;
			if (smode == PIN_STRIP_SHALLOW) {
			    if (parsep->pp_last_structural == PIN_TYPE_CLOSE)
				break;
			    if (parsep->pp_last_structural == PIN_TYPE_OPEN) {
				free(parsep->pp_ws_pending);
				parsep->pp_ws_pending = malloc(len + 1);
				if (parsep->pp_ws_pending) {
				    memcpy(parsep->pp_ws_pending, data, len);
				    parsep->pp_ws_pending[len] = '\0';
				    parsep->pp_ws_pending_len = (int) len;
				}
				break;
			    }
			}
		    }
		    if (parsep->pp_ws_pending) {
			pin_insert_text(parsep, parsep->pp_ws_pending,
					(size_t) parsep->pp_ws_pending_len,
					PIN_TYPE_TEXT);
			free(parsep->pp_ws_pending);
			parsep->pp_ws_pending = NULL;
			parsep->pp_ws_pending_len = 0;
		    }
		}
		pin_insert_text(parsep, data, len, type);
		/* If in VALUE_OF mode, also cache text for subsequent select="." reuse */
		{
		    pin_body_exec_t *vbody = &pip->pin_body;
		    if (vbody->pbe_depth > 0) {
			pin_body_frame_t *vbfp =
			    &vbody->pbe_stack[vbody->pbe_depth - 1];
			if (vbfp->pbf_mode == PBMODE_VALUE_OF)
			    xo_buf_append(&vbfp->pbf_value_cache, data, (ssize_t) len);
		    }
		}
	    }
	    break;

	case PIN_TYPE_OPEN:	/* Open tag */
	case PIN_TYPE_EMPTY:	/* Empty tag */
	    {
		if (parsep->pp_ws_pending) {
		    free(parsep->pp_ws_pending);
		    parsep->pp_ws_pending = NULL;
		    parsep->pp_ws_pending_len = 0;
		}
		parsep->pp_last_structural = (type == PIN_TYPE_EMPTY)
		    ? PIN_TYPE_CLOSE : PIN_TYPE_OPEN;
	    }
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

	    /* Push the strip mode for non-EMPTY elements */
	    if (type != PIN_TYPE_EMPTY) {
		pin_rulebook_t *srb = parsep->pp_rulebook;
		uint8_t smode = srb
		    ? pin_rulebook_strip_mode(srb, name_id) : PIN_STRIP_NONE;
		if (parsep->pp_strip_len >= parsep->pp_strip_size) {
		    uint32_t newsize = parsep->pp_strip_size
			? parsep->pp_strip_size * 2 : 32;
		    uint8_t *np = realloc(parsep->pp_strip_stack,
					  newsize * sizeof(*np));
		    if (np != NULL) {
			parsep->pp_strip_stack = np;
			parsep->pp_strip_size = newsize;
		    }
		}
		if (parsep->pp_strip_len < parsep->pp_strip_size) {
		    parsep->pp_strip_stack[parsep->pp_strip_len] = smode;
		    parsep->pp_strip_len += 1;
		}
	    }

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
	     * PBMODE_VALUE_OF bypass: collecting text from the matched element.
	     * Child element open/close events are suppressed from the output
	     * tree; only text content flows through.  Advance the filter to
	     * keep its depth counter in sync, and track nesting via the counter.
	     */
	    {
		pin_body_exec_t *body = &pip->pin_body;
		if (body->pbe_depth > 0
			&& body->pbe_stack[body->pbe_depth - 1].pbf_mode
			   == PBMODE_VALUE_OF) {
		    pin_body_frame_t *vbfp = &body->pbe_stack[body->pbe_depth - 1];
		    xo_filter_t *vof_filter = parsep->pp_filter;
		    if (vof_filter) {
			pin_filter_set_attribs(vof_filter, rest);
			xo_filter_walk_open(NULL, vof_filter, localp, -1);
		    }
		    vbfp->pbf_depth_counter += 1;
		    if (type == PIN_TYPE_EMPTY) {
			if (vof_filter)
			    xo_filter_walk_close(NULL, vof_filter, localp, -1);
			vbfp->pbf_depth_counter -= 1;
		    }
		    break;
		}
	    }

	    /*
	     * PBMODE_APPLY bypass: while a BIA_APPLY is waiting for its element
	     * to close, dispatch each child through the apply-templates patricia
	     * tree rather than through the normal filter+rulebook path.  The
	     * patricia tree maps element name atoms directly to template rules.
	     */
	    {
		pin_body_exec_t *body = &pip->pin_body;
		if (body->pbe_depth > 0
			&& body->pbe_stack[body->pbe_depth - 1].pbf_mode
			   == PBMODE_APPLY) {
		    xo_filter_t *apl_filter = parsep->pp_filter;
		    if (apl_filter) {
			pin_filter_set_attribs(apl_filter, rest);
			xo_filter_walk_open(NULL, apl_filter, localp, -1);
		    }
		    pin_rule_t *apl_rule = NULL;
		    pin_rule_t discard_rule;
		    pin_rulebook_t *rb = parsep->pp_rulebook;
		    pin_body_frame_t *abfp = &body->pbe_stack[body->pbe_depth - 1];
		    pin_name_id_t apply_mode = abfp->pbf_apply_mode_id;
		    if (rb) {
			if (!pin_name_id_is_null(apply_mode)) {
			    /* Mode dispatch: scan linked list for (name, mode) match */
			    for (pin_apply_id_t scan_aid = rb->prb_apply_list;
				 !pin_apply_id_is_null(scan_aid); ) {
				pin_apply_entry_t *ep = pin_apply_addr(rb, scan_aid);
				if (ep == NULL)
				    break;
				if (pin_name_id_equal(ep->pae_name, name_id)
					&& pin_name_id_equal(ep->pae_mode, apply_mode)) {
				    apl_rule = pin_rulebook_rule(rb, ep->pae_rule);
				    break;
				}
				scan_aid = ep->pae_next;
			    }
			} else if (rb->prb_apply_pat) {
			    /* Default-mode dispatch: fast Patricia tree lookup */
			    pa_pat_node_t *pat_node = pa_pat_get(rb->prb_apply_pat,
								 sizeof(pin_name_id_t),
								 &name_id);
			    if (pat_node) {
				pa_pat_data_atom_t datom =
				    pa_pat_node_data(rb->prb_apply_pat, pat_node);
				if (!pa_pat_data_is_null(datom)) {
				    pin_apply_id_t aid = pin_apply_id(
					pa_pat_data_atom_of(datom));
				    pin_apply_entry_t *ep = pin_apply_addr(rb, aid);
				    if (ep)
					apl_rule = pin_rulebook_rule(rb, ep->pae_rule);
				}
			    }
			}
		    }
		    if (apl_rule == NULL) {
			bzero(&discard_rule, sizeof(discard_rule));
			discard_rule.pr_action = PIA_DISCARD;
			apl_rule = &discard_rule;
		    }
		    pin_parse_handle_rule(parsep, name_id, data, localp,
					 rest, apl_rule);
		    if (type == PIN_TYPE_EMPTY) {
			pin_insert_close(parsep, data, localp);
			if (apl_filter)
			    xo_filter_walk_close(NULL, apl_filter, localp, -1);
		    }
		    break;
		}
	    }

	    /*
	     * PBMODE_FOR_EACH_WAIT bypass: BIA_FOR_EACH is waiting for the
	     * matched element to be fully retained before executing.  Save all
	     * children to the tree, exactly like PBMODE_COPY.
	     */
	    {
		pin_body_exec_t *body = &pip->pin_body;
		if (body->pbe_depth > 0
			&& body->pbe_stack[body->pbe_depth - 1].pbf_mode
			   == PBMODE_FOR_EACH_WAIT) {
		    xo_filter_t *fe_filter = parsep->pp_filter;
		    if (fe_filter) {
			pin_filter_set_attribs(fe_filter, rest);
			xo_filter_walk_open(NULL, fe_filter, localp, -1);
		    }
		    pin_rule_t save_rule;
		    bzero(&save_rule, sizeof(save_rule));
		    save_rule.pr_action = PIA_SAVE_ATTRIB;
		    pin_parse_handle_rule(parsep, name_id, data, localp,
					 rest, &save_rule);
		    if (type == PIN_TYPE_EMPTY) {
			pin_insert_close(parsep, data, localp);
			if (fe_filter)
			    xo_filter_walk_close(NULL, fe_filter, localp, -1);
		    }
		    break;
		}
	    }

	    /*
	     * Root template (match="/"): fires exactly once for the first
	     * depth-0 element (the document root's direct child).  The rule
	     * is not registered with the element filter (no document-root
	     * streaming event), so we intercept it here before the normal
	     * filter dispatch.
	     */
	    {
		pin_rulebook_t *rb = parsep->pp_rulebook;
		if (rb && !pin_rule_id_is_null(rb->prb_root_rule)
			&& !parsep->pp_root_rule_fired
			&& pip->pin_depth == 0) {
		    parsep->pp_root_rule_fired = 1;
		    pin_rule_t *root_rulep =
			pin_rulebook_rule(rb, rb->prb_root_rule);
		    if (root_rulep != NULL) {
			if (parsep->pp_filter) {
			    pin_filter_set_attribs(parsep->pp_filter, rest);
			    xo_filter_walk_open(NULL, parsep->pp_filter,
					       localp, -1);
			}
			pin_parse_handle_rule(parsep, name_id, data,
					      localp, rest, root_rulep);
			if (type == PIN_TYPE_EMPTY) {
			    pin_insert_close(parsep, data, localp);
			    if (parsep->pp_filter)
				xo_filter_walk_close(NULL, parsep->pp_filter,
						     localp, -1);
			}
			break;
		    }
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
	    {
		if (parsep->pp_ws_pending) {
		    pin_insert_text(parsep, parsep->pp_ws_pending,
				    (size_t) parsep->pp_ws_pending_len,
				    PIN_TYPE_TEXT);
		    free(parsep->pp_ws_pending);
		    parsep->pp_ws_pending = NULL;
		    parsep->pp_ws_pending_len = 0;
		}
		parsep->pp_last_structural = PIN_TYPE_CLOSE;
		if (parsep->pp_strip_len > 0)
		    parsep->pp_strip_len -= 1;
	    }
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

	    /*
	     * PBMODE_VALUE_OF bypass: child element closes decrement the
	     * counter; when it reaches zero the matched element itself is
	     * closing, so we exit the mode and resume body execution.
	     * The matched element was never opened in the output tree.
	     */
	    {
		pin_body_exec_t *vbody = &pip->pin_body;
		if (vbody->pbe_depth > 0) {
		    pin_body_frame_t *vbfp =
			&vbody->pbe_stack[vbody->pbe_depth - 1];
		    if (vbfp->pbf_mode == PBMODE_VALUE_OF) {
			if (vbfp->pbf_depth_counter > 0) {
			    vbfp->pbf_depth_counter -= 1;
			    break;
			}
			/* The matched element itself is closing */
			vbfp->pbf_mode = PBMODE_EXEC;
			pin_body_exec_advance(parsep);
			break;
		    }
		}
	    }

	    /*
	     * PBMODE_APPLY bypass: the element that triggered apply-templates
	     * was never opened in the output tree, so its close must be
	     * intercepted here.  We match on name and output depth (which
	     * returns to pbf_copy_depth after all children are processed).
	     */
	    {
		pin_body_exec_t *abody = &pip->pin_body;
		if (abody->pbe_depth > 0) {
		    pin_body_frame_t *abfp =
			&abody->pbe_stack[abody->pbe_depth - 1];
		    if (abfp->pbf_mode == PBMODE_APPLY) {
			pin_name_id_t close_id = pin_namepool_atom(
			    pip->pin_tree->pt_workspace, localp, FALSE);
			if (!pin_name_id_is_null(close_id)
				&& pin_name_id_equal(close_id,
						     abfp->pbf_match_name)
				&& pip->pin_depth == abfp->pbf_copy_depth) {
			    abfp->pbf_mode = PBMODE_EXEC;
			    pin_body_exec_advance(parsep);
			    break;
			}
		    }
		}
	    }
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
    const char *xx_encoding;	/* Encoding for XML declaration (NULL=omit) */
} pin_xml_output_t;

static int
pin_is_ws_only (const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
	if (!isspace((unsigned char) data[i]))
	    return 0;
    return 1;
}

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
	if (xmlp->xx_encoding)
	    fprintf(out, "<?xml version=\"1.0\" encoding=\"%s\"?>\n",
		    xmlp->xx_encoding);
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

    case PIN_TYPE_COMMENT:
	if (data)
	    fprintf(out, "\n<!--%s-->", data);
	break;

    case PIN_TYPE_PI: {
	if (data) {
	    const char *nl = strchr(data, '\n');
	    if (nl)
		fprintf(out, "\n<?%.*s %s?>", (int)(nl - data), data, nl + 1);
	    else
		fprintf(out, "\n<?%s?>", data);
	}
	break;
    }

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

    pin_rulebook_t *rb = parsep->pp_rulebook;
    if (rb) {
	pin_output_settings_t *op = &rb->prb_output;

	if (op->pos_omit_xml_decl_set && !op->pos_omit_xml_decl) {
	    const char *enc = NULL;
	    if (!pin_name_id_is_null(op->pos_encoding))
		enc = pin_namepool_string(rb->prb_workspace, op->pos_encoding);
	    xml.xx_encoding = enc ? enc : "UTF-8";
	}

	if (op->pos_indent_set && !op->pos_indent)
	    xml.xx_incr = 0;
    }

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
	    if (nodep->pn_flags & PNF_TRANSIENT) {
		last_depth = nodep->pn_depth;
		node_atom = nodep->pn_next;
		continue;
	    }

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

	} else if (nodep->pn_type == PIN_TYPE_COMMENT
		   || nodep->pn_type == PIN_TYPE_PI) {
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
pin_parse_passthru (pin_parse_t *parsep, int enable)
{
    pin_parse_set_default_rule(parsep,
                               enable ? PIA_SAVE : PIA_DISCARD);
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

int
pin_parse_set_param (pin_parse_t *parsep, const char *name, const char *value)
{
    pin_rulebook_t *rb = parsep->pp_rulebook;
    if (rb == NULL) {
	psu_warning(NULL, 0, "pin_parse_set_param: no rulebook attached");
	return -1;
    }

    pin_workspace_t *pwp = rb->prb_workspace;
    pin_name_id_t name_id = pin_namepool_atom(pwp, name, TRUE);

    if (!pin_rulebook_global_defined(rb, name_id)) {
	psu_warning(NULL, 0, "parameter '%s' is not defined in the stylesheet", name);
	return -1;
    }

    pin_name_id_t value_id = pin_namepool_atom(pwp, value, TRUE);
    pin_rulebook_global_add(rb, name_id, value_id);
    return 0;
}
