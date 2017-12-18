.. _SLAX language:

==========================
Building Content with SLAX
==========================

SLAX (and XSLT) is a "declarative" language, meaning that a SLAX
script will describe the output that should be generated, not give
imperative instructions about how to generate that output.  This is
quite different than traditional procedural programming, in both
content and style.

As a SLAX script executes, it uses the description contained in rules
and templates to generate a "result tree", which is a hierarchy of XML
output nodes.  Some logic statements and conditional processing
statements are intermixed with the output description to allow
flexibility in the output generated.

We'll start by examining how SLAX generated these output nodes.

.. index:: operators; "&&"
.. index:: operators; "||"
.. index:: operators; "!"
.. index:: operators; "_"
.. _expressions:

Expressions
-----------

SLAX makes extensive use of the XPath expression language.
Expressions are used to select nodes, specify conditions, and to
generate output content.  Expressions contain five constructs:

- Paths to elements

  - Select nodes based on node names

    - Example: chapter

  - Selects child nodes based on parent node names

    - Example: doc/chapter/section/paragraph

  - Selects child nodes under at any depth

    - Example: doc//paragraph

  - Selects parent nodes relative to the current node

    - Example: ../../chapter

  - Selects attributes using "@" followed by attribute name

    - Example: ../../chapter/\@number

- Predicate tests

  - Selects nodes for which the expression in the square brackets
    evaluates to "true" (with boolean() type conversion)

    - Example chapter[@number == 1]

  - Can be applied to any path member

    - Example: chapter[@number == 1]/section[@number == 2]

  - Use a number to select the <n>th node from a node set

    - Example: chapter[1]

  - Multiple predicate tests can be specified (ANDed together)

    - Example: chapter[@number > 15][section/\@number > 15]

- References to variables or parameters

  - Variable and parameter names start with "$"

    - Example: $this

  - Variables can hold expressions, node sets, etc

    - Example: $this/paragraph

- Literal strings

  - SLAX accepts single or double quotes

    - Example: "test"

  - Character escaping is allowed

    - Example: '\tthat\'s good\n\tnow what?\n'

- Calls to functions

  - Function calls can be used in expressions or predicate tests

    - Example: chapter[count(section) > 15]

  - Allows calls to pre-defined or user-defined functions

    - Example: chapter[substring-before(title, "ne") == "O"]

SLAX follows XPath syntax, with the following additions:

- "&&" may be used in place of the "and" operator
- "||" may be used in place of the "or" operator
- "==" may be used in place of the "=" operator
- "!" may be used in place of the "not()" operator
- "_" is the concatenation operator ("x" _ "y" === concat("x", "y"))
- "?:" is converted into choose/when/otherwise elements

The first four additions are meant to prevent programmers from
learning habits in SLAX that will negatively affect their ability to
program in other languages.  The last two additions are for
convenience.

When referring to XPath expressions in this document, we
mean this extended syntax.

Strings are encoded using quotes (single or double) in a way that will
feel natural to C programmers.  The concatenation operator is
underscore ("_"), which is the new concatenation operator for Perl 6.
(The use of "+" or "." would have created ambiguities in the SLAX
language.)

XPath expression can be added to the result tree using the "expr" and
"uexpr" statements.

In this example, the contents of the <three> and <four> element are
identical, and the <five> element's contents are nearly identical,
differing only in the use of the XPath concat() function::

    <top> {
        <one> "test";
        <two> "The answer is " _ results/answer _ ".";
        <three> results/count _ " attempts made by " _ results/user;
        <four> {
            expr results/count _ " attempts made by " _ results/user;
        }
        <five> {
            expr results/count;
            expr " attempts made by ";
            expr results/user;
        }
        <six> results/message;
    }

.. admonition::  XSLT Equivalent

    The following is the XSLT equivalent of the above example::

        <top>
            <one><xsl:text>test</xsl:text></one>
            <two><xsl:value-of select='concat("The answer is ", 
                                        results/answer, ".")'/></two>
            <three><xsl:value-of select='concat(results/count,
                       " attempts made by ", , results/user)'/></three>
            <four><xsl:value-of select='concat(results/count,
                       " attempts made by ", , results/user)'/></four>
            <five>
                <xsl:value-of select="results/count"/>
                <xsl:text> attempts made by </xsl:text>
                <xsl:value-of select="results/user"/>
            </five>
            <six><xsl:value-of select='results/message'/></six>
        </top>

