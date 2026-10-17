#!/usr/bin/env python3
"""Self tests for mbc.  Usage: MBC=/path/to/mbc python3 tests/test_mbc.py [-v] [filter]
Needs GNU diff/patch for the cross checks (skipped when absent)."""
import csv
import io
import os
import random
import shutil
import subprocess
import sys
import tempfile

MBC = os.environ.get("MBC") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mbc")
HERE = os.path.dirname(os.path.abspath(__file__))
SESSIONS = os.path.join(HERE, "..", "..", "BCSessions.xml")
HAVE_GNU = shutil.which("diff") and shutil.which("patch")
VERBOSE = "-v" in sys.argv
ARGS = [a for a in sys.argv[1:] if a != "-v"]

passed = failed = skipped = 0
failures = []


def run(*args, input=None, cwd=None):
    p = subprocess.run([MBC, *args], capture_output=True, input=input, cwd=cwd)
    return p.returncode, p.stdout, p.stderr


def runs(*args, **kw):
    rc, o, e = run(*args, **kw)
    return rc, o.decode("utf8", "replace"), e.decode("utf8", "replace")


def check(cond, msg):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        failures.append(msg)
        print("FAIL:", msg)


def wf(path, data):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    if isinstance(data, str):
        data = data.encode("utf8")
    with open(path, "wb") as f:
        f.write(data)


def rf(path):
    with open(path, "rb") as f:
        return f.read()


class T:
    registry = []

    def __init__(self, fn):
        T.registry.append(fn)
        self.fn = fn


def test(fn):
    T.registry.append(fn)
    return fn


# ------------------------------------------------------------------ helpers
VOCAB = ["alpha", "beta", "gamma", "delta", "", "x", "y", "  indented", "}", "{", "return 0;", "int a;"]


def rand_lines(rng, n):
    return [rng.choice(VOCAB) + (str(rng.randint(0, 3)) if rng.random() < 0.5 else "") for _ in range(n)]


def mutate(rng, lines):
    out = list(lines)
    for _ in range(rng.randint(0, 6)):
        k = rng.random()
        pos = rng.randint(0, len(out)) if out else 0
        if k < 0.35 and out:
            del out[min(pos, len(out) - 1) : min(pos, len(out) - 1) + rng.randint(1, 3)]
        elif k < 0.7:
            out[pos:pos] = rand_lines(rng, rng.randint(1, 3))
        elif out:
            out[min(pos, len(out) - 1)] = rng.choice(VOCAB) + "!"
    return out


def join(lines, final_nl=True):
    if not lines:
        return ""
    return "\n".join(lines) + ("\n" if final_nl else "")


def changed_count(patch_text):
    n = 0
    for ln in patch_text.splitlines():
        if (ln.startswith("-") and not ln.startswith("---")) or (ln.startswith("+") and not ln.startswith("+++")):
            n += 1
    return n


# ------------------------------------------------------------------ text diff
@test
def t_text_basic(tmp):
    wf(tmp + "/a.txt", "a\nb\nc\n")
    wf(tmp + "/b.txt", "a\nb\nc\n")
    rc, o, e = runs(tmp + "/a.txt", tmp + "/b.txt")
    check(rc == 1, "identical files -> exit 1, got %d" % rc)
    wf(tmp + "/c.txt", "a\nB\nc\n")
    rc, o, e = runs("diff", tmp + "/a.txt", tmp + "/c.txt")
    check(rc == 13, "different -> 13, got %d" % rc)
    check("-b" in o and "+B" in o, "unified output contains change: " + o)
    rc, _, _ = runs("diff", "-i", "-q", tmp + "/a.txt", tmp + "/c.txt")
    check(rc == 2, "ignore case -> 2, got %d" % rc)
    wf(tmp + "/d.txt", "a\r\nb\r\nc\r\n")
    rc, _, _ = runs("diff", "-q", tmp + "/a.txt", tmp + "/d.txt")
    check(rc == 2, "EOL only difference -> 2 (rules same), got %d" % rc)
    rc, _, _ = runs("diff", "-q", "--no-ignore-eol", tmp + "/a.txt", tmp + "/d.txt")
    check(rc == 13, "EOL difference with --no-ignore-eol -> 13, got %d" % rc)
    wf(tmp + "/e.txt", "a\n  b \nc\n")
    rc, _, _ = runs("diff", "-q", tmp + "/a.txt", tmp + "/e.txt")
    check(rc == 13, "whitespace differs -> 13")
    rc, _, _ = runs("diff", "-q", "-w", tmp + "/a.txt", tmp + "/e.txt")
    check(rc == 2, "ignore all whitespace -> 2, got %d" % rc)
    wf(tmp + "/f.txt", "a b\nxy\n")
    wf(tmp + "/g.txt", "a   b\nx y\n")
    rc, _, _ = runs("diff", "-q", "-b", tmp + "/f.txt", tmp + "/g.txt")
    check(rc == 13, "ignore whitespace amount: 'xy' vs 'x y' still differs -> 13, got %d" % rc)
    wf(tmp + "/g.txt", "a   b\nxy\n")
    rc, _, _ = runs("diff", "-q", "-b", tmp + "/f.txt", tmp + "/g.txt")
    check(rc == 2, "ignore whitespace amount: 'a b' vs 'a   b' -> 2, got %d" % rc)
    rc, _, _ = runs("diff", "-q", "--ignore-leading-space", "--ignore-trailing-space", tmp + "/a.txt", tmp + "/e.txt")
    check(rc == 2, "ignore leading+trailing space -> 2, got %d" % rc)
    rc, _, _ = runs(tmp + "/a.txt", tmp + "/nonexistent")
    check(rc == 100, "missing file -> 100, got %d" % rc)
    rc, _, _ = runs("/qc", tmp + "/a.txt", tmp + "/c.txt")
    check(rc == 11, "/qc different -> 11, got %d" % rc)
    rc, _, _ = runs("/qc", tmp + "/a.txt", tmp + "/b.txt")
    check(rc == 1, "/qc same -> 1")


@test
def t_regex_blank(tmp):
    wf(tmp + "/a.txt", "x\n// build 123\ny\n")
    wf(tmp + "/b.txt", "x\n// build 456\ny\n")
    rc, _, _ = runs("diff", "-q", tmp + "/a.txt", tmp + "/b.txt")
    check(rc == 13, "regex baseline differs")
    rc, _, _ = runs("diff", "-q", "--ignore-regex", r"build \d+", tmp + "/a.txt", tmp + "/b.txt")
    check(rc == 2, "ignore regex -> 2, got %d" % rc)
    wf(tmp + "/c.txt", "x\n\n\ny\n")
    wf(tmp + "/d.txt", "x\ny\n")
    rc, _, _ = runs("diff", "-q", tmp + "/c.txt", tmp + "/d.txt")
    check(rc == 13, "blank lines differ by default")
    rc, _, _ = runs("diff", "-q", "-B", tmp + "/c.txt", tmp + "/d.txt")
    check(rc == 2, "ignore blank lines -> 2, got %d" % rc)


