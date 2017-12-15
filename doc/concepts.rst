
===============
SLAX (Overview)
===============

SLAX is purely syntactic sugar.  The underlaying constructs are
completely native to XSLT.  All SLAX constructs can be represented in
XSLT.  The SLAX parser parses an input document and builds an XML tree
identical to one produced when the XML parser reads an XSLT document.

SLAX can be viewed as a pre-processor for XSLT, turning SLAX
constructs (like if/then/else) into the equivalent XSLT constructs
(like <xsl:choose> and <xsl:if>) before the real XSLT transformation
engine gets invoked.

The current distribution for libslax is built on libxslt.  SLAX uses
the xsltSetLoaderFunc() in libxslt to tell libxslt to use a different
function when loading script documents.  When xsltParseStylesheetDoc()
loads a script, it then calls the SLAX loader code, which reads the
script, constructs an XML document (xmlDoc) which mimics the document
the normal XML parser would have created for a functionally equivalent
script.  After this document is returned to the existing libxslt code,
normal XSLT processing continues, unaffected by the fact that the
script was written in SLAX.

The "build-as-if" nature of SLAX makes is trivial to write a
SLAX-to-XSLT convertor, which reads in SLAX and emits XSLT using the
normal libxslt file writing mechanism.  In addition, an XSLT-to-SLAX
convertor is also available, which emits an XSLT document in SLAX
format.  These make a great learning aid, as well as allowing
conversion of SLAX scripts to XSLT for environments where libxslt is
not available.

Composite Technologies
----------------------

SLAX builds on three existing technologies, XML, XPath, and XSLT.

XML is a set of encoding rules that allow simplified parsing of
documents, which gives a simple way to represent hierarchical data
sets in text.  XML parsers can be used to encode any type of content,
and their reuse has breed a set of high-quality implementations that
can be easily incorporated into any software project.  Having a single
encoding helps removing parsing errors, increase code reuse, simplify
developer interaction, reduce development costs, and reduce errors.

XPath is an expression language that allows the specification of sets
of nodes within an XML document.  The language is simultaneously
powerfully expressive in terms of the nodes that can be selected, but
also simple and readable.  It follows the path of making simple things
simple, while making hard things possible.

XSLT is a transformation language that turns XML input document into
an XML output document.  The basic model is that an XSLT engine (or
"processor") reads a script (or stylesheet) and an XML document.  The
XSLT engine uses the instructions in the script to process the XML
document by traversing the document's hierarchy.  The script indicates
what portion of the tree should be traversed, how it should be
inspected, and what XML should be generated at each point.

We'll examine each of these technologies in more detail in the
following sections.

XML Concepts
++++++++++++

XML is a set of encoding rules that allow simplified parsing of
documents, which gives a simple way to represent hierarchical data
sets in text.  There are six basic constructs in XML:

Open tags: <foo>
    Starts a level of hierarchy.  An open tag and it's matching close
    tag constitute an XML element.  An element can contain additional
    elements, data, or both.

Close tags: </foo>
    Ends a level of hierarchy.  There must be a close tag for 
    every open tag.

Empty tags: <foo/>
    Equivalent to <foo></foo>.  Functions as a shorthand.

Text: <tag>data</tag>
    Any additional text is data.

Attributes: <tag attribute="value"/>
    Attributes are used to encode name=value pairs inside the open or
    empty tag.  A specific attribute can appear only once per tag.

Namespaces: <prefix:tag xmlns:prefix="URI"/>
    Defines the scope of a tag, as indicated by the prefix.  This
    allows the same tag used in different namespaces to be unique.  The
    URI is not dereferenced, but provides a unique name for the
    namespace.  The "xmlns:" attribute maps the prefix to the given
    URI.

In addition, comments can be added in XML using the delimiters "<!--"
and "-->".  For example::

    <!-- This is a comment -->