Using Elements as Function Arguments
++++++++++++++++++++++++++++++++++++

Beginning with SLAX-1.2, elements may be used directly as function
arguments.  Arguments can be either a single element or a block of
SLAX code, placed inside braces.

::

    var $a = my:function(<elt>, <max> 15);
    var $b = my:test({
        <min> 5;
        <max> 15;
        if ($step) {
            <step> $step;
        }
    });
    var $c = my:write(<content> {
        <document> "total.txt";
        <size> $file/size;
        if (node[@type == "full]) {
            <full>;
        }
    });

.. index:: statements; expr

The `expr` Statement
++++++++++++++++++++

The `expr` statement adds an XPath expression to the result tree.  The
expression is the argument to the statement::

    expr "Test: ";
    expr substring-before(name, ".");
    expr status;

.. index:: statements; uexpr

The `uexpr` Statement
+++++++++++++++++++++

The `uexpr` behaves identically to the `expr` statement, except that
the contents are not escaped.  Normally characters like "<", ">", and
"&" are escaped into proper XML (as "&lt;", "&gt;", and "&amp;",
respectively), but uexpr avoids this escaping mechanism.

::

    uexpr "<:-&>";

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#disable-output-escaping

Elements
--------

Elements are the primary encoding mechanism of XML, and can be
combined and arranged to encoding complex hierarchical constructs.

The XML encoding uses three tags: the start tag, the end tag, and the
empty tag.  The start tag consists of the less than character ('<'),
the element name, a set of optional attributes (discussed later), and
the greater than character ('>').  This is followed by the contents of
the XML element, which may include additional elements.  The end tag
consists of the less than character ('<'), the slash character ('/'),
the element name, and the greater than character ('>').  If an element
has no content, an empty tag can be used in place of an open and close
tags.  The empty tag consists of the less than character ('<'), the
element name, a set of optional attributes (discussed later), the
slash character ('/'), and the greater than character ('>').

::

    <doc>
        <chapter>
            <section>
                <paragraph>A brief introduction</paragraph>
            </section>
       </chapter>
       <index/>
    </doc>

.. index:: elements; creating
.. _xml-elements:

XML Elements
++++++++++++

In SLAX, XML elements are encoded using braces instead of the closing
tag.  The element is given, wrapped in chevrons as in XML, but it then
followed by an open brace, the elements contents, and a close brace.
If the element is empty, a semi-colon can be used to signify an empty
element.  If the element contains only a single XPath expression, that
expression can be used in place of the braces and the element is ended
with a single semi-colon.

Elements are written with in a C-like syntax, with only the open tag.
The contents of the tag appear immediately following the open tag.
These contents can either be a simple expression, or a more complex
expression placed inside braces.

The following SLAX is equivalent to the above XML data::

    <doc> {
        <chapter>
            <section> {
                <paragraph> "A brief introduction";
            }
        }
        <index>;
    }

Programmers are accustomed to using braces, indentations, and editor support
to delineate blocks of data.  Using these nesting techniques and
removing the close tag reduces the clutter and increases the clarity
of code.

::

    <top> {
        <one> 1;
        <two> {
            <three> 3;
            <four> 4;
            <five> {
                <six> 6;
            }
        }
    }

This is equivalent to::

    <top>
        <one>1</one>
        <two>
            <three>3</three>
            <four>4</four>
            <five>
                <six>6</six>
            </five>
        </two>
    </top>

.. index:: statements; element
.. index:: elements; by name

The `element` Statement
+++++++++++++++++++++++

The name of an element can be specified by an XPath expression, using
the `element` statement.  The contents of the element must be placed
inside a set of braces.

In this example, the value of the "name" node (rather than the literal
string "name") will be used to create an XML element, whose contents
are an empty element with a name of "from-" concatenated with the
value of the address node and an emply element whose name is the value
of the variable "$my-var" (refer to :ref:`main-var` for more
information about variables).  Node values are selected from the
current context.

