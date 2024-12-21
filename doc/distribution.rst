.. _libslax software:

========================
The libslax Distribution
========================

SLAX is available as an open-source project with the "New BSD"
license.  Current releases, source code, documentation, and support
materials can be downloaded from:

    https://github.com/Juniper/libslax

The libslax Library
-------------------

The core of the distribution is the libslax library, which
incorporates a SLAX parser to read SLAX files, a SLAX writer to write
SLAX files, a debugger, a profiler, and a commandline tool.

The reader turns a SLAX source file into an XSLT tree (xmlDocPtr)
using the xsltSetLoaderFunc() hook.  The writer turns an XSLT tree
(xmlDocPtr) into a file containing SLAX statements.

To support SLAX in your application, link with libslax and call the
libslax initializer function::

    #include <libslax/slax.h>

    slaxEnable(1);

.. _slaxproc:

slaxproc: The SLAX Processor
----------------------------

The SLAX software distribution contains a library (libslax) and a
command line tool (slaxproc).  The command line tool can be used to
convert between XSLT and SLAX syntax, as well as run stylesheets and
check syntax.

::

  Usage: slaxproc [mode] [options] [script] [files]
    Modes:
        --check OR -c: check syntax and content for a SLAX script
        --format OR -F: format (pretty print) a SLAX script
        --json-to-xml: Turn JSON data into XML
        --run OR -r: run a SLAX script (the default mode)
        --show-select: show XPath selection from the input document
        --show-variable: show contents of a global variable
        --slax-to-xslt OR -x: turn SLAX into XSLT
        --xml-to-json: turn XML into JSON
        --xml-to-yaml: turn XML into YAML
        --xpath <xpath> OR -X <xpath>: select XPath data from input
        --xslt-to-slax OR -s: turn XSLT into SLAX

    Options:
        --debug OR -d: enable the SLAX/XSLT debugger
        --empty OR -E: give an empty document for input
        --encoding <name>: specifies the input document encoding
        --exslt OR -e: enable the EXSLT library
        --expression <expr>: convert an expression
        --help OR -h: display this help message
        --html OR -H: Parse input data as HTML
        --ignore-arguments: Do not process any further arguments
        --include <dir> OR -I <dir>: search directory for includes/imports
        --indent OR -g: indent output ala output-method/indent
        --indent-width <num>: Number of spaces to indent (for --format)
        --input <file> OR -i <file>: take input from the given file
        --json-tagging: tag json-style input with the 'json' attribute
        --keep-text: mini-templates should not discard text
        --lib <dir> OR -L <dir>: search directory for extension libraries
        --log <file>: use given log file
        --mini-template <code> OR -m <code>: wrap template code in a script
        --name <file> OR -n <file>: read the script from the given file
        --no-json-types: do not insert 'type' attribute for --json-to-xml
        --no-randomize: do not initialize the random number generator
        --no-tty: do not fall back to stdin for tty io
        --output <file> OR -o <file>: make output into the given file
        --param <name> <value> OR -a <name> <value>: pass parameters
        --partial OR -p: allow partial SLAX input to --slax-to-xslt
        --slax-output OR -S: Write the result using SLAX-style XML (braces, etc)
        --trace <file> OR -t <file>: write trace data to a file
        --verbose OR -v: enable debugging output (slaxLog())
        --version OR -V: show version information (and exit)
        --version-only: show version information line (and exit)
        --want-parens: emit parens for control statements even for V1.3+
        --width <num>: Target line length before wrapping (for --format)
        --write-version <version> OR -w <version>: write in version

  Project libslax home page: https://github.com/Juniper/libslax

To use slaxproc to convert a SLAX file to XSLT::

    $ slaxproc -x mine.slax new.xsl

To convert an XSLT file to SLAX::

    $ slaxproc -s existing.xsl new.slax

To run a script::

    $ slaxproc mine.slax infile.xml outfile.xml

Use the :option:`-g` option to produce good-looking output by enabling
indenting (aka "pretty-printing") of output.  In this example,
since the output filename is not given, the output is written to
the standard output stream (stdout)::

    $ slaxproc -g mine.slax infile.xml

Use the :option:`-p` flag to perform conversion of SLAX and XML
formats for partial data files.  This can be used as a filter inside
an editor to convert a region from one format to the other::

    $ cat in.xml | slaxproc -s -p > out.slax

Use the :option:`-w` option to restrict the output of slaxproc to 1.0
features.

.. _slaxproc-arguments:

Argument Handling
+++++++++++++++++

`slaxproc` accepts the script name, input name, and output name in two ways.
You can say::

    slaxproc script.slax input.xml output.xml

