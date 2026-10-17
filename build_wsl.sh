#!/bin/bash
# builds in a native WSL dir (faster than /mnt/d) and copies the binary back
set -e
here="$(cd "$(dirname "$0")" && pwd)"
rm -rf /tmp/mbcbuild && mkdir -p /tmp/mbcbuild && cp -r "$here/src" "$here/Makefile" /tmp/mbcbuild/
cd /tmp/mbcbuild && make -j4 mbc 2>&1 | grep -E "error|warning|Error" -A3 || true
cp /tmp/mbcbuild/mbc "$here/mbc"
echo built
