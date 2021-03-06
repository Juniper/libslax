
==============
SLAX Functions
==============

SLAX includes a number of utility functions, both for internal and
external use.  This section documents the external functions.

SLAX External Functions
-----------------------

SLAX defines a set of built-in extension functions.  When one of these
functions is referenced, the SLAX namespace is automatically added to
the script.

slax:base64-decode
++++++++++++++++++

Use the slax:base64-decode function to decode BASE64 encoded data.
BASE64 is means of encoding arbitrary data into a radix-64 format
that can be more easily transmitted, typically via STMP or HTTP.

The argument is a string of BASE64 encoded data and an option string
which is used to replace any non-XML control characters, if any, in
the decoded string.  If this argument is an empty string, then non-xml
characters will be removed.  The decoded data is returned to the caller.

::

    SYNTAX::
        string slax:base64-decode(string [,control])

    EXAMPLE::
        var $real-data = slax:base64-decode($encoded-data, "@");

slax:base64-encode
++++++++++++++++++

Use the slax:base64-encode function to encode a string of data in the
BASE64 encoding format.  BASE64 is means of encoding arbitrary data
into a radix-64 format that can be more easily transmitted, typically
via STMP or HTTP.

The argument is a string of data, and the encoded data is returned.

::

    SYNTAX::
        string slax:base64-encode(string)

    EXAMPLE::
        var $encoded-data = slax:base64-encode($real-data);

slax:break-lines
++++++++++++++++

Use the slax:break-lines function to break multi-line text content
into multiple elements, each containing a single line of text.

::

    SYNTAX::
        node-set slax:break-lines(node-set)

    EXAMPLE::
        var $lines = slax:break-lines(.//p);
        for-each ($lines) {
            if (start-with("pfe:", .)) {
                <output> .;
            }
        }

For historical reasons, this function is also available using the name
"slax:break_lines" (with an underscore instead of a dash).  Scripts
should however avoid using this name.

slax:dampen
+++++++++++

Use the slax:dampen() function to limit the rate of occurrence of a
named event.  If slax:dampen() is called more than "max" times in
"time-period" minutes, it will return "true", allowing the caller to
react to this condition.

::

    SYNTAX::
        boolean slax:dampen(name, max, time-period)

    EXAMPLE::
        if (slax:dampen("reset", 2, 10)) {
            message "reset avoided";
        } else {
            <reset>;
        }

slax:document
+++++++++++++

Use the slax:document() function to read a data from a file or URL.
The data can be encoded in any character set (defaulting to "utf-8")
and can be BASE64 encoded.  In addition, non-XML control characters,
if any, can be replaced.

::

    SYNTAX::
        string slax:document(url [, options])

    EXAMPLE::
        var $data = slax:document($url);

        var $options := {
            <encoding> "ascii";
            <format> "base64";
            <non-xml> "#";
        }
        var $data2 = slax:document($url, $options);

============ =============================================
 Option       Description
============ =============================================
 <encoding>   Character encoding scheme ("utf-8")
 <format>     "base64" for BASE64-encoded data
 <non-xml>    Replace non-xml characters with this string
============ =============================================

If the <non-xml> value is an empty string, then non-xml characters
will be removed, otherwise they will be replaced with the given
string.

slax:evaluate
+++++++++++++

Use the slax:evaluate() function to evaluate a SLAX expression.  This
permits expressions using the extended syntax that SLAX provides in
addition to what is allowed in XPath (see :ref:`expressions`).  The
results of the expression are returned.

::

    SYNTAX::
        object slax:evaluate(expression)

    EXAMPLE::
        var $result = slax:evaluate("expr[name == '&']");

slax:first-of
+++++++++++++

Use the slax:first-of() function to find the first value present in a
test of arguments.  The first non-empty or non-zero length string will
be returned.

::

    SYNTAX::
        object slax:first-of(object+)

    EXAMPLE::
        var $title = slax:first-of(@title, $title, "Unknown");

slax:get-command
++++++++++++++++

Use the slax:get-command() function to return an input string provided
by the user in response to a given prompt string.  If the "readline"
(or "libedit") library was found at install time, the returned value
is entered in the readline history, and will be available via the
readline history keystrokes (Control-P and Control-N).

::

    SYNTAX::
        string slax:get-command(prompt)

    EXAMPLE::
        var $response = slax:get-command("# ");

slax:get-input
++++++++++++++

Use the slax:get-input() function to return an input string provided
by the user in response to a given prompt string.

::

    SYNTAX::
        string slax:get-input(prompt)

    EXAMPLE::
        var $response = slax:get-input("Enter peer address: ");

slax:get-secret
+++++++++++++++

Use the slax:get-secret() function to return an input string provided
by the user in response to a given prompt string.  Any text entered by
the user will not be displayed or echoed back to the user, making this
function suitable for obtaining secret information such as passwords.

::

    SYNTAX::
        string slax:get-secret(prompt)

    EXAMPLE::
        var $response = slax:get-secret("Enter password: ");

slax:is-empty
+++++++++++++

Use the slax:is-empty() function to determine if a node-set or RTF is
truly empty.

::

    SYNTAX::
        boolean slax:is-empty(object)

    EXAMPLE::
        if (slax:is-empty($result)) {
            message "missing result";
        }

slax:printf
+++++++++++

Use the slax:printf() function to format text in the manner of the
standard "C" "printf" function (printf(3)).  The normal printf format
values are honored, as are a number of "%j" extensions.

============= =====================================
 Format        Description
============= =====================================
 "%jcs"        Capitalize first letter
 "%jt{TAG}s"   Prepend TAG if string is not empty
 "%j1s"        Skip field if value has not changed
============= =====================================

::

    SYNTAX::
        string slax:printf(format, string*)

    EXAMPLE::
        for-each (article) {
            for-each (author) {
                message  slax:printf("%8j1s%8s%8jcj1s %jt{b:}s",
                                    ../title, name, dept, born);
            }
        }

slax:regex
++++++++++

::

    SYNTAX::
        node-set slax:regex(pattern, string, opts?)

Match a regex, returning a node set of the full string matched plus any parenthesized matches.  Options include "b", "i", "n", "^", and "$", for boolean results, ICASE, NEWLINE, NOTBOL, and NOTEOL.

slax:sleep
++++++++++

::

    SYNTAX::
        void slax:sleep(seconds, milliseconds)

Sleep for a given time period.

slax:split
++++++++++

::

    SYNTAX::
        node-set slax:split(pattern, string, limit)

Break a string into a set of elements, up to the limit times, at the pattern.

slax:sysctl
+++++++++++

::

    SYNTAX::
        string slax:sysctl(name, format)

Retrieve a sysctl variable.  Format is "i" or "s".

slax:syslog
+++++++++++

::

    SYNTAX::
        void slax:syslog(priority, string+)

Syslog the concatenation of set of arguments.
