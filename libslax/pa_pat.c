/*
 * $Id$
 *
 * Copyright (c) 1996-2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>

#include <libslax/slax.h>
#include <libslax/pa_pat.h>

#if 0
#include <libjuise/common/bits.h>
#include <libjuise/common/aux_types.h>
#include <libjuise/common/bits.h>
#include <libjuise/data/patricia.h>
#endif /* 0 */

/*
 * This table contains a one bit mask of the highest order bit
 * set in a byte for each index value.
 */
const u_int8_t pa_pat_hi_bit_table[256] = {
    0x00, 0x01, 0x02, 0x02, 0x04, 0x04, 0x04, 0x04,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
};

/*
 * Table to translate from a bit index to the mask part of a patricia
 * bit number.
 */
const u_int8_t pa_pat_bit_masks[8] = {
    0x7f, 0xbf, 0xdf, 0xef, 0xf7, 0xfb, 0xfd, 0xfe
};

/*
 * Mask for the bit index part of a prefix length.
 */
#define	PAT_PLEN_BIT_MASK		0x7

#define	PAT_PLEN_BYTE_MASK		(0xff << 3)

/*
 * pa_pat_root_alloc
 *
 * Built-in root allocator.
 */
static pa_pat_root_t *
pa_pat_root_alloc (void)
{
    return malloc(sizeof(pa_pat_root_t));
}

/*
 * pa_pat_root_free
 *
 * Built-in root deallocator.
 */
static void
pa_pat_root_free (pa_pat_root_t *root)
{
    assert(root);

    free(root);
}

/*
 * Patricia tree users can specify their own root alloc & free
 * functions if they desire. These are used ONLY to allocate
 * and free pa_pat_root_t structures. They are NOT used for pa_pat_node_t
 * structures (the caller is responsible for pa_pat_node_ts). If the
 * user doesn't specify his own functions then the built-in
 * functions using malloc/free are used.
 */
static struct pa_pat_root_alloc_s {
    pa_pat_root_alloc_fn pat_root_alloc;
    pa_pat_root_free_fn pat_root_free;
} alloc_info = {
    pa_pat_root_alloc,
    pa_pat_root_free
};

void
pa_pat_set_allocator (pa_pat_root_alloc_fn my_alloc,
                        pa_pat_root_free_fn my_free)
{
    assert(my_alloc);
    assert(my_free);
    
    alloc_info.pat_root_alloc = my_alloc;
    alloc_info.pat_root_free = my_free;
}
    
/*
 * Given the length of a key in bytes (not to exceed 256), return the
 * length in patricia bit format.
 */
static inline u_int16_t
pat_plen_to_bit (u_int16_t plen)
{
    u_int16_t result;

    result = (plen & PAT_PLEN_BYTE_MASK) << 5;
    if (plen & PAT_PLEN_BIT_MASK) {
	result |= pa_pat_bit_masks[plen & PAT_PLEN_BIT_MASK];
    } else {
	result--;	/* subtract 0x100, or in 0xff */
    }

    return result;
}


/*
 * Given the length of a key in patricia bit format, return its length
 * in bytes.
 */
#define	PAT_BIT_TO_LEN(bit)	(((bit) >> 8) + 1)

/*
 * Given a key and a key length, traverse a tree to find a match
 * possibility.
 */
static inline pa_pat_node_t *
pa_pat_search (pa_pat_node_t *node, u_int16_t keylen, const u_int8_t *key)
{
    u_int16_t bit = PA_PAT_NOBIT;

    while (bit < node->ppn_bit) {
	bit = node->ppn_bit;
	if (bit < keylen && pat_key_test(key, bit)) {
	    node = node->ppn_right;
	} else {
	    node = node->ppn_left;
	}
    }

    return node;
}

/*
 * Given pointers to two keys, and a bit-formatted key length, return
 * the first bit of difference between the keys.
 */
static inline u_int16_t
pa_pat_mismatch (const u_int8_t *k1, const u_int8_t *k2, u_int16_t bitlen)
{
    u_int16_t i, len;

    /*
     * Get the length of the key in bytes.
     */
    len = PAT_BIT_TO_LEN(bitlen);

    /*
     * Run through looking for a difference.
     */
    for (i = 0; i < len; i++) {
	if (k1[i] != k2[i]) {
	    bitlen = pa_pat_makebit(i, k1[i] ^ k2[i]);
	    break;
	}
    }

    /*
     * Return what we found, or the original length if no difference.
     */
    return bitlen;
}

