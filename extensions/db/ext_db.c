/*
 * $Id$
 *
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/param.h>
#include <stdlib.h>
#include <time.h>

#include <sys/queue.h>

#include <libxml/xpathInternals.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlsave.h>
#include <libxml/HTMLparser.h>

#include "slaxconfig.h"

#include <libxslt/extensions.h>
#include <libslax/slaxdata.h>
#include <libslax/slaxdyn.h>
#include <libslax/slaxio.h>
#include <libslax/xmlsoft.h>
#include <libslax/slaxutil.h>

#include "ext_db.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#if !defined(HAVE_DLFUNC)
#define dlfunc(_p, _n)          dlsym(_p, _n)
#endif
#else /* HAVE_DLFCN_H */
#define dlopen(_n, _f)          NULL /* Fail */
#define dlsym(_p, _n)           NULL /* Fail */
#define dlfunc(_p, _n)          NULL /* Fail */
#endif /* HAVE_DLFCN_H */


/*
 * Function pointer for driver initialization function
 */
typedef void (*db_driver_init_func_t)(db_driver_t *);

TAILQ_HEAD(db_drivers_s, db_driver_s) extDbDrivers;
TAILQ_HEAD(db_session_s, db_handle_s) extDbSessions;

/*
 * Given name of the driver, dlopens the library, initializes it, adds to the
 * list of available drivers and returns it
 */
static db_driver_t *
db_load_driver (const char *name)
{
    char buf[MAXPATHLEN];
    void *dlp = NULL;
    db_driver_init_func_t func;

    size_t len = snprintf(buf, sizeof(buf), "%s/db/db_driver_%s.so",
			  SLAX_EXTDIR, name);

    if (len > sizeof(buf))
	return NULL;

    dlp = dlopen((const char *) buf, RTLD_NOW|RTLD_GLOBAL);
    if (dlp) {
	slaxLog("db: loading driver - %s", buf);

	func = (db_driver_init_func_t) dlfunc(dlp, DB_DRIVER_INIT_NAME);
    	if (func) {
	    db_driver_t *driver = xmlMalloc(sizeof(*driver));
	    if (driver) {
		bzero(driver, sizeof(*driver));

		snprintf(driver->dd_name, sizeof(driver->dd_name), 
			 "%s", name);

		TAILQ_INSERT_TAIL(&extDbDrivers, driver, dd_link);

		(*func)(driver);

		return driver;
	    }
	}
	dlclose(dlp);
    }
    slaxLog("extension failed: %s", dlerror() ?: "none");

    return NULL;
}

/*
 * Allocate db handler
 */
static db_handle_t *
db_handle_alloc (db_driver_t *driver)
{
    db_handle_t *handle = xmlMalloc(sizeof(*handle));
    static unsigned seed = 1234;

    if (handle) {
	bzero(handle, sizeof(*handle));

	snprintf(handle->dh_name, sizeof(handle->dh_name), "%udb", seed++);
	handle->dh_driver = driver;
	
	TAILQ_INSERT_TAIL(&extDbSessions, handle, dh_link);
    }

    return handle;
}

/*
 * Free given handle
 */
static void
db_handle_free (db_handle_t *dbhp)
{
    if (dbhp) {
	TAILQ_REMOVE(&extDbSessions, dbhp, dh_link);
	xmlFree(dbhp);
    }
}

/*
 * Given name of database engine, return driver structure. If we don't have a
 * drive available with that name, try loading it and return the loaded
 * driver. If everything fails, return NULL
 */
static db_driver_t *
db_get_driver (const char *name)
{
    db_driver_t *driver;
    /*
     * Iterate through the list of available drivers and return the matching
     * one
     */
    TAILQ_FOREACH(driver, &extDbDrivers, dd_link) {
	if (streq(driver->dd_name, name)) {
	    return driver;
	}
    }

    /*
     * Look up and load driver library
     */
    return db_load_driver(name);
}

/*
 * Finds db_handle_t corresponding to the give session name
 */
