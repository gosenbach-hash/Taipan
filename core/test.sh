#!/bin/sh
# Run from Git Bash: MSYS_NO_PATHCONV=1 docker run --rm -v "C:\Games\Taipan\core:/src:ro" ubuntu:22.04 sh /src/test.sh
apt-get update -qq >/dev/null && apt-get install -y -qq g++ >/dev/null 2>&1
set -e
g++ -std=c++11 -Wall -Wextra -O1 -fsanitize=address,undefined -o /tmp/t /src/taipan_core.cpp /src/test_core.cpp && /tmp/t