From these simple concepts, hierarchical documents are constructed,
such as the following JUNOS configuration file::

    <configuration
           xmlns:junos="http://xml.juniper.net/junos/7.1I0/junos">
      <system>
        <host-name>my-router</host-name> 
        <domain-name>example.net</domain-name> 
        <time-zone>America/New_York</time-zone>
        <name-server>
          <name>10.4.1.40</name>
        </name-server>
        <accounting inactive="inactive">
          <events>login</events>
          <events>change-log</events>
          <events>interactive-commands</events>
          <destination>
            <tacplus>
              <server>
                <name>10.1.1.1</name>
              </server>
              <server>
                <name>10.2.2.2</name>
              </server> 
            </tacplus>
          </destination> 
        </accounting>
        <syslog>
          <archive>
            <size>64k</size>
            <files>5</files>
            <world-readable/>
          </archive>
          <host>
            <name>lhs</name>
            <contents>
              <name>any</name>
              <alert/>
            </contents>
            <explicit-priority/>
          </host>
        </syslog>
      </system>
      <routing-options>
        <static>
          <route>
            <name>0.0.0.0/0</name>
            <next-hop>10.10.1.1</next-hop>
            <retain/>
          </route>
        </static>
        <autonomous-system inactive="inactive">
          <as-number>42</as-number>
          <loops>9</loops>
        </autonomous-system>
      </routing-options>
    </configuration>

See also https://www.w3.org/TR/1998/REC-xml-19980210.

XPath Concepts
++++++++++++++

The XPath expression language allows selection of arbitrary nodes from
with an XML document.  XSLT uses the XPath standard to specify and
locate elements in the input document's XML hierarchy.  XPath's
powerful expression syntax allows complex criteria for selecting
portions of the XML input document.

XPath views every piece of the document hierarchy as a node, including
element nodes, text nodes, and attribute nodes.

An XPath expression can include a path of XML node names, which
select child nodes based on their ancestors' names.  Each member of
the path must match an existing node, and only nodes that match all
path members will be included in the results.  The members of the path
are separated by the slash character ('/').

For example, the following expression selects all <paragraph> elements
that are parented by a <section> element, which in turn must be
parented by a <chapter> element, which must be parented by a <doc>
element::

    doc/chapter/section/paragraph

XSLT views nodes as being arranged in certain "axes".  The "ancestor"
axis points from a node up through it's series of parent nodes.  The
"child" axis points through the list of an element node's direct child
nodes.  The "attribute" axis points through the list of an element
node's set of attributes.  The "following-sibling" axis points through
the nodes which follow a node but are under the same parent, while the
"proceeding-sibling" axis points through the nodes that appear before
a node and are under the parent.  The "descendant" axis contains all
the descendents of a node.  There are numerous other axes which are
not detailed here.

