.. templates:

=========
Templates
=========

A SLAX script consists of a set of templates.  There are two types of
templates, match templates and named templates.  This section
discusses each of these types.

Named Templates
---------------

In addition to the template processing, templates can be given
explicit names and called directly, allowing a programming style that
follows more traditional procedural languages.  Named templates are
called like function, returning their XML output nodes to the caller,
where they can be merged into the caller's XML output tree.

.. index:: templates; named
.. index:: statements; template
.. _template:

The `template` Statement
++++++++++++++++++++++++

Named templates are defined using their name and parameters and
invoked using the `call` statement.  The template definition consists
of the `template` keyword, the template name, a set of parameters, and
a braces-delineated block of code.  Parameter declarations can be
either inline, with the parameter name and optionally an equals sign
("=") and a value expression.  Additional parameters can be declared
inside the block using the `param` statement.

The template is invoked using the `call` statement, which consists of
the `call` keyword followed by a set of parameter bindings.  These
binding are a comma-separated list of parameter names, optionally
followed by an equal sign and a value expression.  If the value is not
given, the current value of that variable or parameter is passed,
giving a simple shorthand for passing parameters if common names are
used.  Additional template parameters can be supplied inside the block
using the `with` statement.

.. index:: templates; calling
.. index:: statements; call
.. _call:

The `call` Statement
++++++++++++++++++++

Named templates accept parameters by name, rather than by position.
This means the caller needs to indicate the name of the desired
parameter when passing a value::

    call test($arg1 = 1, $arg4 = 4, $arg2 = 2);

As a convenience, arguments with the same local name as the argument
name can be passed directly::

    var $message = "test " _ $name _ " is " _ $status;
    call emit-message($message)

This is identical to "call emit-message($message = $message)", but is
simpler and the use of common names increased readability.

The call statement can be used as the initial value of a variable or
parameter, without using an enclosing set of braces::

    var $a = call test($a = 1);

.. index:: statements; with
.. _with-call:

Using the `with` Statement with `call`
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In addition, template arguments can be passed using the `with`
statement.  This statement is placed inside braces after the `call`
statement, and gives the parameter name and the value::

    call test {
        with $arg1 = 1;
        with $arg4 = 4;
        with $arg2 = 2;
    }

.. admonition:: Variables Always Use Dollar Signs

    Note that in SLAX parameter names are always prefixed with the
    dollar sign ("$").  This is not true for XSLT, where the dollar
    sign is not used when defining variables.

The `with` statement is described in more detail below (:ref:`with`).

