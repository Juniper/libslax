/**
 * @file
 *
 * @brief Accessor functions for libxml2 tree structs
 *
 * `xmlNode`, `xmlNs`, `xmlAttr`, `xmlDoc`, and `xmlDtd` expose their
 * fields directly, and consumers throughout this tree read and write
 * those fields by hand (`node->type`, `ns->prefix`, ...). That's a
 * hidden dependency on today's in-memory layout of those structs.
 *
 * This header gives each field a `static inline` getter (and a setter,
 * where a field is actually mutated by callers) so consumers can stop
 * touching struct internals directly. Once callers go through these
 * functions instead, the struct layout is free to change without a
 * tree-wide rewrite.
 *
 * Naming: `xml<Type>Get<Field>` / `xml<Type>Set<Field>`, where `<Type>`
 * is the struct name (`Node`, `Ns`, `Attr`, `Doc`, `Dtd`), not the
 * pointer typedef.
 *
 * These are plain accessors: no validation, no NULL checks beyond what
 * the struct layout requires. Callers are still responsible for NULL
 * checking the node/ns/etc pointer itself, same as before.
 */

#ifndef __XML_ACCESSORS_H__
#define __XML_ACCESSORS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------
 * xmlNode accessors
 */

static inline xmlElementType
xmlNodeGetType (const xmlNodePtr node)
{
    return node->type;
}

static inline void
xmlNodeSetType (xmlNodePtr node, xmlElementType type)
{
    node->type = type;
}

static inline const xmlChar *
xmlNodeGetName (const xmlNodePtr node)
{
    return node->name;
}

/*
 * `xmlNodeSetName` is already public libxml2 API with different
 * semantics (dict-aware rename). This sets the raw `name` field
 * pointer instead.
 */
static inline void
xmlNodeSetNameRaw (xmlNodePtr node, const xmlChar *name)
{
    node->name = name;
}

static inline xmlNodePtr
xmlNodeGetChildren (const xmlNodePtr node)
{
    return node->children;
}

static inline void
xmlNodeSetChildren (xmlNodePtr node, xmlNodePtr children)
{
    node->children = children;
}

static inline xmlNodePtr
xmlNodeGetLast (const xmlNodePtr node)
{
    return node->last;
}

static inline void
xmlNodeSetLast (xmlNodePtr node, xmlNodePtr last)
{
    node->last = last;
}

static inline xmlNodePtr
xmlNodeGetParent (const xmlNodePtr node)
{
    return node->parent;
}

static inline void
xmlNodeSetParent (xmlNodePtr node, xmlNodePtr parent)
{
    node->parent = parent;
}

static inline xmlNodePtr
xmlNodeGetNext (const xmlNodePtr node)
{
    return node->next;
}

static inline void
xmlNodeSetNext (xmlNodePtr node, xmlNodePtr next)
{
    node->next = next;
}

static inline xmlNodePtr
xmlNodeGetPrev (const xmlNodePtr node)
{
    return node->prev;
}

static inline void
xmlNodeSetPrev (xmlNodePtr node, xmlNodePtr prev)
{
    node->prev = prev;
}

static inline xmlDocPtr
xmlNodeGetDoc (const xmlNodePtr node)
{
    return node->doc;
}

/*
 * `xmlNodeSetDoc` collides with an internal (static) helper of the
 * same name in tree.c, which recursively updates a subtree's dict
 * along with the doc pointer. This sets the raw `doc` field pointer
 * on a single node instead.
 */
static inline void
xmlNodeSetDocRaw (xmlNodePtr node, xmlDocPtr doc)
{
    node->doc = doc;
}

static inline xmlNsPtr
xmlNodeGetNs (const xmlNodePtr node)
{
    return node->ns;
}

static inline void
xmlNodeSetNs (xmlNodePtr node, xmlNsPtr ns)
{
    node->ns = ns;
}

/*
 * `xmlNodeGetContent`/`xmlNodeSetContent` are already public libxml2
 * API with different semantics (allocate-and-return, copy-and-escape).
 * These access the raw `content` field pointer instead.
 */
static inline xmlChar *
xmlNodeGetContentRaw (const xmlNodePtr node)
{
    return node->content;
}

static inline void
xmlNodeSetContentRaw (xmlNodePtr node, xmlChar *content)
{
    node->content = content;
}

static inline xmlAttrPtr
xmlNodeGetProperties (const xmlNodePtr node)
{
    return node->properties;
}

static inline void
xmlNodeSetProperties (xmlNodePtr node, xmlAttrPtr properties)
{
    node->properties = properties;
}

