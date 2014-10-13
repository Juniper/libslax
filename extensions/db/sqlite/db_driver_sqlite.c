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
#include <libslax/slaxext.h>

#include <extensions/db/ext_db.h>

#include "sqlite3.h"

/*
 * Sqlite driver specific handler
 */
typedef struct db_sqlite_handle_s {
    TAILQ_ENTRY(db_sqlite_handle_s) dsh_link;	/* Link to next session */
    db_handle_t *dsh_db_handle;			/* External database handle */
    sqlite3 *dsh_sqlite_handle;			/* Sqlite3 database handle */
} db_sqlite_handle_t;

/*
 * Structure to hold prepared statement
 */
typedef struct db_sqlite_stmt_s {
    TAILQ_ENTRY(db_sqlite_stmt_s) dss_link; /* Link to next statement */
    char dss_name[DB_NAME_SIZE];	    /* Unique ID for this statement */
    sqlite3_stmt *dss_stmt;		    /* Prepared sqlite3 statement */
    db_sqlite_handle_t *dss_handle;	    /* Sqlite3 handler */
    db_input_t *dss_in;			    /* Input structure used to 
					       prepare this statement */
} db_sqlite_stmt_t;

TAILQ_HEAD(db_sqlite_session_s, db_sqlite_handle_s) db_sqlite_sessions;
TAILQ_HEAD(db_sqlite_stmts_s, db_sqlite_stmt_s) db_sqlite_stmts;

/*
 * Unique identifier for open sqlite sessions
 */
static unsigned int seed = 1234;

/*
 * Allocates and initializes a session
 */
static db_sqlite_handle_t *
db_sqlite_handle_alloc (db_handle_t *db_handle)
{
    db_sqlite_handle_t *dbsp;

    dbsp = xmlMalloc(sizeof(*dbsp));
    if (dbsp) {
	bzero(dbsp, sizeof(*dbsp));

	dbsp->dsh_db_handle = db_handle;

	TAILQ_INSERT_TAIL(&db_sqlite_sessions, dbsp, dsh_link);
    }

    return dbsp;
}

/*
 * Frees handle
 */
static void
db_sqlite_handle_free (db_sqlite_handle_t *dbsp)
{
    TAILQ_REMOVE(&db_sqlite_sessions, dbsp, dsh_link);
    xmlFree(dbsp);
}

/*
 * Given name of the session, returns the handle
 */
static db_sqlite_handle_t *
db_sqlite_handle_find (const char *name)
{
    db_sqlite_handle_t *dbsp;

    TAILQ_FOREACH(dbsp, &db_sqlite_sessions, dsh_link) {
	if (streq(dbsp->dsh_db_handle->dh_name, name))
	    return dbsp;
    }

    return NULL;
}

/*
 * Given identifier of prepared statement, returns it
 */
static db_sqlite_stmt_t *
db_sqlite_stmt_find (const char *name)
{
    db_sqlite_stmt_t *dbssp;

    TAILQ_FOREACH(dbssp, &db_sqlite_stmts, dss_link) {
	if (streq(dbssp->dss_name, name))
	    return dbssp;
    }

    return NULL;
}

/*
 * Given sqlite driver handle, finalizes all the prepared statements
 * associated with it
 */
static void
db_sqlite_stmt_free_by_handle (db_sqlite_handle_t *dbsp)
{
    db_sqlite_stmt_t *dbssp;
    
    TAILQ_FOREACH(dbssp, &db_sqlite_stmts, dss_link) {
	if (dbssp->dss_handle == dbsp) {
	    sqlite3_finalize(dbssp->dss_stmt);
	    TAILQ_REMOVE(&db_sqlite_stmts, dbssp, dss_link);
	    
	    if (dbssp->dss_in) {
		db_input_free(dbssp->dss_in);
	    }

	    xmlFree(dbssp);
	    return;
	}
    }
}

/*
 * Builds and appends conditions
 */
