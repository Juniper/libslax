/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, April 2016
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <stddef.h>

#include "slaxconfig.h"
#include <libslax/slax.h>
#include <libslax/pa_common.h>
#include <libslax/pa_mmap.h>

#define PA_VERS_MAJOR		1 /* Major numbers are mutually incompatible */
#define PA_VERS_MINOR		0 /* Minor numbers are compatible */

#define PA_MMAP_FREE_MAGIC	0xCABB1E16 /* Denoted free atoms */

/*
 * The magic number allow us to "know" that the file is our's as well
 * as knowing that it's in the correct endian-ness.  Allows us to report
 * this common error to the user in a meaningful way.
 */
#define PA_MAGIC_NUMBER	0xBE1E	/* "Our" endian-ness magic */
#define PA_MAGIC_WRONG	0x1EBE	/* "Wrong" endian-ness magic */

/* Default initial mmap file size */
#define PA_DEFAULT_COUNT	32
#define PA_DEFAULT_SIZE		(PA_DEFAULT_COUNT << PA_MMAP_ATOM_SHIFT)

/*
 * This structure defines the header of the mmap'd memory segment.
 */
typedef struct pa_mmap_info_s {
    uint16_t pmi_magic;		/* Magic number */
    uint8_t pmi_vers_major;	/* Major version number */
    uint8_t pmi_vers_minor;	/* Minor version number */
    uint32_t pmi_max_size;	/* Maximum size (or 0) */
    uint32_t pmi_num_headers;	/* Number of named headers following ours */
    size_t pmi_len;		/* Current size */
    pa_matom_t pmi_free;		/* First free memory segment */
} pa_mmap_info_t;

typedef struct pa_mmap_free_s {
    uint32_t pmf_magic;		/* Magic number */
    pa_matom_t pmf_size;		/* Number of atoms free here */
    pa_matom_t pmf_next;		/* Free list */
} pa_mmap_free_t;

typedef struct pa_mmap_header_s {
    char pmh_name[PA_MMAP_HEADER_NAME_LEN]; /* Simple text name */
    uint16_t pmh_type;		/* Type of data (PA_TYPE_*) */
    uint16_t pmh_flags;		/* Flags for this data (unused) */
    uint32_t pmh_size;		/* Length of header (bytes) */
    char pmh_content[];		/* Content, inline */
} pa_mmap_header_t;

#define PA_ADDR_DEFAULT		0x200000000000
#define PA_ADDR_DEFAULT_INCR	0x020000000000

static uint8_t *pa_mmap_next_address = (void *) PA_ADDR_DEFAULT;
static ptrdiff_t pa_mmap_incr_address = PA_ADDR_DEFAULT_INCR;

#if 0
/*
 * Remove an item from a free list.
 */
static void
pa_mmap_list_remove (pa_mmap_t *pmp, pa_matom_t atom, unsigned count UNUSED)
{
    pa_matom_t fa;		/* Free atom number */
    pa_mmap_free_t *pmfp;
    pa_matom_t *lastp = &pmp->pm_infop->pmi_free;

    for (fa = *lastp; fa != PA_NULL_ATOM; fa = *lastp) {
	pmfp = pa_pointer(pmp->pm_addr, fa, PA_MMAP_ATOM_SHIFT);
	if (fa == atom) {
	    *lastp = pmfp->pmf_next; /* Remove it from list */
	    return;
	}

	lastp = &pmfp->pmf_next; /* Move to next item on free list */
    }
}
#endif /* 0 */

/*
 * Add an item from a free list.
 */
static void
pa_mmap_list_add (pa_mmap_t *pmp, pa_matom_t atom, unsigned size)
{
    pa_matom_t fa;		/* Free atom number */
    pa_mmap_free_t *pmfp;
    pa_matom_t *lastp = &pmp->pm_infop->pmi_free;

    for (fa = *lastp; fa != PA_NULL_ATOM; fa = *lastp) {
	pmfp = pa_pointer(pmp->pm_addr, fa, PA_MMAP_ATOM_SHIFT);

	if (size <= pmfp->pmf_size)
	    break;

	lastp = &pmfp->pmf_next; /* Move to next item on free list */
    }

    /* Insert atom into the chain */
    pmfp = pa_pointer(pmp->pm_addr, atom, PA_MMAP_ATOM_SHIFT);
    pmfp->pmf_magic = PA_MMAP_FREE_MAGIC;
    pmfp->pmf_size = size;
    pmfp->pmf_next = *lastp;
    *lastp = atom;
}

/*
 * Allocate a chunk of memory and return its offset.
 */