static db_handle_t *
db_get_handle_by_name (const char *name)
{
    db_handle_t *dbhp;

    TAILQ_FOREACH(dbhp, &extDbSessions, dh_link) {
	if (streq(dbhp->dh_name, name)) {
	    return dbhp;
	}
    }

    return NULL;
}

#define DB_STRING_SET(_v) \
    do { \
	if (_v) \
	    xmlFree(_v); \
	_v = xmlStrdup2(xmlNodeValue(nodep) ?: ""); \
    } while (0)

#define DB_XML_NODE_SET(_v) \
    do { \
	if (_v) \
	    xmlFreeNode(_v); \
	_v = xmlCopyNode(nodep, 1); \
    } while (0)

/*
 * Parse input from XML node
 */
static void
db_parse_node (db_input_t *input, xmlNodePtr nodep)
{
    const char *key;

    key = xmlNodeName(nodep);
    if (key == NULL)
	return;

    slaxLog("db: parsing node %s", key);

    if (streq(key, "engine"))
	DB_STRING_SET(input->di_engine);
    else if (streq(key, "database"))
	DB_STRING_SET(input->di_database);
    else if (streq(key, "collection"))
	DB_STRING_SET(input->di_collection);
    else if (streq(key, "limit"))
	input->di_limit = atoi(xmlNodeValue(nodep));
    else if (streq(key, "skip"))
	input->di_skip = atoi(xmlNodeValue(nodep));
    else if (streq(key, "access"))
	DB_XML_NODE_SET(input->di_access);
    else if (streq(key, "fields"))
	DB_XML_NODE_SET(input->di_fields);
    else if (streq(key, "instance"))
	DB_XML_NODE_SET(input->di_instance);
    else if (streq(key, "instances"))
	DB_XML_NODE_SET(input->di_instances);
    else if (streq(key, "conditions"))
	DB_XML_NODE_SET(input->di_conditions);
    else if (streq(key, "constraints"))
	DB_XML_NODE_SET(input->di_constraints);
    else if (streq(key, "sort"))
	DB_XML_NODE_SET(input->di_sort);
    else if (streq(key, "retrieve"))
	DB_XML_NODE_SET(input->di_retrieve);
    else if (streq(key, "update"))
	DB_XML_NODE_SET(input->di_update);
}

#define COPY_STRING(_v) \
    do { \
	top->_v = xmlStrdup2(fromp->_v); \
    } while (0)

#define COPY_NODE_SET(_v) \
    do { \
	top->_v = xmlCopyNode(fromp->_v, 1); \
    } while (0)

void
db_input_copy (db_input_t *top, db_input_t *fromp)
{
    slax_printf_buffer_t pb;

    bzero(&pb, sizeof(pb));
    bzero(top, sizeof(*top));

    COPY_STRING(di_engine);
    COPY_STRING(di_database);
    COPY_STRING(di_collection);

    fromp->di_limit = top->di_limit;
    fromp->di_skip = top->di_skip;

    COPY_NODE_SET(di_access);
    COPY_NODE_SET(di_fields);
    COPY_NODE_SET(di_instance);
    COPY_NODE_SET(di_instances);
    COPY_NODE_SET(di_conditions);
    COPY_NODE_SET(di_constraints);
    COPY_NODE_SET(di_sort);
    COPY_NODE_SET(di_retrieve);
    COPY_NODE_SET(di_update);

    if (fromp->di_buf.pb_buf) {
	slaxExtPrintAppend(&pb, (const xmlChar *) fromp->di_buf.pb_buf,
			   strlen(fromp->di_buf.pb_buf));
    }
    top->di_buf = pb;
}

/*
 * Parse input XML and fill up db_input_t structure to be consumed later
 */
