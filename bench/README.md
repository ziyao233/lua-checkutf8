# UTF-8 speed corpus v1

Each index row is one Lua API call. The checked-in corpus is ready to replay
offline; only rebuilding it requires Python 3 and the pinned upstream Git
revision. The agent sessions, text deltas and tool logs are deterministic
synthetic scenarios. The file and line samples come from the project's actual
upstream source. Group counts describe this fixture and provide no estimate
of production traffic shares.

From the checkout with the module built or installed:

```sh
(cd bench/v1 && sha256sum -c SHA256SUMS)
lua bench/replay.lua check
lua bench/replay.lua validate bench/v1 agent-session
lua bench/replay.lua repair bench/v1 tool-log-replay
lua bench/replay.lua validate bench/v1 length-chinese/192B
```

Omit the group to run all groups, or select a length family such as
`length-mixed` for its full sweep. Compare modules in separate invocations
using the same corpus, Lua version, compiler flags and CPU affinity. For
example, set `LUA_CPATH='/path/to/build/?.so;;'` for each run.

| Group | Inputs |
| --- | --- |
| agent-session | 24 ordered exchanges with user text, tool arguments, a tool result and a Markdown answer; one call per event |
| text-deltas, raw-byte-chunks | The same replies split at code point boundaries or transport byte boundaries. These are standalone calls, so a split inside a character can legitimately fail |
| length-ascii, length-chinese, length-mixed, length-multilingual | 38 exact byte sizes per family from 0 to 65,536, including neighbors of 16, 32, 64, 80, 192, 512 and 2,048. Each size is reported separately. A partial final code point is replaced with ASCII padding |
| chinese-paragraphs, shuffled-widths | Chinese with line breaks and mixed passages; deterministic shuffled one-, two-, three- and four-byte characters |
| tool-jsonl, tool-log-replay | JSONL batches of 1 to 512 rows; 256 ordered log records with the final code point truncated in every 64th record |
| repo-files, repo-lines | README, C sources and Lua benchmark from upstream revision `39ca359`, replayed as whole files or individual lines including line endings |
| invalid-head, invalid-middle, invalid-tail | Matched total lengths and corruption types; misplaced trailers, overlong forms, surrogates, out-of-range code points and illegal lead bytes |
| truncated, corruption-density, binary | Incomplete two- to four-byte characters; varying spacing between bad bytes; embedded NUL, all byte values, and deterministic binary noise |

`v1/inputs.bin` stores the bytes verbatim. `v1/index.tsv` provides the group,
zero-based blob offset, byte length, first invalid sequence's one-based byte
offset (zero for valid), and a 32-bit FNV-1a fingerprint of the expected
default repair output. Python's strict decoder supplies the error offset; a
custom decoder error handler advances one byte per replacement to match the
library. SHA-256 covers both files.

Replay verifies both APIs before timing. Timing excludes I/O, slicing and
oracle checks, includes Lua loop and call overhead, and uses the median of
five CPU-time rounds after calibration. MiB/s uses input bytes for full-scan
cases. Validation groups containing an invalid input report ns/call alone
because validation may return early. Repair includes allocation and normal
Lua garbage collection. Results for the default corpus are hot replay.

To rebuild and compare byte for byte:

```sh
python3 bench/build.py /tmp/checkutf8-v1
cmp bench/v1/index.tsv /tmp/checkutf8-v1/index.tsv
cmp bench/v1/inputs.bin /tmp/checkutf8-v1/inputs.bin
```

For a separate working-set benchmark, generate a large valid file in a local
scratch directory sized for the run. The optional file repeats a one-MiB
block of Chinese and multilingual text; it is outside the versioned corpus
checksums. The existing whole-file driver can scan it directly:

```sh
python3 bench/build.py /tmp/checkutf8-v1 --stream-mib 576
ITERATIONS=7 lua benchmark.lua /tmp/checkutf8-v1/stream-576MiB.txt
```

## Traditional Chinese fiction supplement

The upstream benchmark lists a 2.5 MiB Traditional Chinese plaintext copy
of *Dream of the Red Chamber*. Its exact edition and checksum are unspecified.
This independent supplement uses Cao Xueqin's *紅樓夢*, digitized by Wei-yi Kao,
Project Gutenberg ebook 24264, updated 2021-12-22.
[Source edition](https://www.gutenberg.org/ebooks/24264)

The 851 calls cover six exact lengths (1, 4, 16, 64, 256 and 1,024 Unicode
code points, one excerpt per chapter at the same anchor for each length),
118 indented paragraphs, 12 whole chapters spaced from chapter 1 to 120,
and the entire 2,615,302-byte novel. The two chapters without a matching
indented paragraph remain in the length sweep and full book. The body
runs from the first chapter heading to the end marker, with LF line endings
and one final newline. Internal hard wraps, punctuation and spaces are preserved.

Run all nine lengths and groupings offline from the checkout:

```sh
lua bench/replay.lua validate bench/red-chamber-v1
lua bench/replay.lua repair bench/red-chamber-v1 red-chamber-book
```

To rebuild from the pinned public archive, the builder checks the input SHA-256
before extracting the book:

```sh
curl -fL https://www.mirrorservice.org/sites/ftp.ibiblio.org/pub/docs/books/gutenberg/2/4/2/6/24264/24264-0.txt -o /tmp/24264-0.txt
python3 bench/build_red_chamber.py /tmp/24264-0.txt /tmp/red-chamber-v1
cmp bench/red-chamber-v1/inputs.bin /tmp/red-chamber-v1/inputs.bin
```
