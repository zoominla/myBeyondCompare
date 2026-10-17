cp "/mnt/d/tools/Beyond Compare 4/mbc/mbc" /tmp/mbc && chmod +x /tmp/mbc
cd "/mnt/d/tools/Beyond Compare 4/mbc" && MBC=/tmp/mbc python3 tests/test_mbc.py "$@" 2>&1 | head -${HEADN:-80}
