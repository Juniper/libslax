jQuery(function ($) {
    var $body = $("body");
    var $newp = $("<div id='top'><div id='top-left'/><div id='top-right'/></div");
    var $left = $("div#top-left", $newp);
    var $right = $("div#top-right", $newp);

    $right.append($("<div id='nav-bar'><button id='nav-prev'/><button id='nav-next'/></div>"));

    $("div.content", $body).each(function (i, elt) {
        $(elt).appendTo($right);
    });

    $body.append($newp);
    $left.append($("div#toc"));
    $("#rfc.toc").remove();
    $("h1", $left).remove();
    $("hr", $body).remove();

    $body.append($("<div id='debug-log'/>"));
    $left.append($("<div class='padding'/>"));

    var active;

    setActive($($right.children("div.content").get(0)));

    function setActive ($elt) {
        if ($elt && $elt.length > 0 && $elt.hasClass("content")) {
            if (active)
                active.removeClass("active");
            active = $elt;
            active.addClass("active");
        }
    }

    $("a", $left).click(function (event) {
        event.preventDefault();
        var id = this.href.split("#");
        id = id[id.length - 1];
        var $target = $(document.getElementById(id));
        setActive($target.parents("div.content"));
        var $tt = $("ul.top-toc", $(this).parent());
        if ($tt)
            $tt.toggleClass("top-toc-active");
        $("html").animate({ scrollTop: 0 }, 500);
    });

    $("button#nav-prev").button({
        label: "<< Previous",
    }).click(function (event) {
        setActive(active.prev());
    });

    $("button#nav-next").button({
        label: "Next >>",
    }).click(function (event) {
        setActive(active.next());
    });
});
