#!/bin/bash

VERBOSE=0
if [ "x$1" != "x" ]; then
  VERBOSE=$1
  shift
fi

TESTS="/src/t/common/t/*.t"
if [ "x$1" != "x" ]; then
  TESTS="$*"
  shift
fi

omd stop dashboard >/dev/null 2>&1

sudo -iu demo \
PERL_DL_NONLAZY=1 \
  perl -MExtUtils::Command::MM -e "test_harness($VERBOSE, '/thruk/t', 'lib/')" \
  $TESTS
exit $?
