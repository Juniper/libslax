/*
 * Copyright (c) 2016-2017, Juniper Networks, Inc.
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

#include <libpsu/psualloc.h>
#include <libpsu/psulog.h>
#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <libpsu/psualloc.h>

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
    pa_mmap_atom_t pmi_free;	/* First free memory segment */
} pa_mmap_info_t;

typedef struct pa_mmap_free_s {
    uint32_t pmf_magic;		/* Magic number */
    pa_atom_t pmf_size;		/* Number of atoms free here */
    pa_mmap_atom_t pmf_next;	/* Free list */
} pa_mmap_free_t;

typedef struct pa_mmap_header_s {
    char pmh_name[PA_MMAP_HEADER_NAME_LEN]; /* Simple text name */
    uint16_t pmh_type;		/* Type of data (PA_TYPE_*) */
    uint16_t pmh_flags;		/* Flags for this data (unused) */
    uint32_t pmh_size;		/* Length of header (bytes) */
    psu_byte_t pmh_content[];	/* Content, inline */
} pa_mmap_header_t;

#define PA_ADDR_DEFAULT		0x200000000000
#define PA_ADDR_DEFAULT_INCR	0x020000000000
#define PA_ADDR_MAX		0x600000000000

static uint8_t *pa_mmap_next_address = (void *) PA_ADDR_DEFAULT;
static ptrdiff_t pa_mmap_incr_address = PA_ADDR_DEFAULT_INCR;

/*
 * Add an item from a free list.
 */
static void
pa_mmap_list_add (pa_mmap_t *pmp, pa_mmap_atom_t atom, unsigned size)
{
    pa_mmap_atom_t fa;		/* Free atom number */
    pa_mmap_free_t *pmfp;
    pa_mmap_atom_t *lastp = &pmp->pm_infop->pmi_free;

    for (fa = *lastp; !pa_mmap_is_null(fa); fa = *lastp) {
	pmfp = pa_mmap_addr(pmp, fa);

	if (size <= pmfp->pmf_size)
	    break;

	lastp = &pmfp->pmf_next; /* Move to next item on free list */
    }

    /* Insert atom into the chain */
    pmfp = pa_mmap_addr(pmp, atom);
    pmfp->pmf_magic = PA_MMAP_FREE_MAGIC;
    pmfp->pmf_size = size;
    pmfp->pmf_next = *lastp;
    *lastp = atom;
}

/*
 * Allocate a chunk of memory and return its offset.
 */
