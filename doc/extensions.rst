
.. _libslax-extensions:

==============================
Extension Libraries in libslax
==============================

libslax supports a means of dynamically loading extension libraries.
After a script is parsed, any extension prefixes are determined, along
with their namespaces.  Each namespace are then URL-escaped and a
".ext" suffix is appended to generate a filename for the extension
library that supports that namespace.

Extension libraries should be placed in the directory named that is
either:

- the /usr/local/lib/slax/extensions directory

- the "lib/slax/extensions" directory under the "configure --prefix"
  option given at build time

- the directory specified with the "configure --with-extension-dir"
  option given at build time

- one of the directories listed in the environment variable
  SLAX_EXTDIR (colon separated)

- one of the directories provided via the --lib/-L argument to "slaxproc"

The "bit" Extension Library
---------------------------

The "bit" extension library has functions that interpret a string as a
series of bit, allowing arbitrary length bit arrays and operations on
those arrays.

=========================== ====================================
 Function and Arguments      Description
=========================== ====================================
 bit:and(b1, b2)             Return AND of two bit strings
 bit:or(b1, b2)              Return OR of two bit strings
 bit:nand(b1, b2)            Return NAND of two bit strings
 bit:nor(b1, b2)             Return NOR of two bit strings
 bit:xor(b1, b2)             Return XOR of two bit strings
 bit:xnor(b1, b2)            Return XNOR of two bit strings
 bit:not(b1)                 Return NOT of a bit string
 bit:clear(b, off)           Clear a bit within a bit string
 bit:compare(b1, b2)         Compare two bit strings
 bit:set(b, off)             Set a bit within a bit string
 bit:mask(count, len?)       Return len bits set on
 bit:to-int(  bs)            Return integer value of bit string
 bit:from-int(val, len?)     Return bit string of integer value
 bit:to-hex(bs)              Return hex value of a bit string
 bit:from-hex(str, len?)     Return bit string of hex value
 bit:shift-left(b, cnt)      Return logical shift (b << cnt)
 bit:shift-right(b, cnt)     Return logical shift (b >> cnt)
 bit:ashift-right(b, cnt)    Return arithmetic shift (b >> cnt)
=========================== ====================================

The "curl" Extension Library
----------------------------

curl and libcurl are software components that allow access to a number
of protocols, include http, https, smtp, ftp, and scp.  They are open
source and available from the web site http://curl.haxx.se/libcurl/.
Please refer to that site for additional information.

.. _curl-elements:

curl Elements
+++++++++++++

Curl operations are directed using a set of elements, passed to the
curl extension functions.  These elements closely mimic the options
used by the native C libcurl API via libcurl's curl_easy_setopt()
function), as documented on the following web page::

    http://curl.haxx.se/libcurl/c/curl_easy_setopt.html

Once these values are set, a call to curl_easy_perform() performs the
requested transfer.

In the SLAX curl extension library, these options are represented as
individual elements.  This means the <url> element is mapped to the
"CURLOPT_URL" option, the <method> option is mapped to the
CURLOPT_CUSTOMREQUEST option, and so forth.

These elements can be used in three ways:

- The curl:single() extension function allows a set of options to be
  used in a single transfer operation with no persistent connection
  handle.

- The curl:perform() extension function allows a set of options to be
  used with a persistent connection handle.  The handle is returned from
  the curl:open() extension function and can be closed with the
  curl:close() extension function.

- The curl:set() extension function can be used to record a set of
  options with a connection handle and keep those options active for
  the lifetime of the connection.

For example, if the script needs to transfer a number of files, it can
record the <username> and <password> options and avoid repeating them
in every curl:perform() call.

This section gives details on the elements supported.  The functions
themselves are documented in the next section (:ref:`curl-functions`).

<cc>
~~~~

The <cc> element gives a "Cc" address for "email" (SMTP) requests.
For multiple address, use multiple <cc> elements::

    SYNTAX::
      <cc> "cc-user@email.examplecom";

<connect-timeout>
~~~~~~~~~~~~~~~~~

The <connect-timeout> element gives the number of seconds before a
connection attempt is considered to have failed::

    SYNTAX::
      <connect-timeout> 10;

<contents>
~~~~~~~~~~

The <contents> element gives the contents to be transfered::

    SYNTAX::
      <contents> "multi-\nline\ncontents\n";

<content-type>
~~~~~~~~~~~~~~

The <content-type> element gives the MIME type for the transfer
payload.

    SYNTAX::
      <content-type> "mime/type";

<errors>
~~~~~~~~

The <errors> element controls how HTML and XML parsing errors are
handled.  The default (display them on stderr) is often not
attractive.  Instead errors can be ignored, logged, or recorded, based
on the contents of the <errors> element:

========= ===================================
 Value     Special behavior
========= ===================================
 default   Errors are displayed on stderr
 ignore    Errors are discarded
 log       Errors are logged (via slaxLog())
 record    Errors are recorded
========= ===================================

When <errors> is set to "record", all errors appear in a string under
the <errors> element in the XML node (as returned by
e.g. curl:perform).  If no errors are generated, the <errors> element
will not be present, allowing it to be used as a test for errors::

    var $opt = {
        <url> $url;
        <format> "html";
        <errors> "record";
    }
    var $res = curl:single($opts);
    if $res/errors {
        terminate "failure: " _ $res/errors;
    }