using positional arguments.  This way allows slaxproc to be plug
compatible with xsltproc.

The other way is to give explicit option values using :option:`-n`,
:option:`-i`, and :option:`-o`.  The above command line can be given
as::

   slaxproc -i input.xml -n script.slax -o output.xml

These options can be in any order and can be intermixed with other
arguments.  If none of the values are given, they can still be
parsed positionally.  In this example, the script name is positional
but the input and output file names are positional::

   slaxproc -i input.xml -o output.xml -g -v script.slax

.. _slaxproc-pound-bang:

"#!" Support
~~~~~~~~~~~~

SLAX supports the "#!" unix scripting mechanism, allowing the first
line of a script to begin with the characters "#" and "!"  followed by
a path to the executable that runs the script and a set of command
line arguments.  For SLAX scripts, this might be something like::

    #!/usr/bin/slaxproc -n

or::

    #!/opt/local/bin/slaxproc -n

or::

    #!/usr/bin/env slaxproc -n

The operating system will add the name of the scripts and any command
line arguments to the command line that follows the "#!".  Adding the
:option:`-n` option (as shown above) allows additional arguments to be
passed in on the command line.  Flexible argument parsing allows
aliases and #! arguments to tailor the slaxproc invocation to match
specific needs.  For example if a script begins with::

    #!/usr/bin/slaxproc -E -n

then additional slaxproc arguments can be given::

    $ that-script -g output.xml

and the resulting command should be::

    /usr/bin/slaxproc -E -n /path/to/that-script -g output.xml

The :option:`-E` option tells slaxproc to use an empty input document,
removing the need for the :option:`-i` option or a positional argument.

If the input or output arguments have the value "-" (or is not
given), the standard input or standard output file will be used.
This allows slaxproc to be used as a traditional unix filter.

Command Line Options
++++++++++++++++++++

Command line options to slaxproc can be divided into two types.  Mode
options control the operation of slaxproc, and are mutually exclusive.
Behavioral options tailor the behavior of slaxproc in minor ways.

.. _slaxproc-modes:

Modes Options
~~~~~~~~~~~~~

.. option:: --check
.. option:: -c

  Perform syntax and content check for a SLAX script, reporting any
  errors detected.  This mode is useful for off-box syntax checks for
  scripts before installing or uploading them.

  ::

     % slaxproc --check ~/trash/test.slax
     script check succeeds

.. option:: --format
.. option:: -F

  Format (aka "pretty print") a SLAX script, correcting indentation and
  spacing to the style preferred by the author (that is, me).

  ::

     % slaxproc --format ugly.slax pretty.slax

.. index:: json; json-to-xml
.. option:: --json-to-xml

  Transform JSON input into XML, using the conventions defined in
  :ref:`json-elements`.

  ::

     % echo '{"a": "b"}' | slaxproc --json-to-xml
     <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
     <json>
       <a>b</a>
     </json>

.. option:: --run
.. option:: -r

  Run a SLAX script.  The script name, input file name, and output file
  name can be provided via command line options and/or using positional
  arguments as described in :ref:`slaxproc-arguments`.  Input defaults to
  standard input and output defaults to standard output.  `-r` is the
  default mode for slaxproc.

  The following command lines are equivalent::

     % slaxproc my-script.slax input.xml output.xml
     % slaxproc -r -n my-script.slax -i input.xml -o output.xml

.. option:: --show-select <xpath-expression>

  Show an XPath selection from the input document.  Used to extract
  selections from a script out for external consumption.  This allows
  the consumer to avoid a SLAX parser, but still have visibility into
  the contents of the script.

  The output is returned inside an XML hierarchy with a root element
  named "select".  This makes it possible to return attributes::

     % slaxproc --show-select 'xsl:template/@match' script.slax
     <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
     <select match="@* | * | processing-instruction() | comment()"/>

.. option:: --show-variable <variable-name>

  Show contents of a global variable.  Used to extract static variable
  contents for external consumption.  This allows the consumer of the
  data to avoid a SLAX parser, but still have access to the static
  contents of global variables, such as the $arguments variable.

  The output is returned inside an XML hierarchy with a root element
  named "select"::

     % slaxproc --show-variable '$global' script.slax
     <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
     <select>
       <xsl:variable xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                     name="global">
         <thing>
           <xsl:value-of select="1"/>
         </thing>
         <thing>
           <xsl:value-of select="2"/>
         </thing>
       </xsl:variable>
     </select>

