# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(seq-read) begin
(seq-read) First read hit count: 19
(seq-read) First read access acount: 20
(seq-read) Second read hit count : 20
(seq-read) Second read access acount: 20
(seq-read) end
seq-read: exit(0)
EOF
pass;
