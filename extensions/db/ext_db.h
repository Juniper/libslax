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
#define DB_FULL_NS "http://xml.libslax.org/db"
#define DB_NAME_SIZE 12

#define DB_DRIVER_INIT_NAME "db_driver_init"
#define DB_DRIVER_VERSION 1


/*
 * Various status codes returned from database drivers. We take action and
 * provide information to the user based on the return code received from
 * driver callback
 */
typedef enum db_ret_e {
    DB_DATA,    /* DB engine returned data to the user */
    DB_DONE,    /* No more data */
    DB_ERROR,   /* Error occured. See message in *out */
    DB_FAIL,    /* Unknown failure */
    DB_OK       /* Operation succeded with no return data */
} db_ret_t;

/*
 * Structure that holds input from user
 */
typedef struct db_input_s {
    char *di_engine;            /* Database engine name */
    char *di_database;          /* Database name */
    char *di_collection;        /* Table name */
    unsigned int di_limit;      /* Limit for result instances */
    unsigned int di_skip;       /* skip over these number of instances */
    xmlNodePtr di_access;       /* Connection parameters to the database */
    xmlNodePtr di_fields;       /* Fields when creating table */
    xmlNodePtr di_instance;     /* Row that need to be inserted */
    xmlNodePtr di_instances;    /* Rows that need to be inserted */
    xmlNodePtr di_conditions;   /* Conditions when inserting/finding data */
    xmlNodePtr di_constraints;  /* Constraints when creating sql table */
    xmlNodePtr di_sort;         /* Result sorting order */
    xmlNodePtr di_retrieve;     /* Subset of fields to retrieve */
    xmlNodePtr di_update;       /* New data to update instances with */
    xmlNodePtr di_access;       /* Access details for database engine */
    slax_printf_buffer_t di_buf;/* Hold a string buffer */
} db_input_t;

/*
 * Forward declare db_handle_s
 */
typedef struct db_handle_s db_handle_t;

/*
 * Structure filled in by various drivers with callbacks for various
 * functionality
 */
typedef struct db_driver_s {
    TAILQ_ENTRY(db_driver_s) dd_link;   /* Link to next driver */
    int dd_version;                     /* Driver version */
    char dd_name[DB_NAME_SIZE];         /* Name of the driver engine */

    db_ret_t (* dd_open) (db_handle_t *, db_input_t *, 
                          xmlXPathObject **, int);
    db_ret_t (* dd_create) (db_handle_t *, db_input_t *, 
                            xmlXPathParserContext *, int, 
                            slax_printf_buffer_t *);
    db_ret_t (* dd_insert) (db_handle_t *, db_input_t *, 
                            xmlXPathParserContext *, int, 
                            slax_printf_buffer_t *);
    db_ret_t (* dd_delete) (db_handle_t *, db_input_t *, 
                            xmlXPathParserContext *, int, 
                            slax_printf_buffer_t *);
    db_ret_t (* dd_update) (db_handle_t *, db_input_t *, 
                            xmlXPathParserContext *, int, 
                            slax_printf_buffer_t *);
    db_ret_t (* dd_find) (db_handle_t *, db_input_t *, 
                          xmlXPathParserContext *, int, 
                          slax_printf_buffer_t *);
    db_ret_t (* dd_fetch) (db_handle_t *, const char *, 
                           xmlXPathObject *, xmlXPathParserContext *, 
                           int, slax_printf_buffer_t *);
    db_ret_t (* dd_findAndFetch) (db_handle_t *, const char *, 
                                  db_input_t *, xmlXPathParserContext *, 
                                  int, slax_printf_buffer_t *);
    db_ret_t (* dd_query) (db_handle_t *, db_input_t *, 
                           xmlXPathParserContext *, int, 
                           slax_printf_buffer_t *);
    db_ret_t (* dd_close) (db_handle_t *, xmlXPathParserContext *, int);
} db_driver_t;

/*
 * Database handle representing this session
 */
struct db_handle_s {
    TAILQ_ENTRY(db_handle_s) dh_link;   /* Link to next session */
    char dh_name[DB_NAME_SIZE];         /* Unique ID for this handle */
    db_driver_t *dh_driver;             /* Backend database engine driver */
};

/*
 * DB extension initialization function
 */
void extDbInit (void);

/*
 * Database driver initialization function in backend adapter
 */
void db_driver_init (db_driver_t *driver);

/*
 * Copies data from one input structure to other
 */
void db_input_copy (db_input_t *top, db_input_t *fromp);

/*
 * Frees data from db_input_t *
 */
void db_input_free (db_input_t *input);


/*
 * Database driver call back signatures
 */
#define DB_DRIVER_OPEN(x) \
    static db_ret_t x (db_handle_t *db_handle, db_input_t *in, \
                       xmlXPathObject *ostack[] UNUSED, int nargs UNUSED)

#define DB_DRIVER_CREATE(x) \
    static db_ret_t x (db_handle_t *db_handle UNUSED, db_input_t *in UNUSED, \
                       xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED, \
                       slax_printf_buffer_t *out UNUSED)

#define DB_DRIVER_INSERT(x) \
    static db_ret_t x (db_handle_t *db_handle UNUSED, db_input_t *in UNUSED, \
                       xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED, \
                       slax_printf_buffer_t *out UNUSED)

#define DB_DRIVER_DELETE(x) \
    static db_ret_t x (db_handle_t *db_handle UNUSED, db_input_t *in UNUSED, \
                       xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED, \
                       slax_printf_buffer_t *out UNUSED)

#define DB_DRIVER_UPDATE(x) \
    static db_ret_t x (db_handle_t *db_handle UNUSED, db_input_t *in UNUSED, \
                       xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED, \
                       slax_printf_buffer_t *out UNUSED)

#define DB_DRIVER_FIND(x) \
    static db_ret_t x (db_handle_t *db_handle, db_input_t *in, \
                       xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED, \
                       slax_printf_buffer_t *out UNUSED)

#define DB_DRIVER_FETCH(x) \
    static db_ret_t x (db_handle_t *db_handle UNUSED, const char *name, \
                       xmlXPathObject *in UNUSED, \
                       xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED, \
                       slax_printf_buffer_t *out UNUSED)

#define DB_DRIVER_FIND_FETCH(x) \
    static db_ret_t x (db_handle_t *db_handle UNUSED, \
                       const char *name UNUSED, db_input_t *in UNUSED, \
                       xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED, \
                       slax_printf_buffer_t *out UNUSED)

#define DB_DRIVER_QUERY(x) \
    static db_ret_t x (db_handle_t *db_handle, db_input_t *in, \
                       xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED, \
                       slax_printf_buffer_t *out UNUSED)

#define DB_DRIVER_CLOSE(x) \
    static db_ret_t x (db_handle_t *db_handle, \
                       xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)