static db_input_t *
db_input_parse (xmlXPathObject *ostack[], int nargs)
{
    db_input_t *input;
    int osi;

    if (nargs < 1) {
	return NULL;
    }

    input = xmlMalloc(sizeof(*input));
    if (input) {
	bzero(input, sizeof(*input));

	for (osi = nargs - 1; osi >= 0; osi--) {
	    xmlXPathObject *xop = ostack[osi];

	    if (xop->nodesetval) {
		xmlNodeSetPtr nodeset;
		xmlNodePtr nop, cop;
		int i;

		nodeset = xop->nodesetval;
		for (i = 0; i < nodeset->nodeNr; i++) {
		    nop = nodeset->nodeTab[i];

		    if (nop->type == XML_ELEMENT_NODE)
			db_parse_node(input, nop);

		    for (cop = nop->children; cop; cop = cop->next) {
			if (cop->type != XML_ELEMENT_NODE)
			    continue;

			db_parse_node(input, cop);
		    }
		}
	    } else if (xop->stringval) {
		slaxExtPrintAppend(&input->di_buf, xop->stringval,
				   xmlStrlen(xop->stringval));
	    }
	}
    }

    return input;
}

#define DB_XML_NODE_FREE(_v) \
    do { \
	if (_v) \
	    xmlFreeNode(_v); \
    } while (0)

#define DB_STRING_FREE(_v) \
    do { \
	if (_v) \
	    xmlFree(_v); \
    } while (0)

/*
 * Free input structure
 */
void
db_input_free (db_input_t *input)
{
    DB_STRING_FREE(input->di_engine);
    DB_STRING_FREE(input->di_database);
    DB_STRING_FREE(input->di_collection);

    DB_XML_NODE_FREE(input->di_access);
    DB_XML_NODE_FREE(input->di_fields);
    DB_XML_NODE_FREE(input->di_instances);
    DB_XML_NODE_FREE(input->di_conditions);
    DB_XML_NODE_FREE(input->di_constraints);
    DB_XML_NODE_FREE(input->di_sort);
    DB_XML_NODE_FREE(input->di_retrieve);
    DB_XML_NODE_FREE(input->di_update);

    if (input->di_buf.pb_buf) {
	xmlFree(input->di_buf.pb_buf);
    }

    xmlFree(input);
}

static void
extDbClose (xmlXPathParserContext *ctxt UNUSED, int nargs)
{
    char *name = NULL;
    db_handle_t *dbhp;
    db_ret_t rc;

    if (nargs != 1) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    name = (char *) xmlXPathPopString(ctxt);
    if (name == NULL) {
	LX_ERR("db:close: no handle provided\n");
	return;
    }

    dbhp = db_get_handle_by_name(name);

    if (dbhp) {
	/* Call driver close callback */
	if (dbhp->dh_driver->dd_close) {
	    rc = (dbhp->dh_driver->dd_close)(dbhp, ctxt, nargs);
	    if (rc != DB_OK) {
		LX_ERR("db:close: failed closing database handler\n");
		return;
	    }
	}
	db_handle_free(dbhp);
    }

    xmlFree(name);
    xmlXPathReturnString(ctxt, xmlStrdup((const xmlChar *) ""));
}

/*
 * Performs create/insert/update/delete operation by calling corresponding
 * callbacks
 */