pa_mmap_atom_t
pa_mmap_alloc (pa_mmap_t *pmp, size_t size)
{
    if (size == 0) {
	pa_warning(0, "pa_mmap_alloc called with zero size");
	return pa_mmap_null_atom();
    }

    pa_mmap_atom_t fa;		/* Free atom number */
    unsigned count = (size + PA_MMAP_ATOM_SIZE - 1) >> PA_MMAP_ATOM_SHIFT;
    unsigned new_count;
    pa_mmap_free_t *pmfp;
    pa_mmap_atom_t *lastp = &pmp->pm_infop->pmi_free;

    for (fa = *lastp; !pa_mmap_is_null(fa); fa = *lastp) {
	pmfp = pa_mmap_addr(pmp, fa);

	if (pmfp->pmf_size >= count) {
	    /*
	     * We "cheat", allocating from the end so we don't need to
	     * touch the current by_addr list.  We'll still have to mung
	     * the by_size list.
	     */
	    if (count < pmfp->pmf_size) {
		pmfp->pmf_size -= count;
		fa.pma_atom += pmfp->pmf_size; /* Reference end of the chunk */
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
	return pa_mmap_null_atom();
    }

    /* If we've got a file attached, we need to extend the file */
    if (pmp->pm_fd > 0) {
	if (ftruncate(pmp->pm_fd, new_len) < 0) {
	    pa_warning(errno, "cannot extend memory file to %d", new_len);
	    return pa_mmap_null_atom();
	}

	/* Re-mmap the segment */
	void *addr = mmap(pmp->pm_addr, new_len, pmp->pm_mmap_prot,
			  pmp->pm_mmap_flags | MAP_FIXED | MAP_SHARED,
			  pmp->pm_fd, 0);
	if (addr == NULL || addr == MAP_FAILED) {
	    pa_warning(errno, "mmap failed");
	    return pa_mmap_null_atom();
	}

	if (addr != pmp->pm_addr) {
	    pa_warning(0, "mmap was moved (%p:%p)", pmp->pm_addr, addr);
	    return pa_mmap_null_atom();
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
			  pmp->pm_mmap_flags | MAP_FIXED | MAP_SHARED,
			  pmp->pm_fd, 0);
	if (addr == NULL || addr == MAP_FAILED) {
	    pa_warning(errno, "mmap failed");
	    return pa_mmap_null_atom();
	}

	if (addr != target) {
	    pa_warning(0, "mmap was moved (%p:%p:%p)",
		       pmp->pm_addr, target, addr);
	    return pa_mmap_null_atom();
	}

	pa_mmap_record_t *pmrp = psu_calloc(sizeof(*pmrp));
	if (pmrp) {
	    pmrp->pmr_addr = target;
	    pmrp->pmr_len = new_len - old_len;
	    pmrp->pmr_next = pmp->pm_record;
	    pmp->pm_record = pmrp;
	}
    }

    pmp->pm_len = new_len;	/* Record our new length */
    /* We'll use the first chunk for this allocation */
    fa = pa_mmap_atom(old_len >> PA_MMAP_ATOM_SHIFT);

    if (new_count > count) {
	/* Put the rest on the free list */
	pa_mmap_atom_t na = { fa.pma_atom + count };

	pmfp = pa_mmap_addr(pmp, na);
	pmfp->pmf_magic = PA_MMAP_FREE_MAGIC;
	pmfp->pmf_size = new_count - count;
	pmfp->pmf_next = pa_mmap_null_atom();

	*lastp = na;		/* Add 'na' to the free list */
    }

    return fa;
}

void
pa_mmap_free (pa_mmap_t *pmp, pa_mmap_atom_t atom, unsigned size)
{
    if (pa_mmap_is_null(atom)) {
	pa_warning(0, "pa_mmap_free called with NULL");
	return;
    }

    unsigned count = pa_items_shift32(size, PA_MMAP_ATOM_SHIFT);
    if (count == 0) {
	pa_warning(0, "pa_mmap_free called with count 0");
	return;
    }

    pa_mmap_list_add(pmp, atom, count);
}

pa_mmap_t *
pa_mmap_open (const char *filename, const char *base,
	      pa_mmap_flags_t flags, unsigned mode)
{
    int mmap_flags = MAP_SHARED | MAP_FIXED;
    int fd = 0;
    int oflags;
    int prot = PROT_READ | PROT_WRITE;
    struct stat st;
    pa_mmap_info_t *pmip = NULL;
    pa_mmap_t *pmp = NULL;
    int created = 0;
    unsigned len = 0;
    psu_byte_t *addr = NULL;

    if (flags & PMF_READ_ONLY) {
	prot = PROT_READ;
	oflags = O_RDONLY;
    } else {
	oflags = O_RDWR;
    }

    if (filename) {
	if (mode == 0)
	    mode = pa_config_value32(base, "perm", 0644);

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

	    len = pa_config_value32(base, "size", PA_DEFAULT_SIZE);
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

    for (;;) {
	if (pa_mmap_next_address > (psu_byte_t *) PA_ADDR_MAX)
	    goto fail;

	addr = mmap(pa_mmap_next_address, len, prot, mmap_flags | MAP_SHARED,
		    fd, 0);
	if (addr == pa_mmap_next_address) /* Success */
	    break;

	if (addr == NULL || addr == MAP_FAILED) {
	    pa_warning(errno, "mmap failed (%p.vs.%p)",
		       addr, pa_mmap_next_address);
	    if (errno != EINVAL)
		goto fail;

	    pa_mmap_next_address += pa_mmap_incr_address;
	    continue;
	} else if (addr != pa_mmap_next_address) {
	    pa_warning(errno, "mmap returns wrong address (%p.vs.%p)",
		       addr, pa_mmap_next_address);
	    goto fail;
	}
    }

    pa_mmap_next_address += pa_mmap_incr_address;

    pmip = (void *) addr;
    if (created) {
	pmip->pmi_magic = PA_MAGIC_NUMBER;
	pmip->pmi_vers_major = PA_VERS_MAJOR;
	pmip->pmi_vers_minor = PA_VERS_MINOR;
	pmip->pmi_len = len;
	pmip->pmi_max_size = pa_config_value32(base, "max-size", 0);

	/* We waste the rest of the first atom, but we're atom aligned */
	pmip->pmi_free = pa_mmap_atom(1);

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
    pmp = psu_calloc(sizeof(*pmp));
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
	pa_mmap_record_t *pmrp = psu_calloc(sizeof(*pmrp));
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
	    psu_free(pmrp);
	}
    } else {
	if (pmp->pm_addr != NULL)
	    munmap(pmp->pm_addr, pmp->pm_len);
    }

    if (pmp->pm_fd > 0)
	close(pmp->pm_fd);

    psu_free(pmp);
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
    psu_byte_t *endp = pmp->pm_addr + PA_MMAP_ATOM_SIZE;
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

/**
 * Dump the internal state of a pa_mmap table
 */
void
pa_mmap_dump (pa_mmap_t *pmp, psu_boolean_t full)
{
    pa_mmap_info_t *pmip = pmp->pm_infop;

    psu_log("begin pa_mmap dump of %p", pmip);
    psu_log("magic %#x, version %d.%03d, max-size %u, len %lu, free %#x",
	    pmip->pmi_magic, pmip->pmi_vers_major, pmip->pmi_vers_minor,
	    pmip->pmi_max_size, pmip->pmi_len,
	    pa_mmap_atom_of(pmip->pmi_free));

    psu_log("dumping headers: (%d)", pmip->pmi_num_headers);

    if (full) {
	uint32_t i;
	pa_mmap_header_t *pmhp;
	psu_byte_t *base = pmp->pm_addr;

	base += sizeof(*pmip); /* Named headers start after ours */

	for (i = 0; i < pmip->pmi_num_headers; i++) {
	    pmhp = (void *) base;
	    base += sizeof(*pmhp) + pmhp->pmh_size;

	    if (pmhp->pmh_name[0]) {
		psu_log("header #%u: [%.*s] type %d, size %u, flags %x, "
			"start %p",
			i, (int) sizeof(pmhp->pmh_name), pmhp->pmh_name,
			pmhp->pmh_type, pmhp->pmh_size, pmhp->pmh_flags,
			&pmhp->pmh_content[0]);
	    }
	}
    }

    psu_log("end pa_mmap dump of %p", pmip);
}