@test
def t_encoding(tmp):
    s = "héllo\nwörld 中文\n"
    wf(tmp + "/u8.txt", s.encode("utf8"))
    wf(tmp + "/u8bom.txt", b"\xef\xbb\xbf" + s.encode("utf8"))
    wf(tmp + "/u16.txt", b"\xff\xfe" + s.replace("\n", "\r\n").encode("utf-16-le"))
    wf(tmp + "/u16be.txt", b"\xfe\xff" + s.encode("utf-16-be"))
    for other in ("u8bom", "u16", "u16be"):
        rc, o, e = runs("diff", "-q", tmp + "/u8.txt", tmp + "/%s.txt" % other)
        check(rc == 2, "same text different encoding (%s) -> 2, got %d %s" % (other, rc, e))
    wf(tmp + "/lat.txt", "caf\xe9\n".encode("latin-1"))
    wf(tmp + "/lat2.txt", "caf\xe9\n".encode("latin-1"))
    rc, _, _ = runs(tmp + "/lat.txt", tmp + "/lat2.txt")
    check(rc == 1, "latin-1 identical")


@test
def t_eof_newline(tmp):
    wf(tmp + "/a.txt", "a\nb")
    wf(tmp + "/b.txt", "a\nb\n")
    rc, o, _ = runs("diff", "--no-ignore-eol", tmp + "/a.txt", tmp + "/b.txt")
    check(rc == 13 and "No newline" in o, "missing eof newline reported: rc=%d %s" % (rc, o))
    rc, _, _ = runs("diff", "-q", tmp + "/a.txt", tmp + "/b.txt")
    check(rc == 2, "missing final newline unimportant by default, got %d" % rc)


@test
def t_fuzz_patch(tmp):
    if not HAVE_GNU:
        return "skip"
    rng = random.Random(12345)
    aligns = ["smart", "complete", "fast", "patience", "none"]
    n = 0
    for seed in range(120):
        base = rand_lines(rng, rng.randint(0, 40))
        other = mutate(rng, base)
        nl1 = rng.random() < 0.85
        nl2 = rng.random() < 0.85
        a, b = join(base, nl1), join(other, nl2)
        wf(tmp + "/l.txt", a)
        wf(tmp + "/r.txt", b)
        for al in aligns:
            for style, flag in (("unified", "-u"), ("context", "-c"), ("normal", "-n")):
                rc, out, err = run(style_cmd(style), "--no-ignore-eol", "--align", al, tmp + "/l.txt", tmp + "/r.txt") if False else run("diff", "--no-ignore-eol", "--align", al, "--style", style, tmp + "/l.txt", tmp + "/r.txt")
                exp = 1 if a == b else 13
                if rc != exp:
                    check(False, "fuzz seed=%d align=%s style=%s rc=%d expected=%d err=%s" % (seed, al, style, rc, exp, err[:200]))
                    continue
                if a == b:
                    continue
                shutil.copy(tmp + "/l.txt", tmp + "/work.txt")
                pargs = ["patch", "-s", "-f", "--no-backup-if-mismatch", tmp + "/work.txt"]
                if style == "normal":
                    pargs.insert(1, "--normal")
                p = subprocess.run(pargs, input=out, capture_output=True)
                ok = p.returncode == 0 and rf(tmp + "/work.txt") == b.encode()
                if not ok:
                    check(False, "fuzz patch seed=%d align=%s style=%s failed: %s %s\n--- patch:\n%s" % (seed, al, style, p.stdout[:200], p.stderr[:200], out.decode()[:600]))
                else:
                    check(True, "")
                n += 1
        # minimality of "complete" vs GNU diff --minimal
        g = subprocess.run(["diff", "--minimal", "-u", tmp + "/l.txt", tmp + "/r.txt"], capture_output=True).stdout.decode()
        rc, out, _ = runs("diff", "--no-ignore-eol", "--align", "complete", "--style", "unified", tmp + "/l.txt", tmp + "/r.txt")
        # GNU counts "\ No newline" differently, so compare only when both end with newline
        if nl1 and nl2:
            check(changed_count(out) == changed_count(g), "complete align minimal seed=%d ours=%d gnu=%d" % (seed, changed_count(out), changed_count(g)))
        rc, out, _ = runs("diff", "--no-ignore-eol", "--align", "smart", "--style", "unified", tmp + "/l.txt", tmp + "/r.txt")
        if nl1 and nl2:
            check(changed_count(out) <= changed_count(g) * 2 + 2, "smart align reasonable seed=%d ours=%d gnu=%d" % (seed, changed_count(out), changed_count(g)))
    check(n > 0, "fuzz ran")


def style_cmd(s):
    return "diff"


@test
def t_unified_matches_gnu(tmp):
    """For simple edits the unified output must equal GNU diff -u (modulo header)."""
    if not HAVE_GNU:
        return "skip"
    rng = random.Random(7)
    same = tot = 0
    for seed in range(60):
        base = ["line%d" % i for i in range(rng.randint(5, 40))]
        other = list(base)
        for _ in range(rng.randint(1, 3)):
            k = rng.randint(0, len(other) - 1)
            op = rng.random()
            if op < 0.3:
                del other[k]
            elif op < 0.6:
                other.insert(k, "new%d" % rng.randint(0, 99))
            else:
                other[k] = "mod%d" % rng.randint(0, 99)
        wf(tmp + "/l.txt", join(base))
        wf(tmp + "/r.txt", join(other))
        rc, out, _ = runs("diff", "--align", "complete", "--style", "unified", tmp + "/l.txt", tmp + "/r.txt")
        g = subprocess.run(["diff", "-u", tmp + "/l.txt", tmp + "/r.txt"], capture_output=True).stdout.decode()
        body = lambda t: [l for l in t.splitlines()[2:]]
        tot += 1
        if rc == 13 or rc == 1:
            if body(out) == body(g):
                same += 1
            else:
                print("  note: unified differs from GNU seed", seed) if VERBOSE else None
    check(same >= tot * 0.9, "unified body equals GNU diff -u in %d/%d cases" % (same, tot))