static void
extDbOperate (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED, 
		const char *operation)
{
    xmlXPathObject *ostack[nargs];
    db_input_t *in = NULL;
    slax_printf_buffer_t pb;
    xmlNodePtr childp, resultp, statusp;
    xmlXPathObjectPtr ret;
    xmlDocPtr container;
    xmlNodeSet *results;
    db_handle_t *dbhp;
    db_ret_t rc = DB_FAIL;
    char *name;
    int osi;

    if (nargs < 2) {
	LX_ERR("db:%s: too few arguments\n", operation);
	return;
    }

    for (osi = nargs - 1; osi >= 0; osi--)
	ostack[osi] = valuePop(ctxt);

    name = (char *)xmlXPathCastToString(ostack[0]);
    if (name == NULL) {
	LX_ERR("db:%s: missing handle\n", operation);
	goto fail;
    }

    in = db_input_parse(ostack + 1, nargs - 1);
    if (in ==  NULL) {
	LX_ERR("db:%s: unable to parse data\n", operation);
	goto fail;
    }

    bzero(&pb, sizeof(pb));

    /*
     * Get database handle and invoke insert callback on it
     */
    dbhp = db_get_handle_by_name(name);
    if (dbhp && dbhp->dh_driver) {
	if (streq(operation, "create")) {
	    rc = (*dbhp->dh_driver->dd_create)(dbhp, in, ctxt, nargs, &pb);
	} else if (streq(operation, "insert")) {
	    rc = (*dbhp->dh_driver->dd_insert)(dbhp, in, ctxt, nargs, &pb);
	} else if (streq(operation, "update")) {
	    rc = (*dbhp->dh_driver->dd_update)(dbhp, in, ctxt, nargs, &pb);
	} else if (streq(operation, "delete")) {
	    rc = (*dbhp->dh_driver->dd_delete)(dbhp, in, ctxt, nargs, &pb);
	}

	if (rc == DB_OK) {
	    container = slaxMakeRtf(ctxt);
	    resultp = xmlNewDocNode(container, NULL, 
				    (const xmlChar *) "result", NULL);
	    statusp = xmlNewDocNode(container, NULL, 
				    (const xmlChar *) "status", NULL);
	    childp = xmlNewDocNode(container, NULL, 
				    (const xmlChar *) "success", NULL);
	    if (resultp == NULL || statusp == NULL || childp == NULL) {
		LX_ERR("db:%s: failed to create result node\n", operation);
		goto fail;
	    }

	    xmlAddChild(statusp, childp);
	    xmlAddChild(resultp, statusp);

	    results = xmlXPathNodeSetCreate(NULL);
	    xmlXPathNodeSetAdd(results, resultp);
	    ret = xmlXPathNewNodeSetList(results);

	    valuePush(ctxt, ret);
	    xmlXPathFreeNodeSet(results);
	} else if (rc == DB_ERROR) {
	    LX_ERR("db:%s: failed with error - %s\n", operation, pb.pb_buf);
	    goto fail;
	} else {
	    LX_ERR("db:%s: failed to perform operation\n", operation);
	    goto fail;
	}
    } else {
	LX_ERR("db:%s: invalid handle\n", operation);
	goto fail;
    }

 fail:
    for (osi = nargs - 1; osi >= 0; osi--) {
	if (ostack[osi]) {
	    xmlXPathFreeObject(ostack[osi]);
	}
    }

    if (in) {
	db_input_free(in);
    }

    if (pb.pb_buf) {
	xmlFree(pb.pb_buf);
    }

}

/*
 * Inserts given instances into the collection and uses defaults if there are
 * any missing field values
 */
static void
extDbInsert (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)
{
    extDbOperate(ctxt, nargs, "insert");
}

/*
 * Given handle and schema, creates a collection/table
 */
static void
extDbCreate (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)
{
    extDbOperate(ctxt, nargs, "create");
}

/*
 * Deletes instances matching provided conditions
 */
static void
extDbDelete (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)
{
    extDbOperate(ctxt, nargs, "delete");
}

/*
 * Updates matching instances with provided data
 */
static void
extDbUpdate (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)
{
    extDbOperate(ctxt, nargs, "update");
}

/*
 * Given a cursor, returns the next set of results. Cursor is used to uniquely
 * identify the query that was run. It is a combination of handler id +
 * driver specific statement id that was returned to us when creating query 
 * statement. Our final cursor will look like xxxxdbxxxx.
 */