<fail-on-error>
~~~~~~~~~~~~~~~

The <fail-on-error> element indicates that the transfer should fail if
any errors where detected, including insignificant ones::

    SYNTAX::
      <fail-on-error>;

<format>
~~~~~~~~

The <format> element gives the expected format of the returned
results, allowing the curl extension to automatically make the content
available in the native format::

    <format> "xml";

============= =============================
 Format name   Special behavior
============= =============================
 html          Result is parsed as HTML
 name          Result is name-value pairs
 text          None
 url-encoded   Result is n1-v1&n2-v2 pairs
 xml           Result is parsed as XML
============= =============================

The "name" encoding is used for name=value pairs separated by
newlines, where the url-encoded encoding is used when the name=value
pairs are separated by "&".

The parsed data is returned in the <data> element, using <name>
elements.  In the following example results, <format> was set
to "url-encoded"::

    <results>
      <url>https://api.example.com/request_token</url>
      <curl-success/>
      <raw-headers>HTTP/1.1 200 OK&#xD;
    Server: XXXX&#xD;
    Date: Tue, 18 Jun 2013 18:56:31 GMT&#xD;
    Content-Type: application/x-www-form-urlencoded&#xD;
    Transfer-Encoding: chunked&#xD;
    Connection: keep-alive&#xD;
    x-server-response-time: 69&#xD;
    x-example-request-id: 123456&#xD;
    pragma: no-cache&#xD;
    cache-control: no-cache&#xD;
    x-http-protocol: None&#xD;
    x-frame-options: SAMEORIGIN&#xD;
    X-RequestId: 12345&#xD;
    &#xD;
    </raw-headers>
      <headers>
        <version>HTTP/1.1</version>
        <code>200</code>
        <message>OK</message>
        <header name="Server">XXXXX</header>
        <header name="Date">Tue, 18 Jun 2013 18:56:31 GMT</header>
        <header name="Content-Type"
               >application/x-www-form-urlencoded</header>
        <header name="Transfer-Encoding">chunked</header>
        <header name="Connection">keep-alive</header>
        <header name="x-server-response-time">69</header>
        <header name="x-example-request-id">123456</header>
        <header name="pragma">no-cache</header>
        <header name="cache-control">no-cache</header>
        <header name="x-http-protocol">None</header>
        <header name="x-frame-options">SAMEORIGIN</header>
        <header name="X-RequestId">12345</header>
      </headers>
      <raw-data>oauth_token_secret=s&amp;oauth_token=t</raw-data>
      <data format="url-encoded">
        <name name="oauth_token_secret">s</name>
        <name name="oauth_token">t</name>
      </data>
    </results>

<from>
~~~~~~

The <from> element gives the "From" address to use for "email" (SMTP)
requests::

    SYNTAX::
      <from> "source-user@email.example.com";

<header>
~~~~~~~~

The <header> element gives additional header fields for the request::

    SYNTAX::
      <header name="name"> "value";

<insecure>
~~~~~~~~~~

The <insecure> element indicates a willingness to tolerate insecure
communications operations.  In particular, it will allow SSL Certs
without checking the common name::

    SYNTAX::
      <insecure>;

<local>
~~~~~~~

The <local> element gives the name to use as the local hostname for
"email" (SMTP) requests::

    SYNTAX::
      <local> "local host name";

<method>
~~~~~~~~

The <method> element sets the method used to transfer data.  This
controls the HTTP request type, as well as triggering other transfer
mechanisms::

    SYNTAX::
      <method> $method;

Method names are listed in the table below:

======== ================================
 Method   Description
======== ================================
 get      HTTP GET or FTP GET operation
 post     HTTP POST operation
 delete   HTTP DELETE operation
 head     HTTP HEAD operation
 email    SMTP email send operation
 put      HTTP PUT operation
 upload   HTTP POST or FTP PUT operation
======== ================================

The "get" method is the default.

<param>
~~~~~~~

The <param> element gives additional parameter values for the
request.  These parameters are typically encoded into the URL::

    SYNTAX::
      <param name="x"> "y";

<password>
~~~~~~~~~~

The <password> element sets the user's password for the transfer::

    SYNTAX::
      <password> "password";

<secure>
~~~~~~~~

The <secure> element requests the use of the "secure" sibling of many
protocols, including HTTPS and FTPS::

    SYNTAX::
      <secure>;

<server>
~~~~~~~~

The <server> element gives the outgoing SMTP server name.  At present,
MX records are not handled, but that will be fixed shortly::

    SYNTAX::
      <server> "email-server.example.com";

<subject>
~~~~~~~~~

The <subject> element gives the "Subject" field for "email" (SMTP)
requests::

    SYNTAX::
      <subject> "email subject string";

<timeout>
~~~~~~~~~

The <timeout> element gives the number of seconds before an open
connection is considered to have failed::

    SYNTAX::
      <timeout> 10;

<to>
++++

The <to> element gives a "To" address for "email" (SMTP) requests.
For multiple address, use multiple <to> elements::

    SYNTAX::
      <to> "to-user@email.examplecom";

<upload>
~~~~~~~~

The <upload> element indicates this is a file upload request::

    SYNTAX::
      <upload>;

<url>
~~~~~

The <url> element sets the base URL for the request::

    SYNTAX::
      <url> "target-url";

<username>
~~~~~~~~~~

