/*
 * Standalone test for the mvar RVT refcounting primitives added to
 * libbxslt/libxslt/variables.c (build/plans/mvars-redo.md, Section 6
 * step 2): xsltStackElemReplaceValue() and the static helpers it drives
 * (xsltStackElemAttachMvarDocs / xsltMvarDocsRelease / Detach).
 *
 * This is the verification tool called for by Section 6 step 3. It is
 * deliberately NOT wired into any Makefile.am -- it is a throwaway
 * diagnostic for this step, not a permanent regression test. Compile
 * and run it by hand from the build tree, e.g.:
 *
 *   cd build/tests/bxslt
 *   gcc -std=gnu23 -DHAVE_CONFIG_H -g -O1 -fsanitize=address \
 *     -I. -I../../../tests/bxslt -I../../libslax \
 *     -I../../libbxml/include/libxml -I../../libbxslt -I../.. \
 *     -I../../.. -I../../../libbxslt -I../../libbxslt \
 *     -I../../libbxml/include -I../../../libbxml/include \
 *     -o /tmp/mvar_refcount ../../../tests/bxslt/mvar_refcount.c \
 *     ../../libbxslt/libxslt/.libs/libbxslt.dylib \
 *     ../../libbxml/.libs/libbxml.dylib
 *   DYLD_LIBRARY_PATH=$PWD/../../libbxslt/libxslt/.libs:$PWD/../../libbxml/.libs \
 *     /tmp/mvar_refcount
 *
 * macOS's ASan does not include LeakSanitizer, so it only confirms the
 * absence of use-after-free/double-free here, not leaks; the test's own
 * CHECK()s on refcount values are what confirm no leaks (every scenario
 * drives its docs back to a fully-released, freed state). For an
 * OS-level leak double-check, run under `leaks -atExit -- /tmp/mvar_refcount`
 * instead of the ASan build, or under valgrind on Linux.
 *
 * Each scenario below uses installNewMvarDocs() to bring a brand-new RVT
 * doc into existence and attach it to its owning stack element, mirroring
 * what libslax/slaxmvar.c (Section 6 step 4) will do for a fresh `mvar`
 * container: mvars-redo.md 4.2 says a doc's refcount is "set to 1 when
 * an mvar-owned RVT is first created"; that seed only exists so
 * xsltStackElemAttachMvarDocs() recognizes the doc as mvar-owned on this
 * first attach (it requires refcount > 0 already), and is backed out
 * again right after, so the doc ends up with refcount 1 for exactly one
 * reason -- the owning elem's new mvarDocsTab entry -- not two.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxslt/xsltInternals.h>

static int failures;

#define CHECK(cond, msg) do { \
    if (cond) { \
        printf("ok   - %s\n", msg); \
    } else { \
        printf("FAIL - %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        failures++; \
    } \
} while (0)

/* A fresh RVT-like document with one element node, usable as an
   mvar-owned container. */
static xmlDocPtr
mkDoc(void)
{
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    xmlNodePtr root = xmlNewDocNode(doc, NULL, BAD_CAST "r", NULL);

    xmlDocSetRootElement(doc, root);
    return doc;
}

static xmlXPathObjectPtr
mkNodesetVal(xmlNodePtr *nodes, int n)
{
    xmlNodeSetPtr set = xmlXPathNodeSetCreate(nodes[0]);
    int i;

    for (i = 1; i < n; i++)
        xmlXPathNodeSetAdd(set, nodes[i]);

    return xmlXPathWrapNodeSet(set);
}

static xsltStackElemPtr
newElem(void)
{
    xsltStackElemPtr elem = (xsltStackElemPtr) calloc(1, sizeof(xsltStackElem));

    elem->mvarDocsMax = 16;
    elem->mvarDocsTab = (xmlDocPtr *) calloc(elem->mvarDocsMax, sizeof(xmlDocPtr));
    return elem;
}

static void
freeElem(xsltStackElemPtr elem)
{
    xsltStackElemReplaceValue(elem, NULL);
    free(elem->mvarDocsTab);
    free(elem);
}

/*
 * Marks @docs[0..n) as mvar-owned and installs @val (a value whose
 * nodeset touches exactly those docs, none of which @elem already
 * referenced) as @elem's current value, via the real, public
 * xsltStackElemReplaceValue(). See the file header for why the seed/
 * unseed dance around the call is needed and correct.
 */
static void
installNewMvarDocs(xsltStackElemPtr elem, xmlDocPtr *docs, int n,
	xmlXPathObjectPtr val)
{
    int i;

    for (i = 0; i < n; i++)
        xmlDocSetMvarRefcount(docs[i], 1);

    xsltStackElemReplaceValue(elem, val);

    for (i = 0; i < n; i++)
        xmlDocSetMvarRefcount(docs[i], xmlDocGetMvarRefcount(docs[i]) - 1);
}

static xmlXPathObjectPtr
valOf(xmlDocPtr doc)
{
    xmlNodePtr root = xmlDocGetRootElement(doc);

    return mkNodesetVal(&root, 1);
}

