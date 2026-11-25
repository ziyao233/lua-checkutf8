# Benchmark

The [versioned speed corpus](bench/README.md) replays individual agent, tool,
length-sweep and malformed-input calls through both Lua APIs.

## Example Texts

Testing is done with `benchmark.lua` on some real-world texts,

|               Text                            |       Type		| Language			| Size		|
|-----------------------------------------------|-----------------------|-------------------------------|---------------|
| Dream of the Red Chamber                      | Plaintext		| Traditional Chinese		| 2.5MiB	|
| Linux v7.3 Documentation (ja_JP)		| reStructuredText	| Japanese			| 144KiB	|
| Linux v7.3 Documentation (zh_CN)		| reStructuredText	| Simplified Chinese		| 2.3MiB	|
| My Blog					| Markdown		| English/Simplified Chinese	| 124KiB	|
| Pride and Prejudice                           | Plaintext		| English			| 754KiB	|
| The Miracles of the Namiya General Store	| Plaintext		| Japanese			| 637KiB	|
| Transky					| Markdown		| Simplified Chinese		| 6.8MiB	|

For Linux's documentation, my blog, and Transky, the testing text is constructed
by `cat` them together.

`checkutf8.so` is built with Clang 21.1.8 with `-O2` optimization flags.
`benchmark.lua` is invoked with environment variable `ITERATIONS=512`.

## Testing Platforms

- Intel Core i5-7300U @ 2700MHz, LPDDR3 1866MT/s
- ARM Cortex-A53 @ 2016MHz on AMLogic A311D, LPDDR4
- ARM Cortex-A73 @ 2208MHz on AMLogic A311D, LPDDR4
- Xuantie C920v1 @ 2000MHz on Sophgo SG2042, DDR4
- Loongson LA464 @ 2500MHz on Loongson-3A5000HV, DDR4 3200MT/s

## Results

### Throughput (MiB/s)

| Text                                     | 7300U | A53 | A73 | C920 | LA464 |
|------------------------------------------|-------|-----|-----|------|-------|
| Dream of the Red Chamber                 | 1213  | 260 | 453 | 445  | 827   |
| Linux v7.3 Documentation (ja_JP)         | 1070  | 262 | 446 | 427  | 773   |
| Linux v7.3 Documentation (zh_CN)         | 1045  | 257 | 438 | 411  | 732   |
| My Blog                                  | 1111  | 269 | 459 | 419  | 775   |
| Pride and Prejudice                      | 2290  | 308 | 581 | 463  | 1074  |
| The Miracles of the Namiya General Store | 1353  | 268 | 474 | 466  | 874   |
| Transky                                  | 1307  | 267 | 469 | 461  | 860   |

### Throughput per MHz (MiB/s/MHz)

| Text                                     | 7300U | A53   | A73   | C920  | LA464 |
|------------------------------------------|-------|-------|-------|-------|-------|
| Dream of the Red Chamber                 | 0.449 | 0.129 | 0.205 | 0.223 | 0.331 |
| Linux v7.3 Documentation (ja_JP)         | 0.396 | 0.130 | 0.202 | 0.214 | 0.309 |
| Linux v7.3 Documentation (zh_CN)         | 0.387 | 0.127 | 0.198 | 0.205 | 0.293 |
| My Blog                                  | 0.412 | 0.133 | 0.208 | 0.209 | 0.310 |
| Pride and Prejudice                      | 0.848 | 0.153 | 0.263 | 0.232 | 0.430 |
| The Miracles of the Namiya General Store | 0.501 | 0.133 | 0.215 | 0.233 | 0.350 |
| Transky                                  | 0.484 | 0.132 | 0.212 | 0.230 | 0.344 |

### Wide SIMD comparison on Xeon 6966P-C

Lua 5.4.6, Clang 18.1.3, `-O2`, pinned to one core. Both modules were
loaded in the same Lua process. Times are medians over nine interleaved rounds
of calls to `validate` on the same string. The baseline is commit `ab275a6`.
The test machine supports AVX-512BW.

