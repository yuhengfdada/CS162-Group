# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(seq-write) begin
(seq-write) Total access: 332973
(seq-write) Bufcache coalesce writes successfully
(seq-write) end
seq-write: exit(0)
EOF
pass;