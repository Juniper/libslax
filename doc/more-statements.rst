
==========================
Additional SLAX Statements
==========================

This section contains statements and constructs that don't easily
fit into other categories.

.. _pound-bang:

The "#!" Line
-------------

If the first line of a SLAX script begins with the characters "#!",
the rest of that line is ignored.  This allows scripts to be invoked
directly by name on a unix command line.

See :ref:`slaxproc-arguments` for details about the slaxproc utility
argument handling and the "#!" line.

Additional details are available at the following site:

    http://en.wikipedia.org/wiki/Shebang_%28Unix%29

.. admonition:: Potato, tomato

    Personally, I've never heard it pronounced "shebang" before; we
    call it "pound-bang" around here, just as we say "pound define"
    rather than "hash define".

.. _include-import:

Combining Scripts (`include` and `import`)
------------------------------------------

Scripts files can use multiple source files, allowing library files to
be shared among a number of source files.  The `import` and `include`
statements incorporate the templates and functions from one file into
another, allowing them to be used implicitly or explicitly by the XSLT
engine.  Files need not be SLAX files, but can be traditional XSLT
files.

::

    include "foo.slax";
    import "goo.xsl";

A script may include another script using the `include` statement.
Content from included files is conceptually inserted into the script
at the point where the `include` statement is used.  The `import`
statement works similarly, but the contents of an imported file are
given a lower priority (for expression matching and template or
function redefinition).  Conceptually, imported files are inserted at
the top of the script, in the order in which they are given in the
source files.

Import and include documents do _not_ need to be in written in SLAX.
SLAX can import and include normal XSLT documents.

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#section-Combining-Stylesheets

    The `include` and `import` statements mimic the <xsl:include> and
    <xsl:import> elements.  The following is the XSLT equivalent of
    the above example::

        <xsl:include href="foo.slax"/>
        <xsl:import href="goo.xsl"/>

.. index:: statements; key
.. _key:

The `key` Statement
-------------------

The `key` statement defines a key for use by the "key()" XPath
function.  Keys allow a script to locate nodes in a document that are
referenced by other nodes.  Each key definition indicates the nodes on
which the key is defined and the value of the key.  The key() function
can then be used to locate the appropriate node.

::

    SYNTAX::
        'key' key-name '{'
            'match' match-expression ';'
            'value value-expression ';'
        '}'

The `key-name` uniquely identifies the key within the script, and is
passed as the first argument to the key() function.  The `match`
statement gives an XPath expression selecting the set of nodes on
which the keys are found, and the `value` statement gives an XPath
expression for the value of the key.

::

    key func {
        match prototype;
        value @name;
    }

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#key

.. index:: statements; decimal-format
.. _decimal-format:

The `decimal-format` Statement
------------------------------

The `decimal-format` statement defines formatting parameters for use
by the format-number() XPath function.

::

    SYNTAX::
        'decimal-format' format-name ';'
        'decimal-format' format-name '{'
            'decimal-separator' data_value ';'
            'grouping-separator' data_value ';'
            'infinity' data_value ';'
            'minus-sign' data_value ';'
            'nan' data_value ';'
            'percent' data_value ';'
            'per-mille' data_value ';'
            'zero-digit' data_value ';'
            'digit' data_value ';'
            'pattern-separator' data_value ';'
        '}'

The format-name is the name passed as the third argument to the
format-number() XPath function.  The statements under the
`decimal-format` statement follow the meaning of their counterparts
under the <xsl:decimal-format> element, as detailed in the reference
below. 

::

    decimal-format us {
        decimal-separator ".";
        grouping-separator ",";
    }
    decimal-format eu {
        decimal-separator ",";
        grouping-separator ".";
    }

    match / {
        <top> {
            <data> format-number(24535.2, "###.###,00", "eu");
            <data> format-number(24535.2, "###,###.00", "us");
        }
    }

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#format-number

Messages
--------

Typical scripts work by generating XML content as a result tree, but
occasionally a script may need to make explicit, immediate output.
The statements in this section allow for such output.

.. index:: statements; message
.. _message:

The `message` Statement
+++++++++++++++++++++++

The `message` statement allows output to be generated immediately,
without waiting until the script generates its final result tree.

::

    SYNTAX::
        'message' message-expression ';'
        'message' '{'
            block-statements
        '}'

    if (not(valid)) {
        message name() _ " invalid";
    } else if (failed) {
        message {
            expr "Failed";
            if ($count > 1) {
                expr ", again!";
            }
        }
    }

The message-expression is an XPath expression that is emitted as
output, typically on the standard error file descriptor.
Alternatively, a block of statements can be used to generate the
content of the message.  The output of the block will be converted to
a string using the normal XSLT rules.

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#message

    The following is the XSLT equivalent of the above example::

        <xsl:choose>
          <xsl:when test="not(valid)">
            <xsl:message>
              <xsl:value-of
                  select="concat(name(), &quot; invalid&quot;)"/>
            </xsl:message>
          </xsl:when>
          <xsl:when test="failed">
            <xsl:message>
              <xsl:text>Failed</xsl:text>
              <xsl:if test="$count &gt; 1">
                <xsl:text>, again!</xsl:text>
              </xsl:if>
            </xsl:message>
          </xsl:when>
        </xsl:choose>

.. index:: statements; terminate
.. _terminate:

The `terminate` Statement
+++++++++++++++++++++++++

The `terminate` statement can be used to deliver a message to the user
and then exit the script.

::

    SYNTAX::
        'terminate' message-expression ';'
        'terminate' '{'
            block-statements
        '}'