::

    element name {
        element "from-" _ address;
        element $my-var;
    }

.. index::
   pair: elements; json

.. _json-elements:

JSON Elements
+++++++++++++

XML elements can also be specified using a JSON-like style, where
quoted strings name the element, followed by a colon and the contents
of the element.  The contents can be any of the following:

========= =====================================
 Type      Examples                            
========= =====================================
 String    "test", "when in the course", "42"  
 Number    42, 1.5, 2e45.5                     
 boolean   true, false                         
 null      null                                
 Array     [ 1, 2, 3, 4, 5 ]                   
 Object    { "this": { "that": "the other" } } 
========= =====================================

Hierarchical nesting is done using objects::

    "top" : {
        "one": 1,
        "two": {
            "three": 3,
            "four": 4,
            "five": {
                "six: 6
            }
        }
    }

This would generate XML equivalent to the examples in :ref:`xml-elements`.

.. index::
   pair: attributes; json

.. _json-attributes:

Attributes for JSON Elements
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Since there is a mismatch between the data encoding capabilities in
XML and JSON, a set of attributes can be used to control the exact
rendering of JSON data.

=========== ======== ============================================
 Attribute   Value    Description                                
=========== ======== ============================================
 name        string   A name to use in place of the element name 
 type        string   An indication of the desired encoding      
=========== ======== ============================================

The value of the "type" attribute must be one of the subset listed
below.  This allows SLAX to distinguish between '{ "value": "5" }' and
'{ "value": 5 }' to control the encoding of the value as a string or a
number.  Similarly the caller can control whether "null" is a string
or the special null value.  If the type attribute is missing, the
element is assumed to be a simple field.

======== =================================
 Type     Description                     
======== =================================
 array    Contains a JSON array           
 false    Contains the JSON "false" value 
 null     Contains the JSON "null" value  
 member   Is a member of an array         
 number   Contains a number value         
 true     Contains the JSON "true" value  
======== =================================

In this example, the type attribute is used to indicate the encoding of
JSON in XML.

::

    JSON:
      "name": "Skip Tracer",
      "location": "The city that never sleeps", 
      "age": 5,
      "real": false, 
      "cases": null, 

    XML:
      <name>Skip Tracer</name>
      <location>The city that never sleeps</location>
      <age type="number">5</age>
      <real type="false">false</real>
      <cases type="null">null</cases>

.. index:: json; arrays
.. _json-arrays:

