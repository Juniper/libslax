
================
Additional Notes
================

This section contains additional notes regarding SLAX, libslax, its
history, problems, and future.

Language Notes
--------------

This section includes discussion of SLAX language issues.

Type Promotion for XPath Expressions
++++++++++++++++++++++++++++++++++++

XPath expressions use a style of type promotion that coerces values
into the particular type needed for the expression.  For example, if
a predicate refers to a node, then that predicate is true if the node
exists.  The value of the node is not considered, just it's
existence.

For example, the expression "chapter[section]" selects all
chapters that have a section element as a child.

Similarly, if a predicate uses a function that needs a string, the
argument is converted to a string value by concatenating all the text
values of that node and all that node's child elements.

For example, the expression "chapter[starts-with(section, 'A')]" will
inspect all <chapter> elements, convert their <section> elements to
strings, and select those whose string value starts with 'A'.  This
may be an expensive operation.

.. _ternary-operator:

The Ternary Conditional Operator
++++++++++++++++++++++++++++++++

As the last touch to SLAX-1.1, I've added the ternary operator from C,
allowing expressions like::

    var $a = $b ? $c : $d;
    var $e = $f ?: $g;

The caveat is that this uses an extension function slax:value() which
may not be available in all XSLT environments. Coders must consider
whether should a restriction deems this operator unusable. Portability
considerations are identical to mutable variables (mvars).

Mutable Variables
+++++++++++++++++

XSLT has immutable variables. This was done to support various
optimizations and advanced streaming functionality. But it remains one
of the most painful parts of XSLT. We use SLAX in JUNOS and provide
the ability to perform XML-based RPCs to local and remote JUNOS
boxes. One RPC allows the script to store and retrieve values in an
SNMP MIB (the jnxUtility MIB). We have users using this to "fake"
mutable variables, so for our environment, any theoretical arguments
against the value of mutable variables are lost. They are happening,
and the question becomes whether we want to force script writers into
mental anguish to allow them.

Yes, exactly. That was an apologetical defense of the following code,
which implements mutable variables. Dio, abbi pieta della mia anima.

The rest of this section contacts mind-numbing comments on the
implementation and inner working of mutable variables.

For the typical scriptor, the important implications are:

- Non-standard feature: mutable variables are not available outside
  the libslax environment. This will significantly affect the
  portability of your scripts. Avoid mutable variables if you want to
  use your scripts in other XSLT implementations or without libslax.

- Memory Overhead: Due to the lifespan of XML elements and RTFs inside
  libxslt, mutable variables must retain copies of their previous
  values (when non-scalar values are used) to avoid dangling
  references. This means that heavy use of mutable variables will
  significantly affect memory overhead, until the mutable variables
  fall out of scope.

- Axis Implications: Since values for mutable variables are copied
  (see above), the operations of axes will be affected. This is a
  relatively minor issue, but should be noted.

Let's consider the memory issues associated with mutable variables.
libxslt gives two ways to track memory WRT garbage collection:

contexts
  as RTF/RVT (type XPATH_XSLT_TREE)

variables
  strings (simple; forget I even mentioned them) or node
  sets (type XPATH_NODESET)

via the nodesetval field
  does not track nodes, but references nodes in other trees

The key is that by having node sets refer to nodes "in situ" where
they reside on other documents, the idea of refering to nodes in the
input document is preserved. Node sets don't require an additional
memory hook or a reference count.

The key functions here are xmlXPathNewValueTree() and
xmlXPathNewNodeSet(). Both return a fresh xmlXPathObject, but
xmlXPathNewValueTree will set the xmlXPathObject's "boolval" to 1,
which tells xmlXPathFreeObject() to free the nodes contained in a
nodeset, not just the nodeTab that holds the references.

Also note that if one of the nodes in the node set is a document (type
XML_DOCUMENT_NODE) then xmlFreeDoc() is called to free the
document. For RTFs, the only member of the nodeset is the root of the
document, so freeing that node will free the entire document.

All this works well for immutable objects and RTFs, but does not allow
my mutable variables to work cleanly. This is quite annoying.

I need to allow a variable to hold a nodeset, a document, or a scalar
value, without caring about the previous value. But I need to hold on
to the previous values to allow others to refer to them without
dangling references.  Dangling References

Consider the following input document::

    <top>
        <x1/>
        <x2/> 
        <x3/>
    </top>

The following code makes a nodeset (type XPATH_NODESET) whose nodeTab
array points into the input document::

    var $x = top/*[starts-with("x", name())];

The following code make an RTF/RVT (type XPATH_XSLT_TREE), whose
"fake" document contains a root element (type XML_DOCUMENT_NODE) that
contains the "top" element node.

::

    var $y = <top> {
        <x1>;
        <x2>;
        <x3>;
    }

The following code makes a nodeset (type XPATH_NODESET) that refers to
nodes in the "fake" document under $y::

    var $z = $y/*[starts-with("x", name())];

Now consider the following code::

    mvar $z = $y/*[starts-with("x", name())];
    var $a = $z[1];
    if ($a) {
        set $z = <rvt> "a";  /* RVT */
        var $b = $z[1];      /* refers to nodes in "fake" $y doc */
        set $z = <next> "b"; /* RVT */
        var $c = $z[1];      /* refers to node in <next> RVT */
        <a> $a;
        <b> $b;
        <c> $c;
    }