/*
 * Given a bit number and a starting node, find the leftmost leaf
 * in the (sub)tree.
 */
static inline pa_pat_node_t *
pa_pat_find_leftmost (u_int16_t bit, pa_pat_node_t *node)
{
    while (bit < node->ppn_bit) {
	bit = node->ppn_bit;
	node = node->ppn_left;
    }

    return node;
}

/*
 * Given a bit number and a starting node, find the rightmost leaf
 * in the (sub)tree.
 */
static inline pa_pat_node_t *
pa_pat_find_rightmost (u_int16_t bit, pa_pat_node_t *node)
{
    while (bit < node->ppn_bit) {
	bit = node->ppn_bit;
	node = node->ppn_right;
    }

    return node;
}

/*
 * pa_pat_root_init()
 * Initialize a patricia root node.  Allocate one if not provided.
 */
pa_pat_root_t *
pa_pat_root_init (pa_pat_root_t *root, u_int16_t klen, u_int8_t off)
{
    assert(klen && klen <= PA_PAT_MAXKEY);

    if (!root)
	root = alloc_info.pat_root_alloc();

    if (root) {
	root->ppr_root = NULL;
	root->ppr_key_bytes = klen;
	root->ppr_key_offset = off;
    }

    return root;
}

/*
 * pa_pat_root_delete()
 * Delete the root of a tree.  The tree itself must be empty for this to
 * succeed.
 */
void
pa_pat_root_delete (pa_pat_root_t *root)
{
    if (root) {
	assert(root->ppr_root == NULL);
	alloc_info.pat_root_free(root);
    }
}

/*
 * pa_pat_node_in_tree
 * Return TRUE if a node is in the tree.  For now, this is only a syntactic
 * check.
 */
boolean
pa_pat_node_in_tree (const pa_pat_node_t *node)
{
    return ((node->ppn_bit != PA_PAT_NOBIT)
	    || (node->ppn_right != NULL)
	    || (node->ppn_left != NULL));
}

/*
 * pa_pat_node_init_length()
 * Passed a pointer to a patricia node, initialize it.  Fortunately, this
 * is easy.
 */
void
pa_pat_node_init_length (pa_pat_node_t *node, u_int16_t key_bytes)
{
    if (key_bytes) {
	assert(key_bytes <= PA_PAT_MAXKEY);
	node->ppn_length = pa_pat_length_to_bit(key_bytes);
    } else {
	node->ppn_length = PA_PAT_NOBIT;
    }

    node->ppn_bit = PA_PAT_NOBIT;
    node->ppn_left = NULL;
    node->ppn_right = NULL;
}

/*
 * pa_pat_add
 * Add a node to a Patricia tree.  Returns TRUE on success.
 */
boolean
pa_pat_add (pa_pat_root_t *root, pa_pat_node_t *node)
{
    pa_pat_node_t *current;
    pa_pat_node_t **ptr;
    u_int16_t bit;
    u_int16_t diff_bit;
    const u_int8_t *key;

    /*
     * Make sure this node is not in a tree already.
     */
    assert((node->ppn_bit == PA_PAT_NOBIT) &&
	   (node->ppn_right == NULL) &&
	   (node->ppn_left == NULL));
  
    if (node->ppn_length == PA_PAT_NOBIT)
	node->ppn_length = pa_pat_length_to_bit(root->ppr_key_bytes);

    /*
     * If this is the first node in the tree, then it gets links to
     * itself.  There is always exactly one node in the tree with
     * PA_PAT_NOBIT for the bit number.  This node is always a leaf
     * since this avoids ever testing a bit with PA_PAT_NOBIT, which
     * leaves greater freedom in the choice of bit formats.
     */
    if (root->ppr_root == NULL) {
	root->ppr_root = node->ppn_left = node->ppn_right = node;
	node->ppn_bit = PA_PAT_NOBIT;
	return TRUE;
    }

    /*
     * Start by waltzing down the tree to see if a duplicate (or a prefix
     * match) of the key is in the tree already.  If so, return FALSE.
     */
    key = pa_pat_key(root, node);
    current = pa_pat_search(root->ppr_root, node->ppn_length, key);

    /*
     * Find the first bit that differs from the node that we did find, to
     * the minimum length of the keys.  If the nodes match to this length
     * (i.e. one is a prefix of the other) we'll get a bit greater than or
     * equal to the minimum back, and we bail.
     */
    bit = (node->ppn_length < current->ppn_length)
	? node->ppn_length : current->ppn_length;
    diff_bit = pa_pat_mismatch(key, pa_pat_key(root, current), bit);
    if (diff_bit >= bit)
	return FALSE;

    /*
     * Now waltz the tree again, looking for where the insertbit is in the
     * current branch.  Note that if there were parent pointers or a
     * convenient stack, we could back up.  Alas, we apply sweat...
     */
    bit = PA_PAT_NOBIT;
    current = root->ppr_root;
    ptr = &root->ppr_root;
    while (bit < current->ppn_bit && current->ppn_bit < diff_bit) {
	bit = current->ppn_bit;
	if (pat_key_test(key, bit)) {
	    ptr = &current->ppn_right;
	    current = current->ppn_right;
	} else {
	    ptr = &current->ppn_left;
	    current = current->ppn_left;
	}
    }

    /*
     * This is our insertion point.  Do the deed.
     */
    node->ppn_bit = diff_bit;
    if (pat_key_test(key, diff_bit)) {
	node->ppn_left = current;
	node->ppn_right = node;
    } else {
	node->ppn_right = current;
	node->ppn_left = node;
    }

    *ptr = node;
    return TRUE;
}