When referring to an axis, use the axis name followed by two colons
followed by the element name (which may include an optional prefix and
it's trailing colon).

There are two axis aliases that make a convenient shorthand when
writing expressions.  The "@" alias refers to the attribute axis,
allowing either "attribute::number" or "@number" to refer to the
"number" attribute.  The "//" alias refers to the "descendant-or-self"
axis, so "doc//paragraph" will find all <paragraph> elements that have
a <doc> element as an ancestor.

See also .....

Each XPath expression is evaluated from a particular node, which is
referred to as the "context node" (or simply "context").  XSLT changes
the context as the document's hierarchy is traversed, and XPath
expressions are evaluated from that particular context node.

XPath expression contain two types of syntaxes, a path syntax and a
predicate syntax.  Path syntax allows traversal along one of the axis
in the document's hierarchy from the current context node.

accounting-options
    selects an element node named "accounting-options" that is a child
    of the current context

server/name
    selects an element node named "name" that is parented by an
    element named "server" that is a child of the current context

/configuration/system/domain-name
    selects an element node named "domain-name" that is parented by
    a "system" element that is parented by a "configuration" element
    that is the root element of the document.

parent::system/host-name
    selects an element node name "host-name" that is parented by an
    element named "system" that is the parent of the current context
    node.  The "parent::" axis can be abbreviated as "..".

A predicate is a boolean test must be satisfied for a node to be
selected.  Each path member of the XPath expression can have zero or
more predicates specified, and the expression will only select nodes
for which all the predicates are true.

The predicate syntax allows tests to be performed at each member of
the path syntax, and only nodes that pass the test are selected.  A
predicate appears inside square brackets ("[]") after a path member.

server[name == '10.1.1.1']
    selects a "server" element that is a child of the current context
    and has a "name" element whose value is '10.1.1.1'

\*[@inactive]
    selects any node ('\*' matches any node) that is a child of the
    current context that has an attribute ('@' selects node from the
    "attribute" axis) named "inactive"

route[starts-with(next-hop, '10.10.')]
    selects a "route" element that is a child of the current context
    and has a "next-hop" element whose value starts with the string
    "10.10."

The "starts-with" function is one of many functions that are built
into XPath.  XPath also supports relational tests, equality tests,
boolean operations, and many more features not listed here.

SLAX defines a concatenation operator "_" that concatenates its two
arguments using the XPath "concat()" function.  The following two
lines are equivalent::

    expr "Today is " _ $date _ ", " _ $user _ "!!";
    expr concat("Today is ", $date, ", ", $user, "!!");

The SLAX statement "expr" evaluates an XPath expression and inserts
its value into the output tree.

XPath is fully described in the W3C's specification,
http://w3c.org/TR/xpath.

XSLT Concepts
+++++++++++++

This section contains some overview material, intended as both
overview and introduction.  Careful reading of the specification or any
of the numerous books on XSLT will certainly be helpful before using
commit scripts, but the information here should provide a starting
point.

XSLT is a language for transforming one XML document into another XML
document.  The basic model is that an XSLT engine (or processor) reads
a script (or stylesheet) and an XML document.  The XSLT engine uses the
instructions in the script to process the XML document by traversing
the document's hierarchy.  The script indicates what portion of the
tree should be traversed, how it should be inspected, and what XML
should be generated at each point.

::

                        +-------------+
                        |  XSLT       |
                        |   Script    |
                        +-------------+
                               |
        +-----------+          |           +-----------+
        |XML Input  |          v           |XML Output |
        |  Document |   +-------------+    |  Document |
        |     i     |-->|  XSLT       |--->|     o     |
        |    /|\    |   |   Engine    |    |    / \    |
        |   i i i   |   +-------------+    |   o   o   |
        |  /| |  \  |                      |  /|\  |   |
        | i i i   i |                      | o o o o   |
        +-----------+                      +-----------+

XSLT has seven basic concepts:

- XPath -- expression syntax for selecting node from the input document
- Templates -- maps input hierarchies to instructions that handle them
- Parameters -- a mechanism for passing arguments to templates
- Variables -- defines read-only references to nodes
- Programming Constructs -- how to define logic in XSLT
- Recursion -- templates that call themselves to facilitate looping
- Context (Dot) -- the node currently be inspected in the input document

XPath has be discussed above.  The other concepts are discussed in the
following sections.  These sections are meant to be introductory in
nature, and later sections will provide additional details and
complete specifications.

Templates
~~~~~~~~~

An XSLT script consists of a series of template definitions.  There are
two types of templates, named and unnamed.  Named templates operate
like functions in traditional programming languages, although with a
verbose syntax.  Parameters can be passed into named templates, and can
be declared with default values.

::

    template my-template ($a, $b = 'false', $c = name) {
      /* ... body of the template goes here ... */
    }

A template named "my-template" is defined, with three parameters, one
of which defaults to the string "false", and one of which defaults to
the contents of the element node named "name" that is a child of the
current context node.  If the template is called without values for
these parameters, the default values will be used.  If no select
attribute is given for a parameter, it defaults to an empty value.

::

    call my-template($c = other-name);

In this example, the template "my-template" is called with the
parameter "c" containing the contents of the element node named
"other-name" that is a child of the current context node.

The parameter value can contain any XPath expression.  If no nodes
are selected, the parameter is empty.

Unnamed Templates
~~~~~~~~~~~~~~~~~

Unnamed templates are something altogether different.  Each unnamed
template contains a select attribute, specifying the criteria for
nodes upon which that template should be invoked.

::

    match route[starts-with(next-hop, '10.10.')] {
      /* ... body of the template goes here ... */
    }

By default, when XSLT processes a document, it will recursively
traverse the entire document hierarchy, inspecting each node, looking
for a template that matches the current node.  When a matching
template is found, the contents of that template are evaluated.

The <xsl:apply-templates> element can be used inside a template to
continue XSLT's default, hierarchical traversal of nodes.  If the
<xsl:apply-templates> element is used with a "select" attribute, only
nodes matching the XPath expression are traversed.  If no nodes are
selected, nothing is traversed and nothing happens.  Without a
"select" attributes, all children of the context node are traversed.
In the following example, a <route> element is processed.  First all
the nodes containing a "changed" attribute are processed under a <new>
element.  Then all children are processed under an <all> element.  The
particular processing depends on the other templates in the script and
how they are applied.

::

    match route {
        <new> {
            apply-templates *[@changed];
        }
        <all> {
            apply-templates;
        }
    }

Named templates can also use the "match" statement to perform dual
roles, so the template can be used via "apply-templates" or be calling
it explicitly.

Parameters
~~~~~~~~~~

Parameters can be passed to either named or unnamed templates using
either parameter lists or the "with" statement.  Inside the template,
parameters must be declared with a "param" statement and can then be
referenced using their name prefixed by the dollar sign.

::

    /*
     * This template matches on "/", the root of the XML document.
     * It then generates an element named "outer", and instructs
     * the XSLT engine to recursively apply templates to only the
     * subtree only "configuration/system".  A parameter called
     * "host" is passed to any templates that are processed.
     */
    match / {
        <outer> {
            apply-templates configuration/system {
                with $host = configuration/system/host-name;
            }
        }
    }
        
    /*
     * This template matches the "system" element, which is the top
     * of the subtree selected above.  The "host" parameter is
     * declared with no default value.  An "inner" element is
     * generated, which contains the value of the host parameter.
     */
    match system {
        param $host;
        <inner> $host;
    }

Parameters can be declares with default values by using the "select"
attribute to specify the desired default.  If the template is invoked
without the parameter, the XPath expression is evaluated and the
results are assigned to the parameter.

::

    match system {
      call report-changed($changed = @changed || ../@changed);
    }

    template report-changed($dot = ., $changed = $dot/@changed) {
      /* ... */
    }

The second template declares two parameters, $dot which defaults to
the current node, and $changed, which defaults to the "changed"
attribute of the node $dot.  This allows the caller to either use a
different source for the "changed" attribute, use the "changed"
attribute but relative to a different node that the current one, or 
use the default of the "changed" attribute on the current node.

Variables
~~~~~~~~~

XSLT allows the definition of both local and global variables, but the
value of variables can only be set when the variable is defined.  After
that point, they are read only.

A variable is defined using the "var" statement.

::

    template emit-syslog ($device, $date, $user) {
        var $message = "Device " _ $device _ " was changed on "
                       _ $date _ " by user '" _ $user _ "'";
        <syslog> {
            <message> $message;
        }
    }

Although this example used an XSL variable, the above example could have
used an XSL parameter for $message, allowing users to pass in their own
message.

Mutable Variables
~~~~~~~~~~~~~~~~~

SLAX adds the ability to assign new values to variables and to append
to node sets.

::

    mvar $count = 10;
    if (this < that) {
        set $count = that;
    }

    mvar $errors;
    if ($count < 2) {
        append $errors += <error> {
            <location> location;
            <message> "Not good, dude.";
        }
    }

Mutable variables can be used like normal variables, including use in
XPath expressions.

Character Encoding
~~~~~~~~~~~~~~~~~~

SLAX supports a C-like escaping mechanism for encoding characters.
The following escapes are available:

============= ============================ 
 Escape        Meaning                     
============= ============================ 
 "\n"          Newline (0x0a)              
 "\r"          Return (0x0d)               
 "\t"          Tab (0x09)                  
 "\xXX"        Hex-based character number  
 "\u+XXXX"     UTF-8 4-byte hex value      
 "\u-XXXXXX"   UTF-8 6-byte hex value      
 "\\"          The backslash character     
============= ============================ 

Other character encodings based on '\' may be added at a later time. 

Programming Constructs
~~~~~~~~~~~~~~~~~~~~~~

XSLT has a number of traditional programming constructs::

    if (xpath-expression) {
        /* Code here is evaluated if the expression is true */
    }

    if (xpath-expression) {
        /*
         * xsl:choose is similar to a switch statement, but
         * the "test" expression can vary among "when" statements.
         */
    
    } else if (another-xpath-expression) {
        /*
         * xsl:when is the case of the switch statement.
         * Any number of "when" statements may appear.
         */
    
    } else {
        /* xsl:otherwise is the 'default' of the switch statement */
    }
    
    for-each (xpath-expression) {
        /*
         * Code here is evaluated for each node that matches 
         * the xpath expression.  The context is moved to the
         * node during each pass.
         */
    }

    for $item (items) {
        /* 
         * Code here is evaluated with the variable $item set
         * to each node that matches the xpath expression.
         * The context is not moved.
         */
    }

    for $i (1 ... 20) {
        /*
         * Code here is evaluated with the variable $i moving
         * thru a sequence of values between 1 and 20.  The
         * context is not changed.
         */
    }

    while ($count < 10) {
        /*
         * Code here is evaluated until the XPath expression is
         * false.  Note that this is normally only useful with
         * mutable variables.
         */
    }

XSLT is a declarative language, mixing language statements (in the
"xsl" namespace) with output elements in other namespaces.  For
example, the following snippet makes a <source> element containing
the value of the "changed" attribute::

    if (@changed) {
        <source> {
            <notify> name();
            if (@changed == "changed") {
                <changed>;
            
            } else {
                <status> $changed;
            }
        }
    }

Recursion
~~~~~~~~~

XSLT depends on recursion as a looping mechanism.  Recursion occurs
when a section of code calls itself, either directly or
indirectly.  Both named and unnamed templates can recurse, and
different templates can mutually recurse, with one calling another
that in turn calls the first.

Care must be taken to prevent infinite recursion.  The XSLT engine used
by JUNOS limits the maximum recursion, to avoid excessive consumption
of system resources.  If this limit is reached, the commit script fails
and the commit is stopped.

In the following example, an unnamed template matches on a <count>
element.  It then calls the "count-to-max" template, passing the value
of that element as "max".  The "count-to-max" template starts by
declaring both the "max" and "cur" parameters, which default to
one.  Then the current value of "$cur" is emitted in an <out>
element.  Finally, if "$cur" is less than "$max", the "count-to-max"
template recursively invokes itself, passing "$cur + 1" as "cur".  This
recursive pass then output the next number and recurses again until
"$cur" equals "$max".

::

    match count {
        call count-to-max($max = count);
    }
    
    template count-to-max ($cur = "1", $max = "1") {
        param $cur = "1";
        param $max = "1";
        
        expr "count: " _ $cur;
        if ($cur < $max) {
            call count($cur = $cur + 1, $max);
        }
    }

Context (Dot)
~~~~~~~~~~~~~

As mentioned earlier, the current context node changes as the
apply-templates logic traverses the document hierarchy and as 
an <xsl:for-each> iterates through a set of nodes that match an XPath
expression.  All relative node references are relative to the current
context node.  This node is abbreviated "." (read: dot) and can be
referred to in XPath expressions, allowing explicit references to the
current node.

::

    match system {
        var $system = .;
        
        for-each (name-server/name[starts-with(., "10.")]) {
            <tag> .;
            if (. == "10.1.1.1") {
                <match> $system/host-name;
            }
        }
    }

This example contains four uses for ".".  The "system" node is saved
in the "system" variable for use inside the "for-each", where the
value of "." will have changed.  The "for-each"'s "select" expression uses
"." to mean the value of the "name" element.  "." is then used to pull
the value of the "name" element into the <tag> element.  The <xsl:if>
test then uses ".", also to reference the value of the current context
node.

Additional Resources
++++++++++++++++++++

The `XPath specification`_ 
and the `XSLT specification`_ are on the `W3C`_ web site.

.. _XPath specification: http://www.w3c.org/TR/xpath
.. _XSLT specification: https://www.w3.org/TR/1999/REC-xslt-19991116
.. _W3C: https://www.w3c.org

Books and tutorials on XSLT abound, helping programmers learn the
technology.  XSLT processors (programs that run XSLT scripts) are
available from both commercial and open-source developers, allowing
users to play with XSLT offline.  IDEs with extended debuggers also
exist.  XSLT is common enough that piecing together a simple script is
easy, as is finding help and advice.

This document lists the SLAX statements, with brief examples followed
by their XSLT equivalents.  The utility of SLAX will hopefully be
appearent, as will the simple transformation that SLAX parsing code is
performing.