The <username> element sets the user name to use for the transfer::

    SYNTAX::
      <username> "username";

<verbose>
~~~~~~~~~

The <verbose> element requests an insanely detailed level of debug
information that can be useful when debugging requests.  The curl
extension will display detailed information about the operations and
communication of the curl transfer::

    SYNTAX::
      <verbose>;

.. _curl-functions:

curl Extension Functions
++++++++++++++++++++++++

The curl namespace defines a set of extension functions.  This section
describes those functions.

.. _curl-perform:

curl:perform
~~~~~~~~~~~~

The "curl:perform" extension function performs simple transfers using
a persistent connection handle, as provided by curl:open (:ref:`curl-open`).

The arguments are the connection handle and a set of option elements
as listed in :ref:`curl-elements`.  The returned object is an XML hierarchy
containing the results of the transfer::

    SYNTAX::
        object curl:perform(handle, options)

The returned object may contain the following elements:

============== =====================================
 Element        Contents
============== =====================================
 url            The requested URL
 curl-success   Indicates sucess (empty)
 raw-headers    Raw header fields from the reply
 raw-data       Raw data from the reply
 error          Contains error message text, if any
 header         Parsed header fields
 data           Parsed data
============== =====================================

The <header> element can contain the following elements:

========= ==========================================
 Element   Contents
========= ==========================================
 code      HTTP reply code
 version   HTTP reply version string
 message   HTTP reply message
 field     HTTP reply fields (with @name and value)
========= ==========================================

The following is an example of the <header> element, with header
fields parsed into <field> elements::

      <header>
        <version>HTTP/1.1</version>
        <code>404</code>
        <message>Not Found</message>
        <field name="Content-Type">text/html</field>
        <field name="Content-Length">345</field>
        <field name="Date">Mon, 08 Aug 2011 03:40:21 GMT</field>
        <field name="Server">lighttpd/1.4.28 juisebox</field>
      </header>

.. _curl-open:

curl:open
~~~~~~~~~

The "curl:open" extension function opens a connection to a remote
server, allowing multiple operations over a single connection::

    SYNTAX::
        handle curl:open();

The returned handle can be passed to curl:perform() or curl:close().

curl:set
~~~~~~~~

The "curl:set" extension function records a set of parameters that
will persist for the lifespan of a connection::

    SYNTAX::
        void cur:set(handle, options);

curl:set() sets options on the handle, so they don't need to be
repeated on each curl:perform() call::

    var $curl = curl:open();
    var $global-options = {
        <method> "post";
        <header name="id"> "phil";
        <header name="location"> "raleigh";
    }

    expr curl:set($curl, $global-options);

    call some-other-template($curl);

curl:single
~~~~~~~~~~~

The "curl:single" extension function performs transfer operations
without using a persistent connection::

    SYNTAX::
        object curl:single(options);

The returned object is identical in structure to the one returned by
curl:perform.  Refer to :ref:`curl-perform` for additional information.

curl:close
~~~~~~~~~~

The "curl:close" extension function closes an open connection.
Further operations cannot be performed over the connection::

    SYNTAX::
        void curl:close(handle);

Examples
++++++++

This section contains a set of example scripts that use the curl
extension to perform simple gets, google authorization, and send
email.

Simple GET
~~~~~~~~~~

This script gets a vanilla web page, but just to be interesting,
includes a header field for the HTTP header and a parameter that is
incorporated into the requested URL::

    version 1.1;

    ns curl extension = "http://xml.libslax.org/curl";

    param $url = "http://www.juniper.net";

    match / {
        <op-script-results> {
            var $options = {
                <header name="client"> "slaxproc";
                <param name="smokey"> "bandit";
            }

            var $results = curl:single($url, $options);
            message "completed: " _ $results/headers/message;
            <curl> {
                copy-of $results;
            }
        }
    }

Google Auth
~~~~~~~~~~~

This script take a username and password and uses the google login
services to translate them into an "Authorization" string::

    version 1.1;

    ns curl extension = "http://xml.libslax.org/curl";

    param $url = "https://www.google.com/accounts/ClientLogin";
    param $username;
    param $password;

    var $auth-params := {
        <url> $url;
        <method> "post";
        <insecure>;
        <param name="Email"> $username;
        <param name="Passwd"> $password;
        <param name="accountType"> "GOOGLE";
        <param name="service"> "wise";
        <param name="source"> "test-app";
    }

    match / {
        var $curl = curl:open();

        var $auth-cred = curl:perform($curl, $auth-params);

        <options> {
            for-each(slax:break-lines( $auth-cred/raw-data )) {
                if(starts-with(.,"Auth")) {
                    <header name="GData-Version"> "3.0";
                    <header name="Authorization"> "GoogleLogin " _ .;
                }
            }
        }

        expr curl:close($curl);
    }

Email
~~~~~

This script sends an email via a server provided as a parameter::

    version 1.1;

    ns curl extension = "http://xml.libslax.org/curl";

    param $server;

    match / {
        <out> {
            var $info = {
                <method> "email";
                <server> $server;
                <from> "muffin@example.com";
                <to> "phil@example.net";
                <subject> "Testing...";
                <contents> "Hello,
    This is an email.  But you know that.

    Thanks,
     Phil
    ";
            }

            var $res = curl:single($info);
            <res> {
                copy-of $res;
            }
        }
    }

