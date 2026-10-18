b 6
c
p $top
set $top = "test"
p $top
set $top = <one> 1;
p $top
set $top = { <two> 2; <three> "333"; }
p $top
append $top += <four> "forty"
p $top
append $top += <five>
p $top
set $top = <six>
p $top
c
quit
y
