#!/bin/sh
# Run inside the ubuntu container with the SDK volume mounted at /BUILD and this project at /src.
set -e
apt-get update -qq >/dev/null
apt-get install -y -qq cmake make libtinfo5 libncurses5 >/dev/null
mkdir -p /tmp/b && cd /tmp/b
cmake -DCMAKE_TOOLCHAIN_FILE=/BUILD/SDK-B300-6.8/share/cmake/arm_conf.cmake -DCMAKE_BUILD_TYPE=Release /src/ui
make
cp taipan.app /src/ui/
ls -l /src/ui/taipan.app