static void
db_sqlite_build_conditions (xmlNodePtr conditions, slax_printf_buffer_t *pb,
			   const char *op)
{
    xmlNodePtr cur, childp;
    const char *selector, *operator, *value, *key;
    int count = 0;

    cur = conditions;
    /*
     * Count number of valid nodes
     */
    while (cur) {
	if (cur->type == XML_ELEMENT_NODE) {
	    count++;
	}
	cur = cur->next;
    }

    /*
     * Process conditions
     */
    cur = conditions;
    while (cur) {
	if (cur->type == XML_ELEMENT_NODE) {
	    /*
	     * If we are 'and' or 'or', we have nested condition
	     */
	    if (streq(xmlNodeName(cur), "or") 
		|| streq(xmlNodeName(cur), "and")) {
		db_sqlite_build_conditions(cur->children, pb, xmlNodeName(cur));
	    } else if (streq(xmlNodeName(cur), "condition")) {
		childp = cur->children;
		selector = NULL;
		value = NULL;
		operator = NULL;

		while (childp) {
		    if (childp->type == XML_ELEMENT_NODE) {
			key = xmlNodeName(childp);
			if (streq(key, "selector")) {
			    selector = xmlNodeValue(childp);
			} else if (streq(key, "operator")) {
			    operator = xmlNodeValue(childp);
			} else if (streq(key, "value")) {
			    value = xmlNodeValue(childp);
			}
		    }
		    childp = childp->next;
		}

		if (selector == NULL || operator == NULL || value == NULL)
		    continue;

		slaxExtPrintAppend(pb, (const xmlChar *) " ", 1);
		slaxExtPrintAppend(pb, (const xmlChar *) selector, 
				   strlen(selector));
		slaxExtPrintAppend(pb, (const xmlChar *) " ", 1);
		slaxExtPrintAppend(pb, (const xmlChar *) operator, 
				   strlen(operator));
		slaxExtPrintAppend(pb, (const xmlChar *) " ", 1);

		/*
		 * Operator is mostly like, match, regexp and has to be
		 * enclosed in quotes
		 */
		if (strlen(operator) > 2) {
		    slaxExtPrintAppend(pb, (const xmlChar *) "\"", 1);
		}

		slaxExtPrintAppend(pb, (const xmlChar *) value, 
				   strlen(value));

		if (strlen(operator) > 2) {
		    slaxExtPrintAppend(pb, (const xmlChar *) "\"", 1);
		}

		/*
		 * If we don't have an operator, we can process only one
		 * condition
		 */
		if (op == NULL) {
		    return;
		}
	    }

	    if (op && count > 1) {
		slaxExtPrintAppend(pb, (const xmlChar *) " ", 1);
		slaxExtPrintAppend(pb, (const xmlChar *) op, strlen(op));
	    }
	    count--;
	}
	cur = cur->next;
    }
}

/*
 * Given conditions node and print buffer, appends where clause
 */
static void
db_sqlite_build_where (xmlNodePtr conditions, slax_printf_buffer_t *pb)
{
    if (conditions && conditions->type == XML_ELEMENT_NODE 
	&& conditions->children) {
	slaxExtPrintAppend(pb, (const xmlChar *) " WHERE", 6);

	/* Build and append conditions */
	db_sqlite_build_conditions(conditions->children, pb, NULL);
    }
}

/*
 * Given sorting information, appends order by clause to query
 */
static void
db_sqlite_build_sort (xmlNodePtr sort, slax_printf_buffer_t *pb)
{
    xmlNodePtr cur;
    int order = 1, more = 0;

    if (sort && sort->type == XML_ELEMENT_NODE) {
	slaxExtPrintAppend(pb, (const xmlChar *) " ORDER BY", 9);

	cur = sort->children;
	while (cur) {
	    if (cur->type == XML_ELEMENT_NODE) {
		if (streq(xmlNodeName(cur), "by") && xmlNodeValue(cur)) {
		    if (more == 0) {
			slaxExtPrintAppend(pb, (const xmlChar *) " ", 1);
			more = 1;
		    } else {
			slaxExtPrintAppend(pb, (const xmlChar *) ", ", 2);
		    }
		    slaxExtPrintAppend(pb, 
				       (const xmlChar *) xmlNodeValue(cur), 
				       strlen(xmlNodeValue(cur)));
		} else if (streq(xmlNodeName(cur), "order") 
			   && xmlNodeValue(cur)) {
		    /*
		     * Default order is asc
		     */
		    if (streq(xmlNodeValue(cur), "desc")) {
			order = 0;
		    }
		}
	    }
	    cur = cur->next;
	}
	
	if (order) {
	    slaxExtPrintAppend(pb, (const xmlChar *) " ASC", 4);
	} else {
	    slaxExtPrintAppend(pb, (const xmlChar *) " DESC", 5);
	}
    }
}

/*
 * Deletes rows filtered with given conditions
 */
static slax_printf_buffer_t
db_sqlite_build_delete (db_input_t *in)
{
    char buf[BUFSIZ];
    slax_printf_buffer_t pb;
    
    bzero(&pb, sizeof(pb));

    if (in && in->di_collection && &in->di_collection) {
	slaxExtPrintAppend(&pb, (const xmlChar *) "DELETE FROM ", 12);
	slaxExtPrintAppend(&pb, (const xmlChar *) in->di_collection,
			   strlen(in->di_collection));

	/*
	 * Add conditions to the update statement
	 */
	if (in->di_conditions) {
	    db_sqlite_build_where(in->di_conditions, &pb);
	}

	/*
	 * Take care of sorting if any
	 */
	if (in->di_sort) {
	    db_sqlite_build_sort(in->di_sort, &pb);
	}

	/*
	 * Handle limit
	 */
	if (in->di_limit) {
	    snprintf(buf, sizeof(buf), " LIMIT %u", in->di_limit);
	    slaxExtPrintAppend(&pb, (const xmlChar *) buf, strlen(buf));
	}

	/*
	 * Handle offset
	 */
	if (in->di_skip) {
	    snprintf(buf, sizeof(buf), " OFFSET %u", in->di_skip);
	    slaxExtPrintAppend(&pb, (const xmlChar *) buf, strlen(buf));
	}
    }

    return pb;
}