The "db" Extension Library
--------------------------

The db extension library provides a way to store/retrieve/manipulate/delete
data stored in a backend database from SLAX scripts

db Elements
+++++++++++

Following are the elements that can be used in options and data to db
extension functions

<engine>
~~~~~~~~

Used to specify the backend database engine that is used to
access/store data::

    <engine> "sqlite";

"sqlite" is the currently supported backend engine.  The <engine>
element can also take mysql/mongodb as values once the adapters for
corresponding database engines are made available.

<database>
~~~~~~~~~~

Used to specify the name of the database to operate on::

    <database> "test.db";

<collection>
~~~~~~~~~~~~

This is used to specify the data collection on which to perform operations.
This corresponds to a database table in SQL world::

    <collection> "employee";

<fields>
~~~~~~~~

This is used to specify meta data about various fields that a collection
contain using <field> elements::

    <fields> {
        <field> {
            ...
            ...
        }
        <field> {
            ...
            ...
        }
    }

<field>
~~~~~~~

Contains meta data about each field in collection. This corresponds to column
definition in SQL world and is used when creating a collection.

::

    <field> {
        <name> "name";
        <type> "integer";
        <primary>;
    }

<field> can contain following elements:

================ ==========================================================
 Element          Description
================ ==========================================================
 name             Name of the field
 type             Type of this field. Can take integer/text/real as values
 primary          Field is primary key
 unique           Field value are unique
 notnull          Field value must be specified
 auto-increment   Field value is auto incremented; must be integer type
 default          Value field will have if not specified by the user
================ ==========================================================

<instance>
~~~~~~~~~~

Represents a single instance from collection. Contains fields and their
corresponding values in that record. Used when inserting/manipulating data in
the datastore. Example::

    <instance> {
        <id> 5;
        <name> "John";
    }


<instances>
~~~~~~~~~~~

Used to hold multiple instances::

    <instances> {
        <instance> {
            ...
            ...
        }
        <instance> {
            ...
            ...
        }
    }

<condition>
~~~~~~~~~~~

Used to specify a condition that must to be satisfied when operating with
data instances from datastore. This forms the condition used with WHERE clause
when operating with SQL datastore

<condition> will contain following mandatory elements.

================ ==========================================================
 Element          Description
================ ==========================================================
 selector         Name of the field to apply this condition
 operator         Operator for this condition. Can take one of comparison
                  or logical operators (<, >, <=, >=, =, LIKE, IN, NOT)
 value            Value to be used with operator on this field
================ ==========================================================

Example::

    <condition> {
        <selector> "id;
        <operator> "in";
        <value> "(1, 10)";
    }

<conditions>
~~~~~~~~~~~~

This is used to specify multiple conditions with <and> or <or> as parent
node. For example::

    <conditions> {
        <and> {
            <condition> {
                ... /* c1 */
            }
            <condition> {
                ... /* c2 */
            }
            <or> {
                <condition> {
                    ... /* c3 */
                }
                <condition> {
                    ... /* c4 */
                }
            }
        }
    }

Above condition set gets translated to "c1 AND c2 AND c3 OR c4"

<retrieve>
~~~~~~~~~~

This is used to specify only the fields that should appear as part of result
set when querying the datastore. For example::

    <retrieve> {
        <id>;
        <name>;
    }

<sort>
~~~~~~

Used to specify the fields and order by which the result set must be sorted
before returning. List of fields can be specified using <by> and order using
<order>. "asc" and "desc", corresponds to ascending and descending order are
the only valid values for order, which defaults to "asc"::

    <sort> {
        <by> "id";
        <by> "age";
        <order> "desc";
    }

<limit>
~~~~~~~

Used to limit the number of instances that a result can contain::

    <limit> 10;

<skip>
~~~~~~

Used to skip over a specified number of instances in the result set before
returning to the user::

    <skip> 5;

<result>
~~~~~~~~

This is the output node from most of the extension functions. This contains
<status> and may contain one or more <instance>s.

<status> can take one of the following values:

=============== =======================================================
 Value           Description
=============== =======================================================
 <done>          Operation ran to completion. Usually means no more
                 data to return
 <ok>            Operation is successful. Returned with insert/update
                 functions
 <data>          Data is available in the result as <instance> elements
 <error>         Error has occurred. Error message will be the value of
                 this element
=============== =======================================================

db Extension Functions
++++++++++++++++++++++

The db extension functions require the following ns statement::

    ns db extension = "http://xml.libslax.org/db";

db:open()
~~~~~~~~~

Used to open a database connection using provided options. Options contain
backend engine and database name. For example::

    var $options = {
        <engine> "sqlite";
        <database> "test.db";
    }

    var $handle = db:open($options);

db:open() returns database handle (a session cookie), that can be used to
perform further operations on this opened connection.

If sqlcipher is available, db:open() also takes an optional <access> config
where in we can specify the key that can be used to encrypt/decrypt the
database. In such a case, options will look as below::

    var $options = {
        <engine> "sqlite";
        <database> "test.db";
        <access> {
            <key> "testKey";
        }
    }

If the user provides a rekey in access, database key will be changed to the
valued mentioned in rekey::

    var $options = {
        <engine> "sqlite";
        <database> "test.db";
        <access> {
            <key> "testKey";
            <rekey> "newTestKey";
        }
    }

