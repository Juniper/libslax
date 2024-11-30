
======================
Programming Constructs
======================

This section covers the programming constructs in SLAX.  SLAX tries to
use language paradigms and constructs from traditional languages
(primarily C and Perl) to aid the user in readability and familiarity.
But SLAX is built on top of XSLT, and the declarative nature of XSLT
means that many of its constructs are visible.

.. index:: variables
.. _main-var:

Variables
---------

Variables in SLAX are immutable.  Once defined, the value of a
variable cannot be changed.  This is a major change from traditional
programming language, and requires a somewhat different approach to
programming.  There are two tools to help combat this view: recursion
and complex variable assignments.

Recursion allows new values for variables to be set, based on the
parameters passed in on the recursive call::

    template count ($cur = 1, $max = 10) {
        if $cur < $max {
            <output> $cur;
            call count($cur = $cur + 1, $max);
        }
    }

Complex variable assignments allow the use of programming constructs
inside the variable definition::

    var $message = {
        if $red > 40 {
            expr "Invalid red value";
        } else if $blue > 80 {
            expr "Invalid red value";
        } else if $green > 10 {
            expr "Invalid green value";
        }
    }

This requires different approaches to problems.  For example,
instead of appending to a message as you loop thru a list of nodes,
you would need to make the message initial value contain a `for-each`
loop that emits parts of the message::

    var $message = {
        <output> "acceptable colors: ";
        for-each color {
            if red <= 40 && blue <= 80 && green < 10 {
                <output> "    " _ name _
                   " (" _ red _ "," _ green _ "," _ blue _ ")";
            }
        }
    }

Variable and parameter syntax uses the `var` and `param` statements to
declare variables and parameters, respectively.  SLAX declarations
differ from XSLT in that the variable name contains the dollar sign
even in the declaration, which is unlike the "name" attribute of
<xsl:variable> and <xsl:parameter>.  This was done to enhance the
consistency of the language::

    param $fido;
    var $bone;

An initial value can be given by following the variable name with an
equals sign and an expression::

    param $dot = .;
    var $location = $dot/@location;
    var $message = "We are in " _ $location _ " now.";

.. admonition:: XSLT Equivalent

    The following is the XSLT equivalent of the above examples::

        <xsl:parameter name="fido"/>
        <xsl:variable name="bone"/>

        <xsl:parameter name="dot" select="."/>
        <xsl:variable name="location" select="$dot/location"/>
        <xsl:variable name="message" select="concat('We are in ',
                                               $location, ' now.')"/>

    SLAX is using the same constucts as XSLT, but packaged in a more
    readable, maintainable syntax.

.. index::
    pair: immutable; variables

.. index:: statements; var
.. _var:

The `var` Statement
+++++++++++++++++++

Variable are defined using the `var` statement, which accept a
variable name and an initial value.  Variable names (and parameter
names) always begin with a dollar sign ("$")::

    SYNTAX::
        'var' name '=' initial-value ';'
        'var' name '=' '{' initial-value-block '}'

The initial value can be an XPath expresion, an element, or a block of
statements (enclosed in braces) that emit a set of nodes::

    var $x = 4;
    var $y = <price> cost div weight;
    var $z = {
        expr "this: ";
        copy-of $this;
    }

.. index::
    pair: mutable; variables

.. index:: statements; set
.. index:: statements; append
.. index:: statements; mvar
.. index:: operators; "+="
.. _mvar:

The `mvar` Statement
++++++++++++++++++++

In XSLT, all variables are immutable, meaning that once created, their
value cannot be changed.  This creates a distinct programming
environment which is challenging to new programmers.  Immutable
variables allow various optimizations and advanced streaming
functionality.

Given the use case and scenarios for libslax (especially our usage
patterns in JUNOS), we've added mutable variables, which are variables
that can be changed.  The `set` statement allows a new value to be
assigned to a variable and the `append` statement allows the value to
be extended, with new data appended to it::

    SYNTAX::
        'mvar' variable-name '=' initial-value ';'
        'set' variable-name '=' new-value ';'
        'append' variable-name '+=' new-content ';'