.. option:: --slax-to-xslt
.. option:: -x

  Convert a SLAX script into XSLT format.  The script name and output file
  name can be provided via command line options and/or using positional
  arguments as described in :ref:`slaxproc-arguments`.

  ::

     % slaxproc --slax-to-xslt my-script.slax your-script.xsl

.. index:: json; xml-to-json
.. option:: --xml-to-json

  Transform XML input into JSON, using the conventions defined in
  :ref:`json-elements`.

  ::

     % echo '<json><a>b</a></json>' | slaxproc --xml-to-json
     { "a": "b" }

.. index:: yaml; xml-to-yaml
.. option:: --xml-to-yaml

  Transform XML input into YAML.

  ::

     % echo '<json><a>b</a></json>' | slaxproc --xml-to-yaml
     ---
     "a": "b"

.. option:: --xpath <xpath-expression>
.. option:: -X <xpath-expression>

  Select data matching an XPath data from input document.  This allows
  slaxproc to operate as a filter.  Note that :option:`--xpath` and
  :option:`--show-select` differ only in the lack of the root element
  on the latter::

     % slaxproc --xpath 'xsl:stylesheet/xsl:template/@match' /tmp/foo.xsl
     <?xml version="1.0"?>
     <results match="@* | * | processing-instruction() | comment()"/>

.. option:: --xslt-to-slax
.. option:: -s

  Convert a XSLT script into SLAX format.  The script name and output file
  name can be provided via command line options and/or using positional
  arguments as described in :ref:`slaxproc-arguments`.

  ::

     % slaxproc --xslt-to-slax your.xsl my.slax


.. _slaxproc-options:

Behavioral Options
~~~~~~~~~~~~~~~~~~

.. option:: --debug
.. option:: -d

  Enable the SLAX/XSLT debugger.  See :ref:`sdb` for complete details
  on the operation of the debugger.

.. option:: --empty
.. option:: -E

  Provide an empty document as the input data set.  This is useful for
  scripts that do not expect or need meaningful input.  The input
  document consists only of a root element ("top")::

    % slaxproc -E -m 'main <top> { copy-of /;} '
    <?xml version="1.0"?>
    <top/>

.. option:: --encoding <name>

  Specifies the text encoding to be used for input documents.  This
  defaults to "UTF-8".

.. option:: --exslt
.. option:: -e

  Enables the EXSLT library, which provides a set of standard extension
  functions.  See exslt.org for more information.

  This option is deprecated; SLAX now finds all extension functions
  automatically and no longer needs specific instructions for the
  EXSLT library.

.. option:: --expression <expr>

  Converts a SLAX expression to an XPATH one, or vice versa, depending
  on the presence of --slax-to-xslt and --xslt-to-slax.

  ::

     % slaxproc -x --expression 'f[name == $one _ "-ext" && mtu]'
     f[name = concat($one, "-ext") and mtu]

.. option:: --help
.. option:: -h

  Displays this help message and exits.  The help message has received
  an unknown number of industry awards from various well-meaning
  organizations.

.. option:: --html
.. option:: -H

  Parse input data using the HTML parser, which differs from XML.  The
  rules are more flexible, but are HTML specific.

.. option:: --ignore-arguments

  Do not process any further arguments.  This can be combined
  with "#!" to allow "distinct" styles of argument parsing.

.. option:: --include <dir>
.. option:: -I <dir>

  Add a directory to the list of directories searched for
  :ref:`include and import <include-import>` files.  The environment
  variable SLAXPATH can be set to a list of search directories,
  separated by colons.

.. index:: pretty-printing
.. option:: --indent
.. option:: -g

  Indent output to make it good looking, aka "pretty-printing".  This
  option is identical to the behavior triggered by the
  ref:`output-method <output-method>`'s `indent` statement::

     output-method {
         indent "true";
     }

.. option:: --indent-width

  Change the default indent level from the default value of 4.  This
  only affects :option:`--format` mode.

.. option:: --input <file>
.. option:: -i <file>

  Use the given file for input.

.. index:: json; json-tagging
.. option:: --json-tagging

  Tag JSON elements in SLAX script input with the 'json' attribute as
  they are parsed into XML.  This allows the :option:`--format` mode
  to transform them back into JSON format.

.. index:: mini-templates; keep-text
.. option:: --keep-text

  When building a script from mini-templates, do not add a template to
  discard normal text.  By default XSLT will display unmatched text
  data, but mini-templates adds a discard action automatically.  The
  `--keep-text` option preserves the original default behavior instead
  of replacing it with the discard action that is typically more
  desirable .

.. option:: --lib <dir>
.. option:: -L <dir>

  Add a directory to the list of directories searched for extension
  libraries.