static void
extDbFetch (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)
{
    xmlXPathObject *ostack[nargs];
    char *name, *cp;
    char buf[BUFSIZ];
    db_handle_t *dbhp;
    db_ret_t rc;
    slax_printf_buffer_t pb;
    xmlNodePtr childp = NULL, resultp = NULL, statusp = NULL;
    xmlXPathObjectPtr ret;
    xmlXPathObject *input = NULL;
    xmlDocPtr container;
    xmlNodeSet *results;
    int osi;

    if (nargs < 1) {
	LX_ERR("db:fetch: too few arguments\n");
	return;
    }

    bzero(&pb, sizeof(pb));

    for (osi = nargs - 1; osi >= 0; osi--)
	ostack[osi] = valuePop(ctxt);

    name = (char *)xmlXPathCastToString(ostack[0]);
    if (name == NULL) {
	LX_ERR("db:fetch: missing cursor\n");
	goto fail;
    }

    /*
     * If input data is provided with fetch, we use it to bind to prepared
     * statement and execute it. This will be useful when we want to reuse a
     * prepared statement to insert multiple instances
     */
    if (nargs > 1) {
	input = ostack[1];
    }

    /*
     * Extract handle id from cursor
     */
     cp = strchr(name, 'b');
     if (cp && cp > name) {
	memcpy(buf, name, cp - name + 1);
	buf[cp - name + 1] = '\0';

	dbhp = db_get_handle_by_name(buf);
	if (dbhp == NULL || dbhp->dh_driver == NULL) {
	    LX_ERR("db:fetch: invalid curosr\n");
	    goto fail;
	}

	container = slaxMakeRtf(ctxt);
	if (container == NULL) {
	    LX_ERR("db:fetch: failed to create result container\n");
	    goto fail;
	}

	resultp = xmlNewDocNode(container, NULL, (const xmlChar *) "result",
				NULL);

	statusp = xmlNewDocNode(container, NULL, (const xmlChar *) "status",
				NULL);

	if (resultp == NULL || statusp == NULL) {
	    LX_ERR("db:fetch: failed to create result/status nodes\n");
	    goto fail;
	}

	rc = (*dbhp->dh_driver->dd_fetch)(dbhp, cp + 1, input, ctxt, nargs, 
					  &pb);
	if (rc == DB_DATA) {
	    xmlDocPtr xmlp;

	    xmlp = xmlReadMemory(pb.pb_buf, strlen(pb.pb_buf), "raw_data", 
				 NULL, XML_PARSE_NOENT);
	    if (xmlp == NULL) {
		slaxLog("db:fetch: failed to read raw data from result");
		goto fail;
	    }

	    childp = xmlDocGetRootElement(xmlp);
	    if (childp) {
		xmlAddChild(resultp, childp);
	    }
	    
	    childp = xmlNewDocNode(container, NULL, (const xmlChar *) "data",
				   NULL);
	} else if (rc == DB_DONE) {
	    childp = xmlNewDocNode(container, NULL, (const xmlChar *) "done",
				   NULL);
	} else if (rc == DB_OK) {
	    childp = xmlNewDocNode(container, NULL, (const xmlChar *) "done",
				   NULL);
	} else if (rc == DB_ERROR) {
	    childp = xmlNewDocNode(container, NULL, (const xmlChar *) "error",
				   (const xmlChar *) pb.pb_buf);
	} else if (rc == DB_FAIL) {
	    childp = xmlNewDocNode(container, NULL, (const xmlChar *) "fail",
				   (const xmlChar *) "");
	}

	if (childp) {
	    xmlAddChild(statusp, childp);
	}

	xmlAddChild(resultp, statusp);

	results = xmlXPathNodeSetCreate(NULL);
	xmlXPathNodeSetAdd(results, resultp);
	ret = xmlXPathNewNodeSetList(results);

	valuePush(ctxt, ret);
	xmlXPathFreeNodeSet(results);
     } else {
	LX_ERR("db:fetch: invalid cursor\n");
	goto fail;
     }

 fail:
    for (osi = nargs - 1; osi >= 0; osi--) {
	if (ostack[osi]) {
	    xmlXPathFreeObject(ostack[osi]);
	}
    }

    if (pb.pb_buf) {
	xmlFree(pb.pb_buf);
    }
}

/*
 * Finds instances matching given conditions
 */