The `terminate` statement mimics :ref:`message`.  The
message-expression is an XPath expression that is emitted as output,
like the `message` statement.  Alternatively, a block of statements
can be used to generate the content of the message.  The output of the
block will be converted to a string using the normal XSLT rules.

After emitting the message, the script stops any further processing.

.. index:: statements; trace
.. _trace:

The `trace` Statement
+++++++++++++++++++++

Trace output is vital to writing, debugging, and maintaining scripts.
SLAX introduces a trace facility that will record XPath expressions or
template contents in a trace file.  If tracing is not enabled, then
the trace template is not evaluated and no trace output is generated.
The enabling of tracing and the naming of trace files is not covered
here, since it is typically a feature of the environment in which a
SLAX script is called.  For example, the `slaxproc` command uses the
`-t` and `-T file` to enable tracing.

::

    SYNTAX::
        'trace' trace-message ';'
        'trace' '{'
            trace-template
        '}'

The trace-message is an XPath expression that is written to the trace
file.  The trace-template is executed and the results are written to
the trace file.

::

    trace "max " _ $max _ "; min " _ $min;
    trace {
        <max> $max;
        <min> $min;
    }
    trace {
        if ($my-trace-flag) {
            expr "max " _ $max _ "; min " _ $min;
            copy-of options;
        }
    }

Since `trace` is non-standard, it can only be used when the associated
extension functions are present, such as with the `libslax` software.

.. index:: statements; output-method
.. _output-method:

The `output-method` Statement
-----------------------------

The `output-method` statement defines the output style to be used when
outputing the result tree.

::

    SYNTAX::
        'output-method' [style] '{'
            'version' data_value ';'
            'encoding' data_value ';'
            'omit-xml-declaration' data_value ';'
            'standalone' data_value ';'
            'doctype-public' data_value ';'
            'doctype-system' data_value ';'
            'cdata-section-elements' cdata_section_element_list ';'
            'indent' data_value ';'
            'media-type' data_value ';'
        '}'

The style can be `xml`, `html`, or `text` (without quotes).

======================== ====================================== 
 Statement                Description                           
======================== ====================================== 
 version                  Version number of output method       
 encoding                 Character set                         
 omit-xml-declaration     (yes/no) Omit initial XML DECL        
 standalone               (yes/no) Emit standalone declaration  
 doctype-public           Public identifier in DTD              
 doctype-system           System identifier in DTD              
 cdata-section-elements   List of elements to use CDATA         
 indent                   (yes/no) Emit pretty indentation      
 media-type               MIME type for document                
======================== ====================================== 

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#output

    The substatements of `output-method` correspond to the attributes of
    the XSLT <xsl:output> element.


.. index:: statements; fallback
.. _fallback:

The `fallback` Statement
------------------------

The `fallback` statement directs the XSLT engine to perform a block of
code when an extension element is invoked which is not supported.

::

    SYNTAX::
        'fallback' '{' statements '}'

    EXAMPLE::
        if ($working) {
            <some:fancy> "thing";
            fallback {
                message "nothing fancy, please";
            }
        }

Whitespace Handling
-------------------

SLAX includes a means of retaining or removing text nodes that contain
only whitespace.  Whitespace for XML is the space, tab, newline or
carriage return characters.

.. index:: statements; strip-space
.. _strip-space:

The `strip-space` Statement
+++++++++++++++++++++++++++

The `strip-space` statement tells the engine to discard the given
elements if they contain only whitespace.

::

    SYNTAX::
        'strip-space' list-of-element-names ';'

The list-of-element-names is a space separated list of element names
that should have their contents discarded if they contain only
whitespace::

    strip-space section paragraph bullet;

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#strip

    The `strip-space` statement mimics the <xsl:strip-space> element.
    The following is the XSLT equivalent of the above example::

        <xsl:strip-space elements="section paragraph bullet"/>

.. index:: statements; preserve-space
.. _preserve-space:

The `preserve-space` Statement
++++++++++++++++++++++++++++++

The `preserve-space` statement works similar to the `strip-space`
statement, but with the opposite result.

::

    SYNTAX::
        'preserve-space' list-of-element-names ';'

The list-of-element-names is a space separated list of element names
that should have their contents retained even if they contain only
whitespace.

::

    preserve-space art picture line;

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#strip

    The `preserve-space` statement mimics the <xsl:preserve-space>
    element.  The following is the XSLT equivalent of the above
    example::

        <xsl:preserve-space elements="art picture line"/>

.. index:: statements; version
.. _version:

The `version` Statement
-----------------------

The `version` statement contains the current version of the SLAX
language, allowing scripts and interpreters to progress independently.
Old engines will not understand new constructs and should stop with an
error when a version number that is unknown to them is seen.  New
engines should accept any previous language version number, so allow
old scripts to run on new engines.

::

    SYNTAX::
        'version' version-number ';'

The version-number should be either "1.2", "1.1" or "1.0".  The
current version is "1.2" and newly developed scripts should use this
version number.

::

    version 1.2;

All SLAX stylesheets must begin with a `version` statement, which
gives the version number for the SLAX language.  This is currently
fixed at "1.2" and will increase as the language evolves.  Version 1.2
is completely backward compatible with version 1.1, which is in turn
completely backward compatible with version 1.0.  Newer versions add
additional functionality that may cause issues when used with earlier
implementations of SLAX.

SLAX version 1.2 implies XML version 1.0 and XSLT version 1.1.

In addition, the "xsl" namespace is implicitly defined (as
'xmlns:xsl="http://www.w3.org/1999/XSL/Transform"').