/*
 * Updates matching rows with given data
 */
static slax_printf_buffer_t
db_sqlite_build_update (db_input_t *in)
{
    char buf[BUFSIZ];
    slax_printf_buffer_t pb;
    xmlNodePtr cur;
    const char *key, *value;
    int count = 0;
    
    bzero(&pb, sizeof(pb));

    if (in && in->di_collection && &in->di_collection && in->di_update 
	&& in->di_update->type == XML_ELEMENT_NODE) {
	slaxExtPrintAppend(&pb, (const xmlChar *) "UPDATE ", 7);
	slaxExtPrintAppend(&pb, (const xmlChar *) in->di_collection,
			   strlen(in->di_collection));
	slaxExtPrintAppend(&pb, (const xmlChar *) " SET ", 5);

	cur = in->di_update->children;
	while (cur) {
	    if (cur->type == XML_ELEMENT_NODE) {
		count ++;
	    }
	    cur = cur->next;
	}

	cur = in->di_update->children;
	while (cur) {
	    if (cur->type == XML_ELEMENT_NODE) {
		key = xmlNodeName(cur);
		value = xmlNodeValue(cur);

		if (key && value) {
		    slaxExtPrintAppend(&pb, (const xmlChar *) key, strlen(key));
		    slaxExtPrintAppend(&pb, (const xmlChar *) " = \"", 4);
		    slaxExtPrintAppend(&pb, (const xmlChar *) value, 
				       strlen(value));
		    slaxExtPrintAppend(&pb, (const xmlChar *) "\"", 1);
		}
		count--;
	    }

	    if (count > 0) {
		slaxExtPrintAppend(&pb, (const xmlChar *) ", ", 2);
	    }

	    cur = cur->next;
	}

	/*
	 * Add conditions to the update statement
	 */
	if (in->di_conditions) {
	    db_sqlite_build_where(in->di_conditions, &pb);
	}

	/*
	 * Take care of sorting if any
	 */
	if (in->di_sort) {
	    db_sqlite_build_sort(in->di_sort, &pb);
	}

	/*
	 * Handle limit
	 */
	if (in->di_limit) {
	    snprintf(buf, sizeof(buf), " LIMIT %u", in->di_limit);
	    slaxExtPrintAppend(&pb, (const xmlChar *) buf, strlen(buf));
	}

	/*
	 * Handle offset
	 */
	if (in->di_skip) {
	    snprintf(buf, sizeof(buf), " OFFSET %u", in->di_skip);
	    slaxExtPrintAppend(&pb, (const xmlChar *) buf, strlen(buf));
	}
    }

    return pb;
}

/*
 * Builds and returns sqlite3 insert statement
 */
static slax_printf_buffer_t
db_sqlite_build_insert (db_input_t *in)
{
    xmlNodePtr cur, childp;
    slax_printf_buffer_t pb;
    int count = 0, i;
    const char *value, *key;

    bzero(&pb, sizeof(pb));

    if (in && in->di_collection && &in->di_collection) {
	slaxExtPrintAppend(&pb, (const xmlChar *) "BEGIN TRANSACTION;", 18);

	if (in->di_instance || in->di_instances) {
	    if (in->di_instance && in->di_instance->type == XML_ELEMENT_NODE) {
		cur = in->di_instance;
	    } else {
		cur = in->di_instances->children;
	    }

	    while (cur) {
		/*
		 * Extract instance values and build insert statements
		 */
		if (cur->type == XML_ELEMENT_NODE) {
		    slaxExtPrintAppend(&pb, (const xmlChar *) " INSERT INTO ", 
				       13);
		    slaxExtPrintAppend(&pb, (const xmlChar *) in->di_collection,
				       strlen(in->di_collection));
		    slaxExtPrintAppend(&pb, (const xmlChar *) " (", 2);
		    childp = cur->children;
		    count = 0;
		    while (childp) {
			if (childp->type == XML_ELEMENT_NODE) count++;
			childp = childp->next;
		    }

		    /*
		     * Get all column names
		     */
		    childp = cur->children;
		    i = count;
		    while (childp) {
			if (childp->type == XML_ELEMENT_NODE) {
			    key = xmlNodeName(childp);
			    i--;

			    slaxExtPrintAppend(&pb, (const xmlChar *) key,
					       strlen(key));

			    if (i > 0) {
				slaxExtPrintAppend(&pb, 
						   (const xmlChar *) ", ", 2);
			    } else {
				slaxExtPrintAppend(&pb, 
					(const xmlChar *) ") VALUES (", 10);
			    }
			}
			childp = childp->next;
		    }

		    /*
		     * Get values assigned to each of these columns
		     */
		    childp = cur->children;
		    i = count;
		    while (childp) {
			if (childp->type == XML_ELEMENT_NODE) {
			    value = xmlNodeValue(childp);
			    i--;
			    if (value) {
				slaxExtPrintAppend(&pb, 
						   (const xmlChar *) "\"", 1);
				slaxExtPrintAppend(&pb, (const xmlChar *) value,
						   strlen(value));
				slaxExtPrintAppend(&pb, 
						   (const xmlChar *) "\"", 1);
			    }

			    if (i > 0) {
				slaxExtPrintAppend(&pb, 
						   (const xmlChar *) ", ", 2);
			    } else {
				slaxExtPrintAppend(&pb, 
						   (const xmlChar *) ");", 2);
			    }
			}
			childp = childp->next;
		    }
		}
		cur = cur->next;
	    }
	}
	slaxExtPrintAppend(&pb, (const xmlChar *) " COMMIT TRANSACTION;", 20);
    }

    return pb;
}