static void
extDbFind (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)
{
    xmlXPathObject *ostack[nargs];
    db_handle_t *dbhp;
    db_input_t *in = NULL;
    int osi;
    char *name;
    db_ret_t rc;
    slax_printf_buffer_t pb;
    char buf[BUFSIZ];

    if (nargs < 2) {
	LX_ERR("db:find: too few arguments. Must provide handle and input\n");
	return;
    }

    bzero(&pb, sizeof(pb));

    for (osi = nargs - 1; osi >= 0; osi--)
	ostack[osi] = valuePop(ctxt);

    name = (char *) xmlXPathCastToString(ostack[0]);
    if (name == NULL) {
	LX_ERR("db:find: missing handle\n");
	goto fail;
    }

    in = db_input_parse(ostack + 1, nargs - 1);
    if (in == NULL) {
	LX_ERR("db:find: unable to parse input\n");
	goto fail;
    }

    /*
     * Get database handle and invoke find callback on driver
     */
    dbhp = db_get_handle_by_name(name);
    if (dbhp && dbhp->dh_driver) {
	rc = (*dbhp->dh_driver->dd_find)(dbhp, in, ctxt, nargs, &pb);
	if (rc == DB_DATA) {
	    snprintf(buf, sizeof(buf), "%s%s", dbhp->dh_name, pb.pb_buf);
	    xmlXPathReturnString(ctxt, xmlStrdup((const xmlChar *) buf));
	} else if (rc == DB_ERROR) {
	    LX_ERR("db:find: failed with error - %s\n", pb.pb_buf);
	    goto fail;
	} else {
	    LX_ERR("db:find: failed to find\n");
	    goto fail;
	}
    } else {
	LX_ERR("db:find: invalid handle\n");
	goto fail;
    }

 fail:
    for (osi = nargs - 1; osi >= 0; osi--) {
	if (ostack[osi]) {
	    xmlXPathFreeObject(ostack[osi]);
	}
    }
    
    if (in) {
	db_input_free(in);
    }

    if (pb.pb_buf) {
	xmlFree(pb.pb_buf);
    }
}

/*
 * Finds and fetchs all the data matching provided options
 */
static void
extDbFindAndFetch (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)
{
    xmlXPathObject *ostack[nargs];
    db_handle_t *dbhp;
    db_input_t *in = NULL;
    int osi;
    char *name;
    db_ret_t rc;
    slax_printf_buffer_t pb;
    xmlNodePtr childp, resultp = NULL, statusp = NULL;
    xmlXPathObjectPtr ret;
    xmlDocPtr container;
    xmlNodeSet *results;


    if (nargs < 2) {
	LX_ERR("db:find-and-fetch: too few arguments. "
	       "Must provide handle and input\n");
	return;
    }

    bzero(&pb, sizeof(pb));

    for (osi = nargs - 1; osi >= 0; osi--)
	ostack[osi] = valuePop(ctxt);

    name = (char *) xmlXPathCastToString(ostack[0]);
    if (name == NULL) {
	LX_ERR("db:find-and-fetch: missing handle\n");
	goto fail;
    }

    in = db_input_parse(ostack + 1, nargs - 1);
    if (in == NULL) {
	LX_ERR("db:find-and-fetch: unable to parse input\n");
	goto fail;
    }

    /*
     * Get database handle and invoke findAndFetch callback on driver
     */
    dbhp = db_get_handle_by_name(name);
    if (dbhp == NULL || dbhp->dh_driver == NULL) {
	LX_ERR("db:find-and-fetch: invalid handle\n");
	goto fail;
    }

    container = slaxMakeRtf(ctxt);
    if (container == NULL) {
	LX_ERR("db:find-and-fetch: failed to create result container\n");
	goto fail;
    }

    resultp = xmlNewDocNode(container, NULL, (const xmlChar *) "result",
			    NULL);

    statusp = xmlNewDocNode(container, NULL, (const xmlChar *) "status",
			    NULL);

    if (resultp == NULL || statusp == NULL) {
	LX_ERR("db:find-and-fetch: failed to create result/status nodes\n");
	goto fail;
    }

    rc = (*dbhp->dh_driver->dd_findAndFetch)(dbhp, name, in, ctxt, nargs, 
					     &pb);
    if (rc == DB_DATA) {
	xmlDocPtr xmlp;

	xmlp = xmlReadMemory(pb.pb_buf, strlen(pb.pb_buf), "raw_data", NULL,
			     XML_PARSE_NOENT);
	if (xmlp == NULL) {
	    slaxLog("db:find-and-fetch: failed to read raw data from result");
	    goto fail;
	}

	childp = xmlDocGetRootElement(xmlp);
	if (childp) {
	    /*
	     * If driver emits XML output wrapped in output tag, get the
	     * output and append it to result
	     */
	    if (streq(xmlNodeName(childp), "output")) {
		childp = childp->children;
		while (childp) {
		    xmlAddChild(resultp, childp);
		    childp = childp->next;
		}
	    } else {
		xmlAddChild(resultp, childp);
	    }
	}

	childp = xmlNewDocNode(container, NULL, (const xmlChar *) "data",
			       NULL);
	if (childp) {
	    xmlAddChild(statusp, childp);
	}
    } else if (rc == DB_DONE) {
	childp = xmlNewDocNode(container, NULL, (const xmlChar *) "done",
			       NULL);
	if (childp) {
	    xmlAddChild(statusp, childp);
	}
    } else if (rc == DB_OK) {
	childp = xmlNewDocNode(container, NULL, (const xmlChar *) "done",
			       NULL);
	if (childp) {
	    xmlAddChild(statusp, childp);
	}
    } else if (rc == DB_ERROR) {
	childp = xmlNewDocNode(container, NULL, (const xmlChar *) "error",
			       (const xmlChar *) pb.pb_buf);
    }

    xmlAddChild(resultp, statusp);

    results = xmlXPathNodeSetCreate(NULL);
    xmlXPathNodeSetAdd(results, resultp);
    ret = xmlXPathNewNodeSetList(results);

    valuePush(ctxt, ret);
    xmlXPathFreeNodeSet(results);

 fail:
    for (osi = nargs - 1; osi >= 0; osi--) {
	if (ostack[osi]) {
	    xmlXPathFreeObject(ostack[osi]);
	}
    }
    
    if (in) {
	db_input_free(in);
    }

    if (pb.pb_buf) {
	xmlFree(pb.pb_buf);
    }
}