@test
def t_sidebyside_html_summary(tmp):
    wf(tmp + "/a.txt", "one\ntwo\nthree\nfour\n")
    wf(tmp + "/b.txt", "one\n2\nthree\nfour\nfive\n")
    rc, o, _ = runs("diff", "--style", "sidebyside", tmp + "/a.txt", tmp + "/b.txt")
    check(rc == 13 and "|" in o and ">" in o, "sidebyside markers")
    rc, o, _ = runs("diff", "--style", "html", tmp + "/a.txt", tmp + "/b.txt")
    check(rc == 13 and "<table>" in o and "ch" in o, "html report")
    rc, o, _ = runs("diff", "--style", "summary", tmp + "/a.txt", tmp + "/b.txt")
    check("Different lines" in o and "files differ" in o, "summary report: " + o)
    rc, o, _ = runs("diff", "--style", "sidebyside", "--only-diffs", "--context", "0", tmp + "/a.txt", tmp + "/b.txt")
    check("three" not in o and "two" in o, "only-diffs hides context: " + o)
    out = tmp + "/rep.html"
    rc, o, _ = runs("diff", "--style", "html", "-o", out, tmp + "/a.txt", tmp + "/b.txt")
    check(rc == 13 and os.path.exists(out) and b"<html>" in rf(out), "-o writes report file")


# ------------------------------------------------------------------ hex
@test
def t_hex(tmp):
    wf(tmp + "/a.bin", bytes(range(256)) * 4)
    wf(tmp + "/b.bin", bytes(range(256)) * 4)
    rc, _, _ = runs(tmp + "/a.bin", tmp + "/b.bin")
    check(rc == 1, "identical binary -> 1, got %d" % rc)
    d = bytearray(bytes(range(256)) * 4)
    d[500] ^= 0xFF
    d[501] ^= 0x01
    d[900] ^= 0x10
    wf(tmp + "/c.bin", bytes(d))
    rc, o, _ = runs(tmp + "/a.bin", tmp + "/c.bin")
    check(rc == 11, "different binary -> 11, got %d" % rc)
    check("3 differing byte" in o and "0x1f4" in o, "hex report counts: " + o[:300])
    rc, _, _ = runs("hex", tmp + "/a.bin", tmp + "/c.bin", "-q")
    check(rc == 11, "hex -q")
    rc, o, _ = runs("hex", "--context", "0", tmp + "/a.bin", tmp + "/c.bin")
    check(len(o.splitlines()) < 20, "hex dump limited to differing rows: %d lines" % len(o.splitlines()))
    wf(tmp + "/e.bin", bytes(range(256)) * 4 + b"zz")
    rc, o, _ = runs("hex", tmp + "/a.bin", tmp + "/e.bin")
    check(rc == 11, "different length -> 11")


# ------------------------------------------------------------------ merge
@test
def t_merge_fuzz(tmp):
    rng = random.Random(99)
    for seed in range(100):
        segs = rng.randint(2, 6)
        base, left, right, exp = [], [], [], []
        conflict = False
        cid = 0
        for s in range(segs):
            keep = ["k%d_%d_%d" % (seed, s, i) for i in range(rng.randint(2, 4))]
            base += keep; left += keep; right += keep; exp += keep
            kind = rng.choice(["none", "L", "R", "both-same", "conflict", "Ldel", "Rins"])
            seg = ["seg%d_%d" % (seed, s)]
            base += seg
            if kind == "none":
                left += seg; right += seg; exp += seg
            elif kind == "L":
                left += ["L%d_%d" % (seed, s)]; right += seg; exp += ["L%d_%d" % (seed, s)]
            elif kind == "R":
                left += seg; right += ["R%d_%d" % (seed, s)]; exp += ["R%d_%d" % (seed, s)]
            elif kind == "both-same":
                left += ["B%d_%d" % (seed, s)]; right += ["B%d_%d" % (seed, s)]; exp += ["B%d_%d" % (seed, s)]
            elif kind == "Ldel":
                right += seg
            elif kind == "Rins":
                left += seg; right += seg + ["ins%d_%d" % (seed, s)]; exp += seg + ["ins%d_%d" % (seed, s)]
            else:
                conflict = True
                left += ["cL%d_%d" % (seed, s)]; right += ["cR%d_%d" % (seed, s)]
                exp += ["cL%d_%d" % (seed, s)]  # for resolve=left
        tail = ["end%d" % seed, "end2_%d" % seed]
        base += tail; left += tail; right += tail; exp += tail
        wf(tmp + "/base.txt", join(base)); wf(tmp + "/l.txt", join(left)); wf(tmp + "/r.txt", join(right))
        out = tmp + "/out.txt"
        if os.path.exists(out):
            os.remove(out)
        rc, o, e = runs("merge", tmp + "/l.txt", tmp + "/r.txt", tmp + "/base.txt", "-o", out, "-q")
        if not conflict:
            check(rc == 0 and rf(out) == join(exp).encode(), "merge clean seed=%d rc=%d" % (seed, rc))
        else:
            check(rc == 101 and not os.path.exists(out), "merge conflict unsaved seed=%d rc=%d" % (seed, rc))
            rc, o, e = runs("merge", tmp + "/l.txt", tmp + "/r.txt", tmp + "/base.txt", "-o", out, "--resolve", "left", "-q")
            check(rc == 0 and rf(out) == join(exp).encode(), "merge resolve=left seed=%d rc=%d" % (seed, rc))
            rc, o, e = runs("merge", tmp + "/l.txt", tmp + "/r.txt", tmp + "/base.txt", "-o", out, "--force", "-q")
            txt = rf(out).decode()
            check(rc == 14 and "<<<<<<<" in txt and ">>>>>>>" in txt and "=======" in txt, "merge markers seed=%d rc=%d" % (seed, rc))
        if HAVE_GNU and shutil.which("diff3") and not conflict:
            g = subprocess.run(["diff3", "-m", tmp + "/l.txt", tmp + "/base.txt", tmp + "/r.txt"], capture_output=True)
            if g.returncode == 0:
                check(g.stdout == rf(out), "merge equals diff3 -m seed=%d" % seed)


@test
def t_merge_both(tmp):
    wf(tmp + "/base.txt", "a\nb\nc\n")
    wf(tmp + "/l.txt", "a\nL\nc\n")
    wf(tmp + "/r.txt", "a\nR\nc\n")
    rc, _, _ = runs("merge", tmp + "/l.txt", tmp + "/r.txt", tmp + "/base.txt", "-o", tmp + "/o.txt", "--resolve", "both", "-q")
    check(rc == 0 and rf(tmp + "/o.txt") == b"a\nL\nR\nc\n", "resolve both: %r" % rf(tmp + "/o.txt"))
    rc, _, _ = runs("merge", tmp + "/l.txt", tmp + "/r.txt", tmp + "/base.txt", "/mergeoutput=" + tmp + "/o2.txt", "--resolve", "right", "-q")
    check(rc == 0 and rf(tmp + "/o2.txt") == b"a\nR\nc\n", "/mergeoutput + resolve right")
    rc, _, _ = runs(tmp + "/l.txt", tmp + "/r.txt", tmp + "/base.txt", "--resolve", "right", "-o", tmp + "/o3.txt", "-q")
    check(rc == 0 and rf(tmp + "/o3.txt") == b"a\nR\nc\n", "3 positional args imply merge")