pa_matom_t
pa_mmap_alloc (pa_mmap_t *pmp, size_t size)
{
    if (size == 0)
	return PA_NULL_ATOM;

    pa_matom_t fa;		/* Free atom number */
    unsigned count = (size + PA_MMAP_ATOM_SIZE - 1) >> PA_MMAP_ATOM_SHIFT;
    unsigned new_count;
    pa_mmap_free_t *pmfp;
    pa_matom_t *lastp = &pmp->pm_infop->pmi_free;

    for (fa = *lastp; fa != PA_NULL_ATOM; fa = *lastp) {
	pmfp = pa_pointer(pmp->pm_addr, fa, PA_MMAP_ATOM_SHIFT);

	if (pmfp->pmf_size >= count) {
	    /*
	     * We "cheat", allocating from the end so we don't need to
	     * touch the current by_addr list.  We'll still have to mung
	     * the by_size list.
	     */
	    if (count < pmfp->pmf_size) {
		pmfp->pmf_size -= count;
		fa += pmfp->pmf_size; /* Reference end of the chunk */
	    } else
		*lastp = pmfp->pmf_next; /* Unlink this chunk */

	    return fa;		/* Return offset */
	}

	lastp = &pmfp->pmf_next;
    }

    /*
     * Okay, so there's nothing but enough to fit this, but we do
     * know that *lastp is the end of the free list.  So we grow our
     * database, and toss the excess onto the free list.
     */
    if (count < PA_DEFAULT_COUNT)
	new_count = PA_DEFAULT_COUNT;
    else
	new_count = pa_roundup32(count, PA_DEFAULT_COUNT);

    size_t new_len = pmp->pm_len + (new_count << PA_MMAP_ATOM_SHIFT);
    size_t old_len = pmp->pm_len;

    if (pmp->pm_infop->pmi_max_size != 0
	&& new_len > pmp->pm_infop->pmi_max_size) {
	pa_warning(0, "max size reached");
	return PA_NULL_ATOM;
    }

    /* If we've got a file attached, we need to extend the file */
    if (pmp->pm_fd > 0) {
	if (ftruncate(pmp->pm_fd, new_len) < 0) {
	    pa_warning(errno, "cannot extend memory file to %d", new_len);
	    return PA_NULL_ATOM;
	}

	/* Re-mmap the segment */
	void *addr = mmap(pmp->pm_addr, new_len, pmp->pm_mmap_prot,
			  pmp->pm_mmap_flags | MAP_FIXED, pmp->pm_fd, 0);
	if (addr == NULL || addr == MAP_FAILED) {
	    pa_warning(errno, "mmap failed");
	    return PA_NULL_ATOM;
	}

	if (addr != pmp->pm_addr) {
	    pa_warning(0, "mmap was moved (%p:%p)", pmp->pm_addr, addr);
	    return PA_NULL_ATOM;
	}

    } else {
	/*
	 * With mmap and no file, we can't extend our mapping, so
	 * we map a new segment at the end of our current one and
	 * record that fact so we can unmap it during close.
	 */

	uint8_t *target = pmp->pm_addr;
	target += old_len;

	void *addr = mmap(target, new_len - old_len, pmp->pm_mmap_prot,
			  pmp->pm_mmap_flags | MAP_FIXED, pmp->pm_fd, 0);
	if (addr == NULL || addr == MAP_FAILED) {
	    pa_warning(errno, "mmap failed");
	    return PA_NULL_ATOM;
	}

	if (addr != target) {
	    pa_warning(0, "mmap was moved (%p:%p:%p)",
		       pmp->pm_addr, target, addr);
	    return PA_NULL_ATOM;
	}

	pa_mmap_record_t *pmrp = calloc(1, sizeof(*pmrp));
	if (pmrp) {
	    pmrp->pmr_addr = target;
	    pmrp->pmr_len = new_len - old_len;
	    pmrp->pmr_next = pmp->pm_record;
	    pmp->pm_record = pmrp;
	}
    }

    pmp->pm_len = new_len;	/* Record our new length */

    if (new_count > count) {
	fa = old_len >> PA_MMAP_ATOM_SHIFT; /* We'll use the first chunk */
	pa_matom_t na = fa + count; /* And put the rest on the free list */

	pmfp = pa_pointer(pmp->pm_addr, na, PA_MMAP_ATOM_SHIFT);
	pmfp->pmf_magic = PA_MMAP_FREE_MAGIC;
	pmfp->pmf_size = new_count - count;
	pmfp->pmf_next = 0;

	*lastp = na;		/* Add 'na' to the free list */
    }

    return fa;
}