/*
 * Opens database using provided options and returns session cookie
 */
static void
extDbOpen (xmlXPathParserContext *ctxt, int nargs)
{
    xmlXPathObject *ostack[nargs];
    int osi;
    db_driver_t *driver;
    db_handle_t *handle;
    db_ret_t rc;
    db_input_t *in;

    if (nargs < 1) {
	LX_ERR("db:open: too few arguments\n");
	return;
    }

    for (osi = nargs - 1; osi >= 0; osi--)
	ostack[osi] = valuePop(ctxt);

    in = db_input_parse(ostack, nargs);

    if (in == NULL) {
	LX_ERR("db:open: failed to parse input\n");
	goto fail;
    }

    if (in->di_engine == NULL) {
	LX_ERR("db:open: backend database engine not specified\n");
	goto fail;
    }

    if (in->di_database == NULL) {
	LX_ERR("db:open: database name not specified\n");
	goto fail;
    }

    /* Get the driver for this database engine */
    driver = db_get_driver(in->di_engine);

    if (driver == NULL) {
	LX_ERR("db:open: failed to find driver for database engine %s\n",
	       in->di_engine);
	goto fail;
    }

    /* Create a session and call open on the database driver with this id */
    handle = db_handle_alloc(driver);

    if (handle == NULL) {
	LX_ERR("db:open: failed to create database handle\n");
	goto fail;
    }

    rc = (*driver->dd_open)(handle, in, ostack, nargs);

    switch (rc) {
	case DB_FAIL:
	    LX_ERR("db:open: failed to open database\n");
	    goto fail;
	default:
	    xmlXPathReturnString(ctxt, 
				 xmlStrdup((const xmlChar *) handle->dh_name));
    }

fail:
    for (osi = nargs - 1; osi >= 0; osi--)
	if (ostack[osi])
	    xmlXPathFreeObject(ostack[osi]);
    
    if (in)
	db_input_free(in);
}

/*
 * Runs raw query on the database and returns cursor that can be used with
 * fetch later
 */
