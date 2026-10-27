# Testing MIM

Everything about MIM's tests: how to run them, what each one covers, the techniques they use,
what was measured about them, and what they still don't catch.

## 1. Running the tests

```sh
cmake --preset debug             # or clang, asan, tsan, valgrind, fuzz
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
ctest --test-dir build/debug -R test_guest   # one test
```

The build needs clang with the wasm32 target and `wasm-ld` (lld), and Rust with
`rustup target add wasm32v1-none`: the guests are built before the tests that run them.

| Preset | Compiler | What it adds | What it catches |
|---|---|---|---|
| `debug` | default (GCC) | `-Wall -Wextra -Werror -pedantic`, strict C99 | test failures, warnings |
| `clang` | clang | the same flags | what GCC accepts and clang doesn't (e.g. glibc's `_Generic` `strchr` under `-pedantic`) |
| `asan` | default | AddressSanitizer + UBSan, no recovery | out-of-bounds, use-after-free, leaks, undefined behaviour; also MIM's own poisoned guard bytes |
| `tsan` | default | ThreadSanitizer + UBSan | data races, wrong memory orders |
| `valgrind` | default | every test runs under `valgrind --leak-check=full --errors-for-leak-kinds=all` | uninitialised reads, leaks, without recompiling |
| `fuzz` | clang `-O1` | ASan + libFuzzer; `fuzz_mem` becomes a real fuzzer | allocator bugs found by coverage-guided inputs |

Every preset is a Debug build, so `assert` is on everywhere.

### CI