The mvar is typically a node set, and appended adds the new objects to
the nodeset::

    mvar $test;

    set $test = <block> "start here";

    for $item in list {
        append $test += <item> {
            <name> $item/name;
            <size> $item/size;
        }
    }

.. index:: RTF

Result Tree Fragments
---------------------

The most annoying "features" of XSLT is the concept of "Result Tree
Fragments" (aka RTF).  These fragments are produced with nodes are
created that are not directly emitted as output.  The main source is
variable or parameter definitions that have complex content::

    var $x = {
        <color> {
            <name> "cornflower blue";
            <red> 100;
            <green> 149;
            <blue> 237;
        }
    }

Only three operations can be performed on an RTF:

- Emit as output
- Conversion to a string
- Conversion to a proper node-set

In this example, an RTF is generated, and then each of the three valid
operations is performed::

    var $rtf = <rtf> {
        <rats> "bad";
    }
    if $rtf == "bad" { /* Converts the RTF into a string */
        copy-of $rtf;  /* Emits the RTF to the output tree */

        /* Convert RTF to a node set (see discussion below) */
        var $node-set = ext:node-set($rtf);
    }

Any XPath operation performed against an RTF will result in an
"Invalid type" error.

In truth, the only interesting thing to do with an RTF is to convert
it to a node set, which is not a standard XPath/XSLT operation.  Most
scripts will use the extension function "ext:node-set()" (which is
specific to libxslt) or "exslt:node-set()" (which is in the EXSLT
extension library; see http://exslt.org for additional information)::

    ns ext = "http://xmlsoft.org/XSLT/namespace";
    ...
        var $alist = ext:node-set($alist-raw);

This must be done when a variable or paramter has a complex initial
value::

    var $this-raw = <this> {
        <that>;
        <the-other>;
    }
    var $this = ext:node-set($this-raw);

Fortunately for SLAX programmers, the ":=" operator does away with
these conversion issues, as the following section details.

.. index:: operators; ":="
.. _colon-equals:

The ":=" Operator
+++++++++++++++++

The ":=" operator is designed to hide the conversion of RTFs to node
sets from the programmer.  It is used in assigning initial values to
variables and parameters::

    var $this := <this> {
        <that> "one";
        <the-other> "one";
    }
    if $this/that == "one" {
        <output> "not an invalid type error";
    }

Calling named templates can also produce RTFs, since the `call`
statement would be considered complex variable content.  But using the
":=" operator removes this problem::

    var $output := call matching-color($match = "corn");

Behind the scenes, SLAX is performing the ext:node-set() call but the
details are hidden from the user.

Control Statements
------------------

This section gives details and examples for each of the control
statements in SLAX.

In versions of SLAX prior to 1.3, parentheses are required around the
XPath espression used in these statements.  These are optional, in
that we are backward compatible, and scripts that use parens will
continue to work normally.  The only statement with different behavior
is the `for` statement, which uses a new `in` keyword.  Either form is
accepted::

    if condition { <code>; } else if condition { <code>; }
    if (condition) { <code>; } else if (condition) { <code>; }

    for-each list { <code>; }
    for-each (list) { <code>; }

    for $var in list { <code>; }
    for $var (list) { <code>; }

    while condition { <code>; }
    while (condition) { <code>; }

.. index:: statements; if
.. index:: statements; else
.. _if-else:

The `if` and `else` Statements
++++++++++++++++++++++++++++++

SLAX supports an `if` statement that uses a C-like syntax.  The
expressions that appear in parentheses are extended form of XPath
expressions, which support the double equal sign ("==") in place of
XPath's single equal sign ("=").  This allows C programmers to avoid
slipping into dangerous habits::

    if this && that || the/other[one] {
        /* SLAX has a simple "if" statement */
    } else if yet[more == "fun"] {
        /* ... and it has "else if" */
    } else {
        /* ... and "else" */
    }

Depending on the presence of the `else` clause, an `if` statement can
be transformed into either an <xsl:if> element or an <xsl:choose>
element::

    if starts-with(name, "fe-") {
        if mtu < 1500 {
           /* Deal with fast ethernet interfaces with low MTUs */
        }
    } else {
        if mtu > 8096 {
           /* Deal with non-fe interfaces with high MTUs */
        }
    }

.. admonition:: XSLT Equivalent

    The following is the XSLT equivalent of the above example::

        <xsl:choose>
          <xsl:when select="starts-with(name, 'fe-')">
            <xsl:if test="mtu &lt; 1500">
              <!-- Deal with fast ethernet interfaces with low MTUs -->
            </xsl:if>
          </xsl:when>
          <xsl:otherwise>
            <xsl:if test="mtu &gt; 8096">
              <!-- Deal with non-fe interfaces with high MTUs -->
            </xsl:if>
          </xsl:otherwise>
        </xsl:choose>

.. index:: statements; for-each
.. _for-each:

The `for-each` Statement
++++++++++++++++++++++++

The `for-each` statement iterates through the members of a node set,
evaluating the contents of the statement with the context set to each
node::

    SYNTAX::
        'for-each' xpath-expression '{'
            contents
        '}'

The XPath expression is evaluated into a set of nodes, and then each
node is considered as the "context" node, the contents of the
`for-each` statement are evaluated::

    for-each $inventory/chassis/chassis-module
              /chassis-sub-module[part-number == '750-000610'] {
        <message> "Down rev PIC in " _ ../name _ ", "
                     _ name _ ": " _ description;
    }

The `for-each` statement mimics functionality of the <xsl:for-each>
element.  The statement consists of the `for-each` keyword, the
parentheses-delimited select expression, and a block::

    for-each $inventory/chassis/chassis-module
              /chassis-sub-module[part-number == '750-000610'] {
        <message> "Down rev PIC in " _ ../name _ ", "
                     _ name _ ": " _ description;
    }

.. admonition:: XSLT Equivalent

    The following is the XSLT equivalent of the above example::

        <xsl:for-each select="$inventory/chassis/chassis-module
                  /chassis-sub-module[part-number == '750-000610']">
            <message>
                <xsl:value-of select="concat('Down rev PIC in ', ../name,
                                      ', ', name, ': ', description)"/>
            </message>
        </xsl:for-each>