Above options when used in with db:open() will change test.db key from
"testKey" to "newTestKey".

db:create()
~~~~~~~~~~~

Used to create a collection using opened database handle and  information
about the fields that this collection contains. For example::

    var $schema = {
        <collection> "employee";
        <fields> {
            <field> {
                <name> "id";
                <type> "integer";
                <primary>;
            }
            <field> {
                <name> "name";
                <type> "text";
                <default> "John";
            }
        }
    }

    var $result = db:create($handle. $schema);

db:create() returns result node which contains status of the operation.
Status is returned as ok in case of success and has error with message if it
fails.

db:insert()
~~~~~~~~~~~

Used to insert data into a collection using a handle and input data. For
example, to insert one instance into a collection, we use::

    var $data = {
        <collection> "employee";
        <instance> {
            <id> 3;
            <name> "Joey";
        }
    }

    var $result = db:insert($handle, $data);

To insert a batch of instances, we use::

    var $data = {
        <collection> "employee";
        <instances> {
            <instance> {
                <id> 1;
                <name> "Rachael";
            }
            <instance> {
                <id> 2;
                <name> "Chandler";
            }
            <instance> {
                <id> 3;
                <name> "Monica";
            }
        }
    }

    var $result = db:insert($handle, $data);

db:insert() returns a result node and includes result of operation in status.
Status is returned as ok in case of success and error with message in case of
failure.

db:update()
~~~~~~~~~~~

Used to update a set of instances matching given conditions with a new
provided instance. Takes database handle as first argument and input
as second::

    var $input = {
        <collection> "employee";
        <conditions> {
            <and> {
                <condition> {
                    <selector> "id";
                    <operator> ">";
                    <value> "2";
                }
                <condition> {
                    <selector> "name";
                    <operator> "like";
                    <value> "Monica";
                }
            }
        }
        <update> {
            <name> "Ross";
        }
    }

    var $result = db:update($handle, $input);

Above example will update all the employee instances whose id is greater than
2 with names Monica, to Ross. db:update() returns result set like create/insert
with status of the operation.

db:delete()
~~~~~~~~~~~

Used to delete instances matching given conditions. For example::

    var $input = {
        <collection "employee";
        <conditions> {
            <condition> {
                <selector> "id";
                <operator> ">";
                <value> "4";
            }
        }
        <limit> 10;
    }

    var $result = db:delete($handle, $input);

Above operation deletes up to 10 employee instances whose id is greater than 4.
db:delete() returns result set with status similar to insert/update/create
operations

db:find()
~~~~~~~~~

This call returns a cursor for instances matching given conditions. For
example::

    var $query = {
        <collection> "employee";
        <sort> {
            <by> "id";
            <order> "desc";
        }
        <retrieve> {
            <name>;
        }
        <conditions> {
            <condition> {
                <selector> "id";
                <operator> ">";
                <value> "5";
            }
        }
        <limit> 10;
        <skip> 5;
    }

    var $cursor = db:find($handle, $query);

Above example returns a cursor to result set containing names first 10
employees skipping first 5 whose id is more than 5 sorted in descending order.
We will have to use db:fetch() call to retrieve each of these result
instances.

db:fetch()
~~~~~~~~~~

This function call is used to fetch result instance using cursor returned
from find/query call::

    var $result = db:fetch($cursor);

Returned result contains status and <instance> with fields and values if
available. Status will be returned as <data> if instance is available. It
will be <done> if the query ran to completion and no more data is available
to be read.

db:fetch() also takes an optional second argument that can be used to specify
additional data. This can be useful when fetching on the cursor returned from
custom query using db:query().

db:find-and-fetch()
~~~~~~~~~~~~~~~~~~~

This function call is used to find and read all the instances in one step.
Usage is as below::

    var $result = db:find-and-fetch($handle, $query);

Returned result contains all the available instances from the query and status
will be <data>.  "status" will be <error> with message in case of errors.

db:query()
~~~~~~~~~~

This function can be used to run custom queries. For example::

    var $queryString = "INSERT INTO employee (id, name) "
                       _ "VALUES (@id, @name)";
    var $cursor = db:query($queryString);

    var $input = {
        <id> 11;
        <name> "Phoebe";
    }

    var $result = db:fetch($cursor, $input);

Above example runs a custom query and receives cursor from db:query()
and used db:fetch() to insert data into employee collection using the
previous cursor.

db:close()
~~~~~~~~~~

Used to close the database connection and free all the structures
associated with previous operations performed on this handle::

    var $result = db:close($handle);

The "xutil" Extension Library
-----------------------------

The xutil extension library provides a number of XML- and XSLT-related
utility functions.

"xutil" Extension Functions
+++++++++++++++++++++++++++

The "xutil" extension functions require the following ns statement::

    ns xutil extension = "http://xml.libslax.org/xutil";

xutil:max-call-depth()
~~~~~~~~~~~~~~~~~~~~~~

SLAX and XSLT use recursion as a programming tool for iteration, but
unlimited recursion can lead to disaster.  To avoid this, the libxslt
engine limits the depth of recursive calls to 3,000.  This limit
should be find for almost all uses, but it the value is not suitable,
it can be adjusted using the xutil:max-call-depth() function.

If invoked without an argument, the function returns the current
value.  If a number is passed as the argument, that number is used as
the new max call depth limit::

    EXAMPLE::
        var $limit = xutil:max-call-depth();
        expr xutil:max-call-depth($limit * 2);

