#!/bin/bash

if [ "x$OMD_SITE" != "xdemo" ]; then
  echo "ERROR: this script should be run as demo user only."
  exit 3
fi

echo "building... "
mkdir -p var/tmp/build
rsync -av --exclude=.git/ /src/. var/tmp/build/.
cd var/tmp/build && \
    make build && \
    echo "build OK" && \
    sleep 1 && \
    omd restart apache > /dev/null
echo "done."
