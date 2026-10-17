B="/mnt/d/tools/Beyond Compare 4/mbc/mbc"
cp "$B" /tmp/mbc; chmod +x /tmp/mbc
mkdir -p /tmp/s && cd /tmp/s && rm -rf *
printf 'a\nb\nc\nd\ne\nf\ng\nh\n' > l.txt
printf 'a\nb\nX\nd\ne\nf\ng\nh\ni\n' > r.txt
/tmp/mbc l.txt r.txt; echo "rc=$?"
/tmp/mbc diff --style sidebyside l.txt r.txt; echo "rc=$?"
/tmp/mbc diff -c l.txt r.txt; echo "rc=$?"
/tmp/mbc diff -n l.txt r.txt; echo "rc=$?"
diff -u l.txt r.txt