# ------------------------------------------------------------------ folders
def mktree(root, files):
    for rel, (data, mt) in files.items():
        p = os.path.join(root, rel)
        wf(p, data)
        os.utime(p, (mt, mt))


def dir_csv(tmp, *extra):
    rc, o, e = runs("dir", "--report-style", "csv", *extra, tmp + "/L", tmp + "/R")
    rows = list(csv.DictReader(io.StringIO(o)))
    return rc, {r["Path"]: r for r in rows}, e


@test
def t_dir_basic(tmp):
    T0 = 1700000000
    mktree(tmp + "/L", {"same.txt": ("s\n", T0), "diff.txt": ("old\n", T0), "lonly.txt": ("l\n", T0), "sub/in.txt": ("1\n", T0),
                        "sub/lo.c": ("c\n", T0), "ldir/x.txt": ("x\n", T0), "newer.txt": ("a\n", T0 + 100)})
    mktree(tmp + "/R", {"same.txt": ("s\n", T0), "diff.txt": ("new\n", T0 + 50), "ronly.txt": ("r\n", T0), "sub/in.txt": ("1\n", T0),
                        "rdir/y.txt": ("y\n", T0), "newer.txt": ("b\n", T0)})
    os.makedirs(tmp + "/L/emptydir")
    rc, rows, e = dir_csv(tmp)
    st = {k: v["Status"] for k, v in rows.items()}
    exp = {"same.txt": "same", "diff.txt": "different", "lonly.txt": "left-only", "ronly.txt": "right-only", "sub": "different",
           "sub/in.txt": "same", "sub/lo.c": "left-only", "ldir": "left-only", "ldir/x.txt": "left-only", "rdir": "right-only",
           "rdir/y.txt": "right-only", "newer.txt": "different", "emptydir": "left-only"}
    for k, v in exp.items():
        check(st.get(k) == v, "dir status %s expected %s got %s" % (k, v, st.get(k)))
    check(rc == 11, "dir different -> 11, got %d" % rc)
    check(rows["diff.txt"]["Newer"] == "right", "diff.txt right newer")
    check(rows["newer.txt"]["Newer"] == "left", "newer.txt left newer")
    # identical
    rc, _, _ = runs("dir", "-q", tmp + "/L", tmp + "/L")
    check(rc == 1, "same dir -> 1, got %d" % rc)
    # filters
    rc, rows, _ = dir_csv(tmp, "--filter", "*.txt")
    check("sub/lo.c" not in rows and "lonly.txt" in rows, "include filter *.txt")
    rc, rows, _ = dir_csv(tmp, "--filter", "-*.c;-ldir/")
    check("sub/lo.c" not in rows and "ldir" not in rows and "ldir/x.txt" not in rows and "same.txt" in rows, "exclude filter: %s" % sorted(rows))
    rc, rows, _ = dir_csv(tmp, "--no-recursive")
    check("sub/in.txt" not in rows and "sub" in rows, "no-recursive")
    rc, rows, _ = dir_csv(tmp, "--show", "diff")
    check("same.txt" not in rows and "diff.txt" in rows, "show diff hides same")
    rc, rows, _ = dir_csv(tmp, "--show", "left")
    check(set(r["Status"] for r in rows.values() if r["Type"] == "file") == {"left-only"}, "show left only files")
    # criteria: size,time only
    mktree(tmp + "/L", {"t.txt": ("abcd", T0)})
    mktree(tmp + "/R", {"t.txt": ("wxyz", T0)})
    rc, rows, _ = dir_csv(tmp, "--criteria", "size,time")
    check(rows["t.txt"]["Status"] == "same", "size+time criteria ignores content")
    rc, rows, _ = dir_csv(tmp, "--criteria", "binary")
    check(rows["t.txt"]["Status"] == "different", "binary criteria sees content")
    # tolerance
    mktree(tmp + "/L", {"tol.txt": ("same\n", T0)})
    mktree(tmp + "/R", {"tol.txt": ("same\n", T0 + 3)})
    rc, rows, _ = dir_csv(tmp, "--criteria", "size,time")
    check(rows["tol.txt"]["Status"] == "different", "time differs without tolerance")
    rc, rows, _ = dir_csv(tmp, "--criteria", "size,time", "--tolerance", "5")
    check(rows["tol.txt"]["Status"] == "same", "tolerance accepted")


@test
def t_dir_rules(tmp):
    T0 = 1700000000
    mktree(tmp + "/L", {"a.txt": ("x\ny\n", T0), "b.txt": ("p\nq\n", T0)})
    mktree(tmp + "/R", {"a.txt": ("x\r\ny\r\n", T0), "b.txt": ("p\nQ\n", T0)})
    rc, rows, _ = dir_csv(tmp, "--criteria", "rules")
    check(rows["a.txt"]["Status"] == "similar", "rules: eol-only -> similar, got %s" % rows["a.txt"]["Status"])
    check(rows["b.txt"]["Status"] == "different", "rules: text differs")
    check(rc == 13, "rules-based different -> 13, got %d" % rc)
    mktree(tmp + "/R", {"b.txt": ("p\nq\n", T0)})
    rc, rows, _ = dir_csv(tmp, "--criteria", "rules")
    check(rc == 2, "rules-based same -> 2, got %d" % rc)


@test
def t_dir_reports(tmp):
    T0 = 1700000000
    mktree(tmp + "/L", {"a.txt": ("1", T0), "d/b.txt": ("2", T0)})
    mktree(tmp + "/R", {"a.txt": ("1", T0), "d/b.txt": ("3", T0)})
    for st in ("tree", "list", "csv", "html", "xml", "summary"):
        rc, o, e = runs("dir", "--report-style", st, tmp + "/L", tmp + "/R")
        check(rc == 11 and len(o) > 20, "folder report %s rc=%d" % (st, rc))
    rc, o, _ = runs("dir", "--report-style", "xml", tmp + "/L", tmp + "/R")
    check('path="d/b.txt"' in o and 'status="different"' in o, "xml report content")
    rc, o, _ = runs("dir", "--report-style", "summary", tmp + "/L", tmp + "/R")
    check("different: 1" in o, "summary content: " + o)


