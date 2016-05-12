
@on = ();
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
	    print "f$slot\n";
	    undef $on[$slot];
	} else {
	    $sp = $size ? " " . int(rand($size) + $min) : "";
	    print "a$slot$sp\n";
	    $on[$slot] = 1;
	}
    }
    print "d\n";
}