/*
 * Builds and returns sqlite3 create statement
 */
static slax_printf_buffer_t
db_sqlite_build_create (db_input_t *in)
{
    char buf[BUFSIZ];
    xmlNodePtr cur, childp;
    slax_printf_buffer_t pb;
    const char *name, *type, *def, *key;
    int primary, increment, unique, notnull;

    bzero(&pb, sizeof(pb));

    if (in && in->di_collection && &in->di_collection) {
	slaxExtPrintAppend(&pb, (const xmlChar *) "CREATE TABLE IF "
						  "NOT EXISTS ", 27);
	slaxExtPrintAppend(&pb, (const xmlChar *) in->di_collection,
			   strlen(in->di_collection));

	if (in->di_fields->type == XML_ELEMENT_NODE) {
	    slaxExtPrintAppend(&pb, (const xmlChar *) " (", 2);
	    cur = in->di_fields->children;
	    int count = 0;
	    while (cur) {
		if (cur->type == XML_ELEMENT_NODE)
		    count++;
		cur = cur->next;
	    }
	    cur = in->di_fields->children;

	    while (cur) {
		/*
		 * Extract field details and insert into create statement
		 */
		if (cur->type == XML_ELEMENT_NODE) {
		    childp = cur->children;
		    name = NULL;
		    type = NULL;
		    def = NULL;
		    primary = 0;
		    increment = 0;
		    unique = 0;
		    notnull = 0;

		    while (childp) {
			if (childp->type == XML_ELEMENT_NODE) {
			    key = xmlNodeName(childp);

			    if (streq(key, "name")) {
				name = xmlNodeValue(childp);
			    } else if (streq(key, "type")) {
				type = xmlNodeValue(childp);
			    } else if (streq(key, "primary")) {
				primary = 1;
			    } else if (streq(key, "auto-increment")) {
				increment = 1;
			    } else if (streq(key, "unique")) {
				unique = 1;
			    } else if (streq(key, "default")) {
				def = xmlNodeValue(childp);
			    } else if (streq(key, "notnull")) {
				notnull = 1;
			    }
			}
			childp = childp->next;
		    }

		    if (name) {
			slaxExtPrintAppend(&pb, (const xmlChar *) name, 
					   strlen(name));
			slaxExtPrintAppend(&pb, (const xmlChar *) " ", 1);
			if (streq(type, "text")) {
			    slaxExtPrintAppend(&pb, (const xmlChar *) "TEXT", 4);
    			} else if (streq(type, "integer")) {
			    slaxExtPrintAppend(&pb, 
					       (const xmlChar *) "INTEGER", 7);
			} else if (streq(type, "real")) {
			    slaxExtPrintAppend(&pb, (const xmlChar *) "REAL", 4);
			} else if (type) {
			    slaxExtPrintAppend(&pb, (const xmlChar *) type, 
					       strlen(type));
			}

			if (primary) {
			    slaxExtPrintAppend(&pb, 
					       (const xmlChar *) " PRIMARY "
								 "KEY", 12);
			}

			if (unique) {
			    slaxExtPrintAppend(&pb, 
					       (const xmlChar *) " UNIQUE", 7);
			}

			if (notnull) {
			    slaxExtPrintAppend(&pb, 
					    (const xmlChar *) " NOT NULL", 8);
			}

			if (increment) {
			    slaxExtPrintAppend(&pb, 
					    (const xmlChar *)" AUTOINCREMENT", 14);
			}

			if (def) {
			    if (streq(type, "integer") 
				|| streq(type, "real")) {
				snprintf(buf, sizeof(buf), " DEFAULT %s", def);
			    } else {
				snprintf(buf, sizeof(buf), " DEFAULT \"%s\"",
					 def);
			    }
			    slaxExtPrintAppend(&pb, (const xmlChar *) buf,
					       strlen(buf));
			}
		    }
		    count--;
		}
		cur = cur->next;

		if (count > 0) {
		    slaxExtPrintAppend(&pb, (const xmlChar *) ", ", 2);
		} else {
		    slaxExtPrintAppend(&pb, (const xmlChar *) ") ", 2);
		}
	    }
	}
    }

    return pb;
}