/*
 * pa_pat_delete()
 * Delete a node from a patricia tree.
 */
boolean
pa_pat_delete (pa_pat_root_t *root, pa_pat_node_t *node)
{
    u_int16_t bit;
    const u_int8_t *key;
    pa_pat_node_t **downptr, **upptr, **parent, *current;
    
    /*
     * Is there even a tree?  Is the node in a tree?
     */
    assert(node->ppn_left && node->ppn_right);

    current = root->ppr_root;
    if (!current)
	return FALSE;

    /*
     * Waltz down the tree, finding our node.  There should be two pointers
     * to the node: one internal, pointing to the node from above it, and
     * one at a leaf, pointing up to it from beneath.
     *
     * We want to set downptr to point to the internal pointer down to the
     * node.  The "upnode" is the node in the tree which has an "up" link
     * pointing to the node.  We want to set upptr to point to the pointer
     * to upnode [yes, that's right].
     */
    downptr = upptr = NULL;
    parent = &root->ppr_root;
    bit = PA_PAT_NOBIT;
    key = pa_pat_key(root, node);

    while (bit < current->ppn_bit) {
	bit = current->ppn_bit;
	if (current == node) {
	    downptr = parent;
	}
	upptr = parent;
	if (bit < node->ppn_length && pat_key_test(key, bit)) {
	    parent = &current->ppn_right;
	} else {
	    parent = &current->ppn_left;
	}
	current = *parent;
    }

    /*
     * If the guy we found, `current', is not our node then it isn't
     * in the tree.
     */
    if (current != node)
	return FALSE;

    /*
     * If there's no upptr we're the only thing in the tree.
     * Otherwise we'll need to work a bit.
     */
    if (upptr == NULL) {
	assert(node->ppn_bit == PA_PAT_NOBIT);
	root->ppr_root = NULL;
    } else {
	/*
	 * One pointer in the node upptr points at points at us,
	 * the other pointer points elsewhere.  Remove the node
	 * upptr points at from the tree in its internal node form
	 * by promoting the pointer which doesn't point at us.
	 * It is possible that this node is also `node', the node
	 * we're trying to remove, in which case we're all done.  If
	 * not, however, we'll catch that below.
	 */
	current = *upptr;
	if (parent == &current->ppn_left) {
	    *upptr = current->ppn_right;
	} else {
	    *upptr = current->ppn_left;
	}

	if (!downptr) {
	    /*
	     * We were the no-bit node.  We make our parent the
	     * no-bit node.
	     */
	    assert(node->ppn_bit == PA_PAT_NOBIT);
	    current->ppn_left = current->ppn_right = current;
	    current->ppn_bit = PA_PAT_NOBIT;

	} else if (current != node) {
	    /*
	     * We were not our own `up node', which means we need to
	     * remove ourselves from the tree as in internal node.  Replace
	     * us with `current', which we freed above.
	     */
	    current->ppn_left = node->ppn_left;
	    current->ppn_right = node->ppn_right;
	    current->ppn_bit = node->ppn_bit;
	    *downptr = current;
	}
    }

    /*
     * Clean out the node.
     */
    node->ppn_left = node->ppn_right = NULL;
    node->ppn_bit = PA_PAT_NOBIT;

    return TRUE;
}