xutil:string-to-xml()
~~~~~~~~~~~~~~~~~~~~~

The xutil:string-to-xml() function turns a string containing XML data
into the native representation of that data::

    EXAMPLE::
        var $data = "<doc><title>fred</title></doc>";
        var $xml = xutil:string-to-xml($data);
        message "title is " _ $xml/title;

Multiple strings can be passed, in which case, they will be
concatenated before the XML conversion::

    EXAMPLE::
        var $xml2 = xutil:string-to-xml("<top>", $content, "</top");

xutil:xml-to-string()
~~~~~~~~~~~~~~~~~~~~~

The xutil:xml-to-string() function turns XML content into a string.
This is different than the normal XPath stringification, which
discards open and close tag.  xml-to-string will encode tags as part
of the string::

    EXAMPLE::
        var $xml = <dog> "red";
        var $str = xutil:xml-to-string($xml);
        /* str is now the string "<dog>red</dog>" */

xutil:json-to-xml()
~~~~~~~~~~~~~~~~~~~

The xutil:json-to-xml() function turns a string containing JSON data
into the native representation of that data in XML::

    EXAMPLE::
        var $data = "[ { "a" : 4, "name": "fish"}, 4, 5]";
        var $xml = xutil:json-to-xml($data);
        message "title is " _ $xml/json/name;

An optional second parameter contains a node set of the following
optional elements:

========= ======== ===========================================
 Element   Value    Description
========= ======== ===========================================
 types     "no"     Do not encode type information
 root      string   Name of root node to be returned ("json")
========= ======== ===========================================

::

        var $options = {
            <root> "my-top";
            <types> "no";
        }
        var $xml = xutil:json-to-xml($data, $options);

The XML returned from json-to-xml() is decorated with attributes
(including the "type" and "name" attributes) which allow the data to
be converted back into JSON using xml-to-json().  Refer to that
function for additional information.

For details on the JSON to XML encoding, refer to :ref:`json-attributes`,
:ref:`json-arrays`, and :ref:`json-names`.

.. _xutil-xml-to-json:

xutil:xml-to-json()
~~~~~~~~~~~~~~~~~~~

The xutil:xml-to-json() function turns XML content into a string of
JSON data.  This is different than the normal XPath stringification,
which discards open and close tag.  xml-to-json will encode tags as
JSON objects inside a string::

    EXAMPLE::
        var $xml = <json> {
            <color> "red";
        }
        var $str = xutil:xml-to-json($xml);
        /* str is now the string '{ "color": "red" }' */

An optional second parameter contains a node set of the following
optional elements:

========= ============ ========================================
 Element   Value        Description
========= ============ ========================================
 pretty    empty        Add newlines and indentation to output
 quotes    "optional"   Avoid quotes for names
========= ============ ========================================

For details on the JSON to XML encoding, refer to :ref:`json-attributes`,
:ref:`json-arrays`, and :ref:`json-names`.

xutil:slax-to-xml()
~~~~~~~~~~~~~~~~~~~

The xutil:slax-to-xml() function turns a string of SLAX data into an
XML hierarchy::

    EXAMPLE::
        var $slax = '<color> "red"; <object> "fish";'
        var $xml = xutil:slax-to-xml($slax);
        /* $xml is now an XML hierarchy */

xutil:xml-to-slax()
~~~~~~~~~~~~~~~~~~~

The xutil:xml-to-slax() function turns XML content into a string of
SLAX data::

    EXAMPLE::
        var $xml = <json> {
            <color> "red";
        }
        var $str = xutil:xml-to-slax($xml);
        /* $str is now the string '<color> "red";' */