| Lua input | SSE2 (GiB/s) | Runtime dispatch (GiB/s) |
| --- | ---: | ---: |
| `("文"):rep(64)` | 2.94 | 5.50 |
| `("文"):rep(512)` | 3.89 | 14.92 |
| `("文"):rep(65536)` | 4.22 | 22.42 |
| `("🌛😃🚀🌃"):rep(15000)` | 3.80 | 22.31 |
| `("function hello() return true end\n"):rep(3000)` | 54.68 | 77.72 |

Scanning one 576 MiB string, `("中文段落"):rep(48 * 1024^2)`, yielded
11.50 GiB/s with runtime dispatch (median of seven rounds).

### Aligned loads and the AVX-512VBMI path

Comparison with commit `71eee70` on the same Xeon, Lua 5.4.6 and Clang 18
at `-O2`. Both modules ran in one process pinned to core 4. Each value is the
median of nine rounds of `validate` calls through Lua. Small buffers were
reused across calls. All figures below are GiB/s.

| Input | Previous | New |
| --- | ---: | ---: |
| `("微风吹过湖面，我们沿着山路慢慢往上走。"):rep(3300)` | 22.17 | 101.36 |
| `(("中"):rep(315) .. "\n"):rep(200)` | 22.46 | 35.26 |
| `("今天检查了 API 请求，发现 worker 在读完 JSON 后多等了 20 ms。"):rep(1500)` | 22.36 | 26.33 |
| `("function hello() return true end\n"):rep(3000)` | 73.98 | 122.57 |
| `("中文段落"):rep(48 * 1024^2)` (576 MiB) | 10.72 | 14.18 |
| `("Aλ中🌙"):rep(math.floor(576 * 1024^2 / 10))` (about 576 MiB) | 8.09 | 8.90 |

For 192 bytes of Chinese, the median call time was 31.99 ns before and 32.14 ns
after. With AVX2 forced on this Xeon, the long Chinese sample went from 17.37
to 17.82 GiB/s and the ASCII sample from 66.15 to 98.29 GiB/s.

### Short inputs with AVX-512VBMI

Lua 5.4.6 on Xeon 6966P-C, core 4, Clang 18 at `-O2`. The baseline is
`a4769f5`. Each group replays the fixed samples in corpus order through
`validate`, with both modules loaded and nine interleaved timing rounds.
Times are median ns per Lua call including loop overhead.

| Corpus group | Previous | Short-block path |
| --- | ---: | ---: |
| red-chamber-4cp | 24.66 | 19.22 |
| red-chamber-16cp | 32.20 | 19.24 |
| red-chamber-64cp | 34.58 | 25.04 |
| red-chamber-256cp | 60.42 | 60.14 |
| agent-session | 30.78 | 22.58 |
| text-deltas | 22.06 | 22.85 |
| raw-byte-chunks | 23.48 | 20.86 |
| tool-log-replay | 27.59 | 22.62 |

The dedicated path handles 5 through 511 bytes on CPUs supporting AVX-512BW
and VBMI. Whole-book validation took 134.8 µs before and 134.0 µs after.

### Portable repair batching

Baseline `64fa0bc`, Lua 5.4 on Xeon 6966P-C, core 4. Both modules ran the same
fixed samples in one process with the default replacement. Values are median
ns per Lua `repair` call over seven interleaved rounds, including loop overhead.

| Corpus group | Clang -O2 before | After | GCC -O2 before | After |
| --- | ---: | ---: | ---: | ---: |
| agent-session | 26.8 | 22.6 | 27.0 | 24.0 |
| text-deltas | 28.5 | 22.4 | 27.3 | 23.9 |
| raw-byte-chunks | 59.8 | 55.7 | 59.2 | 56.1 |
| red-chamber-16cp | 24.1 | 19.6 | 23.4 | 20.1 |
| red-chamber-book | 133969 | 133750 | 150469 | 150313 |
| corruption-density, all 8192 bytes invalid | 36273 | 4320 | 36656 | 4305 |
| corruption-density, every second byte invalid | 55023 | 44219 | 58766 | 41711 |
| binary | 22266 | 17598 | 21566 | 17395 |

