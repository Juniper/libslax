.. #
   # Copyright (c) 2014-2017, Juniper Networks, Inc.
   # All rights reserved.
   # This SOFTWARE is licensed under the LICENSE provided in the
   # ../Copyright file. By downloading, installing, copying, or
   # using the SOFTWARE, you agree to be bound by the terms of that
   # LICENSE.
   # Phil Shafer, July 2014
   #

.. _overview:

===========================
The SLAX Scripting Language
===========================

SLAX is an alternate syntax for XSLT, the W3C standard XML-to-XML
transformation.  XSLT is a powerful language, but uses an XML-based
syntax that is painful to read and write.  SLAX uses a syntax modeled
after PERL and C which promotes the basic concepts of XSLT into first
class language constructs.  The result is scripts that are easier to
develop and maintain.

This documentation covers the SLAX language, beginning with an
overview and a reference section listing all SLAX statements.  SLAX
has a number of built-in functions which are also covered.

An implementation of the SLAX is available in an open source project
called `libslax`.  Built on top of `libxslt` and `libxml2`, libslax parses
SLAX files and executes them, and can convert between SLAX and XSLT.
A debugger and profiler are included.  libslax was originally
developed as part of the JUNOS Operating System by Juniper Networks
and is released under a BSD license.  See the "Copyright" file for
details.

XSLT is a commonly used transformation language for XML.  It is a
declarative language that uses XPath expressions to inspect an XML
input document and can generate XML output based on that input
hierarchy.  It's a simple but powerful tool for handling XML, but the
syntax is problematic for developers.

XSLT uses an XML-based syntax, and while XML is great for
machine-to-machine communication, it is inconvenient for humans to
write, especially when writing programs.  The occasional benefits of
having an XSLT stylesheet be an XML document are outweighed by the
readability issues of dealing with the syntax.

SLAX has a simple syntax which follows the style of C and Perl.
Programming constructs and XPath expressions are moved from XML
elements and attributes to first class language constructs.  XML angle
brackets and quotes is replaced by parentheses and curly braces
which are familiar delimiters for programmers.

SLAX allows you to:

- Use if/then/else instead of <xsl:choose> and <xsl:if> elements
- Put test expressions in parentheses
- Use "==" to test equality (to avoid building dangerous habits)
- Use curly braces to show containment instead of closing tags
- Perform concatenation using the "_" operator (lifted from perl6)
- Perform simple logic using the "?:" operator
- Write text strings using simple quotes instead of the <xsl:text> element
- Simplify invoking named templates with a syntax resembling a
  function call
- Simplify defining named template with a syntax resembling a
  function definition
- Simplify namespace declarations
- Reduce the clutter in scripts, allowing the important parts to be
  more obvious to the reader
- Write more readable scripts

The benefits of SLAX are particularly strong for new developers, since
it puts familiar constructs in familiar syntax, allowing them to
concentrate in the new topics introduced by XSLT.

.. default-role:: code

.. toctree::
    :maxdepth: 3
    :caption: Documentation Contents:

    concepts
    content
    templates
    constructs
    more-statements
    functions
    distribution
    building
    extensions
    examples
    notes

