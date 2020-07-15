# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(seek) begin
(seek) open "sample.txt"
(seek) end
seek: exit(0)
EOF
pass;