/*
 * Given input structure, forms and returns select statement
 */
static slax_printf_buffer_t
db_sqlite_build_select (db_input_t *in)
{
    char buf[BUFSIZ];
    xmlNodePtr cur;
    slax_printf_buffer_t pb;

    bzero(&pb, sizeof(pb));

    if (in && in->di_collection && &in->di_collection) {
	/*
	 * Take care of projections
	 */
	if (in->di_retrieve && in->di_retrieve->type == XML_ELEMENT_NODE) {
	    cur = in->di_retrieve->children;
	    int count = 0;
	    slaxExtPrintAppend(&pb, (const xmlChar *) "SELECT ", 7);
	    while (cur) {
		if (cur->type == XML_ELEMENT_NODE)
		    count++;
		cur = cur->next;
	    }
	    cur = in->di_retrieve->children;
	    while (cur) {
		if (cur->type == XML_ELEMENT_NODE) {
		    slaxExtPrintAppend(&pb, (const xmlChar *) cur->name,
				       xmlStrlen(cur->name));
		    count--;
		}
		cur = cur->next;

		if (count > 0) {
		    slaxExtPrintAppend(&pb, (const xmlChar *) ", ", 2);
		} else {
		    slaxExtPrintAppend(&pb, (const xmlChar *) " ", 1);
		}
	    }
	    slaxExtPrintAppend(&pb, (const xmlChar *) "FROM ", 5);
	} else {
	    slaxExtPrintAppend(&pb, (const xmlChar *) "SELECT * FROM ", 14);
	}
	slaxExtPrintAppend(&pb, (const xmlChar *) in->di_collection,
			   strlen(in->di_collection));

	/*
	 * Add conditions if any
	 */
	if (in->di_conditions) {
	    db_sqlite_build_where(in->di_conditions, &pb);
	}

	/*
	 * Take care of sorting if any
	 */
	if (in->di_sort) {
	    db_sqlite_build_sort(in->di_sort, &pb);
	}

	/*
	 * Limit the number of results
	 */
	if (in->di_limit) {
	    snprintf(buf, sizeof(buf), " LIMIT %u", in->di_limit);
	    slaxExtPrintAppend(&pb, (const xmlChar *) buf, strlen(buf));
	}

	/*
	 * Skip over result set
	 */
	if (in->di_skip) {
	    snprintf(buf, sizeof(buf), " OFFSET %u", in->di_skip);
	    slaxExtPrintAppend(&pb, (const xmlChar *) buf, strlen(buf));
	}
    }

    return pb;
}

/*
 * Internal function to create/insert/update/delete from table
 */
static db_ret_t 
db_sqlite_operate (db_handle_t *db_handle, db_input_t *in,
		   xmlXPathParserContext *ctxt UNUSED, 
		   int nargs UNUSED, slax_printf_buffer_t *out,
		   const char *operation)
{
    db_sqlite_handle_t *dbsp;
    slax_printf_buffer_t pb;
    int rc;
    const char *error = NULL;

    bzero(&pb, sizeof(pb));

    if (db_handle && db_handle->dh_name) {
    	dbsp = db_sqlite_handle_find(db_handle->dh_name);

	if (dbsp) {
	    if (in && in->di_collection && &in->di_collection) {
		if (streq(operation, "create")) {
		    pb = db_sqlite_build_create(in);
		} else if (streq(operation, "insert")) {
		    pb = db_sqlite_build_insert(in);
		} else if (streq(operation, "delete")) {
		    pb = db_sqlite_build_delete(in);
		} else if (streq(operation, "update")) {
		    pb = db_sqlite_build_update(in);
		}

		if (pb.pb_buf) {
		    slaxLog("db:sqlite: preparing - %s", pb.pb_buf);
		    rc = sqlite3_exec(dbsp->dsh_sqlite_handle, 
				      pb.pb_buf, 0, 0, 0);

		    xmlFree(pb.pb_buf);

		    if (rc == SQLITE_OK) {
			return DB_OK;
		    } else {
			error = sqlite3_errstr(rc);
			if (error) {
			    slaxExtPrintAppend(out, (const xmlChar *) error, 
					       strlen(error));
			}
			return DB_ERROR;
		    }
		}
	    }
	    slaxExtPrintAppend(out, (const xmlChar *) "invalid input", 13);
	} else {
	    slaxExtPrintAppend(out, (const xmlChar *) "invalid handle", 14);
	}
    }

    if (pb.pb_buf) {
	xmlFree(pb.pb_buf);
    }

    return DB_FAIL;
}