void
pa_mmap_free (pa_mmap_t *pmp, pa_matom_t atom, unsigned size)
{
    if (atom == PA_NULL_ATOM) {
	pa_warning(0, "pa_mmap_free called with NULL");
	return;
    }

    unsigned count = (size + PA_MMAP_ATOM_SIZE - 1) >> PA_MMAP_ATOM_SHIFT;
    if (count == 0) {
	pa_warning(0, "pa_mmap_free called with count 0");
	return;
    }

    pa_mmap_list_add(pmp, atom, count);
}

pa_mmap_t *
pa_mmap_open (const char *filename, pa_mmap_flags_t flags, unsigned mode)
{
    int mmap_flags = MAP_SHARED;
    int fd = 0;
    int oflags = O_RDWR;
    int prot = PROT_READ | PROT_WRITE;
    struct stat st;
    pa_mmap_info_t *pmip = NULL;
    pa_mmap_t *pmp = NULL;
    int created = 0;
    unsigned len = 0;
    void *addr = NULL;

    if (flags & PMF_READ_ONLY) {
	prot = PROT_READ;
	oflags = O_RDONLY;
    }

    if (mode == 0)
	mode = 0600;

    if (filename) {
	fd = open(filename, oflags, mode);
	if (fd < 0) {
	    if (flags & PMF_READ_ONLY) {
		pa_warning(errno, "could not open (read-only) file: '%s'",
			   filename);
		goto fail;
	    }

	    fd = open(filename, O_CREAT | oflags, mode);
	    if (fd < 0) {
		pa_warning(errno, "could not create file: '%s'", filename);
		goto fail;
	    }

	    len = PA_DEFAULT_SIZE;
	    if (ftruncate(fd, len) < 0) {
		pa_warning(errno, "could not extend file length (%d)", len);
		goto fail;
	    }

	    created = 1;

	} else {
	    if (fstat(fd, &st)) {
		pa_warning(errno, "could not stat file: '%s'", filename);
		goto fail;
	    }

	    len = st.st_size;
	}

	mmap_flags |= MAP_FILE;

    } else {
	/* Without a filename, we build an anonymos mmap segment */
	fd = -1;
	mmap_flags |= MAP_ANON;
	len = PA_DEFAULT_SIZE;
	created = 1;
    }

    addr = mmap(pa_mmap_next_address, len, prot, mmap_flags, fd, 0);
    if (addr == NULL || addr == MAP_FAILED) {
	pa_warning(errno, "mmap failed");
	goto fail;
    }

    pa_mmap_next_address += pa_mmap_incr_address;

    pmip = addr;
    if (created) {
	pmip->pmi_magic = PA_MAGIC_NUMBER;
	pmip->pmi_vers_major = PA_VERS_MAJOR;
	pmip->pmi_vers_minor = PA_VERS_MINOR;
	pmip->pmi_len = len;

	/* We waste the rest of the first atom, but we're atom aligned */
	pmip->pmi_free = 1;

	/* Make the first entry in the free list */
	pa_mmap_free_t *pmfp;
	pmfp = pa_pointer(addr, 1, PA_MMAP_ATOM_SHIFT);
	pmfp->pmf_magic = PA_MMAP_FREE_MAGIC;
	pmfp->pmf_size = (len >> PA_MMAP_ATOM_SHIFT) - 1;

    } else {
	/* Check header fields */
	if (pmip->pmi_magic != PA_MAGIC_NUMBER) {
	    if (pmip->pmi_magic != PA_MAGIC_WRONG)
		pa_warning(0, "machine is wrong endian type (0x%x:0x%x)",
			   pmip->pmi_magic, PA_MAGIC_NUMBER);
	    else
		pa_warning(0, "magic number mismatch (0x%x:0x%x)",
			   pmip->pmi_magic, PA_MAGIC_NUMBER);
	    goto fail;

	} else if (pmip->pmi_vers_major != PA_VERS_MAJOR) {
	    pa_warning(0, "major version number mismatch (%d:%d)",
		       pmip->pmi_vers_major, PA_VERS_MAJOR);
	    goto fail;

	} else if (pmip->pmi_vers_minor != PA_VERS_MINOR) {
	    pa_warning(0, "minor version number mismatch (%d:%d); "
		       "ignored", pmip->pmi_vers_minor, PA_VERS_MINOR);

	} else if (pmip->pmi_len != len) {
	    pa_warning(0, "memory size mismatch (%d:%d); "
		       "ignored", pmip->pmi_len, len);
	} else {
	    /* Success!! */
	}
    }

    /*
     * All good news.  Now we allocate our "user space" data structure
     * and fill it in.
     */
    pmp = calloc(1, sizeof(*pmp));
    if (pmp == NULL) {
	pa_warning(errno, "could not allocate memory for pa_mmap_t");
	goto fail;
    }

    pmp->pm_fd = fd;
    pmp->pm_addr = addr;
    pmp->pm_len = len;
    pmp->pm_flags = flags;
    pmp->pm_infop = pmip;
    pmp->pm_mmap_flags = mmap_flags;
    pmp->pm_mmap_prot = prot;

    if (fd < 0) {
	pa_mmap_record_t *pmrp = calloc(1, sizeof(*pmrp));
	if (pmrp) {
	    pmrp->pmr_addr = addr;
	    pmrp->pmr_len = len;
	    pmp->pm_record = pmrp;
	}
    }

    return pmp;

 fail:
    if (addr != NULL)
	munmap(addr, len);
    if (fd > 0)
	close(fd);

    return NULL;
}

