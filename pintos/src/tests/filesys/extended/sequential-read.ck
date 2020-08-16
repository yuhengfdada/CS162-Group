# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(sequential-read) 1
(sequential-read) 2
(sequential-read) 3
sequential-read: exit(0)
EOF
pass;
