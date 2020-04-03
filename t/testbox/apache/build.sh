#!/bin/bash

if [ "x$OMD_SITE" != "xdemo" ]; then
  echo "ERROR: this script should be run as demo user only."
  exit 3
fi

echo -n "building... "
mkdir -p var/tmp/build
rsync -av /src/. var/tmp/build/.
cd var/tmp/build && make clean build
omd restart apache
echo "done."
