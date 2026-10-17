#!/bin/bash
cd "$(dirname "$0")/src"
for f in "$@"; do g++ -std=c++20 -O1 -Wall -Wextra -c $f.cpp -o /tmp/$f.o 2>&1 | grep -E "error|warning" -A3 | head -40; done