.. index:: statements; for
.. _for:

The `for` Statement
+++++++++++++++++++

In addition to the standard XSLT `for-each` statement, SLAX
incorporates a `for` statement that allows iteration through a node
set without changing the context (".")::

    SYNTAX::
        'for' variable-name 'in' xpath-expression'{'
            contents
        '}'
        'for' variable-name '(' xpath-expression ')' '{'
            contents
        '}'

The variable is assigned each member of the node-set selected by
the expression in sequence, and the contents are then evaluated.

::

    for $item in item-list {
        <item> $item;
    }

Internally, this is translated into normal XSLT constructs involving a
pair of nested for-each loops, one to iterate and one to put the
context back to the previous setting.  This allows the script writer
to ignore the context change.

.. index:: statements; while
.. _while:

The `while` Statement
+++++++++++++++++++++

The `while` statement allows a block of code to be repeated until a
condition is no longer true.  This construct is only useful when
combined with mutable variables (:ref:`mvar`)::

    SYNTAX::
        'while' xpath-expression '{'
            contents
        '}'

The xpath-expression is cast to a boolean type and if true, the
contents are evaluated.  The context is not changed.  This loop
continues until the expression is no longer true.  Care must be
taken to avoid infinite loops::

    mvar $seen;
    mvar $count = 1;
    while !$seen {
        if item[$count]/value {
            set $seen = true();
        }
        set $count = $count + 1;
    }

.. index:: statements; sort
.. _sort:

The `sort` Statement
++++++++++++++++++++

The `for-each` normally considers nodes in document order, but the
`sort` statement indicates the specific order the programmer needs.

The `sort` statement takes an expression argument that is used as the
key, as well as substatements that alter the normal sort behavior.

============ ================================
 Statement    Values
============ ================================
 language     Not implemented in libxslt
 data-type    "text", "number", or qname
 order        "ascending" or "descending"
 case-order   Not implemented in libxslt
============ ================================

