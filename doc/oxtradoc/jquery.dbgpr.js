/*
 * $Id$
 *
 * Copyright 2011, Juniper Network Inc, All rights reserved
 * See ../Copyright for more information.
 */

jQuery(function ($) {
    /*
     * dbgpr() is our old-and-trusted debug print function
     */
    $.dbgpr = function () {
        /* The actual work is pretty trivial */
        $('#debug-log').prepend(Array.prototype.slice
                                .call(arguments).join(" ") + "\n");
    }
});