Using Elements As Argument Values
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Beginning with SLAX-1.2, elements may be used directly as argument
values for templates.  Arguments can be either a single element or
a block of SLAX code, placed inside braces::

    call my-template($a = <elt>, $b = <max> 15);
    call test($params = {
        <min> 5;
        <max> 15;
        if ($step) {
            <step> $step;
        }
    }
    call write($content = <content> {
        <document> "total.txt";
        <size> $file/size;
        if (node[@type == "full"]) {
            <full>;
        }
    });

Combination Templates
~~~~~~~~~~~~~~~~~~~~~

While rarely used, XSLT allows named template to be used as match
templates. This is done using the `match` statement after the
`template` statement.  For more information on `match` statements, see
:ref:`match`.

::

    SYNTAX::
        'template' template-name 'match' xpath-expression '{'
            template-contents
        '}'

    template test match paragraph ($message) {
        <message> $message;
    }

    match / {
        call test($message = "called as named template");
        apply-templates {
            with $message = "called as match template";
        }
    } 

Example with XSLT Translation
+++++++++++++++++++++++++++++

This section includes a short example, along with the XSLT into which
the script translates. 

::

    match configuration {
        var $name-servers = name-servers/name;
        call my:thing();
        call my:thing($name-servers, $size = count($name-servers));
        call my:thing() {
            with $name-servers;
            with $size = count($name-servers);
        }
    }

    template my:thing($name-servers, $size = 0) {
        <output> "template called with size " _ $size;
    }

The XSLT equivalent::

    <xsl:template match="configuration">
      <xsl:variable name="name-servers" select="name-servers/name"/>
      <xsl:call-template name="my:thing"/>
      <xsl:call-template name="my:thing">
        <xsl:with-param name="name-servers" select="$name-servers"/>
        <xsl:with-param name="size" select="count($name-servers)"/>
      </xsl:call-template>
      <xsl:call-template name="my:thing">
        <xsl:with-param name="name-servers" select="$name-servers"/>
        <xsl:with-param name="size" select="count($name-servers)"/>
      </xsl:call-template>
    </xsl:template>

    <xsl:template name="my:thing">
      <xsl:param name="name-servers"/>
      <xsl:param name="size" select="0"/>
      <output>
        <xsl:value-of
             select="concat('template called with size ', $size)"/>
      </output>
    </xsl:template>


.. _match-templates:

Match Templates
---------------

The processing model for SLAX is identical to that of XSLT.  A set of
XML input nodes are processed to generate a set of XML output nodes.
This processing begins at the top of the XML input document and
proceeds recursively through the entire document, using rules defined
in the SLAX script, rules imported from other scripts, and a set of
default rules defined by the XSLT specification.

Each rule defines the matching criteria that controls when the rule is
applied, followed by a template for the creation of the XML output
nodes.  The processing engine inspects each node, finds the
appropriate rule that matches that node, executes the template
associated with the rules, builds the XML output nodes, and merges
those nodes with the XML output nodes from other rules to build the
XML output nodes.

.. index:: templates; match
.. index:: statements; match
.. _match:

The `match` Statement
+++++++++++++++++++++

The `match` statement defines a match template, with its matching
criteria and its template.  The keyword `match` is followed by an
XPath expression that selects the nodes on which this template should
be executed.  This is followed by a set of curly braces containing the
template.

::

    SYNTAX::
        'match' xpath-expression '{'
            template-contents
        '}'

The template consists of SLAX code, whose statements are defined later
in this document.

::

    match configuration {
        <error> {
            <message> "System is named " _ system/host-name;
        }
    }

.. index:: templates; applying
.. index:: statements; apply-templates
.. _apply-templates:

The `apply-templates` Statement
+++++++++++++++++++++++++++++++

The `apply-templates` statement instructs the processing engine to
apply the set of templates given in the script to a set of nodes.  The
statement takes as its argument an XPath expression that selects a set
of nodes to use.  If no expression is given, the current node is used.

The set of XML input nodes is processed according to the set of
templates, and the XML output nodes are given to the context in which
the apply-templates statement was issued.

Match templates are applied using the `apply-templates` statement.

::

    match configuration {
        apply-template system/host-name;
    }

    match host-name {
        <hello> .;
    }

.. admonition:: XSLT equivalent

    The following is the XSLT equivalent of the above example::

        <xsl:template match="configuration">
          <xsl:apply-templates select="system/host-name"/>
        </xsl:template>

        <xsl:template match="host-name">
          <hello>
            <xsl:value-of select="."/>
          </hello>
        </xsl:template>

The `apply-templates` statement can also contain two substatements.
The :ref:`mode` statement will limit applied templates to those which contain a matching
`mode` statement::

    main <top> {
        apply-templates;
        apply-templates {
            mode "index";
        }
    }

    match catalog {
        mode "index";
        /* do index-y stuff */
    }

The :ref:`with` statement allows parameters to be passed to match templates::

    apply-templates target {
        with $replace = "some value";
    }

Match templates need a `param` statement to accept parameters.

.. index:: statements; apply-imports
.. _apply-imports:

The `apply-imports` Statement
+++++++++++++++++++++++++++++

The `apply-imports` statement mimics the <xsl:apply-imports> element,
allowing the script to invoke any imported templates.

::

    apply-imports;

.. admonition:: XSLT equivalent

    The following is the XSLT equivalent of the above example::

        <xsl:apply-imports/>

.. index:: templates; mode
.. index:: statements; mode
.. _mode:

The `mode` Statement
++++++++++++++++++++

The `mode` statement allows the apply-template to choice a distinct
set of rules to use during processing.  The argument to the `mode`
statement is a text string that identifies the mode for both the
template and the template processing.  Templates processing will only
select templates that match the current mode value.  If no mode
statement is given with an `apply-templates` invocation, then the
current mode remains in effect.

This statement can appear inside a `match` statement, limited that
template to the given mode, and inside an `apply-templates` statement,
directing that only that templates matching that mode should be used.
The mode is not `sticky`, so additional `apply-templates` statements
should specify the proper mode.

In this example, template processing is invoked twice, first for mode
"one" and then for mode "two".

::

    match * {
        mode "one";
        <one> .;
    }

    match * {
        mode "two";
        <two> string-length(.);
    }

    match / {
        apply-templates version {
            mode "one";
        }
        apply-templates version {
            mode "two";
        }
    }

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#modes

    The the `mode` statement mimics the "mode" attribute of the
    <xsl:template> element.   The following is the XSLT equivalent of
    the above example::

        <xsl:template match="*" mode="one">
          <one>
            <xsl:value-of select="."/>
          </one>
        </xsl:template>

        <xsl:template match="*" mode="two">
          <two>
            <xsl:value-of select="string-length(.)"/>
          </two>
        </xsl:template>

        <xsl:template match="/">
          <xsl:apply-template select="version" mode="one"/>
          <xsl:apply-template select="version" mode="two"/>
        </xsl:template>

.. index:: statements; priority
.. _priority:

The `priority` Statement
++++++++++++++++++++++++

The `priority` statement sets the priority of the template, which is
used as part of the conflict resolution when more that one template
matches a source node.  The highest priority rule is chosen.  The
argument to the `priority` statement is a real number (positive or
negative).  The `priority` statement appears inside a `match`
statement.

In this example, the template is given a high priority::

    match * {
        priority 10;
        <output> .;
    }

.. admonition:: XSLT Equivalent

    https://www.w3.org/TR/1999/REC-xslt-19991116#conflict

    The `priority` statement mimics the `priority` attribute of the
    <xsl:template> element.  The following is the XSLT equivalent of
    the above example::

        <xsl:template match="*" priority="10">
          <output>
            <xsl:value-of select="."/>
          </output>
        </xsl:template>

.. index:: statements; param
.. _param:

The `param` Statement
+++++++++++++++++++++

Template can accept parameters from their callers, and scripts can
accept parameters from the calling environment (typically the command
line).  The `param` statement declares a parameter, along with an
optional default value.

::

    SYNTAX::
        'param' parameter-name ';'
        'param' parameter-name '=' optional-value ';'
        'param' parameter-name '=' '{' value-block '}'

    EXAMPLE::
        param $address = "10.1.2.3";
        param $count = 25;

        templace count {
            /*
             * This defines a local parameter $count and sets
             * its value to that of the global parameter $count.
             */
            param $count = $count;
        }

Like variables, parameters are immutable.  Once created, their value
cannot be changed.  See :ref:`main-var` for a discussion on dealing with
immutable values.

Template parameters can also be defined in a C style following the
template name::

    template area ($width = 10, $length = 10, $scale = 1) {
        <area> $width * $length * $scale;
    }

.. index:: statements; with
.. _with:

The `with` Statement
++++++++++++++++++++

The `with` statement supplies a value for a given parameter.

::

    call area {
        with $length = 2;
        with $width = 100;
        with $max;
    }

Parameter values may also be passed using a C/perl style, but since
arguments in SLAX (and XSLT) are passed by name, the parameter names
are also given::

    call area($width = 100, $length = 2, $max);

If a parameter is not supplied with a value, the current value of that
parameter variable (in the current context) is used, meaning that::

    with $max;

is equivalent to::

    with $max = $max;

Parameters may be passed to match templates using the `with`
statement.  The `with` statement consists of the keyword `with` and
the name of the parameter, optionally followed by an equals sign ("=")
and a value expression.  If no value is given, the current value of
that variable or parameter (in the current scope) is passed, giving a
simple shorthand for passing parameters if common names are used.

::

    match configuration {
        var $domain = domain-name;
        apply-template system/host-name {
            with $message = "Invalid host-name";
            with $domain;
        }
    }

    match host-name {
        param $message = "Error";
        param $domain;
        <hello> $message _ ":: " _ . _ " (" _ $domain _ ")";
    }

.. admonition:: XSLT equivalent

    The following is the XSLT equivalent of the above example::

        <xsl:template match="configuration">
          <xsl:apply-templates select="system/host-name">
            <xsl:with-param name="message" select="'Invalid host-name'"/>
            <xsl:with-param name="domain" select="$domain"/>
          </xsl:apply-templates>
        </xsl:template>

        <xsl:template match="host-name">
          <xsl:param name="message" select="'Error'"/>
          <xsl:param name="domain"/>
          <hello>
            <xsl:value-of select="concat($message, ':: ', ., 
                                        ' (', $domain, ')')"/>
          </hello>
        </xsl:template>

.. index:: statements; main
.. _main-template:

The `main` Template
-------------------

The XSLT programming model used with SLAX calls for a traversal of the
input data hierarchy.  Since the first step is typically the match of
the top of the hierarchy and the creation of the top-level tag of the
output hierarchy.  The `main` statement allows both of these
objectives.  Two forms of the statement are supported, with and
without the output tag.  Without an output element, the `main`
template is equivalent to "match /".  The token `main` is followed by
a block of statements within a set of braces::

    main {
        <top> {
            <answer> 42;
        }
    }

The `main` template can also be used with a top-level output element
following the `main` token.  The element can include attributes.

::

    main <top> {
        <answer> 42;
    }

Both of the preceding examples are equivalent to the following XSLT::

    <xsl:template match="/">
        <top>
            <answer>42</answer>
        </top>
    </xsl:template>
