#!/bin/sh
# Host unit tests for the portable core (no hardware needed). Requires g++ (C++17).
set -e
cd "$(dirname "$0")/.."
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -Ilib/navcore/src \
  test/host_tests.cpp lib/navcore/src/*.cpp -o /tmp/paranav_tests
/tmp/paranav_tests