/*
 * Close the pmap, releasing any resources associated with it.
 */
void
pa_mmap_close (pa_mmap_t *pmp)
{
    if (pmp->pm_record) {
	pa_mmap_record_t *pmrp = pmp->pm_record, *nextp;
	for (; pmrp; pmrp = nextp) {
	    munmap(pmrp->pmr_addr, pmrp->pmr_len);
	    nextp = pmrp->pmr_next;
	    free(pmrp);
	}
    } else {
	if (pmp->pm_addr > 0)
	    munmap(pmp->pm_addr, pmp->pm_len);
    }

    if (pmp->pm_fd > 0)
	close(pmp->pm_fd);

    free(pmp);
}

/*
 * Find or add a header in the first page (page 0) of the mmap file.
 * If 'size' == 0, we don't add it; the caller's just checking.
 */
void *
pa_mmap_header (pa_mmap_t *pmp, const char *name,
		uint16_t type, uint16_t flags, size_t size)
{
    pa_mmap_header_t *pmhp;
    uint8_t *base = pmp->pm_addr;
    uint32_t i;

    base += sizeof(*pmp->pm_infop); /* Named headers start after ours */

    /* Ensure alignment */
    size = pa_roundup32(size, sizeof(long long));

    /*
     * Starting directly after the mmap header (pm_mmap_info_t),
     * we whiffle thru a list of headers, each preceeded by a
     * pa_mmap_header_t that gives the name and length.  If we
     * find an existing header with the given name, we return it.
     * other wise, we add the new one.
     */
    for (i = 0; i < pmp->pm_infop->pmi_num_headers; i++) {
	pmhp = (void *) base;
	if (strncmp(name, pmhp->pmh_name, sizeof(pmhp->pmh_name)) != 0) {
	    base += sizeof(*pmhp) + pmhp->pmh_size;
	    continue;
	}

	/* Found a name; return it */
	return &pmhp->pmh_content[0];
    }

    /* If the caller didn't give the size, they don't want us to make it */
    if (size == 0)
	return NULL;

    /* No match; 'base' is at the end of headers, so we append this one */
    char *endp = ((char *) pmp->pm_addr) + PA_MMAP_ATOM_SIZE;
    pmhp = (void *) base;
    if (&pmhp->pmh_content[size] > endp) {
	pa_warning(0, "out of header space for '%s' (%d)", name, size);
	return NULL;
    }

    /* Setup the header and return the content */
    strncpy(pmhp->pmh_name, name, sizeof(pmhp->pmh_name));
    pmhp->pmh_size = size;
    pmhp->pmh_type = type;
    pmhp->pmh_flags = flags;

    pmp->pm_infop->pmi_num_headers += 1;

    return &pmhp->pmh_content[0];
}

void *
pa_mmap_next_header (pa_mmap_t *pmp, void *header)
{
    pa_mmap_header_t *pmhp;
    uint8_t *base = pmp->pm_addr;
    uint32_t i;

    base -= sizeof(*pmhp);
    base += sizeof(*pmp->pm_infop); /* Named headers start after ours */

    for (i = 0; i < pmp->pm_infop->pmi_num_headers; i++) {
	pmhp = (void *) base;
	base += sizeof(*pmhp) + pmhp->pmh_size;
	if ((header == NULL || header == &pmhp->pmh_content[0])
	    && i < pmp->pm_infop->pmi_num_headers - 1) {
	    pmhp = (void *) base;
	    return &pmhp->pmh_content[0];
	}
    }

    return NULL;
}
