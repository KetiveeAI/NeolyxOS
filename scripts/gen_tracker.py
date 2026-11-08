#!/usr/bin/env python3
"""
gen_tracker.py - build docs/tracker.html, the NeolyxOS beta tracker.

The tracker is generated, never edited by hand. It reads only files in this
repository, so it always shows what `main` (or the current checkout) says:

  docs/beta/workstreams.md            streams, gaps, milestones (each stream edits its row)
  docs/STATUS.md                      what works, per area
  docs/kernel/syscall-registry.md     who owns which syscall numbers and ABI flags
  kernel/include/core/syscall_table.def   syscalls actually registered
  kernel/include/core/syscall.h       syscall numbers declared but not registered
  kernel/include/core/nx_abi.h        ABI version
  kernel/include/core/*.h             NX_ABI_FLAG_* definitions
  git log                             recently merged pull requests (optional)

Usage:  python3 scripts/gen_tracker.py [--out docs/tracker.html]
Exit status is non-zero, with a message, if a required input is missing or
cannot be parsed. Standard library only.
"""

import argparse
import datetime
import html
import os
import re
import subprocess
import sys

REPO_URL = "https://github.com/KetiveeAI/NeolyxOS"

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

INPUTS = {
    "workstreams": "docs/beta/workstreams.md",
    "status": "docs/STATUS.md",
    "registry": "docs/kernel/syscall-registry.md",
    "table": "kernel/include/core/syscall_table.def",
    "syscall_h": "kernel/include/core/syscall.h",
    "abi_h": "kernel/include/core/nx_abi.h",
}


class TrackerError(Exception):
    """An input is missing or not in the expected format."""


def log(msg):
    print("[gen_tracker] " + msg, file=sys.stderr)


def read_input(key):
    path = os.path.join(ROOT, INPUTS[key])
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.read()
    except OSError as e:
        raise TrackerError("cannot read %s: %s" % (INPUTS[key], e))


# --------------------------------------------------------------------------
# Markdown subset: headings and pipe tables, plus inline code, bold, links.
# --------------------------------------------------------------------------

def inline(text):
    """Escape text and render `code`, **bold**, [text](url) and #NN PR links."""
    out = []
    pos = 0
    # Split out code spans first so nothing inside them is rewritten.
    for m in re.finditer(r"`([^`]+)`", text):
        out.append(_inline_plain(text[pos:m.start()]))
        out.append("<code>%s</code>" % html.escape(m.group(1)))
        pos = m.end()
    out.append(_inline_plain(text[pos:]))
    return "".join(out)


def _inline_plain(text):
    s = html.escape(text, quote=False)
    s = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", s)

    def link(m):
        label, url = m.group(1), m.group(2)
        if not re.match(r"^(https?://|[A-Za-z0-9_./#-]+$)", url):
            return label  # refuse odd URL schemes
        return '<a href="%s">%s</a>' % (html.escape(url, quote=True), label)

    s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, s)
    s = re.sub(r"(?<![\w/&])#(\d{1,5})\b",
               lambda m: '<a href="%s/pull/%s">#%s</a>' % (REPO_URL, m.group(1), m.group(1)), s)
    return s


def split_row(line):
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]
    return [c.strip() for c in line.split("|")]


def parse_sections(md):
    """Return a list of (heading, level, paragraphs, tables).

    tables is a list of (header_cells, rows). Text before the first heading
    belongs to a section with heading "".
    """
    sections = []
    cur = {"heading": "", "level": 0, "text": [], "tables": []}
    lines = md.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        h = re.match(r"^(#{1,6})\s+(.*)$", line)
        if h:
            sections.append(cur)
            cur = {"heading": h.group(2).strip(), "level": len(h.group(1)), "text": [], "tables": []}
            i += 1
            continue
        if line.strip().startswith("|") and i + 1 < len(lines) and \
                re.match(r"^\s*\|?\s*:?-{3,}", lines[i + 1]):
            header = split_row(line)
            rows = []
            i += 2
            while i < len(lines) and lines[i].strip().startswith("|"):
                cells = split_row(lines[i])
                if len(cells) < len(header):
                    cells += [""] * (len(header) - len(cells))
                rows.append(cells[:len(header)])
                i += 1
            cur["tables"].append((header, rows))
            continue
        cur["text"].append(line)
        i += 1
    sections.append(cur)
    return sections