Encoding JSON Arrays in XML
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The mismatch of capabilities between JSON and XML also requires the use of
alternative encodings for arrays.  The <member> element is used to
contain member (type="member" of an array (of type="array").

In this example, alternative encodings are used for array values.
Also the "type" attribute is used to hold the JSON type::

    "book" : {
        "topics": [ "colors", "fish", "spots", "stars", "cars" ],
        "pages": 63,
        "counts": [
            { "red": 1 },
            { "blue": 1 },
            { "sad": "some" },
            { "glad": "some" },
        ]
    }

The equivalent in SLAX elements would be::

    <book> {
        <topics type="array"> {
            <member type="member"> "colors";
            <member type="member"> "fish";
            <member type="member"> "spots";
            <member type="member"> "stars";
            <member type="member"> "cars";
        }
        <pages type="number"> "63";
        <counts type="array"> {
            <member type="member"> {
                <red type="number"> "1";
            }
            <member type="member"> {
                <blue type="number"> "1";
            }
            <member type="member"> {
                <sad> "some";
            }
            <member type="member"> {
                <glad> "some";
            }
        }
    }

.. admonition:: XML Equivalent

    The following is the XML equivalent of the above example::

        <book>
          <topics type="array">
            <member type="member">colors</member>
            <member type="member">fish</member>
            <member type="member">spots</member>
            <member type="member">stars</member>
            <member type="member">cars</member>
          </topics>
          <pages type="number">63</pages>
          <counts type="array">
            <member type="member">
              <red type="number">1</red>
            </member>
            <member type="member">
              <blue type="number">1</blue>
            </member>
            <member type="member">
              <sad>some</sad>
            </member>
            <member type="member">
              <glad>some</glad>
            </member>
          </counts>
        </book>

.. index:: json; invalid names
.. _json-names:

Encoding Invalid JSON Names
~~~~~~~~~~~~~~~~~~~~~~~~~~~

In addition, JSON allows values for the names which an invalid in
XML.  When this occurs, an element named "element" is used to hold the
content with the "name" attribute containing the real name::

    JSON style:
        "": {
            "<>": "<>"
        }

    SLAX style:
        <element name=""> {
            <element name="<>"> "<>";
        }

    XML content:
        <element name="">
            <element name="&lt;&gt;">&lt;&gt;</element>
        </element>

.. index:: attributes

Attributes
----------

XML allows a set attribute value to be specified on an open or empty
tag.  These attributes are an unordered set of name/value pairs.  The
XML encoding uses the attribute name, an equals sign ('='), and the
value of the attribute in quotes::

    <chapter number="1" title="Introduction" ref="intro">
        <section number="1">
            <!-- .... content ... -->
        </section>
    </chapter>

.. index:: attributes; concept

XML Attributes
++++++++++++++

SLAX uses an identical encoding scheme for attributes::

    <chapter number="1" title="Introduction" ref="intro"> {
        <section number="1"> {
            /* .... content ... */
        }
    }

In addition, XSLT introduces a means of assigning attribute values,
called "attribute value templates" (AVT).  SLAX encodes these as XPath
values for attributes:

Attributes on elements follow the style of XML, with the attribute
name, an equals sign, and the value of the attribute.

::

    <element attr1="one" attr2="two">;

In this example, the attributes are assigned values using XPath
values.  The "tag" node value is assigned to the "ref" attribute,
while the "title" attribute is assigned the value of the "$title"
variable. 

::

    <chapter number=position() title=$title ref=tag> {
        <section number=position()> {
            /* .... content ... */
        }
    }

Where XSLT allow attribute value templates using curly braces, SLAX
uses the normal expression syntax.  Attribute values can be any XPath
expression, including quoted strings, parameters, variables, and
numbers, as well as the SLAX concatenation operator ("_").

::

    <location state=$location/state
              zip=$location/zip5 _ "-" _ $location/zip4>;

The XSLT equivalent::

    <location state="{$location/state}"
              zip="{concat($location/zip5, "-", $location/zip4}"/>

Note that curly braces placed inside SLAX quoted strings are not
interpreted as attribute value templates, but as real braces and are
escaped when translated into XSLT.

::

    <avt sign="{here}">;

The XSLT equivalent::
   
    <avt sign="{{here}}"/>


.. index:: attributes; creating
.. index:: statements; attribute
.. _attribute:

The `attribute` Statement
+++++++++++++++++++++++++

The name of an attribute can be specified by an XPath expression, using
the `attribute` statement.  The contents of the attribute must be placed
inside a set of braces.

In this example, the value of the "name" node (rather than the literal
string "name") will be used to create an XML attribute, whose contents
are an empty element with a name of "from-" concatenated with the
value of the address node.  Node values are selected from the current
context.

::

    attribute name {
        expr "from-" _ address;
    }

.. index:: attributes; attribute-set

Attribute Sets
--------------

Attribute sets can be used to define sets of attributes that can be
used together.  An attribute set contains a set of `attribute`
statements that define the names and values of attributes.  These
attributes can then be referred to as a collection rather than repeat
the contents in the script.

.. admonition:: FAQ: Do people really use this?

    My guess (which could be wildly wrong) is that this is a rarely
    used feature that are created to cover the condition that a
    template cannot return a set of attributes.  That's my guess
    anyway.

.. index:: statements; attribute-set
.. _attribute-set:

The `attribute-set` Statement
+++++++++++++++++++++++++++++

The `attribute-set` statement defines a set of attributes that can be
used repeatedly.  The argument is the name of the attribute set, and
the contents are a set of attribute statements.

::

    attribute-set table-attributes {
        attribute "order" { expr "0"; }
        attribute "cellpadding" { expr $cellpadding; }
        attribute "cellspacing" { expr $cellspacing; }
    }

.. index:: statements; use-attribute-sets
.. _use-attribute-sets:

The `use-attribute-sets` Statement
++++++++++++++++++++++++++++++++++

The `use-attribute-sets` statement adds the attributes from a given
set to the current element.

::

    <table> {
        use-attribute-sets table-attributes;
    }

The use-attribute-sets statement can be used under the `element`,
`copy-node`, and `attribute-sets` statements, as well as under a
normal element.

.. index:: namespaces

Namespaces
----------

Namespaces map URI strings to prefixes that are used to indicate the
administrative domain of specific XML elements.  The syntax and
semantic constraints for the element <size> will be distinct depending
on the namespace under which it is defined.

The URI is a string that uniquely identifies the namespace.  Here are
some examples:

Example namespaces::

  http://xml.juniper.net/junos                    
  http://www.example.com/example-one              
  http://www.w3.org/1999/XSL/Format               
  http://www.w3.org/Graphics/SVG/SVG-19990812.dtd 

The prefix is a string pre-pended to the local element name with a
colon.  Prefixes map to namespaces and are used as "shorthand" for the
underlaying namespace.

================ =======================
 Example Prefix   Example Element Usage 
================ =======================
 junos            <junos:name/>         
 example          <example:name/>       
 fo               <fo:name/>            
 svg              <svg:name/>           
================ =======================

.. index:: statements; ns
.. _ns:

The `ns` Statement
++++++++++++++++++

The `ns` statement defines a mapping from a prefix to a URI namespace
identifier.  Namespaces must be defined prior to their use.

By default, elements are in the "null" namespace, but the `ns`
statement can be used to change the namespace for unprefixed elements.

The syntax of the `ns` statement is::

    ns [<prefix> <options> = ] <uri>;

If a prefix is the value prefixed to element names to indicate their
namespace should be that of the given URI.   If no prefix is given,
the given URI will be applied to all elements that do not include a
prefix.  The values and meanings of <options> are detailed below.

::

    ns junos =  "http://xml.juniper.net/junos";
    ns example = "http://www.example.com/example-one";
    ns fo = "http://www.w3.org/1999/XSL/Format";
    ns svg = "http://www.w3.org/Graphics/SVG/SVG-19990812.dtd";

In this example, a default namespace is defined, as well as a
namespace mapped to the "test" prefix::

    ns "http://example.com/main";
    ns test = "http://example.com/test";

The XML equivalent is::

    <some-element xmlns="http://example.com/main"
            xmlns:test="http://example.com/test"/>

Namespace definitions are supplied using the `ns` statement.  This
consists of either the `ns` keyword, a prefix string, an equal sign
and a namespace URI or the `ns` keyword and a namespace URI.  The
second form defines the default namespace.

::

    ns junos = "http://www.juniper.net/junos/";

The `ns` statement may appear either following the `version` statement
at the beginning of the stylesheet or at the beginning of any block.

::

    ns a = "http://example.com/1";
    ns "http://example.com/global";
    ns b = "http://example.com/2";

    match / {
        ns c = "http://example.com/3";
        <top> {
            ns a = "http://example.com/4";
            apply-templates commit-script-input/configuration;
        }
    }

When appearing at the beginning of the stylesheet, the ns statement
may include either the `exclude` or `extension` keywords.  These
keywords instruct the parser to add the namespace prefix to the
"exclude-result-prefixes" or "extension-element-prefixes" attribute::

    ns exclude foo = "http://example.com/foo";
    ns extension jcs = "http://xml.juniper.net/jcs";

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#namespaces

    The following is the XSLT equivalent of the above example::

        <xsl:stylesheet xmlns:foo="http://example.com/foo"
                        xmlns:jcs="http://xml.juniper.net/jcs"
                        exclude-result-prefixes="foo"
                        extension-element-prefixes="jcs">
            <!-- ... -->
        </xsl:stylesheet>

.. index:: statements; extension
.. _extension:

The `extension` Statement
+++++++++++++++++++++++++

The `extension` statement instructs the processing engine that
extenstion namespaces, which will cause elements in that namespace to
be given special handling by the engine.

For libslax, the extension keyword instructs the library to search for
an extension library associated with the namespace.  If found, the
extension library is loaded and initialized so the script can use
functions and elements defined within that library.

::

    ns pref extension = "http://some.example.com";

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#extension-element

.. index:: statements; exclude
.. _exclude:

The `exclude` Statement
+++++++++++++++++++++++

The `exclude` statement prevents the namespace from appearing in the
final result tree, effectively blocking it from output.

::

    ns xslax extension = "http://libslax.org/slax";

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#literal-result-element

Reserved Prefixes
+++++++++++++++++

The XML specification reserved all prefixes and attributes beginning
with the characters "xml".  In addition, SLAX reserves all prefixes
and attributes that begin with the characters "slax".  These
reservations help to future proof against changes and enhancements to
the language.

Default Namespaces for Prefixes
+++++++++++++++++++++++++++++++

When a prefix is used without a corresponding `ns` statement in
scope, SLAX will refer to a set of default namespaces.  If the prefix
has a default namespace, that namespace will be automatically mapped
to the prefix.

The following table lists the default set of prefixes installed with
the libslax software distribution:

======== ========= ====================================
 Prefix   Source    URI                                
======== ========= ====================================
 bit      libslax   http://xml.libslax.org/bit         
 curl     libslax   http://xml.libslax.org/curl        
 db       libslax   http://xml.libslax.org/db          
 exsl     exslt     http://exslt.org/common            
 crypto   exslt     http://exslt.org/crypto            
 math     exslt     http://exslt.org/math              
 set      exslt     http://exslt.org/sets              
 func     exslt     http://exslt.org/functions         
 str      exslt     http://exslt.org/strings           
 date     exslt     http://exslt.org/dates-and-times   
 dyn      exslt     http://exslt.org/dynamic           
 saxon    libxslt   http://icl.com/saxon               
 os       libslax   http://xml.libslax.org/os          
 xutil    libslax   http://xml.libslax.org/xutil       
======== ========= ====================================

The libslax extension libraries are documented in this document under
the :ref:`libslax-extensions` section.  The exslt extension libraries
are documented at http://exslt.org and the libxslt extension library
is documented at http://xmlsoft.org.

When using the `slaxproc` tool with the `--format` or `--slax-to-xslt`
command line options, the namespace will be properly displayed::

    % cat /tmp/foo.slax
    version 1.1;
   
    match / {
        <top> {
            expr date:time();
        }
    }
    % slaxproc --format /tmp/foo.slax
    version 1.1;

    ns date extension = "http://exslt.org/dates-and-times";

    match / {
        <top> date:time();
    }

Processing Instructions
-----------------------

An XML processing instruction is a mechanism to convey
application-specific information inside an XML document.  The
application can detect processing instructions and change behaviour
accordingly.

.. index:: statements; processing-instruction
.. _processing-instruction:

The `processing-instruction` Statement
++++++++++++++++++++++++++++++++++++++

The `processing-instruction` statement adds a processing instruction
to the result tree.  The argument to the statement is that name of the
processing instruction and the contents of the statement (within
braces) is the value of that instruction.

::

    processing-instruction "my-app" {
        expr "my-value";
    }

Both the argument and the value may be XPath expressions.

Comments
--------

Comments are information for the user or author.  They are not formal
content and should not be inspected or parsed.  They can be discarded
without affecting the content of the XML.

::

    <!-- This is an XML comment -->

SLAX comments are distinct from XML comments.  SLAX comments appear as
part of the SLAX script, and are not part of either the input or
output XML documents.  SLAX comments follow the C/Perl style of a "/*"
followed by the comment, terminated by "*/".

::

    /* This is a SLAX comment */

XML comments may not appear inside a SLAX script.

Comments in SLAX are entered in the traditional C style, beginning the
"/*" and ending with "*/".  These comments are preserved in the
in-memory XML tree representation of the SLAX script.  This allows
comments to be preserved if the tree is emitted using the normal
XML output renderer.

::

    /*
     * This is a SLAX comment.
     */

.. admonition:: XSLT Equivalent

    The following is the XSLT equivalent of the above example::

        <!-- /*
         * This is a SLAX comment
         */ -->

.. index:: statements; comment
.. _comment:

The `comment` Statement
+++++++++++++++++++++++

The `comment` statement adds an XML comment to the result tree.  The
argument is an XPath expression containing the comment to be added::

    comment "This script was run by " _ $user _ " on " _ $date;
    comment "Added by user " _ $user _ " on " _ $date;

.. admonition:: XSLT Equivalent

    The `comment` statement mimics the <xsl:comment> element.  The
    following is the XSLT equivalent of the above example::

        <xsl:comment>
          <xsl:value-of 
               select='concat("Added by user ", $user, " on ", $date)'/>
        </xsl:comment>

Copying Content
---------------

On many occasions, parts of the input XML document will be copied to
the output XML document.  Such copies can be deep or shallow, meaning
that the entire node hierarchy is copied or just the node itself.
SLAX contains two distinct statements for these two styles of
copying. 

.. index:: statements; copy-of
.. _copy-of:

The `copy-of` Statement
+++++++++++++++++++++++

The `copy-of` statement performs a deep copy of a given set of nodes.
The argument to the statement is an XPath expression specifying which
nodes should be copied.

::

    copy-of $top/my/stuff;
    copy-of .;
    copy-of configuration/protocols/bgp;

See also copy-node (:ref:`copy-node`).

.. admonition:: XSLT Equivalent

    The `copy-of` statement mimics the functionality of the <xsl:copy-of>
    element.  The following is the XSLT equivalent of the above example::

       <xsl:copy-of select="configuration/protocols/bgp"/>

.. index:: statements; copy-node
.. _copy-node:

The `copy-node` Statement
+++++++++++++++++++++++++

The `copy-node` statement performs a shallow copy of the specific node
to the result tree, along with any namespace nodes, but none other
child nodes (including attribute nodes) are copied.  The contents of
the statement are a template specifying what should be inserted into
the new node.

::

    copy-node {
        <that> "one";
    }

See also copy-of (:ref:`copy-of`).

Formatting
----------

This section contains information about statements that control
formatting of output.  See also the :ref:`output-method` and
:ref:`decimal-format`.

.. index:: statements; number
.. index:: statements; level
.. _number:

The `number` Statement
++++++++++++++++++++++

The `number` statement inserts a generated number into the result
tree.  This statement has two distinct forms.  When used with an
argument, the statement inserts the number given by that XPath
expression, and a set of optional statements can be used to
specify the formatting to be used for that number.

When used without an argument, the number is generated based on
position of the current node in the source document, and a set of
optional statements can be used to specify formatting.

The formatting statements are given in the following table:

==================== =========== ===============================
 Statement            Value       Description                   
==================== =========== ===============================
 format               see below   Style of numbering            
 letter-value                     Not implemented in libxslt    
 grouping-separator   character   Used between groups of digits 
 grouping-size        number      Number of digits in a group   
==================== =========== ===============================

The value of the `format` statement gives the style of numbering, as
#detailed in the following table:

======= =============
 Value   Style       
======= =============
 "1"     1 2 3 4     
 "01"    01 02 03 04 
 "a"     a b c d     
 "A"     A B C D     
 "i"     i ii iii iv 
 "I"     I II III IV 
======= =============

The selection statements used when the number statement has no
argument are given in the following table:

=========== =========== ==============================
 Statement   Value       Description                  
=========== =========== ==============================
 count       XPath       What to count                
 from        XPath       Where to start counting from 
 level       See below   How to count                 
=========== =========== ==============================

The `level` statement indicates how to count tags:

========== ================================
 Value      Behavior                       
========== ================================
 single     Count from first ancestor node 
 multiple   Count from any ancestor node   
 any        Count from any node            
========== ================================

In the following example, the value of $this is formatted with three
digits of output and the number of "section" elements before the
current context value is emitted.

::

    number $this {
        format "001";
    }
    number {
        count section;
    }

See also the format-number() XPath function.