/*
 * pa_pat_find_next()
 * Given a node, find the lexical next node in the tree.  If the
 * node pointer is NULL the leftmost node in the tree is returned.
 * Returns NULL if the tree is empty or it falls off the right.  Asserts
 * if the node isn't in the tree.
 */
 
pa_pat_node_t *
pa_pat_find_next (pa_pat_root_t *root, pa_pat_node_t *node)
{
    u_int16_t bit;
    const u_int8_t *key;
    pa_pat_node_t *current, *lastleft;

    /*
     * If there's nothing in the tree we're done.
     */
    current = root->ppr_root;
    if (current == NULL) {
	assert(node == NULL);
	return NULL;
    }

    /*
     * If he didn't specify a node, return the leftmost guy.
     */
    if (node == NULL)
	return pa_pat_find_leftmost(PA_PAT_NOBIT, current);

    /*
     * Search down the tree for the node.  Track where we last went
     * left, so we can go right from there.
     */
    lastleft = NULL;
    key = pa_pat_key(root, node);
    bit = PA_PAT_NOBIT;
    while (bit < current->ppn_bit) {
	bit = current->ppn_bit;
	if (bit < node->ppn_length && pat_key_test(key, bit)) {
	    current = current->ppn_right;
	} else {
	    lastleft = current;
	    current = current->ppn_left;
	}
    }
    assert(current == node);

    /*
     * If we found a left turn go right from there.  Otherwise barf.
     */
    if (lastleft)
	return pa_pat_find_leftmost(lastleft->ppn_bit, lastleft->ppn_right);

    return NULL;
}

/* 
 * This is just a hack to let callers use const trees and 
 * receive back a const node..  The called functions are not const --
 * they can't be since they need to return non-const nodes to the 
 * caller -- even though they do not modify the contents of the 
 * tree.  However, if you're exposing the patricias to other modules
 * that should be looked at, but not modified, these can help.. 
 */
 
const pa_pat_node_t *
pa_pat_cons_find_next (const pa_pat_root_t *root, const pa_pat_node_t *node)
{
    pa_pat_root_t *r = QUIET_CAST(pa_pat_root_t *, root);
    pa_pat_node_t *n = QUIET_CAST(pa_pat_node_t *, node);

    /* does not change or modify tree or node */
    return pa_pat_find_next(r, n); 
}

const pa_pat_node_t *
pa_pat_cons_find_prev (const pa_pat_root_t *root, const pa_pat_node_t *node)
{
    pa_pat_root_t *r = QUIET_CAST(pa_pat_root_t *, root);
    pa_pat_node_t *n = QUIET_CAST(pa_pat_node_t *, node);

    /* does not change or modify tree or node */    
    return pa_pat_find_prev(r, n); 
}

const pa_pat_node_t *
pa_pat_cons_get (const pa_pat_root_t *root, const u_int16_t key_bytes,
		   const void *key)
{
    pa_pat_root_t *r = QUIET_CAST(pa_pat_root_t *, root);

    /* does not change or modify tree or node */
    return pa_pat_get(r, key_bytes, key); 
}

const pa_pat_node_t *
pa_pat_cons_subtree_match (const pa_pat_root_t *root,
			   const u_int16_t prefix_len,
			   const void *prefix)
{
    pa_pat_root_t *r = QUIET_CAST(pa_pat_root_t *, root);

    /* does not change or modify tree or node */    
    return pa_pat_subtree_match(r, prefix_len, prefix); 
}

const pa_pat_node_t *
pa_pat_cons_subtree_next (const pa_pat_root_t *root,
			  const pa_pat_node_t *node,
			  const u_int16_t prefix_len)
{
    pa_pat_root_t *r = QUIET_CAST(pa_pat_root_t *, root);
    pa_pat_node_t *n = QUIET_CAST(pa_pat_node_t *, node);

    /* does not change or modify tree or node */    
    return pa_pat_subtree_next(r, n, prefix_len); 
}


/*
 * pa_pat_find_prev()
 * Given a node, find the lexical previous node in the tree.  If the
 * node pointer is NULL the rightmost node in the tree is returned.
 * Returns NULL if the tree is empty or it falls off the left.  Asserts
 * if the node isn't in the tree.
 */
