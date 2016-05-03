
@on = ();
$max = 1000;
$count = 100;

main: {
    while ($ARGV[0] =~ /^-/) {
	$_ = shift @ARGV;
	$max = shift @ARGV if /^-m/;
	$count = shift @ARGV if /^-c/;
    }

    for ($i = 0; $i < $max; $i++) {
	$slot = int(rand($count));
	if ($on[$slot]) {
	    print "f$slot\n";
	    undef $on[$slot];
	} else {
	    print "a$slot\n";
	    $on[$slot] = 1;
	}
    }
    print "d\n";
}