.. option:: --log <file>

  Write log data to the given file, rather than the default of
  the standard error stream.

.. index:: mini-templates
.. option:: --mini-template <code> or -m <code>

  Allows a simple script to be passed in via the command line using one
  of more `-m` options.  The argument to `-m` is typically a template,
  such as a named or match template, but can be any top-level SLAX
  statement. 

     % slaxproc -E -m 'main <top> { expr date:time(); }'
     <?xml version="1.0"?>
     <top>19:51:11-05:00</top>

.. option:: --name <file>
.. option:: -n <file>

  Use the given file as the SLAX script.

.. index:: json; no-json-types
.. option:: --no-json-types

  Do not generate the "type" attribute in the XML generated by
  `--json-to-xml`.  This type is needed to "round-trip" data back
  into JSON, but is not needed for simple XML output.

.. option:: --no-randomize

  Do not initialize the random number generator.  This is useful if you
  want the script to return identical data for a series of invocation,
  which is typically only used during testing.

.. option:: --no-tty

  Do not use a tty for `sdb` and other tty-related input needs.

.. option:: --output <file>
.. option:: -o <file>

  Write output into the given file.

.. option:: --param <name> <value>
.. option:: --param <name>=<value>
.. option:: -a <name> <value>
.. option:: -a <name>=<value>

  Pass a parameter to the script using the name/value pair provided.
  Note that all values are string parameters, so normal quoting
  rules apply.

.. option:: --partial
.. option:: -p

  Allow the input data to contain a partial SLAX script, with more
  flexible input parsing.  This can be used with the `--slax-to-xslt`
  to perform partial transformations, or with `--format` to format
  sections of SLAX input or to convert XML into SLAX::

    % echo '<top><a>b</a></top>' | slaxproc -s -p
    <top> {
        <a> "b";
    }

.. option:: --slax-output
.. option:: -S

  Write the output of a script using SLAX-style XML (braces, etc).

.. option:: --trace <file>
.. option:: -t <file>

  Write trace data to the given file.

.. option:: --verbose
.. option:: -v

  Adds very verbose internal debugging output to the trace data output,
  including calls to the slaxLog() function.

.. option:: --version
.. option:: -V

  Show version information and exit.

.. option:: --version-only

  Show only the "version information" line and exit.

.. option:: --want-parens

Beginning with version 1.3, SLAX no longer requires parentheses around
the expressions for `if`, `for`, `for-each`, and `while`.  This
follows the highly addictive style of the `Rust` language.  Users more
comfortable with the previous parenthetical style can use this option
to force the use or parens.  This is automatically done with the
`--write-version` option uses a version number less that 1.3.

.. option:: --width <number>

XPath expressions can become quite long and we want `--format` output
to be a pretty as reasonably possible.  The `--width` option allows
the user to specify a particular width for which the formatting will
aim.  Values less that 40 are considered unreasonable and are ignored.

.. option:: --write-version <version>
.. option:: -w <version>

  Write in the given version number on the output file for `-x` or `-s`
  output.  This can be also be used to limit the conversion to avoid
  SLAX 1.1 feature (using `-w 1.0`).

  In this example, the `-w 1.0` option causes `slaxproc` to write the
  `main` statement introduced in SLAX-1.2 in a 1.0 compatible manor::

     % slaxproc --format -m 'main <top> { } ' -w 1.0 --keep-text
     version 1.0;

     match / {
         <top>;
     }

.. index:: sdb
.. index:: debugger
.. _sdb:

The SLAX Debugger (sdb)
-----------------------

The SLAX distribution includes a debugger called `sdb`, which can be
accessed via the `slaxproc` command using the `-d` option.  The
debugger resembles `gdb` command syntax and operation.

::

  (sdb) help
  List of commands:
    break [loc]     Add a breakpoint at [file:]line or template
    callflow [val]  Enable call flow tracing
    continue [loc]  Continue running the script
    delete [num]    Delete all (or one) breakpoints
    finish          Finish the current template
    help            Show this help message
    info            Showing info about the script being debugged
    list [loc]      List contents of the current script
    next            Execute the over instruction, stepping over calls
    over            Execute the current instruction hierarchy
    print <xpath>   Print the value of an XPath expression
    profile [val]   Turn profiler on or off
    reload          Reload the script contents
    run             Restart the script
    step            Execute the next instruction, stepping into calls
    where           Show the backtrace of template calls
    quit            Quit debugger

The `info` command can display the following information:

::

  (sdb) info help
  List of commands:
    info breakpoints  Display current breakpoints
    info insert       Display current insertion point
    info locals       Display local variables
    info output       Display output document
    info profile [brief]  Report profiling information

Many of these commands follow their "gdb" counterparts, to the extent
possible.

The location for the `break`, `continue`, and `list` commands can be
either a line number of the current file, a filename and a line
number, separated by a colon, or the name of a template.

::

  (sdb) b 14
  Breakpoint 1 at file ../tests/core/test-empty-21.slax, line 14
  (sdb) b 19
  Breakpoint 2 at file ../tests/core/test-empty-21.slax, line 19
  (sdb) b three
  Breakpoint 3 at file ../tests/core/test-empty-21.slax, line 24
  (sdb) info br
  List of breakpoints:
      #1 template one at ../tests/core/test-empty-21.slax:14
      #2 template two at ../tests/core/test-empty-21.slax:19
      #3 template three at ../tests/core/test-empty-21.slax:24
  (sdb)

Information on the profiler is in the next section (:ref:`profiler`).

The `info insert` and `info output` commands allow visibility into the
current output document being generated by libxslt.  `insert` shows
the current insertion point, typically as an XML hierarchy, where the
next element inserted will appear at the end of that hierarchy.
`output` displays the current state of the entire output document.

The `info locals` command displays the current set of local variables
and their values.

.. index:: profiler
.. _profiler:

The SLAX Profiler
-----------------

The SLAX debugger includes a profiler which can report information
about the activity and performance of a script.  The profiler is
automatically enabled when the debugger is started, and tracks script
execution until the script terminates.  At any point, profiling
information can be displayed or cleared, and the profiler can be
temporarily disabled or enabled.

Use the `profile` command to access the profiler::

  (sdb) help profile
  List of commands:
    profile clear   Clear  profiling information
    profile off     Disable profiling
    profile on      Enable profiling
    profile report [brief]  Report profiling information
  (sdb)

The profile report includes the following information:

- Line -- line number of the source file
- Hits -- number of times this line was executed
- User -- the number of microseconds if "user" time spent processing this line
- U/Hit -- average number of microseconds per hit
- System -- the number of microseconds if "system" time spent
  processing this line
- S/Hit -- average number of microseconds per hit
- Source -- Source code line

The `brief` option instructs sdb to avoid showing lines that were not
hit, since there is no valid information for them.  Without the
`brief` option, dashes are displayed.

In the following example, the source code data is heavily truncated
(with "....")  to allow the material to fit on this page.  sdb would
not truncate these lines::

  (sdb) run
  <?xml version="1.0"?>
  <message>Down rev PIC in Fruvenator, Fru-Master 3000</message>
  Script exited normally.
  (sdb) profile report
   Line   Hits   User    U/Hit  System    S/Hit Source
      1      -      -        -       -        - version 1.0;
      2      -      -        -       -        -
      3      2      4     2.00       8     4.00 match / {
      4      1     25    25.00      13    13.00     var ....
      5      -      -        -       -        -
      6      -      -        -       -        -     for-each....
      7      1     45    45.00      10    10.00          ..
      8      1     12    12.00       5     5.00         <message>
      9      1     45    45.00      15    15.00          ....
     10      -      -        -       -        -     }
     11      -      -        -       -        - }
  Total      6    131               51   Total
  (sdb) pro rep b
   Line   Hits   User    U/Hit  System    S/Hit Source
      3      2      4     2.00       8     4.00 match / {
      4      1     25    25.00      13    13.00     var  ....
      7      1     45    45.00      10    10.00          ....
      8      1     12    12.00       5     5.00      <message>
      9      1     45    45.00      15    15.00          ....
  Total      6    131               51   Total
  (sdb)

This information not only shows how much time is spent during code
execution, but also shows which lines are being executed, which
can help debug scripts where the execution does not match
expectations.

The profiling is not "Monte Carlo", or clock based, but is based on
trace data generated as each SLAX instruction is executed, giving
more precise data.

.. index

callflow
--------

The `callflow` command enables the printing of informational data when
levels of the script are entered and exited.  The lines are simple,
but reference the instruction, filename, and line number of the
frame::

  callflow: 0: enter <xsl:template> in match / at empty-15.slax:5
  callflow: 1: enter <xsl:variable> at empty-15.slax:13
  callflow: 1: exit <xsl:variable> at empty-15.slax:13
  callflow: 1: enter <xsl:variable> at empty-15.slax:20
  callflow: 1: exit <xsl:variable> at empty-15.slax:20
  callflow: 0: exit <xsl:template> in match / at empty-15.slax:5