@test
def t_sync(tmp):
    if not shutil.which("diff"):
        return "skip"
    T0 = 1700000000
    mktree(tmp + "/L", {"a.txt": ("new-left\n", T0 + 100), "only-l.txt": ("l\n", T0), "d/x.txt": ("x\n", T0), "d/deep/y.txt": ("y\n", T0), "same.txt": ("s\n", T0)})
    mktree(tmp + "/R", {"a.txt": ("old-right\n", T0), "only-r.txt": ("r\n", T0), "same.txt": ("s\n", T0), "rd/z.txt": ("z\n", T0)})
    # dry run changes nothing
    before = subprocess.run(["diff", "-r", tmp + "/L", tmp + "/R"], capture_output=True).stdout
    rc, o, _ = runs("sync", "--dry-run", "--mode", "update:lr", tmp + "/L", tmp + "/R")
    after = subprocess.run(["diff", "-r", tmp + "/L", tmp + "/R"], capture_output=True).stdout
    check(rc == 0 and before == after and "copy L->R" in o, "dry run: " + o)
    # update L->R: copies, no deletes
    rc, o, e = runs("sync", "--mode", "update:lr", tmp + "/L", tmp + "/R")
    check(rc == 0, "update lr rc=%d %s" % (rc, e))
    check(rf(tmp + "/R/a.txt") == b"new-left\n", "newer left copied")
    check(os.path.exists(tmp + "/R/d/deep/y.txt") and os.path.exists(tmp + "/R/only-l.txt"), "left-only copied")
    check(os.path.exists(tmp + "/R/only-r.txt") and os.path.exists(tmp + "/R/rd/z.txt"), "update does not delete")
    check(int(os.path.getmtime(tmp + "/R/a.txt")) == T0 + 100, "mtime preserved")
    # mirror L->R: now identical
    rc, o, e = runs("sync", "--mode", "mirror:lr", tmp + "/L", tmp + "/R")
    check(rc == 0, "mirror rc")
    g = subprocess.run(["diff", "-r", tmp + "/L", tmp + "/R"], capture_output=True)
    check(g.returncode == 0, "mirror makes trees identical: " + g.stdout.decode())
    check(not os.path.exists(tmp + "/R/rd") and not os.path.exists(tmp + "/R/only-r.txt"), "mirror removes orphans")
    rc, _, _ = runs("dir", "-q", tmp + "/L", tmp + "/R")
    check(rc == 1, "after mirror compare is same")
    # bidirectional
    mktree(tmp + "/L", {"bl.txt": ("L-newer\n", T0 + 500), "onlyL2.txt": ("1\n", T0)})
    mktree(tmp + "/R", {"bl.txt": ("R-older\n", T0 + 400), "onlyR2.txt": ("2\n", T0)})
    rc, o, e = runs("sync", "--mode", "update:both", tmp + "/L", tmp + "/R")
    check(rc == 0 and rf(tmp + "/R/bl.txt") == b"L-newer\n" and os.path.exists(tmp + "/L/onlyR2.txt") and os.path.exists(tmp + "/R/onlyL2.txt"), "update both")
    # right to left mirror
    mktree(tmp + "/R", {"rl.txt": ("rl\n", T0)})
    rc, _, _ = runs("sync", "--mode", "mirror:rl", tmp + "/L", tmp + "/R")
    g = subprocess.run(["diff", "-r", tmp + "/L", tmp + "/R"], capture_output=True)
    check(g.returncode == 0 and os.path.exists(tmp + "/L/rl.txt"), "mirror rl")


# ------------------------------------------------------------------ scripts
@test
def t_script(tmp):
    T0 = 1700000000
    mktree(tmp + "/L", {"a.txt": ("a\n", T0), "b.c": ("b\n", T0), "lo.txt": ("lo\n", T0 + 10), "n.txt": ("left\n", T0 + 90)})
    mktree(tmp + "/R", {"a.txt": ("a\n", T0), "b.c": ("B\n", T0 + 5), "ro.txt": ("ro\n", T0), "n.txt": ("right\n", T0)})
    s = tmp + "/s1.txt"
    wf(s, '# comment\nload "%1" "%2"\nfilter "*.txt;*.c"\ncriteria binary\nfolder-report layout:xml options:display-mismatches output-to:"%3/rep.xml"\n'
          'select left.orphan.files\ncopyto right\n')
    wf(tmp + "/out/keep", "")
    rc, o, e = runs("script", s, tmp + "/L", tmp + "/R", tmp + "/out")
    check(rc == 0, "script rc=%d %s" % (rc, e))
    rep = rf(tmp + "/out/rep.xml").decode()
    check('path="b.c"' in rep and 'path="a.txt"' not in rep and 'path="lo.txt"' in rep, "script xml report: " + rep)
    check(os.path.exists(tmp + "/R/lo.txt") and not os.path.exists(tmp + "/R/b.c.bak"), "script copyto right copied left orphan")
    # newer: copy left.newer to right via select, then delete right orphan
    wf(s, 'load "%1" "%2"\nselect left.newer.files\ncopyto right\nselect right.orphan.files\ndelete right\nlog normal "%3/log.txt"\n')
    rc, o, e = runs("script", s, tmp + "/L", tmp + "/R", tmp + "/out")
    check(rc == 0, "script2 rc=%d %s" % (rc, e))
    check(rf(tmp + "/R/n.txt") == b"left\n", "left newer copied")
    check(not os.path.exists(tmp + "/R/ro.txt"), "right orphan deleted")
    # sync + text-report on files
    wf(s, 'load "%1/n.txt" "%2/n.txt"\ntext-report layout:patch output-to:"%3/p.diff"\n')
    wf(tmp + "/R/n.txt", "other\n")
    rc, o, e = runs("script", s, tmp + "/L", tmp + "/R", tmp + "/out")
    check(rc == 0 and b"-left" in rf(tmp + "/out/p.diff") and b"+other" in rf(tmp + "/out/p.diff"), "text-report patch rc=%d %s" % (rc, e))
    wf(s, 'load "%1" "%2"\nsync mirror:left->right\n')
    rc, o, e = runs("script", s, tmp + "/L", tmp + "/R")
    check(rc == 0, "script sync rc=%d %s" % (rc, e))
    rc, _, _ = runs("dir", "-q", tmp + "/L", tmp + "/R")
    check(rc == 1, "script sync mirrors")
    wf(s, 'bogus-command\n')
    rc, o, e = runs("script", s)
    check(rc == 100, "unknown command -> 100, got %d" % rc)
    wf(s, 'load "%1" "%2"\n')
    rc, _, _ = runs("@" + s, tmp + "/L", tmp + "/R")
    check(rc in (0, 100), "@script form accepted")