DB_DRIVER_OPEN (db_sqlite_open)
{
    db_sqlite_handle_t *dbsp;
    int rc;

    if (db_handle) {
	dbsp = db_sqlite_handle_alloc(db_handle);

	if (dbsp) {
	    rc = sqlite3_open(in->di_database, &dbsp->dsh_sqlite_handle);
	    if (rc == SQLITE_OK) {
		return DB_OK;
	    } else {
		slaxLog("sqlite:open: db handle creation failed - %s",
			sqlite3_errstr(rc));
		db_sqlite_handle_free(dbsp);
	    }
	}
    }
    return DB_FAIL;
}

DB_DRIVER_CREATE (db_sqlite_create)
{
    return db_sqlite_operate(db_handle, in, ctxt, nargs, out, "create");
}

DB_DRIVER_INSERT (db_sqlite_insert)
{
    return db_sqlite_operate(db_handle, in, ctxt, nargs, out, "insert");
}

DB_DRIVER_DELETE (db_sqlite_delete)
{
    return db_sqlite_operate(db_handle, in, ctxt, nargs, out, "delete");
}

DB_DRIVER_UPDATE (db_sqlite_update)
{
    return db_sqlite_operate(db_handle, in, ctxt, nargs, out, "update");
}

/*
 * Steps through given cursor, appends rows if any to buffer and returns
 * result code
 */
static int
db_sqlite_step (sqlite3_stmt *stmt, slax_printf_buffer_t *out, 
		xmlXPathObjectPtr input)
{
    int rc, cols, i, idx;
    const char *colName;
    unsigned const char *colVal;
    char buf[BUFSIZ];
    xmlNodeSetPtr nodeset;
    xmlNodePtr nop;
    const char *key, *value;

    /*
     * If we have input, we bind it to the prepared statement and call
     * step
     */
    if (input) {
	if (input->nodesetval) {
	    /*
	     * Reset prepared statement and clear previous bindings
	     */
	    if (sqlite3_reset(stmt) == SQLITE_OK 
		&& sqlite3_clear_bindings(stmt) == SQLITE_OK) {

		nodeset = input->nodesetval;
		if (nodeset->nodeNr > 0 && nodeset->nodeTab[0]->children) {
		    nop = nodeset->nodeTab[0]->children;

		    while (nop) {
			/*
			 * Find index of this variable and bind the value
			 */
			if (nop->type == XML_ELEMENT_NODE) {
			    key = xmlNodeName(nop);
			    value = xmlNodeValue(nop);
			    if (key && value) {
				/*
				 * Our named parameters could have been
				 * provided with a prefix : or $ or @
				 */
				snprintf(buf, sizeof(buf), ":%s", key);
				idx = sqlite3_bind_parameter_index(stmt, buf);
				if (idx == 0) {
				    snprintf(buf, sizeof(buf), "$%s", key);
				    idx = sqlite3_bind_parameter_index(stmt, 
								       buf);
				    if (idx == 0) {
					snprintf(buf, sizeof(buf), "@%s",
						 key);
					idx =
					    sqlite3_bind_parameter_index(stmt,
									 buf);
				    }
				}
				if (idx > 0) {
				    rc = sqlite3_bind_text(stmt, idx, value,
							   strlen(value),
							   SQLITE_TRANSIENT);
				    if (rc != SQLITE_OK) {
					return rc;
				    }
				} else {
				    return SQLITE_ERROR;
				}
			    }
			}
			nop = nop->next;
		    }
		}
	    }
	}
    }

    /*
     * Execute the statement
     */
    rc = sqlite3_step(stmt);    
    if (rc == SQLITE_ROW) {
	/*
	 * Returns rows if statement execution emitted them
	 */
	cols = sqlite3_column_count(stmt);
	slaxExtPrintAppend(out, (const xmlChar *) "<instance>", 10);
	for (i = 0; i < cols; i++) {
	    colName = sqlite3_column_name(stmt, i);

	    if (colName) {
		colVal = sqlite3_column_text(stmt, i);
		snprintf(buf, sizeof(buf), "<%s>", colName);

		slaxExtPrintAppend(out, (const xmlChar *) buf, strlen(buf));
		slaxExtPrintAppend(out, (const xmlChar *) colVal,
				   sqlite3_column_bytes(stmt, i));

		snprintf(buf, sizeof(buf), "</%s>", colName);
		slaxExtPrintAppend(out, (const xmlChar *) buf, strlen(buf));
	    }
	}
	slaxExtPrintAppend(out, (const xmlChar *) "</instance>", 11);
    }
    return rc;
}

/*
 * Reads input, prepares statement and writes cursor to output
 */
