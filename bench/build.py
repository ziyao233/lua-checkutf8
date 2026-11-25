"""Build the version 1 UTF-8 benchmark corpus using Python's standard library."""
import argparse
import codecs
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SOURCE_REV = "39ca359505aa5aeb8ceafddcd88a51e2ffd030b7"
ZH = ("雨停以后，检修组沿着河岸走到旧车站。记录员先核对了昨日的水位，"
      "又把今天发现的裂缝标在图上。两组读数有些出入，大家决定明早再测一次。")
MIX = ("本轮检查读取了 config.json 和 3 份日志。worker 在等待 API 响应时耗时 28 ms，"
       "第二次请求成功。Résumé 和 café 两个标签均可检索，状态为 ready ✅。")
WORLD = "English · Français · Ελληνικά · العربية · हिन्दी · 日本語 · 한국어 · 中文 · 𠀀 · 👩🏽‍💻 · e\u0301\n"
SIZES = (0, 1, 2, 3, 4, 8, 12, 15, 16, 17, 31, 32, 33, 63, 64, 65, 79, 80, 81,
         127, 128, 129, 191, 192, 193, 255, 256, 257, 511, 512, 513, 1024,
         2047, 2048, 2049, 4096, 16384, 65536)


def exact(text, size):
    unit = text.encode()
    raw = (unit * (size // len(unit) + 1))[:size]
    prefix = raw.decode("utf-8", "ignore").encode()
    return prefix.ljust(size, b".")


def events(i):
    file = f"report-{i:03}.json"
    return [
        f"请检查 {file} 中的延迟记录，比较两次采样，用中文总结。",
        json.dumps({"tool": "read_file", "arguments": {"path": file}}, separators=(",", ":")),
        json.dumps({"file": file, "samples": [18 + i, 28 + i], "unit": "ms",
                    "labels": ["网络", "café", "🛰️"]}, ensure_ascii=False),
        f"## 检查结果\n\n第一次为 {18+i} ms，第二次为 {28+i} ms，相差 10 ms。\n\n{MIX}",
    ]


def chunks(data, widths):
    start = 0
    index = 0
    while start < len(data):
        width = widths[index % len(widths)]
        yield data[start:start + width]
        start += width
        index += 1


def groups():
    sessions = [events(i) for i in range(24)]
    replies = [session[-1] for session in sessions]
    yield "agent-session", [s.encode() for session in sessions for s in session]
    yield "text-deltas", [part.encode() for s in replies for part in chunks(s, (1, 2, 5, 1, 8, 3))]
    yield "raw-byte-chunks", [part for s in replies for part in chunks(s.encode(), (1, 7, 13, 16, 31))]
    for name, text in [("ascii", "local value = result[id] or false\n"), ("chinese", ZH),
                       ("mixed", MIX), ("multilingual", WORLD)]:
        yield "length-" + name, [exact(text, n) for n in SIZES]

    yield "chinese-paragraphs", [exact((ZH + sep) * 3 + MIX + "\n", n)
                                 for sep in ("\n", "\n\n", "……") for n in (512, 4096, 65536)]
    yield "tool-jsonl", [("\n".join(events(j)[2] for j in range(n)) + "\n").encode()
                         for n in (1, 8, 64, 512)]
    logs = []
    for i in range(256):
        body = (f"[worker-{i % 8}] request={i:04x} elapsed={12 + i % 37}ms "
                f"path=/reports/{i % 13}.json 读取完成").encode()
        logs.append(body[:-1] if i % 64 == 63 else body)
    yield "tool-log-replay", logs
    # Preserve real source layout and line lengths from a fixed upstream revision.
    docs = [subprocess.check_output(["git", "-C", str(ROOT), "show", f"{SOURCE_REV}:{f}"])
            for f in ("README.md", "checkutf8.c", "checkutf8-impl.h", "benchmark.lua")]
    yield "repo-files", docs
    yield "repo-lines", [line for doc in docs for line in doc.splitlines(keepends=True)]
    bag = ("a", "é", "汉", "🚀")
    draws = hashlib.shake_256(b"lua-checkutf8-widths-v1").digest(32768)
    yield "shuffled-widths", ["".join(bag[b % 4] for b in draws[:n]).encode()
                              for n in (64, 1024, 8192, 32768)]
    bad = [b"\x80", b"\xc0\xaf", b"\xe0\x9f\xbf", b"\xed\xa0\x80",
           b"\xf4\x90\x80\x80", b"\xf5\x80\x80\x80", b"\xff"]
    for where in ("head", "middle", "tail"):
        rows = []
        for n in (32, 256, 4096, 65536):
            for token in bad:
                available = n - len(token)
                k = {"head": 0, "middle": available // 2, "tail": available}[where]
                rows.append(exact(MIX, k) + token + exact(ZH, available - k))
        yield "invalid-" + where, rows
    yield "truncated", [exact(MIX, n) + ch.encode()[:k] for n in (0, 15, 63, 191, 511, 2047)
                        for ch in ("é", "中", "🌍") for k in range(1, len(ch.encode()))]
    yield "corruption-density", [(exact(MIX, gap) + b"\xff") * (8192 // (gap + 1))
                                 for gap in (0, 1, 12, 64, 1023)]
    yield "binary", [b"\x00" * 128, "A\x00中\x00🙂".encode(),
                     bytes(range(256)) * 16, hashlib.shake_256(b"binary-v1").digest(8192)]


def replace_byte(exc):
    return "\ufffd", exc.start + 1


codecs.register_error("checkutf8_per_byte", replace_byte)


def oracle(raw):
    try:
        raw.decode("utf-8")
        return 0, raw
    except UnicodeDecodeError as exc:
        first = exc.start + 1
    return first, raw.decode("utf-8", "checkutf8_per_byte").encode()


def fnv32(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def build(dest, corpus=None):
    dest.mkdir(parents=True, exist_ok=True)
    with (dest / "inputs.bin").open("wb") as blob, (dest / "index.tsv").open("wb") as index:
        index.write(b"group\toffset\tbytes\tfirst_error\trepair_fnv32\n")
        for group, records in (groups() if corpus is None else corpus):
            for raw in records:
                first, repaired = oracle(raw)
                line = f"{group}\t{blob.tell()}\t{len(raw)}\t{first}\t{fnv32(repaired):08x}\n"
                index.write(line.encode("ascii"))
                blob.write(raw)
    files = ("index.tsv", "inputs.bin")
    (dest / "SHA256SUMS").write_bytes("".join(
        f"{hashlib.sha256((dest / f).read_bytes()).hexdigest()}  {f}\n" for f in files).encode())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--stream-mib", type=int, default=0,
                        help="also write a valid mixed UTF-8 file of this size for benchmark.lua")
    args = parser.parse_args()
    if args.stream_mib < 0:
        parser.error("--stream-mib must be nonnegative")
    build(args.destination)
    if args.stream_mib:
        block = exact(ZH + "\n" + MIX + "\n" + WORLD, 1024**2)
        with (args.destination / f"stream-{args.stream_mib}MiB.txt").open("wb") as stream:
            for _ in range(args.stream_mib):
                stream.write(block)
