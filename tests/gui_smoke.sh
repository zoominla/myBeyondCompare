#!/bin/bash
cd "$(dirname "$0")/.."
rm -rf /tmp/guit && mkdir -p /tmp/guit/a/sub /tmp/guit/b/sub
printf 'one\ntwo\nthree\n' > /tmp/guit/a/f.txt; printf 'one\n2\nthree\nfour\n' > /tmp/guit/b/f.txt
echo same > /tmp/guit/a/sub/s.txt; echo same > /tmp/guit/b/sub/s.txt; echo lonely > /tmp/guit/a/only.txt
./mbc gui --no-open --port 18765 --sessions /tmp/guit/sess.xml > /tmp/guit/out.txt 2>&1 &
pid=$!
sleep 1
cat /tmp/guit/out.txt
tok=$(grep -o 'token=[0-9a-f]*' /tmp/guit/out.txt | cut -d= -f2)
echo "--- page"; curl -s -o /dev/null -w "%{http_code} %{size_download}\n" "http://127.0.0.1:18765/?token=$tok"
echo "--- no token"; curl -s -o /dev/null -w "%{http_code}\n" "http://127.0.0.1:18765/"
echo "--- bad host"; curl -s -o /dev/null -w "%{http_code}\n" -H "Host: evil.com" "http://127.0.0.1:18765/?token=$tok"
echo "--- api no token"; curl -s -X POST -d '{}' http://127.0.0.1:18765/api/info -w " %{http_code}\n"
api() { curl -s -X POST -H "x-mbc-token: $tok" -d "$2" "http://127.0.0.1:18765/api/$1"; echo; }
api info '{}'
api probe '{"left":"/tmp/guit/a","right":"/tmp/guit/b"}'
api dir '{"left":"/tmp/guit/a","right":"/tmp/guit/b","opts":{}}'
api text '{"left":{"path":"/tmp/guit/a/f.txt"},"right":{"path":"/tmp/guit/b/f.txt"},"opts":{}}'
api hex/open '{"left":"/tmp/guit/a/f.txt","right":"/tmp/guit/b/f.txt"}'
api dir/op '{"left":"/tmp/guit/a","right":"/tmp/guit/b","opts":{},"op":"copyLR","rels":["only.txt"],"dryRun":true}'
api sessions/save '{"name":"s1","kind":"dir","left":"/tmp/guit/a","right":"/tmp/guit/b"}'
api sessions/list '{}'
api browse '{"path":"/tmp/guit"}'
api quit '{}'
sleep 0.5; kill $pid 2>/dev/null