In this chunk of code, the changing value of $z cannot change the
nodes recorded as the values of $a, $b, or $c. Since I can't count on
the context or variable memory garbage collections, my only choice is
to roll my own. This is quite annoying.

The only means of retaining arbitrary previous values of a mutable
variable is to have a complete history of previous values.

The "overhead" for an mvar must contain all previous values for the
mvar, so references to the node in the mvar (from other variables)
don't become dangling when those values are freed. This is not true
for scalar values that do not set the nodesetval field.

Yes, this is pretty much as ugly as it sounds. After a variable has
been made, it cannot be changed without being risking impacting
existing references to it.

So a mutable variable needs to make two things, a real variable, whose
value can be munged at will, and a hook to handle memory management.

The Rules

- Assigning a scalar value to an mvar just sets the variables value
  (var->value).

- Assigning a non-scalar value to an mvar means making deep copy,
  keeping this copy in "overhead".

But where does the "overhead" live?

In classic SLAX style, the overhead is kept in a shadow variable. The
shadow variable (svar) holds an RTF/RVT that contains all the nodes
ever assigned to the variable, a living history of all values of the
variable.

We don't need to record scalar values, so::

    mvar $x = 4;

becomes::

    <xsl:variable name="slax-x"/>
    <xsl:variable name="x" select="4"/>

But for RTFs, the content must be preserved, so::

    mvar $x = <next> "one";

becomes::

    <xsl:variable name="slax-x">
        <next>one</next>
    </xsl:variable>
    <xsl:variable name="x" select="slax:mvar-init($slax-x)"/>

where slax:mvar-init() is an extension function that returns the value
of another variable, either as a straight value or as a nodeset.

If an mvar is only ever assigned scalar values, the svar will not be
touched. When a non-scalar value is assigned to an mvar, the content
is copied to the svar and the mvar is given a nodeset that refers to
the content inside the svar. Appending to a mvar means adding that
content to the svar and then appending the node pointers to the mvar.

If the mvar has a scalar value, appending discards that value. If the
appended value is a scalar value, then the value is simply assigned to
the mvar. This will be hopelessly confusing, but there's little that
can be done, since appending to an RTF to a number or a number to an
RTF makes little sense. We will raise an error for this condition, to
let the scriptor know what's going on.  Memory

When the mvar is freed, its "boolval" is zero, so the nodes are not
touched but the nodesetval/nodeTab are freed. When the svar is freed,
its "boolval" is non-zero, so xmlXPathFreeObject will free the nodes
referenced in the nodesetval's nodeTab. The only node there will be
the root document of a "fake" RTF document, which will contain all the
historical values of the mvar. In short, the normal libxslt memory
management will wipe up after us.  Implications

The chief implications are:

- memory utilization -- mvar assignments are very sticky and only
  released when the mvar (and its svar) go out of scope

- axis -- since the document that contains the mvar contents is a
  living document, code cannot depend on an axis staying
  unchanged. I'm not sure of what this means yet, but following::foo
  is a nodeset that may change over time, though it won't change
  once fetched (e.g. into a specific variable).

Historical Notes
----------------

This section discusses some historical issues with SLAX and libslax.

Why on earth did you make SLAX?
+++++++++++++++++++++++++++++++

I have worked with XSLT for over ten years, as part of my work for
Juniper Networks. Beginning in 2001, we made an XML API for our line
of routers so that any command that can be issued at the command line
(CLI) can be issued as an XML RPC and the response received in
XML. This work was the foundation for the IETF NETCONF protocol
(RFC6241) (see also RFC6244).

Internally, we used this API with XSLT to make our Junoscope network
management platform, and were happy working with XSLT using multiple
XSLT implementations.

In the 2005-2006 timeframe, we started developing on-box script
capabilities using XSLT. I like the niche and properties of XSLT, but
the syntax makes development and maintenance problematic. First class
constructs are buried in attributes, with unreadable
encodings. Customers objections were fairly strong, and they asked for
a more perl-like syntax. SLAX was our answer.

SLAX simplifies the syntax of XSLT, making an encoding that makes
scripts more readable, maintainable, and helps the reader to see
what's going on. XML escaping is replaced by unix/perl/c-style
escaping. Control elements like <xsl:if> are replaced with the
familiar "if" statement. Minor details are more transparent.

The majority of our scripts are simple, following the pattern::

    if (find/something/bad) {
        call error($message = "found something bad");
    }

The integration of XPath into familiar control statements make the
script writers job fairly trivial.

At the same time, using XSLT constrains our scripting environment and
limits what scripts can and cannot do. We do not need to worry about
system access, processes, connections, sockets, or other features that
are easily available in perl or other scripting languages. The scripts
emit XML that instructs our environment on what actions to take, so
those actions can be controlled.