static inline xmlNsPtr
xmlNodeGetNsDef (const xmlNodePtr node)
{
    return node->nsDef;
}

static inline void
xmlNodeSetNsDef (xmlNodePtr node, xmlNsPtr nsDef)
{
    node->nsDef = nsDef;
}

static inline void *
xmlNodeGetPsvi (const xmlNodePtr node)
{
    return node->psvi;
}

static inline void
xmlNodeSetPsvi (xmlNodePtr node, void *psvi)
{
    node->psvi = psvi;
}

static inline unsigned short
xmlNodeGetLine (const xmlNodePtr node)
{
    return node->line;
}

static inline void
xmlNodeSetLine (xmlNodePtr node, unsigned short line)
{
    node->line = line;
}

static inline unsigned short
xmlNodeGetExtra (const xmlNodePtr node)
{
    return node->extra;
}

static inline void
xmlNodeSetExtra (xmlNodePtr node, unsigned short extra)
{
    node->extra = extra;
}

/* ----------------------------------------------------------------------
 * xmlNs accessors
 */

static inline xmlNsPtr
xmlNsGetNext (const xmlNsPtr ns)
{
    return ns->next;
}

static inline void
xmlNsSetNext (xmlNsPtr ns, xmlNsPtr next)
{
    ns->next = next;
}

static inline xmlNsType
xmlNsGetType (const xmlNsPtr ns)
{
    return ns->type;
}

static inline void
xmlNsSetType (xmlNsPtr ns, xmlNsType type)
{
    ns->type = type;
}

static inline const xmlChar *
xmlNsGetHref (const xmlNsPtr ns)
{
    return ns->href;
}

static inline void
xmlNsSetHref (xmlNsPtr ns, const xmlChar *href)
{
    ns->href = href;
}

static inline const xmlChar *
xmlNsGetPrefix (const xmlNsPtr ns)
{
    return ns->prefix;
}

static inline void
xmlNsSetPrefix (xmlNsPtr ns, const xmlChar *prefix)
{
    ns->prefix = prefix;
}

static inline void *
xmlNsGetPrivate (const xmlNsPtr ns)
{
    return ns->_private;
}

static inline void
xmlNsSetPrivate (xmlNsPtr ns, void *priv)
{
    ns->_private = priv;
}

/* ----------------------------------------------------------------------
 * xmlAttr accessors
 */

static inline xmlElementType
xmlAttrGetType (const xmlAttrPtr attr)
{
    return attr->type;
}

static inline const xmlChar *
xmlAttrGetName (const xmlAttrPtr attr)
{
    return attr->name;
}

static inline xmlNodePtr
xmlAttrGetChildren (const xmlAttrPtr attr)
{
    return attr->children;
}

static inline void
xmlAttrSetChildren (xmlAttrPtr attr, xmlNodePtr children)
{
    attr->children = children;
}

static inline xmlNodePtr
xmlAttrGetLast (const xmlAttrPtr attr)
{
    return attr->last;
}

static inline void
xmlAttrSetLast (xmlAttrPtr attr, xmlNodePtr last)
{
    attr->last = last;
}

static inline xmlNodePtr
xmlAttrGetParent (const xmlAttrPtr attr)
{
    return attr->parent;
}

static inline void
xmlAttrSetParent (xmlAttrPtr attr, xmlNodePtr parent)
{
    attr->parent = parent;
}

static inline xmlAttrPtr
xmlAttrGetNext (const xmlAttrPtr attr)
{
    return attr->next;
}

static inline void
xmlAttrSetNext (xmlAttrPtr attr, xmlAttrPtr next)
{
    attr->next = next;
}

static inline xmlAttrPtr
xmlAttrGetPrev (const xmlAttrPtr attr)
{
    return attr->prev;
}

static inline void
xmlAttrSetPrev (xmlAttrPtr attr, xmlAttrPtr prev)
{
    attr->prev = prev;
}

static inline xmlDocPtr
xmlAttrGetDoc (const xmlAttrPtr attr)
{
    return attr->doc;
}

static inline void
xmlAttrSetDoc (xmlAttrPtr attr, xmlDocPtr doc)
{
    attr->doc = doc;
}

static inline xmlNsPtr
xmlAttrGetNs (const xmlAttrPtr attr)
{
    return attr->ns;
}

static inline void
xmlAttrSetNs (xmlAttrPtr attr, xmlNsPtr ns)
{
    attr->ns = ns;
}

static inline xmlAttributeType
xmlAttrGetAtype (const xmlAttrPtr attr)
{
    return attr->atype;
}