def find_section(sections, heading, source):
    for s in sections:
        if s["heading"].lower() == heading.lower():
            return s
    raise TrackerError("%s has no section '%s'" % (source, heading))


def paragraphs(lines):
    """Join plain text lines into paragraphs; list items stay separate."""
    paras, cur = [], []
    for line in lines:
        st = line.strip()
        if not st:
            if cur:
                paras.append(" ".join(cur))
                cur = []
            continue
        if st.startswith("- ") or st.startswith("> "):
            if cur:
                paras.append(" ".join(cur))
                cur = []
            paras.append(st[2:])
            continue
        cur.append(st)
    if cur:
        paras.append(" ".join(cur))
    return paras


# --------------------------------------------------------------------------
# State badges
# --------------------------------------------------------------------------

BADGES = [
    ("waiting", "wait"),
    ("not started", "todo"),
    ("not used", "bad"),
    ("not shipped", "bad"),
    ("not verified", "todo"),
    ("in progress", "prog"),
    ("in use", "good"),
    ("real", "good"),
    ("working", "good"),
    ("done", "good"),
    ("merged", "good"),
    ("released", "good"),
    ("partial", "prog"),
    ("declared", "prog"),
    ("reserved", "res"),
    ("paused", "todo"),
    ("mock", "bad"),
    ("missing", "bad"),
    ("free", "todo"),
]


def badge_class(text):
    t = text.strip().lower()
    for prefix, cls in BADGES:
        if t.startswith(prefix):
            return cls
    return None


def render_table(header, rows, badge_cols=()):
    out = ["<div class=\"tw\"><table><thead><tr>"]
    out += ["<th>%s</th>" % inline(h) for h in header]
    out.append("</tr></thead><tbody>")
    for r in rows:
        out.append("<tr>")
        for ci, cell in enumerate(r):
            cls = badge_class(cell) if header[ci] in badge_cols else None
            if cls:
                out.append("<td><span class=\"b %s\">%s</span></td>" % (cls, inline(cell)))
            else:
                out.append("<td>%s</td>" % inline(cell))
        out.append("</tr>")
    out.append("</tbody></table></div>")
    return "".join(out)


# --------------------------------------------------------------------------
# Kernel headers
# --------------------------------------------------------------------------

def parse_syscall_table(text):
    """Return a list of dicts for every NX_SYSCALL line, with its group comment."""
    calls = []
    group = ""
    for line in text.splitlines():
        c = re.match(r"^\s*/\*\s*(.*?)\s*(\*/)?\s*$", line)
        if c and not line.strip().startswith("NX_SYSCALL"):
            g = c.group(1).strip(" -*")
            if g and not g.lower().startswith(("neolyxos syscall", "master file", "format",
                                               "allocation", "copyright", "numbers")):
                group = g
            continue
        m = re.match(r'^\s*NX_SYSCALL\(\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*(\w+)\s*,\s*(\d+)\s*,\s*(\w+)\s*,\s*([^)]*)\)', line)
        if m:
            calls.append({
                "num": int(m.group(1)), "name": m.group(2), "impl": m.group(3),
                "argc": int(m.group(4)), "priv": m.group(5), "flags": m.group(6).strip(),
                "group": group,
            })
    if not calls:
        raise TrackerError("no NX_SYSCALL entries found in %s" % INPUTS["table"])
    nums = [c["num"] for c in calls]
    dups = sorted({n for n in nums if nums.count(n) > 1})
    if dups:
        raise TrackerError("duplicate syscall numbers in %s: %s" % (INPUTS["table"], dups))
    return sorted(calls, key=lambda c: c["num"])


def parse_declared(text, registered):
    """SYS_* numbers in syscall.h that are not registered in the table."""
    seen = {}
    for m in re.finditer(r"^\s*#define\s+SYS_(\w+)\s+(\d+)\s*(?:/\*\s*(.*?)\s*\*/)?", text, re.M):
        name, num, comment = m.group(1), int(m.group(2)), (m.group(3) or "")
        if name == "MAX":
            continue
        if num not in registered and num not in seen:
            seen[num] = (name, comment)
    return sorted((n, v[0], v[1]) for n, v in seen.items())


