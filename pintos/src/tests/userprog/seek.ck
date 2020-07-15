# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(seek) begin
(seek) open "sample.txt"
(seek) open "sample.txt" again
seek: exit(-1)
EOF
pass;