pa_pat_node_t *
pa_pat_find_prev (pa_pat_root_t *root, pa_pat_node_t *node)
{
    u_int16_t bit;
    const u_int8_t *key;
    pa_pat_node_t *current, *lastright;

    /*
     * If there's nothing in the tree we're done.
     */
    current = root->ppr_root;
    if (current == NULL) {
	assert(node == NULL);
	return NULL;
    }

    /*
     * If he didn't specify a node, return the rightmost guy.
     */
    if (node == NULL)
	return pa_pat_find_rightmost(PA_PAT_NOBIT, current);

    /*
     * Search down the tree for the node.  Track where we last went
     * right, so we can go right from there.
     */
    lastright = NULL;
    key = pa_pat_key(root, node);
    bit = PA_PAT_NOBIT;
    while (bit < current->ppn_bit) {
	bit = current->ppn_bit;
	if (bit < node->ppn_length && pat_key_test(key, bit)) {
	    lastright = current;
	    current = current->ppn_right;
	} else {
	    current = current->ppn_left;
	}
    }
    assert(current == node);

    /*
     * If we found a right turn go right from there.  Otherwise barf.
     */
    if (lastright)
	return pa_pat_find_rightmost(lastright->ppn_bit, lastright->ppn_left);

    return NULL;
}


/*
 * pa_pat_subtree_match()
 * We're passed in a prefix length, in bits, and a pointer to that
 * many bits of prefix.  Return the leftmost guy for which this
 * is a prefix of the node's key.
 */
pa_pat_node_t *
pa_pat_subtree_match (pa_pat_root_t *root, u_int16_t plen,
		      const void *v_prefix)
{
    u_int16_t diff_bit, p_bit;
    pa_pat_node_t *current;
    const u_int8_t *prefix = v_prefix;

    /*
     * If there's nothing in the tree, return NULL.
     */
    assert(plen && plen <= (PA_PAT_MAXKEY * 8));

    if (root->ppr_root == NULL)
	return NULL;

    /*
     * Okay, express the prefix length as a patricia bit number
     * and search for someone.
     */
    p_bit = pat_plen_to_bit(plen);
    current = pa_pat_search(root->ppr_root, p_bit, prefix);

    /*
     * If the guy we found is shorter than the prefix length, we're
     * doomed (we could walk forward, but since we're guaranteed that
     * we'll never test a bit not in a key on the way there, we're sure
     * not to find any other matches).
     */
    if (p_bit > current->ppn_length)
	return NULL;

    /*
     * Compare the key of the guy we found to our prefix.  If they
     * match to the prefix length return him, otherwise there is no match.
     */
    diff_bit = pa_pat_mismatch(prefix, pa_pat_key(root, current), p_bit);
    if (diff_bit < p_bit)
	return NULL;

    return current;
}


/*
 * pa_pat_subtree_next()
 * Given a node in a subtree, and the prefix length in bits of the prefix
 * common to nodes in the subtree, return the lexical next node in the
 * subtree.  assert()'s if the node isn't in the tree.
 */
pa_pat_node_t *
pa_pat_subtree_next (pa_pat_root_t *root, pa_pat_node_t *node, u_int16_t plen)
{
    const u_int8_t *prefix;
    u_int16_t bit, p_bit;
    pa_pat_node_t *current, *lastleft;

    /*
     * Make sure this is reasonable.
     */
    current = root->ppr_root;
    assert(plen && current);
    p_bit = pat_plen_to_bit(plen);
    assert(node->ppn_length >= p_bit);

    prefix = pa_pat_key(root, node);
    bit = PA_PAT_NOBIT;
    lastleft = NULL;
    while (bit < current->ppn_bit) {
	bit = current->ppn_bit;
	if (bit < node->ppn_length && pat_key_test(prefix, bit)) {
	    current = current->ppn_right;
	} else {
	    lastleft = current;
	    current = current->ppn_left;
	}
    }

    /*
     * If we didn't go left, or the left turn was at a bit in the prefix,
     * we've fallen off the end of the subtree.  Otherwise step right
     * and return the leftmost guy over there.
     */
    assert(current == node);
    if (lastleft == NULL || lastleft->ppn_bit < p_bit)
	return NULL;

    return pa_pat_find_leftmost(lastleft->ppn_bit, lastleft->ppn_right);
}


/*
 * pa_pat_get()
 * Given a key and its length, find a node which matches.
 */
