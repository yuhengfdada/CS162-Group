# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(read-share) begin
(read-share) open "sample.txt"
(read-share) open "sample.txt"
(read-share) end
read-share: exit(0)
EOF
pass;
