function popClick(event, objectId) {
    var sc = window["stickyClick"];
    if (sc && sc[objectId]) {
        popDown(event, objectId);
        sc[objectId] = false;
    } else {
        popUp(event, objectId);
        if (!sc) {
            window["stickyClick"] = sc = [];
        }
        sc[objectId] = true;
    }
    return false;
}

function popUp(event, objectId) {
    var sc = window["stickyClick"];
    if (sc && sc[objectId]) {
        return;
    }

    if (document.getElementById == null)
        return;
    var ie = document.all ? 1 : 0;      /* Is this Internet Explorer? */
    var dm = document.getElementById(objectId);
    if (dm == null)
        return;

    var ds = dm.style;
    var widthPage = window.innerWidth ? window.innerWidth
                : document.body.clientWidth;

    var elementWidth = dm.offsetWidth;
    var top;
    var left;

    if (event.pageY) {
        top = event.pageY + 10;
        left = event.pageX + 10;
    } else if (event.clientY) {
        top = event.clientY + 10;
        left = event.clientX + 10;
    } else {
        return;
    }    

    if (left + elementWidth > widthPage)
        left = widthPage - elementWidth - 25;

    if (ie) {
        ds.width = "1200px";
        ds.display = "block";
        var cw = dm.firstChild.clientWidth;
        if (cw > widthPage - left - 10) {
            cw = widthPage - left - 10;
        }
        ds.width = cw + "px";
    }

    ds.left = "10px";
    ds.top = top + "px";
    ds.width = widthPage / 2 + "px";
    ds.display = "block";

    var tm = document.getElementById("table-" + objectId);
    if (tm == null)
        return;

    elementWidth = tm.offsetWidth;
    if (left + elementWidth > widthPage)
        left = widthPage - elementWidth - 25;

    ds.left = left + "px";
}

function popDown(event, objectId) {
    var sc = window["stickyClick"];
    if (sc && sc[objectId]) {
        return;
    }

    if (document.getElementById == null)
        return;
    var dm = document.getElementById(objectId);
    if (dm) {
        dm.style.display = "none";
    }
}
