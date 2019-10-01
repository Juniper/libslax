.. _example scripts:

===================
Example Stylesheets
===================

A simple example script::

    version 1.2;            /* All scripts start with "version" */

    param $name = "Poe";    /* Script parameters with default values */

    var $favorites := {     /* ":=" avoids RTFs */
        <name hidden="yes"> "Spillane";
        <name> "Doyle";     /* Creates elements and attributes */
        <name> "Poe";
    }

    main <out> {               /* A match template */
        /* Parameters are passed by name to templates */
        call test($elt = "author", $name);
    }

    template test ($name, $elt = "default") {
        for $this ($favorites/name) {
            if ($name == $this && not($this/@hidden)) {
                element $elt {
                    copy-of .//author[name/last == $this];
                }
            } else if ($name == $this) {
                message "Hidden: " _ $name;
            }
        }
    }

The following sections contain examples converted from the libxslt
test/ directory.  The XSLT form can be found in the libxslt source
code.  They were converted using the "slaxproc" tool.

general/itemschoose.xsl
-----------------------

::

    version 1.2;
 
    ns fo = "http://www.w3.org/1999/XSL/Format";
 
    strip-space itemlist;
    match doc {
        <doc> {
            apply-templates;
        }
    }

    match orderedlist/listitem {
        <fo:list-item indent-start="2pi"> {
            <fo:list-item-label> {
                var $level = count(ancestor::orderedlist) mod 3;
     
                if ($level=1) {
                    <number format="i">;
     
                } else if ($level=2) {
                    <number format="a">;
     
                } else {
                    <number format="1">;
                }
                expr ". ";
            }
            <fo:list-item-body> {
                apply-templates;
            }
        }
    }

.. admonition:: XSLT Equivalent

   https://raw.githubusercontent.com/GNOME/libxslt/master/tests/general/itemschoose.xsl

REC2/svg.xsl
------------

::

    version 1.2;
 
    ns "http://www.w3.org/Graphics/SVG/SVG-19990812.dtd";
 
    output-method xml {
        indent "yes";
         media-type "image/svg";
    }

    main <svg width="3in" height="3in"> {
        <g style="stroke: #000000"> {
            /* draw the axes */
            <line x1="0" x2="150" y1="150" y2="150">;
            <line x1="0" x2="0" y1="0" y2="150">;
            <text x="0" y="10"> "Revenue";
            <text x="150" y="165"> "Division";

            for-each (sales/division) {

                /* define some useful variables */
                /* the bar's x position */
                var $pos = (position()*40)-30;

                /* the bar's height */
                var $height = revenue*10;

                /* the rectangle */
                <rect x=$pos y=150 - $height 
                      width="20" height=$height>;

                /* the text label */
                <text x=$pos y="165"> @id;

                /* the bar value */
                <text x=$pos y=145 - $height> revenue;
            }
        }
    }

.. admonition:: XSLT Equivalent

   https://raw.githubusercontent.com/GNOME/libxslt/master/tests/REC2/svg.xsl

XSLTMark/metric.xsl
-------------------

::

    version 1.2;
 
    output-method html {
        encoding "utf-8";
    }

    match measurement {
        var $m = {
            if (@fromunit == 'km') {
                expr . * 1000;
     
            } else if (@fromunit == 'm') {
                expr .;
     
            } else if (@fromunit == 'cm') {
                expr . * 0.01;
     
            } else if (@fromunit == 'mm') {
                expr . * 0.001;
            }
        }
        <measurement unit=@tounit> {
            if (@tounit == 'mi') {
                expr 0.00062137 * $m;
     
            } else if (@tounit == 'yd') {
                expr 1.09361 * $m;
     
            } else if (@tounit == 'ft') {
                expr 3.2808 * $m;
     
            } else if (@tounit == 'in') {
                expr 39.37 * $m;
            }
        }
    }

.. admonition:: XSLT Equivalent

   https://raw.githubusercontent.com/GNOME/libxslt/master/tests/XSLTMark/metric.xsl