DB_DRIVER_FIND (db_sqlite_find)
{
    db_sqlite_handle_t *dbsp;
    slax_printf_buffer_t pb;

    bzero(&pb, sizeof(pb));

    if (db_handle && db_handle->dh_name) {
    	dbsp = db_sqlite_handle_find(db_handle->dh_name);

	if (dbsp) {
	    if (in && in->di_collection && &in->di_collection) {
		/*
		 * Build, prepare sqlite statement and return sqlite cursor
		 * identifier
		 */
		pb = db_sqlite_build_select(in);
		if (pb.pb_buf) {
		    db_sqlite_stmt_t *stmtp = xmlMalloc(sizeof(*stmtp));
		    int rc;

		    slaxLog("db:sqlite: preparing - %s", pb.pb_buf);

		    if (stmtp) {
			bzero(stmtp, sizeof(*stmtp));

			rc = sqlite3_prepare_v2(dbsp->dsh_sqlite_handle,
						pb.pb_buf, -1, 
						&stmtp->dss_stmt, NULL);

			xmlFree(pb.pb_buf);

			if (rc == SQLITE_OK) {
			    /*
			     * Save a copy of input data for future use
			     */
			    db_input_t *inc;
			    inc = xmlMalloc(sizeof(*inc));
			    db_input_copy(inc, in);

			    stmtp->dss_handle = dbsp;
			    stmtp->dss_in = inc;
			    snprintf(stmtp->dss_name, sizeof(stmtp->dss_name), 
				     "%u", seed++);

			    TAILQ_INSERT_TAIL(&db_sqlite_stmts, stmtp, dss_link);
			    slaxExtPrintAppend(out, (const xmlChar *)stmtp->dss_name,
					       strlen(stmtp->dss_name));
			    return DB_DATA;
			} else {
			    const char *errstr = sqlite3_errstr(rc);
			    slaxExtPrintAppend(out, (const xmlChar *) errstr, 
					       strlen(errstr));
			    return DB_ERROR;
			}
		    }
		}
	    } else {
	    }
	} else {
	}
    }
    
    if (pb.pb_buf) {
	xmlFree(pb.pb_buf);
    }

    return DB_FAIL;
}

/*
 * Given id for statement, calls step and returns results to the user
 */
DB_DRIVER_FETCH (db_sqlite_fetch)
{
    db_sqlite_stmt_t *stmtp;
    int rc;
    
    if (name && *name) {
	stmtp = db_sqlite_stmt_find(name);
	if (stmtp) {
	    rc = db_sqlite_step(stmtp->dss_stmt, out, in);
	    if (rc == SQLITE_ROW) {
		return DB_DATA;
	    } else if (rc == SQLITE_OK) {
		return DB_OK;
	    } else if (rc == SQLITE_DONE) {
		return DB_DONE;
	    } else {
		slaxLog("db:sqlite:fetch: Unexpected return status - %s",
			sqlite3_errstr(rc));
		slaxExtPrintAppend(out, (const xmlChar *) sqlite3_errstr(rc),
				   strlen(sqlite3_errstr(rc)));
		return DB_ERROR;
	    }
	} else {
	    slaxLog("db:sqlite:fetch: invalid statement id");
	}
    }

    return DB_FAIL;
}

DB_DRIVER_FIND_FETCH (db_sqlite_find_fetch)
{
    db_sqlite_handle_t *dbsp;
    slax_printf_buffer_t pb;
    char buf[BUFSIZ];

    bzero(&pb, sizeof(pb));

    if (db_handle && db_handle->dh_name) {
    	dbsp = db_sqlite_handle_find(db_handle->dh_name);

	if (dbsp) {
	    if (in && in->di_collection && &in->di_collection) {
		/*
		 * Build, prepare sqlite statement and return sqlite cursor
		 * identifier
		 */
		pb = db_sqlite_build_select(in);
		if (pb.pb_buf) {
		    db_sqlite_stmt_t *stmtp = xmlMalloc(sizeof(*stmtp));
		    int rc;

		    slaxLog("db:sqlite: preparing - %s", pb.pb_buf);

		    if (stmtp) {
			bzero(stmtp, sizeof(*stmtp));

			rc = sqlite3_prepare_v2(dbsp->dsh_sqlite_handle,
						pb.pb_buf, -1, 
						&stmtp->dss_stmt, NULL);

			xmlFree(pb.pb_buf);

			if (rc == SQLITE_OK) {
			    db_input_t *inc;
			    inc = xmlMalloc(sizeof(*inc));
			    db_input_copy(inc, in);

			    stmtp->dss_handle = dbsp;
			    stmtp->dss_in = inc;
			    snprintf(stmtp->dss_name, sizeof(stmtp->dss_name), 
				     "%u", seed++);

			    TAILQ_INSERT_TAIL(&db_sqlite_stmts, stmtp, dss_link);
			    /*
			     * Emit cursor if it needs used in further
			     * fetches. Also wraps the content in output tags
			     */
			    snprintf(buf, sizeof(buf), 
				     "<output><cursor>%s%s</cursor>",
				     stmtp->dss_handle->dsh_db_handle->dh_name,
				     stmtp->dss_name);
			    slaxExtPrintAppend(out, (const xmlChar *) buf, 
					       strlen(buf));

			    /*
			     * Fetch all the available rows
			     */
			    do {
				if (stmtp->dss_in->di_buf.pb_buf == NULL) {
				    rc = db_sqlite_step(stmtp->dss_stmt, out,
							NULL);
				}
			    } while (rc == SQLITE_ROW);

			    slaxExtPrintAppend(out, 
					       (const xmlChar *) "</output>", 
					       9);

			    if (rc == SQLITE_OK) {
				return DB_OK;
			    } else if (rc == SQLITE_DONE) {
				return DB_DATA;
			    } else {
				slaxExtPrintAppend(out, 
					(const xmlChar *) sqlite3_errstr(rc),
					strlen(sqlite3_errstr(rc)));
				return DB_ERROR;
			    }
			} else {
			    const char *errstr = sqlite3_errstr(rc);
			    slaxExtPrintAppend(out, 
					       (const xmlChar *) errstr, 
					       strlen(errstr));
			    return DB_ERROR;
			}
		    }
		}
	    }
	}
    }

    if (pb.pb_buf) {
	xmlFree(pb.pb_buf);
    }

    return DB_FAIL;
}