static void
extDbQuery (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)
{
    xmlXPathObject *ostack[nargs];
    db_handle_t *dbhp;
    db_input_t *in = NULL;
    int osi;
    char *name;
    db_ret_t rc = DB_FAIL;
    slax_printf_buffer_t pb;
    char buf[BUFSIZ];

    if (nargs < 2) {
	LX_ERR("db:query: too few arguments. Must provide handle and query\n");
	return;
    }

    bzero(&pb, sizeof(pb));

    for (osi = nargs - 1; osi >= 0; osi--)
	ostack[osi] = valuePop(ctxt);

    name = (char *) xmlXPathCastToString(ostack[0]);
    if (name == NULL) {
	LX_ERR("db:query: missing handle\n");
	goto fail;
    }

    in = db_input_parse(ostack + 1, nargs - 1);
    if (in == NULL) {
	LX_ERR("db:query: unable to parse input\n");
	goto fail;
    }

    /*
     * Get database handle and invoke query callback on driver
     */
    dbhp = db_get_handle_by_name(name);
    if (dbhp && dbhp->dh_driver) {
	rc = (*dbhp->dh_driver->dd_query)(dbhp, in, ctxt, nargs, &pb);
	if (rc == DB_DATA) {
	    snprintf(buf, sizeof(buf), "%s%s", dbhp->dh_name, pb.pb_buf);
	    xmlXPathReturnString(ctxt, xmlStrdup((const xmlChar *) buf));
	} else if (rc == DB_ERROR) {
	    LX_ERR("db:query: failed with error - %s\n", pb.pb_buf);
	    goto fail;
	} else {
	    LX_ERR("db:query: failed to find\n");
	    goto fail;
	}
    } else {
	LX_ERR("db:query: invalid handle\n");
	goto fail;
    }

 fail:
    if (rc != DB_DATA) {
	xmlXPathReturnString(ctxt, xmlStrdup((const xmlChar *) ""));
    }

    for (osi = nargs - 1; osi >= 0; osi--) {
	if (ostack[osi]) {
	    xmlXPathFreeObject(ostack[osi]);
	}
    }
    
    if (in) {
	db_input_free(in);
    }

    if (pb.pb_buf) {
	xmlFree(pb.pb_buf);
    }
}

slax_function_table_t slaxDbTable[] = {
    {
	"close", extDbClose,
	"Close a database handle",
	"(handle)", XPATH_UNDEFINED,
    },
    {
	"create", extDbCreate,
	"Creates a table with given schema",
	"(handle, schema)", XPATH_NODESET,
    },
    {
	"delete", extDbDelete,
	"Deletes selected instances",
	"(handle, input)", XPATH_NODESET,
    },
    {
	"fetch", extDbFetch,
	"Fetches data from given cursor",
	"(handle, cursor)", XPATH_XSLT_TREE,
    },
    {
	"find", extDbFind,
	"Finds data that matches given input",
	"(handle, input)", XPATH_NODESET,
    },
    {
	"find-and-fetch", extDbFindAndFetch,
	"Finds and returns the data that matches given input",
	"(handle, input)", XPATH_NODESET,
    },
    {
	"insert", extDbInsert,
	"Inserts given data into the database",
	"(handle, data)", XPATH_NODESET,
    },
    {
	"open", extDbOpen,
	"Opens database and returns the handle. Creates if possible",
	"(handle, options)", XPATH_NODESET,
    },
    {
	"query", extDbQuery,
	"Queries the database",
	"(handle, query)", XPATH_NODESET,
    },
    {
	"update", extDbUpdate,
	"Updates selected instances with given data",
	"(handle, input)", XPATH_NODESET,
    },
    { NULL, NULL, NULL, NULL, XPATH_UNDEFINED }
};

void
extDbInit (void)
{
    TAILQ_INIT(&extDbSessions);
    TAILQ_INIT(&extDbDrivers);

    slaxRegisterFunctionTable(DB_FULL_NS, slaxDbTable);
}

SLAX_DYN_FUNC(slaxDynLibInit)
{
    TAILQ_INIT(&extDbSessions);
    TAILQ_INIT(&extDbDrivers);

    arg->da_functions = slaxDbTable; /* Fill in our function table */

    return SLAX_DYN_VERSION;
}