So SLAX meets our needs.  I hope making this an open source projects
allows it to be useful to a broader community.

Why the name conflict?
++++++++++++++++++++++

The SLAX language is named for "eXtensible Stylesheet Language Alternate
syntaX". Juniper started development on SLAX as part of the on-box
scripting features in the 2004/2005 time frame. The name "SLAX" was
adopted after the Juniper management requested that we remove the
leading "X" from the original internal name.

What about the SLAX linux distro?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

At about this same time, the "SLAX" linux distro was named, but not
being involved in the linux world (we're a FreeBSD house), we were not
aware of this name conflict for many years.

When we were made aware of the name conflict, we consulted with
various parts of the Juniper family, and no one was interested in
changing the language name. We repeated this procedure as we were
publishing this open source version, but again, no one was interested
in doing the internal and external work to change the language name,
since the name conflict was considered minor and not an issue for our
customers.

Developers Notes
----------------

This section contains notes for developers wanting to work on or near
libslax.

Dynamic Extension Libraries
+++++++++++++++++++++++++++

libslax provides a means of dynamically loading extension libraries
based on the contents of the "extension-element-prefixes"
attribute. During initialization, a parsed SLAX document is inspected
for both the "extension-element-prefix" attribute on the <stylesheet>
element, and the "xsl:extension-element-prefix" on arbitrary tags.

The prefixes found in the document are translated into namespace URIs,
which are then escaped using the URL-escaping algorithm that turns
unacceptable characters into three character strings like "%2F".

An extension of ".ext" is appended to the URL-escaped URI and this
file is searched but in ${SLAX_EXTDIR} and any directories given via
the "--lib/-L" argument.

When the extension library is found, we dlopen() it and look for the
magic symbol "slaxDynLibInit". This function is called with two
arguments, the API version number and a struct that contains
information about the extension library.

::

    /*
     * This structure is an interface between the libslax main code
     * and the code in the extension library.  This structure should
     * only be extended by additions to the end.
     */
    typedef struct slax_dyn_arg_s {
        unsigned da_version;   /* Version of the caller */
        void *da_handle;       /* Handle from dlopen() */
        void *da_custom;       /* Memory hook for the extension */
        char *da_uri;          /* URI */
        slax_function_table_t *da_functions; /* Functions */
        slax_element_table_t *da_elements;   /* Elements */
    } slax_dyn_arg_t;

The da_functions and da_elements allow the library to register and
unregister there functions and elements.

At cleanup time, in addition to removing functions and elements, a
search is made for the library to find the symbol
"slaxDynLibClean". If the symbol is found, it is called also.

The file slaxdyn.h defines some macros for helping define extension
libraries.

Default Prefixes for Extension Libraries
++++++++++++++++++++++++++++++++++++++++

When a default prefix is used with an extension library, the prefix is
mapped to the extension using a symbolic link ("ln -s").  The source
of the symlink should be the prefix with a ".prefix" appended and
should be located in the extension directory ("${SLAX_EXTDIR}").  The
target of the symlink should be the namespace URI with the escaping
and ".ext" extension as described above.

See the Makefile.am file in any of the libslax extension directories
for examples of creating the appropriate symlink.

Example
+++++++

Here's a quick example::

    slax_function_table_t slaxBitTable[] = {
        { "and", extBitAnd },
        { "or", extBitOr },
        { "nand", extBitNand },
        { "nor", extBitNor },
        { "xor", extBitXor },
        { "xnor", extBitXnor },
        { "not", extBitNot },
        { NULL, NULL },
    };

    SLAX_DYN_FUNC(slaxDynLibInit)
    {
        /* Fill in our function table */
        arg->da_functions = slaxBitTable;
    
        return SLAX_DYN_VERSION;
    }

External Documentation
----------------------

My documentation style tends to be man-page-like, rather than
tutorial-ish. But folks at Juniper Networks have made some
outrageously great documentation and it's available on the Juniper
website. Many thanks for Curtis Call, Jeremy Schulman, and others for
doing this work.

`Junos Automation Reference for SLAX 1.0`_ does not cover any of
the new SLAX-1.1 or -1.2 material, but is an incredible reference
book.

.. _Junos Automation Reference for SLAX 1.0:
    https://www.juniper.net/us/en/community/junos/training-certification/
    day-one/automation-series/junos-automation-slax/

`Mastering Junos Automation Programming`_ covers many introductory
tasks with a tutorial style.

.. _Mastering Junos Automation Programming:
    https://www.juniper.net/us/en/community/junos/training-certification/
    day-one/automation-series/mastering-junos-automation/

The `Script Library`_ contains a set of scripts that will help get you
started.

.. _Script Library:
  https://www.juniper.net/us/en/community/junos/script-automation/

Note these require Juniper-specific permissions, JNet logins, and
other hurdles. Apologies for the inconvenience.

News! The Day One Guides are available for <$2 (some free) on the
itunes store! Search under books for "juniper".
