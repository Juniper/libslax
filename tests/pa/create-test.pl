
@on = ();
@sz = ();
$max = 1000;
$count = 100;
$size = 0;
$min = 32;

main: {
    while ($ARGV[0] =~ /^-/) {
	$_ = shift @ARGV;
	$count = shift @ARGV if /^-c/;
	$min = shift @ARGV if /^-M/;
	$max = shift @ARGV if /^-m/;
	$size = shift @ARGV if /^-s/;
    }

    for ($i = 0; $i < $max; $i++) {
	$slot = int(rand($count));
	if ($on[$slot]) {
            $sp = $size ? " " . $sz[$slot] : "";
	    print "f$slot$sp\n";
	    undef $on[$slot];
	} else {
	    if ($size) {
		$sz = int(rand($size) + $min);
		$sp = " " . $sz;
		$sz[$slot] = $sz;
	    } else {
		$sp = "";
	    }

	    print "a$slot$sp\n";
	    $on[$slot] = 1;
	}
    }

    print "d\n";

    for ($i = 0; $i < $count; $i++) {
	if ($on[$i]) {
	    $sp = $size ? " " . $sz[$i] : "";
	    print "f$i$sp\n";
	}
    }

    print "d\n";
}