Multiple `sort` statements can be used to given secondary sorting keys::

    for-each author {
        sort name/last;
        sort name/first;
        sort age {
            order "descending";
        }
        copy-of .;
    }

.. admonition:: XSLT Equivalent

    The following is the XSLT equivalent of the above example::

        <xsl:for-each select="author">
          <xsl:sort select="name/last"/>
          <xsl:sort select="name/first"/>
          <xsl:sort select="age" order="descending"/>
          <xsl:copy-of select="."/>
        </xsl:for-each>

.. index:: operators; "..."
.. _dotdotdot:

The "..." Operator
++++++++++++++++++

Often a loop is required to iterator through a range of integer
values, such a 1 to 10.  SLAX introduces the "..." operator to
generate sequences of such numbers::

    for $i in 1 ... 10 {
        <player number=$i>;
    }

The operator translates into an XPath function that generates the
sequence as a node set, which contains a node for each value.  The
`for` and `for-each` statements can be used to iterate thru the
nodes in a sequence::

    for-each $min ... $max {
        message "Value: " _ .;
    }

.. index:: operators; "?:"
.. _question-colon:

The "?:" Operator
+++++++++++++++++

The "?:" operator allows simple logic tests to be coded with the
familiar C and Perl operator::

    var $x = ($a > 10) ? $b : $c;
    var $y = $action ?: "display";

The use of slax:value() make the "?:" operator non-standard, in that
it requires a non-standard extension function.  Use of the "?:" should
be limited to environments where this function is available.

The use of <xsl:copy-of> means that attributes cannot be used in
a ?: expression, directly or indirectly::

    /* These are examples of invalid use of attributes */
    var $direct = $test ? @broken : will-not-work;
    var $attrib = @some-attribute;
    var $indirect = $test ? $attrib : wont-work-either;

.. admonition:: XSLT Equivalent

    The generated XSLT uses an <xsl:choose> element.  The following is
    the equivalent of the first example above::

        <xsl:variable name="slax-ternary-1">
          <xsl:choose>
            <xsl:when test="($a &gt; 10)">
              <xsl:copy-of select="$b"/>
            </xsl:when>
            <xsl:otherwise>
              <xsl:copy-of select="$c"/>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:variable>
        <xsl:variable name="x" select="slax:value($slax-ternary-1)"/>

Functions
---------

Functions are one of the coolest extensions defined by EXSLT.  They
allow a script to define an extension function that is available in
XPath expressions.  Functions have several advantages over templates:

- Arguments are passed by position, not name
- Return values _can_ be objects (not RTFs)
- Can be used in expressions
- Can be resolved dynamically (using EXSLT's dyn:evaluate())

This section describes how functions are defined.

.. index:: statements; function
.. _function:

The `function` Statement
++++++++++++++++++++++++

The `function` statement defines a function that can be called in
XPath expressions::

    SYNTAX::
        'function' function-name '(' argument-list ')' '{'
            function-template
        '}'

The argument-list is a comma separated list of parameter names, which
will be positionally assigned based on the function call.  Trailing
arguments can have default values, in a similar way to templates.  If
there are fewer parameters in the invocation than in the definition,
then the default values will be used for any trailing arguments.  If
is an error for the function to be invoke with more arguments than are
defined::

    function size ($width, $length, $scale = 1) {
        ...
    }

Function parameters can also be defined using the `param` statement.

.. index:: statements; result
.. _result:

The `result` Statement
++++++++++++++++++++++

The `result` statement defines a value or template used as the return
value of the function::

    SYNTAX::
        'result' value ';'
        'result' element
        'result' '{'
            result-template
        '}'

The value can be a simple XPath expression, an XML element, or a set
of instructions that emit the value to be returned::


    function size ($width, $length, $scale = 1) {
        result $width * $length * $scale;
    }

    function box-parts ($width, $height, $depth, $scale = 1) {
        result <box> {
            <part count=2> size($width, $depth);
            <part count=2> size($width, $height);
            <part count=2> size($depth, $height);
        }
    }

    function ark () {
        result {
            <ark> {
                expr box-parts(2.5, 1.5, 1.5);
            }
        }
    }