# ------------------------------------------------------------------ sessions
@test
def t_sessions(tmp):
    if not os.path.exists(SESSIONS):
        return "skip"
    rc, o, e = runs("session", "--sessions", SESSIONS, "list")
    n = len([l for l in o.splitlines() if l.strip()])
    check(rc == 0 and n == 26, "session list reads real BCSessions.xml: rc=%d n=%d %s" % (rc, n, e))
    first = o.splitlines()[0].split("\t")[1].split("/")[-1]
    rc, o, e = runs("session", "--sessions", SESSIONS, "show", first)
    check(rc == 0 and "left" in o, "session show")
    cp = tmp + "/sess.xml"
    shutil.copy(SESSIONS, cp)
    wf(tmp + "/a.txt", "a\n"); wf(tmp + "/b.txt", "b\n")
    rc, _, e = runs("session", "--sessions", cp, "save", "my test", tmp + "/a.txt", tmp + "/b.txt")
    check(rc == 0, "session save " + e)
    rc, o, _ = runs("session", "--sessions", cp, "list")
    check(len(o.splitlines()) == 27 and "my test" in o, "session saved")
    rc, o, _ = runs("session", "--sessions", cp, "run", "my test")
    check(rc == 13, "session run -> 13, got %d" % rc)
    rc, _, _ = runs("session", "--sessions", cp, "delete", "my test")
    rc, o, _ = runs("session", "--sessions", cp, "list")
    check(len(o.splitlines()) == 26, "session deleted")


@test
def t_misc(tmp):
    rc, o, _ = runs("help")
    check(rc == 0 and "EXIT CODES" in o, "help")
    rc, o, _ = runs("version")
    check(rc == 0 and "mbc" in o, "version")
    wf(tmp + "/a.txt", "a\n")
    os.makedirs(tmp + "/dd", exist_ok=True)
    rc, _, _ = runs(tmp + "/a.txt", tmp + "/dd")
    check(rc == 100, "file vs folder -> 100")
    rc, _, _ = runs("--bogus", tmp + "/a.txt", tmp + "/a.txt")
    check(rc == 100, "bad option -> 100")
    # unicode path
    os.makedirs(tmp + "/中文", exist_ok=True)
    wf(tmp + "/中文/文件.txt", "x\n"); wf(tmp + "/中文/文件2.txt", "y\n")
    rc, _, _ = runs("diff", "-q", tmp + "/中文/文件.txt", tmp + "/中文/文件2.txt")
    check(rc == 13, "unicode path")