DB_DRIVER_QUERY (db_sqlite_query)
{
    db_sqlite_handle_t *dbsp;
    slax_printf_buffer_t pb;

    bzero(&pb, sizeof(pb));

    if (db_handle && db_handle->dh_name) {
    	dbsp = db_sqlite_handle_find(db_handle->dh_name);

	if (dbsp) {
	    if (in && in->di_buf.pb_buf) {
		/*
		 * prepare query statement and return sqlite cursor
		 * identifier
		 */
		if (*in->di_buf.pb_buf) {
		    db_sqlite_stmt_t *stmtp = xmlMalloc(sizeof(*stmtp));
		    int rc;

		    slaxLog("db:sqlite: preparing - %s", in->di_buf.pb_buf);

		    if (stmtp) {
			bzero(stmtp, sizeof(*stmtp));

			rc = sqlite3_prepare_v2(dbsp->dsh_sqlite_handle,
						in->di_buf.pb_buf, -1, 
						&stmtp->dss_stmt, NULL);
			if (rc == SQLITE_OK) {
			    db_input_t *inc;
			    inc = xmlMalloc(sizeof(*inc));
			    db_input_copy(inc, in);

			    stmtp->dss_handle = dbsp;
			    stmtp->dss_in = inc;
			    snprintf(stmtp->dss_name, sizeof(stmtp->dss_name), 
				     "%u", seed++);

			    TAILQ_INSERT_TAIL(&db_sqlite_stmts, stmtp, dss_link);
			    slaxExtPrintAppend(out, 
					(const xmlChar *) stmtp->dss_name,
					strlen(stmtp->dss_name));
			    return DB_DATA;
			} else {
			    const char *errstr = sqlite3_errstr(rc);
			    slaxExtPrintAppend(out, 
					       (const xmlChar *) errstr, 
					       strlen(errstr));
			    return DB_ERROR;
			}
		    }
		}
	    } else {
	    }
	} else {
	}
    }
    return DB_FAIL;
}

DB_DRIVER_CLOSE (db_sqlite_close)
{
    db_sqlite_handle_t *dbsp;

    if (db_handle && db_handle->dh_name) {
    	dbsp = db_sqlite_handle_find(db_handle->dh_name);

	if (dbsp) {
	    /* Finalize all the associated statements */
	    db_sqlite_stmt_free_by_handle(dbsp);

	    /* Close sqlite3 handle */
	    sqlite3_close_v2(dbsp->dsh_sqlite_handle);

	    /* Free the structures */
	    TAILQ_REMOVE(&db_sqlite_sessions, dbsp, dsh_link);
	    xmlFree(dbsp);

	    return DB_OK;
	}
    }
    return DB_FAIL;
}

void 
db_driver_init (db_driver_t *driver)
{
    driver->dd_version = DB_DRIVER_VERSION;

    driver->dd_open = db_sqlite_open;
    driver->dd_create = db_sqlite_create;
    driver->dd_insert = db_sqlite_insert;
    driver->dd_delete = db_sqlite_delete;
    driver->dd_update = db_sqlite_update;
    driver->dd_find = db_sqlite_find;
    driver->dd_fetch = db_sqlite_fetch;
    driver->dd_findAndFetch = db_sqlite_find_fetch;
    driver->dd_query = db_sqlite_query;
    driver->dd_close = db_sqlite_close;

    TAILQ_INIT(&db_sqlite_sessions);
    TAILQ_INIT(&db_sqlite_stmts);
}