def parse_abi(text):
    major = re.search(r"#define\s+NX_ABI_MAJOR\s+(\d+)", text)
    minor = re.search(r"#define\s+NX_ABI_MINOR\s+(\d+)", text)
    if not major or not minor:
        raise TrackerError("NX_ABI_MAJOR/NX_ABI_MINOR not found in %s" % INPUTS["abi_h"])
    return "%s.%s" % (major.group(1), minor.group(1))


def parse_flags():
    inc = os.path.join(ROOT, "kernel/include/core")
    flags = []
    try:
        names = sorted(os.listdir(inc))
    except OSError as e:
        raise TrackerError("cannot list kernel/include/core: %s" % e)
    for fn in names:
        if not fn.endswith(".h"):
            continue
        try:
            with open(os.path.join(inc, fn), "r", encoding="utf-8", errors="replace") as f:
                txt = f.read()
        except OSError as e:
            log("skipping %s: %s" % (fn, e))
            continue
        for m in re.finditer(r"#define\s+(NX_ABI_FLAG_\w+)\s+(0x[0-9A-Fa-f]+)u?", txt):
            flags.append((int(m.group(2), 16), m.group(1), fn))
    return sorted(flags)


# --------------------------------------------------------------------------
# Git
# --------------------------------------------------------------------------

def git(*args):
    try:
        r = subprocess.run(["git", "-C", ROOT] + list(args), capture_output=True,
                           text=True, timeout=30, check=False)
    except (OSError, subprocess.SubprocessError) as e:
        log("git %s failed: %s" % (" ".join(args), e))
        return None
    if r.returncode != 0:
        log("git %s failed: %s" % (" ".join(args), r.stderr.strip()))
        return None
    return r.stdout


def recent_merges(limit=25):
    out = git("log", "--merges", "-n", str(limit), "--date=short",
              "--format=%h%x1f%ad%x1f%s%x1f%b%x1e")
    if out is None:
        return None
    merges = []
    for rec in out.split("\x1e"):
        rec = rec.strip()
        if not rec:
            continue
        parts = rec.split("\x1f")
        if len(parts) < 4:
            continue
        sha, date, subj, body = parts[0], parts[1], parts[2], parts[3]
        m = re.search(r"#(\d+)", subj)
        title = body.strip().splitlines()[0] if body.strip() else subj
        merges.append((date, m.group(1) if m else "", title, sha))
    return merges


# --------------------------------------------------------------------------
# Page
# --------------------------------------------------------------------------

CSS = """
body{margin:0;background:#0d1117;color:#c9d1d9;font:15px/1.55 -apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif}
.container{max-width:1200px;margin:0 auto;padding:80px 24px 48px}
h1{color:#f0f6fc;margin:0 0 4px;font-size:2rem}
h2{color:#f0f6fc;border-bottom:1px solid #30363d;padding-bottom:6px;margin-top:40px}
h3{color:#e6edf3;margin-top:24px}
a{color:#58a6ff;text-decoration:none}a:hover{text-decoration:underline}
code{background:#161b22;border:1px solid #30363d;border-radius:4px;padding:0 4px;font-size:.85em}
.sub{color:#8b949e;margin:0 0 20px}
.note{background:#161b22;border-left:3px solid #d29922;padding:8px 12px;border-radius:4px;color:#c9d1d9}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px;margin:20px 0}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:14px}
.card .n{font-size:1.7rem;font-weight:600;color:#f0f6fc}.card .l{color:#8b949e;font-size:.85rem}
.toc a{margin-right:14px}
.tw{overflow-x:auto}
table{border-collapse:collapse;width:100%;margin:10px 0 18px;font-size:.9rem}
th,td{border:1px solid #30363d;padding:6px 9px;text-align:left;vertical-align:top}
th{background:#161b22;color:#f0f6fc}tr:nth-child(even) td{background:#0f141b}
.b{display:inline-block;padding:1px 8px;border-radius:10px;font-size:.8rem;font-weight:600;white-space:nowrap}
.good{background:#1f6f3b;color:#d2f8dc}.prog{background:#7a5a10;color:#fff3c4}
.bad{background:#7d2121;color:#ffd7d5}.todo{background:#30363d;color:#c9d1d9}
.res{background:#1c3d6e;color:#cfe3ff}.wait{background:#5a2d82;color:#ecdcff}
footer{margin-top:48px;color:#8b949e;font-size:.85rem}
"""