Since the returned document requires a root tag and SLAX documents are
assumably XSLT scripts, the root node of the returned document is an
`<xsl:stylesheet>` element.  To remove this layer and get a nodeset of
the nodes inside this element, append a "/\*" to your call::

        var $str = xutil:xml-to-slax($xml-snippet)/*;

xutil:common()
~~~~~~~~~~~~~~

The `xutil:common` function returns the set of nodes that appear in
the first argument that match nodes appearing in at least one of the
other arguments, whether those arguments are nodesets or values.

`xutil:common`() returns common nodes based on the contents of the
nodes.  EXSLT_ has a group of `set`-related functions that return the
common and different nodes for two nodesets, but these functions look
at identical nodes, so two nodes with the same name and the same
contents are not seen as identical.  This function (and
`xutil:difference`) test for nodes using content, not specific nodes.

.. _EXSLT: https://exslt.github.io/set/index.html

::

    SYNTAX::
        node-set xutil:common(ns1, ns2, ...)
    EXAMPLE::
	<common> {
	    copy-of xutil:common($s1, $s2);
	}
        var $small-odds = xutil:common($list/*, 1, 3, 5, 7, 9);

During the matching process, mixed content that contains only white
space is ignored, allowing content with differing indentation to
match.  Only the hierarchy and leaf contents are compared.

For example, consider the following input:

    <data>
      <first>
         <one>
             <two>
                  <three>   </three>
             </two>
         </one>
      </first>
      <second>
         <one><two><three>   </three></two></one>
      </second>
    </data>

A script that compares `first` and `second` will find they match,
since the internal text nodes containing white space are mixed and can
be ignored, while the three spaces inside the `three` element are not
mixed content and are considered significant.

xutil:difference
~~~~~~~~~~~~~~~~

The `xutil:difference` function returns the set of nodes that appear
in the first node sets that do not match nodes in any of the other
arguments, whether those arguments are nodesets or values.

This function uses the same criteria as `xutil:common`.

::

    SYNTAX::
        node-set xutil:difference(ns1, ns2, ...)
    EXAMPLE::
	<difference> {
	    copy-of xutil:difference($s1, $s2);
	}
        var $no-small-odds = xutil:common($list/*, 1, 3, 5, 7, 9);

The "os" Extension Library
--------------------------

The "os" extension library provides a set of functions to invoke
operating system-related operations on the local host.  Note that
these are _not_ run on the remote target, but on the machine where the
script is being executed.

"os" Extension Functions
++++++++++++++++++++++++

The "os" extension functions require the following ns statement:

    ns os extension = "http://xml.libslax.org/os";

.. _error-nodes:

Error Nodes
~~~~~~~~~~~

The return value of many os:* functions consists of a set of zero or
more error nodes.  Each node may contain an <error> element, which
in turn may contain the following elements:

========= ===================================
 Element   Description
========= ===================================
 errno     Error message based on errno
 path      The path that triggered the error
 message   The error message
========= ===================================

In addition, the <errno> element contains a "code" attribute which
holds the tag for the errno value, if known::

    <error>
      <errno code="ENOENT">No such file or directory</errno>
      <message>unknown group: phil</message>
    </error>

.. _chmod:

os:chmod
~~~~~~~~

The os:chmod function changes the permissions of one or more files,
allowing or preventing different sets of users from accessing those
files.

The first argument is a mode specification similar to the chmod
command, with either an octal number to set the permissions to, or an
expression consisting of one or more of the letters 'u', 'g', 'o', and
'a' (for user, group, other, and all) followed by '+' or '-' (for
setting or clearing) and one or more of the letters 'r', 'w', and 'x'
(for read, write, and execute).  The expression "ug+rw" would give
read and write permission to the user and group which own the file or
directory.

The second and subsequent arguments can be either a path name string
or a nodeset of <file>, <directory>, and <wildcard> elements, with the
former two contain path for files and directories, and the latter
contains shell/glob-style wildcard patterns.  The following patterns
are permitted:

- '*' matches zero or more characters
- '?' matches any single character
- '[...]' matches any of the enclosed characters
- '{...}' matches any of the enclosed comma-separated sequences

For example, <wildcard> "\*.txt" matches all text files::

    SYNTAX::
        void os:chmod(mode, files, ...);

    EXAMPLE::
        $res = os:chmod("g+w", "file1", {
            <file> "test.test";
            <directory> "dir";
            <wildcard> "*.c";
        });

If successful, nothing is returned.  On failure, a set of error nodes
is returned.  See :ref:`error-nodes` for details.

os:chown
~~~~~~~~

The os:chown function changes for owning user and group for one or
more files.

The first argument is the target name and/or group, in either symbolic
or numeric format::

    SYNTAX::
        [ <user> ]? [ ':' <group> ]
    EXAMPLE:
        phil
        phil:eng
        :eng
        :12
        0:0

The second and subsequent arguments can be either a path name string
or a nodeset of <file>, <directory>, and <wildcard> elements.  See
:ref:`chmod` for details::

    SYNTAX::
        void os:chown(owner, files, ...);

    EXAMPLE::
        $res = os:chown(":eng", "file1", {
            <file> "test.test";
            <directory> "dir";
            <wildcard> "*.c";
        });

If successful, nothing is returned.  On failure, a set of error nodes
is returned.  See :ref:`error-nodes` for details.

os:exit-code
~~~~~~~~~~~~

The os:exit-code function sets the exit code for the process running
the script.  This can be used to indicate an error to the caller.  The
argument to the function is the exit code value, in numeric form::

    SYNTAX::
        void os:exit-code(number);

    EXAMPLE::
        expr os:exit-code(1);

os:mkdir
~~~~~~~~

The os:mkdir function makes directories, similar to the "mkdir"
command or the POSIX "mkdir" library function.  These are two
arguments; the first is name of the directory to be made and the
second is an node-set containing options to be used during the
operation.  The options can include the values in the following table.

========= ==============================================
 Element   Description
========= ==============================================
 create    Error if the last element of the path exists
 mode      Octal value of directory permissions
 path      Create intermediate directories as required
========= ==============================================

::

    SYNTAX::
        node-set os:mkdir(path [, options]);

    EXAMPLE::
        var $res = os:mkdir("/tmp/foo");
        var $opts = {
            <mode> "0700";
            <path>;
            <create>;
        }
        var $res2 = os:mkdir("/tmp/foo/a/b/c/d/e/f", $opts);

If the value for <mode> is a string, it will be converted to an
integer using the default numeric base of 8 (octal), so '<mode> "644"'
will work, but '<mode> 644' will see 644 as a number with base 10
(decimal), which will result in undesirable results since 644 base 10
is 01204 base 8.

If the value of <mode> is not a valid mode integer value, it will be
ignored.

The return value of os:mkdir is a node-set which may contain an
<error> element is an error occurred.  This element may contain
the following elements:

========= ===================================
 Element   Description
========= ===================================
 errno     Error message based on errno
 path      The path that triggered the error
 message   The error message
========= ===================================

In addition, the <errno> element contains a "code" attribute which
holds the tag for the errno value, if known.

If successful, nothing is returned.  On failure, a set of error nodes
is returned.  See :ref:`error-nodes` for details.

os:remove
~~~~~~~~~

The os:remove function removes files and directories.  The input
arguments are nodesets containing either strings or XML content.  The
XML nodes can be either <file>, <directory>, or <wildcard> elements
containing the name of a file, directory, or glob-style wildcard
expression to remove.  Directories must be empty.  If an nodeset
member is a string, it defaults to <file>::

    var $file = <file> { expr "/tmp/some-file"; }
    var $dir = <directory> { expr "/tmp/some-dir"; }
    var $wild = <wildcard> { expr "/tmp/some-*-wild"; }
    var $res = os:remove("/tmp/another-file", $file, $dir, $wild);
    <results> { copy-of $res; }

os:stat
~~~~~~~

The os:stat function returns information about files and directories,
similar to the POSIX stat() function, returning a node-set of <entry>
elements containing details about each file.

The arguments to the os:stat() function are either strings or
node-sets.  os:stat() allows any number of arguments.  If the argument
is a string, it is used as a path specification and information on
matching files is returned.  The path specification can include
glob-style wildcards (e.g. test\*.c, \*.slax).  If the argument is a
node-set, then the node can contains the following elements:

========= ==============================================
 Element   Description
========= ==============================================
 brief     Only summary information is emitted
 depth     The number of subdirectory levels to descend
 hidden    Return information on hidden files
 name      File specification (same as string argument)
 recurse   Show all subdirectories
========= ==============================================

Note the <name> element functions identically to the string argument
details given above::

    var $files = os:stat("/etc/m*");
    var $options = {
        <hidden>;
        <depth> 3;
    }
    var $logs = os:stat("/var/log/*txt", $options);
    for-each $logs {
        message name _ " is a " _ type;
    }

The return value is a node-set of <entry> elements.  Each entry
contains the following elements:

================ ====================================== =======
 Element          Description                            Brief
================ ====================================== =======
 name             Path to the entry                      Y
 type             Type of file (see below)               Y
 executable       Present if entry is executable         Y
 symlink          Present if entry is a symbolic link    Y
 symlink-target   Contents of the symbolic link          N
 permissions      Permissions for the entry              N
 owner            Name of the owning user                N
 group            Name of the owning group               N
 links            Number of hard links to this entry     N
 size             Number of bytes used by the entry      N
 date             Time and date of last modification	  N
 entry            Directory contents                     N
================ ====================================== =======

Only elements tagged "Y" are emitted when the <brief> option is used.

The <type> element contains one of the following:

=========== =======================================
 Value       Description
=========== =======================================
 directory   Directory containing additional files
 file        Regular file
 character   Character-oriented device (tty)
 block       Block-oriented device (disk)
 link        Symbolic link
 socket      AF_UNIX Socket
 fifo        Named pipe (First-in/first-out)
 unknown     Other/unknown file type
=========== =======================================

In some cases, attributes are used to attach useful information to
elements.  The following table lists these attributes and values.

============= =========== ==================================
 Element       Attribute   Description
============= =========== ==================================
 permissions   mode        Octal value for the mode
 owner         uid         Numeric value of the user's uid
 group         gid         Numeric value of the group's gid
 date          date        Seconds since Jan 1, 1970
============= =========== ==================================

os:user-info
~~~~~~~~~~~~

The os:user-info helps know the details of user running the script.

::

    var $userinfo = os:user-info();

This function returns a user element with following information

========== ==================================================
 Element    Description
========== ==================================================
 class      User class (if available)
 dir        User home directory
 gecos      User information
 name       Username
 shell      User shell program
========== ==================================================

This functions returns empty when an error occurs.

Crypto Functions (with EXSLT and libgcrypt)
-------------------------------------------

libexslt defines a set of crypto functions using the namespace::

    ns crypto = "http://exslt.org/crypto"; 

These were added in 2004, but I cannot find suitable documentation
for them.

Building libxslt with libgcrypt-devel
+++++++++++++++++++++++++++++++++++++

To have libxslt expose the crypto function, the library
"libgcrypt-devel" must be present when libxslt is
configured. "libgcrypt" alone is not sufficient, since it does not
have the header files needed for libxslt to compile.

Under macosx, this library is available in the "Mac Ports" collection
and can be installed using the command "port install libgcrypt-devel".

Once libgcrypt-devel is installed, run the libxslt "configure" script
and rebuild using "make".

crypto:md4
++++++++++

Computes the md4 hash of a string and returns it as a hex string::

    var $hex = crypto:md4($data); 

crypto:md5
++++++++++

Computes the md5 hash of a string and returns it as a hex string::

    var $hex = crypto:md5($data); 

crypto:sha1
+++++++++++

Computes the sha1 hash of a string and returns it as a hex string::

    var $hex = crypto:sha1($data); 

crypto:rc4_encrypt
++++++++++++++++++

Encrypts a string and returns it as a hex string::

    var $hex = crypto:rc4_encrypt($key, $data); 

crypto:rc4_decrypt
++++++++++++++++++

Decrypts a string and returns it as hex::

    var $data = crypto:rc4_decrypt($key, $hex); 