/* ---- Scenario 1: repeated non-scalar reassignment ---- */
static void
test_repeated_reassignment(void)
{
    xmlDocPtr d1 = mkDoc(), d2 = mkDoc(), d3 = mkDoc();
    xsltStackElemPtr elem = newElem();

    printf("-- repeated non-scalar reassignment --\n");

    installNewMvarDocs(elem, &d1, 1, valOf(d1));
    CHECK(xmlDocGetMvarRefcount(d1) == 1, "d1 refcount 1 after first set");

    installNewMvarDocs(elem, &d2, 1, valOf(d2));
    CHECK(xmlDocGetMvarRefcount(d2) == 1, "d2 refcount 1 after second set");
    CHECK(elem->mvarDocsNr == 1 && elem->mvarDocsTab[0] == d2,
	    "elem tracks only d2 after second set");

    installNewMvarDocs(elem, &d3, 1, valOf(d3));
    CHECK(xmlDocGetMvarRefcount(d3) == 1, "d3 refcount 1 after third set");
    CHECK(elem->mvarDocsNr == 1 && elem->mvarDocsTab[0] == d3,
	    "elem tracks only d3 after third set");

    /* d1 and d2 were released (and freed, under ASan/valgrind) as soon
       as each reassignment moved elem's value away from them -- nothing
       left to check on them directly, since touching a freed doc would
       itself be the bug this test exists to catch. */

    freeElem(elem);
}

/* ---- Scenario 2: self-assignment (set $foo = $foo;) ---- */
static void
test_self_assignment(void)
{
    xmlDocPtr d = mkDoc();
    xsltStackElemPtr elem = newElem();
    xmlXPathObjectPtr copy;

    printf("-- self-assignment --\n");

    installNewMvarDocs(elem, &d, 1, valOf(d));
    CHECK(xmlDocGetMvarRefcount(d) == 1, "d refcount 1 after set");

    /* "set $foo = $foo;": re-read $foo's current value (a copy, as a
       real XPath variable-lookup would produce) and write it straight
       back. A buggy xsltStackElemReplaceValue() that released the old
       value's docs before attaching the new one would drop d to 0 and
       free it right here, then hand back a dangling doc. */
    copy = xmlXPathObjectCopy(elem->value);
    xsltStackElemReplaceValue(elem, copy);

    CHECK(xmlDocGetMvarRefcount(d) == 1,
	    "d refcount still 1 after self-assignment (no premature free)");
    CHECK(elem->mvarDocsNr == 1 && elem->mvarDocsTab[0] == d,
	    "elem still tracks exactly one entry for d");
    CHECK(xmlDocGetRootElement(d) != NULL &&
	    xmlStrEqual(xmlDocGetRootElement(d)->name, BAD_CAST "r"),
	    "d's content still intact after self-assignment");

    freeElem(elem);
}

/* ---- Scenario 3: a plain variable's captured reference survives a
   later `set` on the mvar it was drawn from ---- */
static void
test_captured_reference_survives(void)
{
    xmlDocPtr d = mkDoc(), d2 = mkDoc();
    xsltStackElemPtr mvarElem = newElem();
    xsltStackElemPtr plainElem = newElem();
    xmlXPathObjectPtr copy;

    printf("-- captured reference survives a later set -- \n");

    /* mvar $foo = <d's content>; */
    installNewMvarDocs(mvarElem, &d, 1, valOf(d));
    CHECK(xmlDocGetMvarRefcount(d) == 1, "d refcount 1 after mvar init");

    /* var $save = $foo; -- captures a live reference into d */
    copy = xmlXPathObjectCopy(mvarElem->value);
    xsltStackElemReplaceValue(plainElem, copy);
    CHECK(xmlDocGetMvarRefcount(d) == 2,
	    "d refcount 2 once a plain variable captures it too");

    /* set $foo = <d2's content>; */
    installNewMvarDocs(mvarElem, &d2, 1, valOf(d2));
    CHECK(xmlDocGetMvarRefcount(d) == 1,
	    "d refcount drops to 1, not 0 -- plainElem's reference keeps it alive");
    CHECK(xmlDocGetMvarRefcount(d2) == 1, "d2 refcount 1 after the set");
    CHECK(xmlDocGetRootElement(d) != NULL &&
	    xmlStrEqual(xmlDocGetRootElement(d)->name, BAD_CAST "r"),
	    "d's content still intact through plainElem (no use-after-free)");

    /* freeElem(plainElem) drops d's last reference and frees it here --
       nothing left to check on d afterward; reading its refcount post-free
       would itself be the use-after-free this test exists to catch. */
    freeElem(plainElem);

    freeElem(mvarElem);
}

/* ---- Scenario 4: a value spanning multiple mvar-owned docs
   (e.g. a nodeset union) ---- */
static void
test_multi_doc_union(void)
{
    xmlDocPtr da = mkDoc(), db = mkDoc(), dc = mkDoc();
    xsltStackElemPtr elem = newElem();
    xmlDocPtr pair[2];
    xmlNodePtr nodes[2];

    printf("-- value spanning multiple mvar-owned docs (union) --\n");

    pair[0] = da; pair[1] = db;
    nodes[0] = xmlDocGetRootElement(da);
    nodes[1] = xmlDocGetRootElement(db);
    installNewMvarDocs(elem, pair, 2, mkNodesetVal(nodes, 2));

    CHECK(xmlDocGetMvarRefcount(da) == 1, "da refcount 1 after union set");
    CHECK(xmlDocGetMvarRefcount(db) == 1, "db refcount 1 after union set");
    CHECK(elem->mvarDocsNr == 2, "elem tracks both docs from the union");

    /* set $foo = <dc's content>; -- moves away from both da and db */
    installNewMvarDocs(elem, &dc, 1, valOf(dc));
    CHECK(xmlDocGetMvarRefcount(dc) == 1, "dc refcount 1 after the set");
    CHECK(elem->mvarDocsNr == 1 && elem->mvarDocsTab[0] == dc,
	    "elem tracks only dc now (da and db released and freed)");

    freeElem(elem);
}

int
main(void)
{
    xmlInitParser();

    test_repeated_reassignment();
    test_self_assignment();
    test_captured_reference_survives();
    test_multi_doc_union();

    xmlCleanupParser();

    if (failures == 0) {
	printf("ALL PASS\n");
	return 0;
    }

    printf("%d CHECK(S) FAILED\n", failures);
    return 1;
}