pa_pat_node_t *
pa_pat_get (pa_pat_root_t *root, u_int16_t key_bytes, const void *key)
{
    return pa_pat_get_inline(root, key_bytes, key);
}

/*
 * pa_pat_getnext()
 * Find the next matching guy in the tree.  This is a classic getnext,
 * except that if we're told to we will return an exact match if we find
 * one.
 */
/**
 * Some more documentation for this function.
 *  Let's see what Doxygen does with it.
 */
pa_pat_node_t *
pa_pat_getnext (pa_pat_root_t *root, u_int16_t klen, const void *v_key, boolean eq)
{
    u_int16_t bit, bit_len, diff_bit;
    pa_pat_node_t *current, *lastleft, *lastright;
    const u_int8_t *key = v_key;

    assert(klen);

    /*
     * If nothing in tree, nothing to find.
     */
    current = root->ppr_root;
    if (current == NULL)
	return NULL;

    /*
     * Search the tree looking for this prefix.  Note the last spot
     * at which we go left.
     */
    bit_len = pa_pat_length_to_bit(klen);
    bit = PA_PAT_NOBIT;
    lastright = lastleft = NULL;
    while (bit < current->ppn_bit) {
	bit = current->ppn_bit;
	if (bit < bit_len && pat_key_test(key, bit)) {
	    lastright = current;
	    current = current->ppn_right;
	} else {
	    lastleft = current;
	    current = current->ppn_left;
	}
    }

    /*
     * So far so good.  Determine where the first mismatch between
     * the guy we found and the key occurs.
     */
    bit = (current->ppn_length > bit_len) ? bit_len : current->ppn_length;
    diff_bit = pa_pat_mismatch(key, pa_pat_key(root, current), bit);

    /*
     * Three cases here.  Do them one by one.
     */
    if (diff_bit >= bit) {
	/*
	 * They match to at least the length of the shortest.  If the
	 * key is shorter, or if the key is equal and we've been asked
	 * to return that, we're golden.
	 */
	if (bit_len < current->ppn_length
	    || (eq && bit_len == current->ppn_length)) {
	    return current;
	}

	/*
	 * If none of the above, go right from `lastleft'.
	 */
    } else if (pat_key_test(key, diff_bit)) {
	/*
	 * The key is bigger than the guy we found.  We need to find
	 * somewhere that tested a bit less than diff_bit where we
	 * went left, and go right there instead.  `lastleft' will
	 * be the spot if it tests a bit less than diff_bit, otherwise
	 * we need to search again.
	 */
	if (lastleft && lastleft->ppn_bit > diff_bit) {
	    bit = PA_PAT_NOBIT;
	    current = root->ppr_root;
	    lastleft = NULL;
	    while (bit < current->ppn_bit && current->ppn_bit < diff_bit) {
		bit = current->ppn_bit;
		if (pat_key_test(key, bit)) {
		    current = current->ppn_right;
		} else {
		    lastleft = current;
		    current = current->ppn_left;
		}
	    }
	}
    } else {
	/*
	 * The key is smaller than the guy we found, so the guy we
	 * found is actually a candidate.  It may be the case, however,
	 * that this guy isn't the first larger node in the tree.  We
	 * know this is true if the last right turn tested a bit greater
	 * than the difference bit, in which case we search again from the
	 * top.
	 */
	if (lastright && lastright->ppn_bit >= diff_bit)
	    return pa_pat_search(root->ppr_root, diff_bit, key);

	return current;
    }

    /*
     * The first two cases come here.  In either case, if we've got
     * a `lastleft' take a right turn there, otherwise return nothing.
     */
    if (lastleft)
	return pa_pat_find_leftmost(lastleft->ppn_bit, lastleft->ppn_right);

    return NULL;
}

int
pa_pat_compare_nodes (pa_pat_root_t *root, pa_pat_node_t *node1,
		      pa_pat_node_t *node2)
{
    u_int16_t bit;
    u_int16_t diff_bit;
    const u_int8_t *key_1, *key_2;
    
    bit = (node1->ppn_length < node2->ppn_length)
	? node1->ppn_length : node2->ppn_length;

    key_1 = pa_pat_key(root, node1);
    key_2 = pa_pat_key(root, node2);
    
    diff_bit = pa_pat_mismatch(key_1, key_2, bit);
    
    if (diff_bit >= bit)
	return 0;
    
    if (pat_key_test(key_1, diff_bit))
	return 1;
    
    return -1;
}