`.github/workflows/ci.yml` runs all six presets on `ubuntu-26.04` (the first Ubuntu that packages
SDL3) on every push to `main` and every pull request
(`fail-fast: false`, so one red job doesn't hide another). On top of `ctest`:

- **Format** (`debug` job): `clang-format --dry-run -Werror` over every tracked `.c`, `.h`, `.cpp`
  outside `extern/`.
- **Platform boundary** (`debug` job): fails if `pthread.h`, `unistd.h`, `fcntl.h`, `time.h`,
  `sys/*` or `SDL3/*` is included anywhere except `platform/`, `tests/` and `extern/`.
- **TSan memory layout** (`tsan` job): `sysctl vm.mmap_rnd_bits=28`. Older TSan runtimes abort with
  "unexpected memory mapping" when the kernel uses 32 bits of mmap randomisation.
- **Guest log on failure**: `test_guest` sends stderr to `build/<preset>/tests/test_guest.log`
  while a guest runs, so a sanitizer report from inside a guest call is only there. CI prints it
  when a job fails.
- **Fuzzing** (`fuzz` job): 60 s of `fuzz_mem` after `ctest`; crashing inputs are uploaded as the
  `fuzz-crashes` artifact.

## 2. The framework

`tests/mim_test.h` is the whole framework: one `CHECK(cond)` macro that prints
`file:line: CHECK failed: cond` and exits with 1. Each test is its own executable
(`mim_add_test(name)` in `tests/CMakeLists.txt` builds `name.c` and links `mim_mem`, which brings
the platform layer), so the first failing check fails that test and nothing else. Under the
`valgrind` preset, `mim_add_test` puts `valgrind` in front of the command (`run_under`).

There are no fixtures and no test registry: `main` calls the test functions in order.

## 3. What each test covers

| Test | Covers |
|---|---|
| `test_smoke` | the toolchain and sanitizers themselves work (malloc, memcpy, free) |
| `test_pl_time` | `pl_time_now_ns` never goes backwards; `pl_sleep_ms` sleeps at least as long as asked |
| `test_pl_log` | line format `<D\|I\|W\|E> <s>.<ms> msg\n`, one line per call, all four severities, cut-off at 511 bytes with `...` |
| `test_pl_file` | size and read; a file that exactly fits vs. one byte too big; empty files; `NULL, 0` buffers; missing file; a directory is not a file |
| `test_pl_thread` | return values through join, thread names over 15 chars, 8 threads × 100 000 locked increments |
| `test_pl_atomic` | every atomic operation and memory order, failed CAS reports the current value, wrap-around, 8-thread lock-free counter, release/acquire message passing |
| `test_pl_mem` | mapping is page-aligned, zeroed and writable to its last byte; an impossible size returns `out_of_memory` |
| `test_pl_tick` | the time event is always last and never goes backwards; SIGINT and SIGTERM each give exactly one quit event; absolute-deadline sleep is never early and returns at once for a past deadline |
| `test_mem_init` | budgets in order, each 64-byte aligned and rounded up, packed back to back |
| `test_mem_bump` | 16-byte alignment, unique 0-byte allocations, exactly-full then one more fails, reset, `mem_try_grow` (in place, not bigger, out of room, 0 bytes grown to 1), fill patterns 0xCD/0xDD, double shutdown |
| `test_mem_heap` | alignment, fill, split, first fit reuses a freed block, coalescing in all three orders, `largest_free` is exact, leak count at shutdown |
| `test_mem_death` | every misuse aborts with the right message (section 4.2) |
| `fuzz_mem` | random alloc/free/grow/reset sequences on a heap and a bump budget (section 4.3) |
| `test_loop` | `dt` rules (1 ms for the first frame, for a 0 gap and for a gap over 1 s; exactly 1 s is real), the 60 Hz grid doesn't drift, an overrun re-anchors it |
| `test_pl_display` | with SDL's `dummy` driver (no display or GPU): open a window, present two frames, a pushed `SDL_EVENT_QUIT` comes out of `pl_tick` as one `pl_event_quit`, close, open again. Not under valgrind (section 5) |
| `test_guest` | the C++ and Rust guests log identical lines and leave the framebuffer untouched; every hostile guest (section 3.1); garbage and truncated modules; a fake clock through `loop_begin`; `bounce_rust`'s box is at the pixel its time puts it, at both ends and on the way back; both budgets are empty after every guest |
| `test_mim` | the `mim` binary end to end (section 4.5) |

### 3.1 Hostile guests

`guests/evil/` holds one WASM module per attack. Each is `<case>.c` plus `common.c` (the normal
`app_tick` and `app_shutdown`, and the `memset` clang emits calls to), unless `guests/CMakeLists.txt`
says otherwise. `test_guest` checks whether each one loads and what it logged.

| Guest | Does | Expected |
|---|---|---|
| `bad_pointer` | logs from `0x7FFFFFF0`, far outside memory | trap: buffer outside guest memory |
| `wrapping_pointer` | `ptr + len` wraps around 32 bits | trap: buffer outside guest memory |
| `log_edge` | logs the last 4 bytes of memory, then 4 bytes one further | first logs `edge`, then traps |
| `huge_len` | a valid `ptr` with `len = 0xFFFFFFFF`, so `size - len` wraps | trap: buffer outside guest memory |
| `bad_level` | log level 7 | trap: bad level |
| `log_injection` | a message containing `\n`, a fake log prefix and DEL | one line, control bytes shown as `?` |
| `log_long` | a 300-byte message | 200 bytes, then `...` |
| `greedy` | grows memory one page at a time until refused, checks each new page is zero, dirties it | loads; refused at the budget; run twice, the second run still sees zeroed pages |
| `recursion` | unbounded recursion | trap: stack overflow |
| `unknown_import` | imports `mim.nope` | trap when called: function body is missing |
| `missing_export` | no `app_tick` | rejected |
| `wrong_signature` | `app_tick(i32)`: wrong argument count and result | rejected |
| `wrong_types` | `app_tick(i64, i64)`: right count, wrong type | rejected |
| `wrong_import` | imports `mim.log(i32)` | rejected: signature mismatch |
| `huge_memory` | asks for 64 MiB at start (link flag) | rejected: memory allocation failed |
| `imported_memory` | imports its memory instead of defining it (link flag) | rejected |
| `tick_trap` | traps in `app_tick` | dead from then on; `app_shutdown` is not called |
| `present_bad_size` | presents a 640 × 480 frame | trap: frame must be 1280x720 |
| `present_bad_pointer` | presents from 100 bytes before the end of memory | trap: frame outside guest memory |
| `present_wrapping` | presents from `0xFFC7C100`, so `ptr + len` wraps around 32 bits to `0x100`, inside memory | trap: frame outside guest memory |

`test_guest` also feeds 256 bytes of noise and the first half of a real module: both rejected.

## 4. Techniques

### 4.1 Capturing stderr

`pl_log` writes straight to file descriptor 2, so tests redirect it with `dup`/`dup2`:
`test_pl_log` into a pipe, `test_guest` into a file (which stays on disk for crash reports).
`test_guest` keeps only the `app:` and `guest:` lines and drops the timestamps, so its
expected strings are exact.

### 4.2 Death tests

`expect_death(fn, how, expected)` in `test_mem_death.c` forks, runs `fn` in the child with stderr
on a pipe, and checks both how the child died and that `expected` appears in what it printed:

- `death_abort`: mem.c's own check fired (`pl_log`, then `abort()`, so SIGABRT).
- `death_asan`: under ASan, a write into poisoned memory is stopped before mem.c's check runs; ASan
  reports and exits non-zero. `damaged_guard` and `write_after_free` expect this under ASan and
  `death_abort` otherwise.

The child sets `RLIMIT_CORE` to 0 so expected deaths don't dump core. Cases:

| Case | Message |
|---|---|
| allocation over the budget | `is full: wanted 5000 bytes`, `largest free block` |
| double free, and double free after the block merged | `double free`, `double free after a merge` |
| free list pointing at a used block | `free list damaged` |
| foreign pointer, `mem_free` on a bump budget, `mem_reset` on a heap | `is not from this budget`, `is a bump budget`, `is a heap budget` |
| `mem_try_grow` on an older allocation | `not the last allocation` |
| byte before the data overwritten | `is not a live block` |
| byte after the data overwritten | `guard bytes ... damaged` |
| write after free, then `mem_check` | `written after free` |
| `mem_check` with two free blocks in a row | `was not merged with the one before it` |
| `mem_check` with a free block not on the free list | `free list doesn't match the heap` |
| `mem_check` with a wrong counter | `counters don't match the heap` |

The last three test the checker itself. `fuzz_mem` relies on `mem_check` to notice corruption,
so its detectors need tests of their own.

### 4.3 Fuzzing with and without libFuzzer

`fuzz_mem.c` defines `LLVMFuzzerTestOneInput`. Every 4 input bytes are one operation (op, slot,
2 bytes of size; bit 3 of op picks the bump budget). Heap allocations are filled with their slot
number and checked before they're freed; the last bump allocation is checked before each bump
operation. Overlapping allocations show up as wrong bytes. Each input must leave the heap as one
free block.

- With `MIM_FUZZ` (the `fuzz` preset) it is a libFuzzer target, and `mim_mem` is built with
  `-fsanitize=fuzzer-no-link` so the fuzzer sees the allocator's branches, not only the harness's.
  `ctest` runs 10 000 inputs; CI then fuzzes for 60 s.
- Without it, `main` feeds 2000 inputs of 256 bytes from a fixed-seed xorshift generator. So every
  preset, valgrind included, runs the same harness.

### 4.4 A fake clock

`runtime/loop` takes timestamps as arguments and never reads a clock, so `test_loop` and
`test_guest`'s `test_fake_clock` feed it any times they like (including a 5 s "debugger pause")
and check the `abs_time_ms` and `dt` the guests receive.

### 4.5 The binary end to end

`tests/test_mim.sh <guest dir> <command...>` runs the real `mim`, always with `--headless`, so it
never opens a window and runs under valgrind too:

1. `hello_rust` exits 0, logs `bye after 3 ticks`, and takes at least 33 ms (3 frames at 60 Hz):
   this catches a main loop that stops pacing.
2. `evil_tick_trap` exits non-zero.
3. `evil_log_injection` never quits by itself. After its first tick (so the signal handler is in
   place) it gets SIGINT and must exit 0. The test has a 60 s timeout, so a `mim` that ignores
   Ctrl-C fails instead of hanging.

## 5. Sanitizer and tool notes

- **Valgrind can't see inside the budgets.** All engine memory is one `mmap`, so to valgrind every
  budget is one valid block. Inside it, mem.c's guard bytes, fill patterns and `mem_check` do
  that job, and under ASan mem.c poisons guards and freed memory with
  `ASAN_POISON_MEMORY_REGION`.
- **wasm3 and clang's UBSan.** wasm3 calls callbacks through a generic function pointer type,
  which `-fsanitize=function` reports. It is turned off for the wasm3 target only.
- **wasm3 and TSan without `musttail`.** GCC before 15 has no `musttail`, so wasm3 can't guarantee
  tail calls and uses several native frames per WASM call. A deeply recursive guest then
  overflows TSan's 64Ki-frame shadow stack before wasm3's 8 MiB native-stack limit traps. Under
  `MIM_SANITIZER=thread`, wasm3 gets `d_m3MaxNativeStack=524288`. To reproduce with a newer GCC:
  `-DCMAKE_C_FLAGS=-DM3_HAS_TAIL_CALL=0`.
- **SDL under valgrind.** With the `dummy` driver SDL leaks nothing, but its D-Bus setup and
  `dlopen` leave about 22 "still reachable" blocks, which `--errors-for-leak-kinds=all` counts as
  errors. So `test_pl_display`, the only test that starts SDL, isn't built in the `valgrind`
  preset; `asan` and `tsan` run it. `test_mim` passes `--headless`, so `mim` never starts SDL.
- **libdbus under TSan.** `SDL_Init` and `SDL_Quit` go through libdbus, which takes two of its own
  locks in opposite orders; TSan reports a potential deadlock. `tests/tsan.supp` suppresses
  deadlock reports from `libdbus-1.so`, for `test_pl_display` only.
- **`(strstr)` and `(strchr)`.** Recent glibc defines them as `_Generic` macros, which clang
  `-pedantic` rejects in C99 mode. The parentheses call the real function.
- **gcov and death tests.** `abort()` skips gcov's exit-time flush, so lines that only run inside
  a death-test child count as never executed. mem.c's coverage below is lower than what is really
  tested.

## 6. Measured

### 6.1 Coverage

gcov, debug build, all 16 tests:

| File | Lines | Branches taken |
|---|---|---|
| `platform/linux/pl_linux.c` | 97.5% | 68.9% |
| `runtime/mem/mem.c` | 91.7% (see 5: death tests not counted) | 75.0% |
| `runtime/guest/guest.c` | 95.5% | 76.4% |
| `runtime/loop/loop.c` | 100% | 83.3% |
| `runtime/main.c` | 75.6% | 71.4% |

To reproduce:

```sh
cmake -S . -B /tmp/mim-cov -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS=--coverage -DCMAKE_EXE_LINKER_FLAGS=--coverage
cmake --build /tmp/mim-cov && ctest --test-dir /tmp/mim-cov
cd /tmp/mim-cov && gcov -b -o runtime/mem/CMakeFiles/mim_mem.dir runtime/mem/CMakeFiles/mim_mem.dir/mem.c.o
```

The lines never run are almost all environment failures (`pthread_create` failing, `EINTR`,
`vsnprintf` failing, the region failing to map) and `main`'s usage and file errors.

### 6.2 Mutation testing

Coverage says a line ran, not that a test checked what it did. To measure that, single bugs
("mutants") were put into a copy of the source one at a time, and the whole suite was run on each
(debug preset). A mutant is killed if any test fails.

The 29 mutants were picked by hand, at the places most likely to be weak: boundary comparisons,
security checks, detectors, and the main loop. The score is for those places, not an average
over all code.

| | Before the tests in 3.1 (`log_edge`, `huge_len`, `log_long`, `wrong_types`), 4.2 (the `mem_check` cases) and 4.5 | After |
|---|---|---|
| Killed | 8 of 26 | 21 of 26 |

Plus 3 equivalent or nearly equivalent mutants that no test can kill, because they don't change
what MIM does: the deadline re-anchor `<=` vs `<` (both give the same deadline), the heap split
threshold `>=` vs `>` (only fragmentation changes), and taking the frame time from
`pl_time_now_ns()` instead of the time event (a few ns apart).

Still surviving:

| Mutant | Why it's not tested |
|---|---|
| a second linear-memory block is allowed | wasm3 never asks for one; a defensive check |
| wasm3's leak warning in `guest_unload` dropped | log only |
| the free-list alignment check dropped | the corruption test uses an aligned pointer |
| bump `peak` not updated by `mem_try_grow` | no test checks `peak` after a grow; one check in `test_mem_bump` would kill it |
| `SA_RESETHAND` dropped (a second Ctrl-C no longer kills a stuck guest) | needs a guest stuck inside a frame |
| leaks don't change `mim`'s exit code | needs a guest that makes wasm3 leak |

## 7. Adding a test

- **A C test:** write `tests/test_<name>.c` with a `main` that uses `CHECK`, add
  `mim_add_test(test_<name>)` to `tests/CMakeLists.txt`, and link anything beyond `mim_mem`
  with `target_link_libraries`. It then runs under every preset, valgrind included.
- **A hostile guest:** write `guests/evil/<case>.c` including `evil.h`, add `<case>` to
  `evil_cases` in `guests/CMakeLists.txt` (set `evil_<case>_sources` or `evil_<case>_flags` if it
  must not use `common.c` or needs link flags), and add an `expect("evil_<case>", ...)` line to
  `test_hostile` in `tests/test_guest.c`.
- **A misuse that must abort:** a `static void` case in `tests/test_mem_death.c` and an
  `expect_death(case, death_abort, "message")` line in its `main`.
- **Prove it can fail:** break the code it guards and watch the test go red before relying on it.