For the all-invalid case, forcing the scalar validator on the same CPU reduced
repair time from 36.5 to 4.34 µs with Clang and from 36.4 to 4.34 µs with GCC.
GCC scalar showed regressions for agent-session (90.7 to 96.0 ns), invalid-tail
(11.2 to 13.3 µs) and one error per 1024 bytes (5.99 to 7.20 µs).

### Copying isolated three-byte replacements

Compared with `b95879e` on Xeon 6966P-C, Lua 5.4.6, core 4, GCC 13 and
Clang 18 at `-O2`. Both modules replayed the same inputs with the default
replacement. Times are median µs per Lua `repair` call over seven interleaved
rounds.

| Input | GCC before → after | Clang before → after |
| --- | ---: | ---: |
| Every second byte invalid, 8192 bytes | 42.22 → 35.82 | 43.87 → 33.50 |
| Every 13th byte invalid, 8190 bytes | 11.29 → 10.42 | 9.35 → 8.72 |
| Binary corpus | 17.18 → 16.43 | 16.69 → 14.86 |

With the scalar validator forced, the every-second-byte case took 37.66 →
30.08 µs (GCC) and 41.91 → 32.72 µs (Clang). Clang scalar's invalid-tail
group regressed from 11.73 to 12.83 µs. All-invalid runs still use the
existing batch operation, and clean inputs bypass replacement copying.

### Reusing Lua buffer reservations across repairs

Compared with `cc52ada` on Xeon 6966P-C, core 4, Lua 5.4.6, default CPU
dispatch, `-O2`. Both modules ran in the same Lua process. Values are median
µs per `repair` call over seven interleaved rounds with the default replacement.

| Input | GCC 13 before → after | Clang 18 before → after |
| --- | ---: | ---: |
| Every second byte invalid, 8192 bytes | 36.36 → 31.52 | 33.79 → 27.68 |
| Every 13th byte invalid, 8190 bytes | 11.15 → 9.98 | 8.96 → 7.58 |
| All 8192 bytes invalid | 4.49 → 4.48 | 4.35 → 4.36 |
| Binary corpus | 16.09 → 16.44 | 13.70 → 14.41 |
| Red Chamber full book | 151.00 → 150.94 | 134.53 → 133.81 |

With the scalar validator forced, the every-second-byte case took 31.77 →
25.00 µs (GCC) and 32.67 → 29.32 µs (Clang). On the same input, an empty,
one-byte or 17-byte replacement also reduced elapsed time with both compilers.

### Standalone bytes at the Lua API boundary

Compared with `5d9c5f6` on Xeon 6966P-C, core 4, Lua 5.4.6. Times are
median ns per Lua call over seven interleaved rounds, with both modules
compiled at `-O2`. The single-byte sweep calls each API once for each byte
value in order. Half of those inputs require replacement. Repair uses U+FFFD.

| Input and API | GCC 13 before → after | Clang 18 before → after |
| --- | ---: | ---: |
| Single-byte sweep, validate | 19.92 → 17.48 | 20.05 → 17.52 |
| Single-byte sweep, repair | 29.29 → 21.45 | 29.74 → 21.83 |
| Raw byte chunks, repair | 54.96 → 51.50 | 53.25 → 51.46 |
| Whole v1 corpus, repair | 229.28 → 222.89 | 224.59 → 221.19 |
| Red Chamber corpus, repair | 289.66 → 289.81 | 315.73 → 316.25 |

On the binary group, Clang repair rose from 13.58 to 14.55 µs while GCC
fell from 16.17 to 14.02 µs. Forced-scalar builds on the same machine also
reduced single-byte repair time (GCC 29.58 → 21.48 ns, Clang 31.53 → 20.99 ns).