static inline void
xmlAttrSetAtype (xmlAttrPtr attr, xmlAttributeType atype)
{
    attr->atype = atype;
}

static inline void *
xmlAttrGetPsvi (const xmlAttrPtr attr)
{
    return attr->psvi;
}

static inline void
xmlAttrSetPsvi (xmlAttrPtr attr, void *psvi)
{
    attr->psvi = psvi;
}

/* ----------------------------------------------------------------------
 * xmlDoc accessors (structural fields only; scalar metadata like
 * compression/standalone/encoding/version/URL/charset/intSubset/
 * extSubset is deferred, see accessor-plan.md)
 */

static inline void *
xmlDocGetPrivate (const xmlDocPtr doc)
{
    return doc->_private;
}

static inline void
xmlDocSetPrivate (xmlDocPtr doc, void *priv)
{
    doc->_private = priv;
}

static inline xmlElementType
xmlDocGetType (const xmlDocPtr doc)
{
    return doc->type;
}

static inline void
xmlDocSetType (xmlDocPtr doc, xmlElementType type)
{
    doc->type = type;
}

static inline const char *
xmlDocGetName (const xmlDocPtr doc)
{
    return doc->name;
}

static inline xmlNodePtr
xmlDocGetChildren (const xmlDocPtr doc)
{
    return doc->children;
}

static inline void
xmlDocSetChildren (xmlDocPtr doc, xmlNodePtr children)
{
    doc->children = children;
}

static inline xmlNodePtr
xmlDocGetLast (const xmlDocPtr doc)
{
    return doc->last;
}

static inline void
xmlDocSetLast (xmlDocPtr doc, xmlNodePtr last)
{
    doc->last = last;
}

static inline xmlNodePtr
xmlDocGetNext (const xmlDocPtr doc)
{
    return doc->next;
}

static inline void
xmlDocSetNext (xmlDocPtr doc, xmlNodePtr next)
{
    doc->next = next;
}

static inline void *
xmlDocGetPsvi (const xmlDocPtr doc)
{
    return doc->psvi;
}

static inline void
xmlDocSetPsvi (xmlDocPtr doc, void *psvi)
{
    doc->psvi = psvi;
}

static inline xmlNodePtr
xmlDocGetPrev (const xmlDocPtr doc)
{
    return doc->prev;
}

static inline void
xmlDocSetPrev (xmlDocPtr doc, xmlNodePtr prev)
{
    doc->prev = prev;
}

static inline xmlNodePtr
xmlDocGetParent (const xmlDocPtr doc)
{
    return doc->parent;
}

static inline void
xmlDocSetParent (xmlDocPtr doc, xmlNodePtr parent)
{
    doc->parent = parent;
}

/* `doc` field on xmlDoc is a self-reference (a doc's containing doc is
 * itself); named to match the field, not to be confused with the
 * accessor for other structs' `doc` field. */
static inline xmlDocPtr
xmlDocGetDoc (const xmlDocPtr doc)
{
    return doc->doc;
}

static inline void
xmlDocSetDoc (xmlDocPtr doc, xmlDocPtr self)
{
    doc->doc = self;
}

static inline xmlNsPtr
xmlDocGetOldNs (const xmlDocPtr doc)
{
    return doc->oldNs;
}

static inline void
xmlDocSetOldNs (xmlDocPtr doc, xmlNsPtr oldNs)
{
    doc->oldNs = oldNs;
}

/* ----------------------------------------------------------------------
 * xmlDtd accessors
 */

static inline xmlElementType
xmlDtdGetType (const xmlDtdPtr dtd)
{
    return dtd->type;
}

static inline const xmlChar *
xmlDtdGetName (const xmlDtdPtr dtd)
{
    return dtd->name;
}

static inline xmlNodePtr
xmlDtdGetChildren (const xmlDtdPtr dtd)
{
    return dtd->children;
}

static inline xmlNodePtr
xmlDtdGetLast (const xmlDtdPtr dtd)
{
    return dtd->last;
}

static inline xmlDocPtr
xmlDtdGetParent (const xmlDtdPtr dtd)
{
    return dtd->parent;
}

static inline xmlNodePtr
xmlDtdGetNext (const xmlDtdPtr dtd)
{
    return dtd->next;
}

static inline xmlNodePtr
xmlDtdGetPrev (const xmlDtdPtr dtd)
{
    return dtd->prev;
}

static inline xmlDocPtr
xmlDtdGetDoc (const xmlDtdPtr dtd)
{
    return dtd->doc;
}

#ifdef __cplusplus
}
#endif

#endif /* __XML_ACCESSORS_H__ */