def build(out_path):
    ws = parse_sections(read_input("workstreams"))
    st = parse_sections(read_input("status"))
    reg = parse_sections(read_input("registry"))
    calls = parse_syscall_table(read_input("table"))
    registered = {c["num"] for c in calls}
    declared = parse_declared(read_input("syscall_h"), registered)
    abi = parse_abi(read_input("abi_h"))
    flags = parse_flags()
    merges = recent_merges()

    head = (git("rev-parse", "--short", "HEAD") or "").strip() or "unknown"
    branch = (git("rev-parse", "--abbrev-ref", "HEAD") or "").strip() or "unknown"
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")

    streams = find_section(ws, "Streams", INPUTS["workstreams"])
    gaps = find_section(ws, "What the beta still needs", INPUTS["workstreams"])
    miles = find_section(ws, "Milestones", INPUTS["workstreams"])
    for s, name in ((streams, "Streams"), (gaps, "What the beta still needs"), (miles, "Milestones")):
        if not s["tables"]:
            raise TrackerError("%s section '%s' has no table" % (INPUTS["workstreams"], name))

    # Status counts
    counts = {"good": 0, "prog": 0, "bad": 0}
    status_html = []
    audit = ""
    for s in st:
        if s["level"] == 0 or s["level"] == 1:
            for p in paragraphs(s["text"]):
                if "audited" in p.lower():
                    audit = p
        if not s["tables"]:
            continue
        status_html.append("<h3>%s</h3>" % inline(s["heading"]))
        for header, rows in s["tables"]:
            status_html.append(render_table(header, rows, badge_cols=("State",)))
            if "State" in header:
                ci = header.index("State")
                for r in rows:
                    cls = badge_class(r[ci])
                    if cls in ("good",):
                        counts["good"] += 1
                    elif cls in ("prog",):
                        counts["prog"] += 1
                    elif cls in ("bad", "todo"):
                        counts["bad"] += 1

    s_hdr, s_rows = streams["tables"][0]
    active = sum(1 for r in s_rows if badge_class(r[s_hdr.index("State")] if "State" in s_hdr else "") == "prog")
    g_hdr, g_rows = gaps["tables"][0]
    open_gaps = sum(1 for r in g_rows if badge_class(r[g_hdr.index("State")] if "State" in g_hdr else "") != "good")

    p = []
    p.append("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"UTF-8\">\n"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
             "<title>NeolyxOS Beta Tracker</title>\n"
             "<link rel=\"stylesheet\" href=\"assets/navbar.css\">\n"
             "<script src=\"assets/navbar.js\"></script>\n<style>%s</style>\n</head>\n<body>\n"
             "<!-- Generated by scripts/gen_tracker.py. Do not edit by hand. -->\n"
             "<div class=\"container\">\n" % CSS)
    p.append("<h1>NeolyxOS Beta Tracker</h1>")
    p.append("<p class=\"sub\">Generated %s from <code>%s</code> at <code>%s</code>. "
             "ABI <strong>%s</strong>. Rebuild with <code>python3 scripts/gen_tracker.py</code>.</p>"
             % (html.escape(now), html.escape(branch), html.escape(head), html.escape(abi)))
    p.append("<p class=\"toc\"><a href=\"#streams\">Streams</a><a href=\"#gaps\">What is missing</a>"
             "<a href=\"#milestones\">Milestones</a><a href=\"#status\">System status</a>"
             "<a href=\"#syscalls\">Syscalls</a><a href=\"#abi\">ABI flags</a>"
             "<a href=\"#merged\">Recently merged</a></p>")
    p.append("<div class=\"cards\">")
    for n, label in ((active, "streams in progress"), (open_gaps, "beta gaps open"),
                     (counts["good"], "areas working"), (counts["prog"], "areas partial"),
                     (counts["bad"], "areas mock, missing or unverified"),
                     (len(calls), "syscalls registered"), (len(declared), "declared, not registered")):
        p.append("<div class=\"card\"><div class=\"n\">%d</div><div class=\"l\">%s</div></div>" % (n, label))
    p.append("</div>")

    intro = paragraphs(ws[0]["text"]) if ws and ws[0]["level"] == 0 else []
    for s in ws:
        if s["level"] == 1:
            intro = paragraphs(s["text"])
            break
    for para in intro:
        p.append("<p>%s</p>" % inline(para))

    p.append("<h2 id=\"streams\">Streams</h2>")
    p.append(render_table(s_hdr, s_rows, badge_cols=("State",)))
    p.append("<h2 id=\"gaps\">What the beta still needs</h2>")
    p.append(render_table(g_hdr, g_rows, badge_cols=("State",)))
    m_hdr, m_rows = miles["tables"][0]
    p.append("<h2 id=\"milestones\">Milestones</h2>")
    p.append(render_table(m_hdr, m_rows, badge_cols=("State",)))

    p.append("<h2 id=\"status\">System status</h2>")
    p.append("<p class=\"note\">From <a href=\"STATUS.md\">docs/STATUS.md</a>. %s "
             "Each stream updates its rows when its work lands.</p>"
             % inline(audit or "Audit date not stated."))
    p.extend(status_html)

    p.append("<h2 id=\"syscalls\">Syscalls</h2>")
    p.append("<h3>Registered on this build (%d)</h3>" % len(calls))
    rows = [[str(c["num"]), "`%s`" % c["name"], str(c["argc"]),
             "system only" if c["priv"] != "PRIV_USER" else "user", c["group"]] for c in calls]
    p.append(render_table(["#", "Name", "Args", "Who may call", "Group"], rows))
    p.append("<h3>Declared in syscall.h but not registered (%d)</h3>" % len(declared))
    p.append("<p>These numbers have a <code>#define</code> but no handler; calls return "
             "<code>-ENOSYS</code>.</p>")
    p.append(render_table(["#", "Define", "Comment"],
                          [[str(n), "`SYS_%s`" % name, comment] for n, name, comment in declared]))
    try:
        blocks = find_section(reg, "Syscall numbers", INPUTS["registry"])
        if blocks["tables"]:
            p.append("<h3>Number blocks by stream</h3>")
            p.append("<p>From <a href=\"kernel/syscall-registry.md\">docs/kernel/syscall-registry.md</a>, "
                     "the only place numbers are allocated.</p>")
            h, r = blocks["tables"][0]
            p.append(render_table(h, r, badge_cols=("State on `main`",)))
    except TrackerError as e:
        log(str(e))
        p.append("<p class=\"note\">Registry table not found: %s</p>" % html.escape(str(e)))

    p.append("<h2 id=\"abi\">ABI version and flags</h2>")
    p.append("<p>Current ABI: <strong>%s</strong>. Apps test a flag with <code>abi.query</code> "
             "(syscall 0), never the minor version.</p>" % html.escape(abi))
    p.append("<h3>Flags defined on this build</h3>")
    p.append(render_table(["Flag", "Name", "Header"],
                          [["0x%04X" % v, "`%s`" % n, "`%s`" % fn] for v, n, fn in flags]))
    for heading in ("ABI capability flags", "ABI versions"):
        try:
            s = find_section(reg, heading, INPUTS["registry"])
            if s["tables"]:
                p.append("<h3>%s (registry)</h3>" % inline(heading))
                h, r = s["tables"][0]
                p.append(render_table(h, r, badge_cols=("State on `main`", "State")))
        except TrackerError as e:
            log(str(e))

    p.append("<h2 id=\"merged\">Recently merged</h2>")
    if merges is None:
        p.append("<p class=\"note\">git history was not available when this page was built.</p>")
    else:
        p.append(render_table(["Date", "PR", "Title"],
                              [[d, ("#" + n) if n else sha, t] for d, n, t, sha in merges]))

    p.append("<footer>Sources: <code>docs/beta/workstreams.md</code>, <code>docs/STATUS.md</code>, "
             "<code>docs/kernel/syscall-registry.md</code>, <code>kernel/include/core/</code>, git log. "
             "<a href=\"index.html\">Back to the docs</a>.</footer>")
    p.append("</div>\n</body>\n</html>\n")

    data = "".join(p)
    tmp = out_path + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(data)
        os.replace(tmp, out_path)
    except OSError as e:
        raise TrackerError("cannot write %s: %s" % (out_path, e))
    log("wrote %s (%d bytes, %d syscalls, ABI %s)" % (os.path.relpath(out_path, ROOT), len(data), len(calls), abi))


def main():
    ap = argparse.ArgumentParser(description="Build docs/tracker.html")
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "tracker.html"))
    args = ap.parse_args()
    try:
        build(os.path.abspath(args.out))
    except TrackerError as e:
        log("error: %s" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