# ------------------------------------------------------------------ GUI API (mbc gui)
class Gui:
    def __init__(self, tmp, *extra):
        import re
        import socket
        s = socket.socket(); s.bind(("127.0.0.1", 0)); self.port = s.getsockname()[1]; s.close()
        self.sess = tmp + "/gui_sessions.xml"
        self.p = subprocess.Popen([MBC, "gui", "--no-open", "--port", str(self.port), "--sessions", self.sess, *extra],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        line = self.p.stdout.readline().decode("utf8", "replace")
        m = re.search(r"token=([0-9a-f]+)", line)
        self.token = m.group(1) if m else ""
        self.base = "http://127.0.0.1:%d" % self.port

    def raw(self, method, path, body=None, headers=None):
        import urllib.request
        import urllib.error
        req = urllib.request.Request(self.base + path, data=body, method=method, headers=headers or {})
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()
        except Exception as e:
            raise RuntimeError("%s %s failed: %r" % (method, path, e))

    def api(self, name, obj=None):
        import json
        st, data = self.raw("POST", "/api/" + name, json.dumps(obj or {}).encode("utf8"), {"X-MBC-Token": self.token})
        try:
            return json.loads(data.decode("utf8"))
        except Exception:
            return {"ok": False, "error": "HTTP %d %r" % (st, data[:80])}

    def close(self):
        try:
            self.api("quit")
            self.p.wait(timeout=5)
        except Exception:
            self.p.kill()


@test
def t_gui_api(tmp):
    g = Gui(tmp)
    try:
        check(bool(g.token), "gui prints token url")
        st, page = g.raw("GET", "/?token=" + g.token)
        check(st == 200 and b"<html" in page and g.token.encode() in page and b"__MBC_TOKEN__" not in page, "page served with token")
        check(g.raw("GET", "/")[0] == 403, "page without token is forbidden")
        check(g.raw("GET", "/?token=" + "0" * 32)[0] == 403, "page with wrong token is forbidden")
        check(g.raw("GET", "/?token=" + g.token, headers={"Host": "evil.example"})[0] in (400, 403), "foreign Host header rejected")
        check(g.raw("POST", "/api/info", b"{}")[0] == 403, "api without token forbidden")
        check(g.raw("POST", "/api/info", b"{}", {"X-MBC-Token": "bad"})[0] == 403, "api with bad token forbidden")
        check(g.raw("GET", "/nothing?token=" + g.token)[0] == 404, "unknown path 404")
        check(g.api("info").get("ok") is True, "info ok")
        check(g.api("nosuch").get("ok") is False, "unknown api fails cleanly")

        a, b = tmp + "/a", tmp + "/b"
        wf(a + "/f.txt", "one\ntwo\nthree\n"); wf(b + "/f.txt", "one\n2\nthree\nfour\n")
        wf(a + "/only.txt", "x\n"); wf(a + "/sub/s.txt", "same\n"); wf(b + "/sub/s.txt", "same\n")
        wf(a + "/bin.dat", bytes(range(20))); wf(b + "/bin.dat", bytes(range(19)) + b"\xff")
        check(g.api("probe", {"left": a, "right": b}).get("kind") == "dir", "probe dir")
        check(g.api("probe", {"left": a + "/f.txt", "right": b + "/f.txt"}).get("kind") == "text", "probe text")
        check(g.api("probe", {"left": a + "/bin.dat", "right": b + "/bin.dat"}).get("kind") == "hex", "probe hex")
        check(g.api("probe", {"left": a, "right": b + "/f.txt"}).get("ok") is False, "probe dir vs file fails")
        check(g.api("probe", {"left": a + "/nope", "right": b}).get("ok") is False, "probe missing fails")

        r = g.api("text", {"left": {"path": a + "/f.txt"}, "right": {"path": b + "/f.txt"}, "opts": {}})
        check(r.get("ok") and r["rows"] == [0, 0, 0, 1, 1, 2, 2, 2, 0, -1, 3, 4], "text rows " + str(r.get("rows")))
        check(r["sections"] == [[1, 2, True], [3, 4, True]] and not r["bytesEqual"], "text sections")
        check(r["left"]["enc"] == "utf-8" and r["left"]["eol"] == "LF" and r["left"]["lines"] == ["one", "two", "three"], "text side info")
        check(r["stats"]["different"] == 1 and r["stats"]["rightOnly"] == 1, "text stats")
        r = g.api("text", {"left": {"lines": ["aab"]}, "right": {"lines": ["aba"]}, "opts": {}})
        check(r.get("ok") and r["inline"], "inline diff handles repeated, reordered characters")
        r = g.api("text", {"left": {"path": a + "/f.txt"}, "right": {"path": a + "/f.txt"}, "opts": {}})
        check(r["bytesEqual"] and r["stats"]["identical"], "identical files")
        # unsaved lines override disk
        r = g.api("text", {"left": {"path": a + "/f.txt", "lines": ["one", "2", "three"], "eols": "LLL"},
                           "right": {"path": b + "/f.txt"}, "opts": {}})
        check(r.get("ok") and r["rows"][:3] == [0, 0, 0], "text with unsaved lines")
        r = g.api("text", {"left": {"path": a + "/bin.dat"}, "right": {"path": b + "/bin.dat"}, "opts": {}})
        check(r.get("binary") is True, "binary detected by text api")
        r = g.api("text", {"left": {"path": a + "/f.txt"}, "right": {"path": tmp + "/missing"}, "opts": {}})
        check(r.get("ok") is False, "text api missing file error")
        r = g.api("text", {"left": {"path": a + "/f.txt"}, "right": {"path": b + "/f.txt"}, "opts": {"ignoreCase": True, "ws": "all", "align": "patience"}})
        check(r.get("ok") is True, "text opts accepted")

        # save: encodings & EOLs round trip
        r = g.api("save", {"path": tmp + "/s1.txt", "lines": ["h\u00e9llo", "w\u00f6rld"], "eols": "CC", "enc": "utf-8-bom"})
        check(r.get("ok") and rf(tmp + "/s1.txt") == b"\xef\xbb\xbfh\xc3\xa9llo\r\nw\xc3\xb6rld\r\n", "save utf-8-bom CRLF " + str(rf(tmp + "/s1.txt")))
        r = g.api("save", {"path": tmp + "/s2.txt", "lines": ["a", "b"], "eols": "Ln", "enc": "utf-16le"})
        check(r.get("ok") and rf(tmp + "/s2.txt") == b"\xff\xfea\x00\n\x00b\x00", "save utf-16le no final newline " + str(rf(tmp + "/s2.txt")))
        r = g.api("text", {"left": {"path": tmp + "/s2.txt"}, "right": {"path": tmp + "/s1.txt"}, "opts": {}})
        check(r["left"]["enc"] == "utf-16le" and r["left"]["lines"] == ["a", "b"] and r["right"]["enc"] == "utf-8-bom" and r["right"]["eol"] == "CRLF", "reload after save keeps enc/eol")

        # dir
        r = g.api("dir", {"left": a, "right": b, "opts": {}})
        st = {e["rel"]: e["status"] for e in r["entries"]}
        check(st == {"f.txt": "different", "only.txt": "left-only", "sub": "same", "sub/s.txt": "same", "bin.dat": "different"}, "dir statuses " + str(st))
        check(r["summary"]["files"] == 4 and r["summary"]["leftOnly"] == 1, "dir summary " + str(r["summary"]))
        r = g.api("dir/op", {"left": a, "right": b, "opts": {}, "op": "copyLR", "rels": ["only.txt"], "dryRun": True})
        check(r.get("ok") and len(r["ops"]) == 1 and not os.path.exists(b + "/only.txt"), "dir op dry run does nothing")
        r = g.api("dir/op", {"left": a, "right": b, "opts": {}, "op": "copyLR", "rels": ["only.txt"], "dryRun": False})
        check(r.get("ok") and rf(b + "/only.txt") == b"x\n", "dir op copy executes")
        r = g.api("dir/op", {"left": a, "right": b, "opts": {}, "op": "deleteR", "rels": ["only.txt"], "dryRun": False})
        check(r.get("ok") and not os.path.exists(b + "/only.txt"), "dir op delete executes")
        r = g.api("dir/sync", {"left": a, "right": b, "opts": {}, "mode": "mirror:lr", "dryRun": False, "createEmpty": True})
        check(r.get("ok") and rf(b + "/f.txt") == rf(a + "/f.txt") and rf(b + "/only.txt") == b"x\n", "dir sync mirror")
        r = g.api("dir", {"left": a, "right": b, "opts": {}})
        check(all(e["status"] == "same" for e in r["entries"]), "all same after mirror")
        r = g.api("dir/op", {"left": a, "right": b, "opts": {}, "op": "bogus", "rels": ["f.txt"], "dryRun": True})
        check(r.get("ok") is False, "bad op rejected")
        r = g.api("dir/op", {"left": a, "right": b, "opts": {}, "op": "deleteL", "rels": ["../outside"], "dryRun": True})
        check(r.get("ok") is False or not r.get("ops"), "path traversal rel does not plan anything")
        for style in ("html", "csv", "xml", "tree", "list", "summary"):
            r = g.api("dir/report", {"left": a, "right": b, "opts": {}, "style": style, "show": "all"})
            check(r.get("ok") and "f.txt" in r["content"] or (style == "summary" and r.get("ok")), "dir report " + style)
        for style in ("html", "sidebyside", "unified", "context", "normal", "summary"):
            wf(tmp + "/r1.txt", "a\nb\n"); wf(tmp + "/r2.txt", "a\nc\n")
            r = g.api("text/report", {"left": tmp + "/r1.txt", "right": tmp + "/r2.txt", "style": style, "opts": {}})
            check(r.get("ok") and len(r["content"]) > 5, "text report " + style)

        # hex
        wf(tmp + "/h1", bytes(range(256)) * 40); h2 = bytearray(bytes(range(256)) * 40); h2[5000] ^= 1; h2[9000] ^= 2
        wf(tmp + "/h2", bytes(h2))
        r = g.api("hex/open", {"left": tmp + "/h1", "right": tmp + "/h2"})
        check(r.get("ok") and r["diffBytes"] == 2 and r["ranges"] == [5000, 1, 9000, 1] and not r["equal"], "hex/open " + str(r))
        r = g.api("hex/window", {"left": tmp + "/h1", "right": tmp + "/h2", "offset": 4998, "length": 4})
        check(r.get("ok") and r["left"] != r["right"] and len(r["left"]) == 8, "hex/window " + str(r))
        r = g.api("hex/window", {"left": tmp + "/h1", "right": tmp + "/h2", "offset": 10200, "length": 100})
        check(r.get("ok") and len(r["left"]) == 80, "hex/window clipped at EOF")
        r = g.api("hex/window", {"left": tmp + "/h1", "right": tmp + "/h2", "offset": 20000, "length": 100})
        check(r.get("ok") and r["left"] == "" and r["right"] == "", "hex/window past EOF is empty")

        # merge
        base = "a\nb\nc\nd\ne\n"
        wf(tmp + "/base", base); wf(tmp + "/left", "a\nB\nc\nd\ne\n"); wf(tmp + "/right", "a\nb2\nc\nd\nE\n")
        r = g.api("merge", {"left": {"path": tmp + "/left"}, "base": {"path": tmp + "/base"}, "right": {"path": tmp + "/right"}, "opts": {}})
        kinds = [x["k"] for x in r["regions"]]
        check(r.get("ok") and kinds == ["same", "conflict", "same", "right"] and r["conflicts"] == 1, "merge regions " + str(kinds))
        check(r["regions"][1]["l"] == ["B"] and r["regions"][1]["r"] == ["b2"] and r["regions"][1]["b"] == ["b"], "merge conflict content")
        check(r["regions"][3]["o"] == ["E"], "merge auto right")

        # browse / mkdir
        r = g.api("browse", {"path": tmp})
        names = [e["name"] for e in r["entries"]]
        check(r.get("ok") and "a" in names and "f" not in names and all("dir" in e for e in r["entries"]), "browse lists entries")
        check(g.api("browse", {"path": tmp + "/nope"}).get("ok") is False, "browse missing path fails")
        check(g.api("mkdir", {"path": tmp + "/newdir/deeper"}).get("ok") and os.path.isdir(tmp + "/newdir/deeper"), "mkdir")

        # sessions
        check(g.api("sessions/list")["items"] == [], "sessions start empty")
        check(g.api("sessions/save", {"name": "s1", "kind": "dir", "left": a, "right": b, "folder": ""}).get("ok"), "session save dir")
        check(g.api("sessions/save", {"name": "m1", "kind": "merge", "left": tmp + "/left", "right": tmp + "/right", "center": tmp + "/base", "folder": "grp"}).get("ok"), "session save merge")
        check(g.api("sessions/save", {"name": "", "kind": "dir"}).get("ok") is False, "empty session name rejected")
        items = g.api("sessions/list")["items"]
        check([(i["name"], i["kind"], i["folder"]) for i in items] == [("s1", "dir", ""), ("m1", "merge", "grp")] and items[1]["center"] == tmp + "/base", "sessions list " + str(items))
        check(g.api("sessions/rename", {"name": "s1", "newName": "s2"}).get("ok"), "session rename")
        check(g.api("sessions/delete", {"name": "m1"}).get("ok"), "session delete")
        check([i["name"] for i in g.api("sessions/list")["items"]] == ["s2"], "sessions after edit")
        rc, o, _ = runs("session", "--sessions", g.sess, "list")
        check(rc == 0 and "s2" in o, "cli reads gui-written sessions")
        check(g.api("sessions/delete", {"name": "zzz"}).get("ok") is False, "delete missing session fails")

        # malformed json
        st, data = g.raw("POST", "/api/text", b"{not json", {"X-MBC-Token": g.token})
        check(b'"ok":false' in data.replace(b" ", b""), "malformed json gives error json")
    finally:
        g.close()


@test
def t_gui_default_sessions(tmp):
    """Without --sessions the GUI uses ./BCSessions.xml of the working directory: created when missing,
    read when present, and new sessions are saved there."""
    import re
    import socket
    import json
    wd = os.path.join(tmp, "wd"); os.makedirs(wd)
    env = dict(os.environ); env.pop("MBC_SESSIONS", None)

    def start():
        s = socket.socket(); s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]; s.close()
        p = subprocess.Popen([MBC, "gui", "--no-open", "--port", str(port)], cwd=wd, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        line = p.stdout.readline().decode()
        g = Gui.__new__(Gui); g.port = port; g.token = re.search(r"token=([0-9a-f]+)", line).group(1)
        g.base = "http://127.0.0.1:%d" % port; g.p = p
        return g
    f = os.path.join(wd, "BCSessions.xml")
    g = start()
    check(os.path.isfile(f), "BCSessions.xml created in the working directory")
    info = g.api("info")
    check(os.path.normcase(os.path.realpath(info["sessionsPath"])) == os.path.normcase(os.path.realpath(f)), "sessionsPath is cwd file: " + info["sessionsPath"])
    check(g.api("sessions/list")["items"] == [], "new sessions file is empty")
    r = g.api("sessions/save", {"name": "s one", "kind": "text", "folder": "", "left": "/a/l.txt", "right": "/a/r.txt", "center": ""})
    check(r.get("ok"), "save session")
    check("s one" in open(f, encoding="utf8", errors="replace").read(), "session written to the file")
    g.close()
    g = start()
    names = [i["name"] for i in g.api("sessions/list")["items"]]
    check(names == ["s one"], "history read back after restart: " + str(names))
    g.close()


@test
def t_gui_start(tmp):
    wf(tmp + "/x.txt", "x\n"); wf(tmp + "/y.txt", "y\n"); wf(tmp + "/z.txt", "z\n")
    g = Gui(tmp)
    g.close()
    import socket
    blocker = socket.socket(); blocker.bind(("127.0.0.1", 0)); blocker.listen(1)
    p = subprocess.Popen([MBC, "gui", "--no-open", "--port", str(blocker.getsockname()[1]), "--sessions", tmp + "/s.xml"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        out, _ = p.communicate(timeout=10)
        check(p.returncode != 0, "busy port fails with an error exit code")
        blocker.close()
    except Exception:
        p.kill()
        check(False, "gui with unbindable port hung")
    # start args reach the page
    import re
    import socket
    s = socket.socket(); s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]; s.close()
    p = subprocess.Popen([MBC, "gui", "--no-open", "--port", str(port), "--sessions", tmp + "/s.xml", tmp + "/x.txt", tmp + "/y.txt", tmp + "/z.txt"],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    line = p.stdout.readline().decode()
    tok = re.search(r"token=([0-9a-f]+)", line).group(1)
    g2 = Gui.__new__(Gui); g2.port = port; g2.token = tok; g2.base = "http://127.0.0.1:%d" % port; g2.p = p
    info = g2.api("info")
    check(info["start"].get("left") == tmp + "/x.txt" and info["start"].get("right") == tmp + "/z.txt" and info["start"].get("base") == tmp + "/y.txt",
          "three positional args start a merge (left, base, right): " + str(info["start"]))
    g2.close()


def main():
    global skipped
    for fn in T.registry:
        if ARGS and not any(a in fn.__name__ for a in ARGS):
            continue
        tmp = tempfile.mkdtemp(prefix="mbct_")
        try:
            r = fn(tmp)
            if r == "skip":
                skipped += 1
                print("skip", fn.__name__)
            elif VERBOSE:
                print("ran", fn.__name__)
        except Exception as ex:  # noqa
            import traceback
            traceback.print_exc()
            check(False, "%s raised %r" % (fn.__name__, ex))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    print("\n%d checks passed, %d failed, %d tests skipped" % (passed, failed, skipped))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
