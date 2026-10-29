# MIM

**MIM Isn't M5.** A small media-app runtime, built from scratch one stage at a time to learn how
these things really work.

A C99 host runs apps compiled to WebAssembly (written in Rust or C++) in a 60 FPS loop. Every
OS-specific call goes through a thin platform layer, so the same host should one day boot on bare
metal with no OS at all.

## How it fits together

```mermaid
%%{init: {"flowchart": {"wrappingWidth": 400, "subGraphTitleMargin": {"top": 10, "bottom": 10}}}}%%
flowchart TB
    accTitle: How MIM fits together
    accDescr: Everything except the guest apps is one executable, mim; on Linux it loads libc and libSDL3 as shared libraries. Three untrusted WASM guest apps (hello_rust, hello_cpp, bounce_rust), loaded one at a time, run inside the wasm3 interpreter. Their imports reach the runtime's guest loader, which checks every pointer and passes drawing calls to the canvas, which draws into the framebuffer. The runtime reaches the OS only through platform/pl.h. Linux implements it today; bare metal in QEMU is planned for stage 9, and Windows is not planned.

    subgraph apps["<b>guest apps (.wasm)</b>: untrusted, one at a time"]
        subgraph list[" "]
            direction LR
            hello_rust["<b>hello_rust</b><br/>logs a few lines"]
            hello_cpp["<b>hello_cpp</b><br/>the same lines, from C++"]
            bounce_rust["<b>bounce_rust</b><br/>a box bouncing in a window"]
            hello_rust ~~~ hello_cpp ~~~ bounce_rust
        end
        abi["<b>exports:</b> app_init · app_tick · app_shutdown<br/><b>imports:</b> mim.log · mim.cv_*, listed in sdk/mim.ffi"]
        list --- abi
    end

    subgraph mim["<b>mim</b>: one executable, everything inside is linked in statically"]
        wasm3["<b>extern/wasm3</b><br/>the WASM interpreter, vendored"]

        subgraph runtime["<b>runtime/</b>"]
            direction LR
            main["<b>main.c</b><br/>the main loop"]
            loader["<b>guest/</b><br/>loads and checks the guest"]
            canvas["<b>canvas/</b><br/>draws into the framebuffer"]
            loop["<b>loop/</b><br/>the frame clock"]
            mem["<b>mem/</b><br/>memory budgets"]
            main ~~~ loader ~~~ canvas ~~~ loop ~~~ mem
        end

        pl(["<b>platform/pl.h</b><br/>every OS call goes through here"])

        subgraph platforms["<b>platform/</b>: one per target, picked at build time"]
            direction LR
            linux["<b>linux/</b><br/>pl_linux.c · pl_display_sdl.c<br/>on Linux + SDL3<br/><i>libc and libSDL3.so stay outside mim:<br/>shared libraries, loaded when it starts</i>"]
            baremetal["<b>baremetal/</b> (stage 9)<br/>boot stub, UART, timer, ramfb<br/>on QEMU aarch64 virt"]
            windows["<b>windows/</b> (not planned)<br/>the same pl.h<br/>on Win32 + SDL3"]
            linux ~~~ baremetal ~~~ windows
        end
    end

    abi <-- "runs inside" --> wasm3
    wasm3 <== "sandbox wall<br/>guest/ checks every pointer" ==> runtime
    runtime ==> pl
    pl --> platforms

    classDef planned fill:none,stroke:#999,stroke-dasharray:6 4,color:#888
    class baremetal,windows planned
    style mim stroke-width:3px
    style list fill:none,stroke:none
```

Everything in the big box is one executable, `mim`: `runtime/`, wasm3 and the platform layer
are static libraries linked into it. Only the guest apps (read at run time) and, on Linux, libc
and SDL3 (shared libraries) are outside it. Thick arrows cross a wall. Dashed boxes are planned,
not built yet.

- **The guest apps** are Rust or C++ compiled to WASM with `sdk/rust` or `sdk/cpp`, and the
  host doesn't trust them. They all have the same exports, which the host calls, and can only
  call back through the `mim.*` imports listed in `sdk/mim.ffi`. The host runs one at a time.
- **`extern/wasm3`** is the WASM interpreter that runs them, vendored with a small patch
  marked MIM. It calls the exports and hands each import call to `guest/`.
- **`runtime/`** is the host:
  - `main.c`: the main loop, one `app_tick` per frame, then show it.
  - `guest/`: loads the .wasm in wasm3, checks its exports, links the imports, and turns a
    guest crash into a dead guest instead of a dead host. The import glue, `ffi.h` and `ffi.c`,
    is generated from `sdk/mim.ffi` by [`tools/ffigen`](tools/ffigen/README.md), along with
    both SDKs' declarations; the handlers (`host_log`, `host_cv_*`) are written by hand.
  - `canvas/`: a 2D drawing API shaped like HTML5 Canvas, in software. It owns the framebuffer,
    and the guest's `mim.cv_*` calls draw straight into it, so no pixels cross the sandbox wall.
  - `loop/`: the frame clock, with 60 Hz deadlines and a `dt` for each frame. It makes no OS
    calls: it's handed the time, so a test can hand it any time it likes.
  - `mem/`: one big block of memory, split into fixed budgets.
- **`platform/pl.h`** is the whole OS interface. Nothing above it touches the OS.
- **`platform/<target>/`** implements it, one per target:
  - `linux/` is the only one so far: `pl_linux.c` does log, time, sleep, files, memory,
    threads, atomics, events and Ctrl-C; `pl_display_sdl.c` is the window (SDL3).
  - `baremetal/` is stage 9: the same apps on QEMU's aarch64 `virt` board, with no OS.
  - `windows/` isn't planned. It's there to show that a port is only a new `pl.h`
    implementation.

CI checks the walls: nothing outside `platform/` may include an OS header (`unistd.h`,
`pthread.h`, `SDL3/…`), and a set of hostile guests in `guests/evil/` try to break out of the
sandbox.

### One frame

```mermaid
sequenceDiagram
    accTitle: One frame
    accDescr: Each frame, main.c polls the platform for events, gets the time and dt from loop/, calls the guest's app_tick through guest/, whose drawing calls go to canvas/, shows the framebuffer, and sleeps until the next 60 Hz deadline.

    participant app as guest app
    box mim
        participant guest as guest/
        participant canvas as canvas/
        participant main as main.c
        participant clock as loop/
        participant pl as platform
    end

    loop every frame, until the guest quits or dies
        main->>pl: pl_tick()
        pl-->>main: events since the last frame, each with its time
        break Ctrl-C or the window was closed
            Note over main,pl: leave the loop
        end
        main->>clock: loop_begin(time of the last event)
        clock-->>main: abs_time_ms, dt
        main->>guest: guest_tick(abs_time_ms, dt)
        guest->>app: app_tick(abs_time_ms, dt), through wasm3
        loop each drawing call
            app->>guest: mim.cv_fill_rect(x, y, w, h), ...
            guest->>canvas: cv_fill_rect(x, y, w, h)
            Note over canvas: fills pixels<br/>in the framebuffer
        end
        app-->>guest: non-zero: keep going, 0: quit
        guest-->>main: running, quit or dead
        opt not --headless
            main->>pl: pl_display_present(framebuffer)
        end
        main->>clock: loop_deadline(now)
        clock-->>main: the next 60 Hz deadline
        main->>pl: pl_sleep_until_ns(deadline)
    end
```

The frame rate comes from the deadline, not vsync: on Wayland vsync doesn't behave like a clock.

### Memory

All memory is mapped once at startup, as one block, and split into named budgets that sit back
to back in this order. Going over one fails loudly with the budget's name, instead of quietly
eating its neighbour's memory.

| guest vm | guest linear | files | framebuffer |
|:---:|:---:|:---:|:---:|
| 8 MiB heap | 32 MiB bump | 4 MiB heap | 4 MiB bump |
| wasm3's own state | the guest's memory: one block that grows in place (`memory.grow`) | the `.wasm` file | 1280×720×4: what the canvas draws on |

## Stages

Each stage only counts as done when there's an automated test in CI that would fail if it broke.

```mermaid
flowchart LR
    S0["0 · skeleton + CI"] --> S1["1 · platform layer"] --> S2["2 · memory budgets"] --> S3["3 · WASM host"]
    S3 --> S4a["4a · main loop + window"] --> S4b0["4b.0 · FFI"] --> S4b["4b · canvas"] --> S4c["4c · app UI"]
    S4b --> S4d["4d · GPU backend"]
    S4a --> S5["5 · input"]
    S3 --> S6["6 · networking"]
    S5 --> S8["8 · Linux kernel driver"]
    S4c --> S9["9 · bare metal (QEMU)"]
    S9 -.optional.-> S10["10 · U-Boot, Raspberry Pi"]
    S7["7 · debugging toolkit<br/>(all along)"]

    classDef done fill:#2da44e,stroke:#1a7f37,color:#fff
    classDef doing fill:#fff8c5,stroke:#9a6700
    class S0,S1,S2,S3,S4a,S4b0 done
    class S4b doing
```

| | Stage | What it is | Done when |
|---|---|---|---|
| ✅ | **0 · Skeleton + CI** | **CMake, clang-format, `-Werror`, a tiny test framework, GitHub Actions with sanitizers and Valgrind.** | **Every preset is green in CI.** |
| ✅ | **1 · Platform layer** | **`pl_*` for Linux: log, time, files, memory mapping, threads, mutexes, atomics.** | **Every function has a test, and a thread stress test passes under TSan.** |
| ✅ | **2 · Memory budgets** | **One block split into named budgets, with hand-written bump and free-list allocators, guard bytes and a leak report.** | **A fuzzer hammers alloc/free with no crash, and going over a budget fails loudly.** |
| ✅ | **3 · WASM host** | **wasm3 runs a Rust and a C++ app. The host checks the exports and every pointer the guest hands over.** | **Both apps give the same log, and hostile apps get rejected, not crashed into.** |
| ✅ | **4a · Main loop + window** | **A 60 Hz loop with a real `dt`, Ctrl-C and window close quit cleanly, the guest pushes 1280×720 frames into an SDL3 window (or runs headless), through `mim.present`, which the canvas replaced in 4b.** | **A fake-clock test checks every `dt` and frame, and bad `mim.present` calls trap.** |
| ✅ | **4b.0 · FFI** | **One table, `sdk/mim.ffi`, lists every import and shared constant. A small generator writes the host's linking code and both SDKs' declarations from it, and checks guest buffers before the handler runs.** | **Editing the table without regenerating fails CI, and a handler with the wrong argument types doesn't compile.** |
| 🚧 | 4b · Canvas | A 2D drawing API shaped like HTML5 Canvas, done in software: paths, fills, anti-aliasing, images, text from a glyph atlas. So far: the canvas owns the framebuffer, and guests draw with `mim.cv_fill_style` and `mim.cv_fill_rect`. | Golden-image tests per primitive, under 16 ms a frame. |
| ⬜ | 4c · App UI | A TV-style UI inside the guest: one state, actions, immediate-mode screens, D-pad focus, scrolling tile rows. Built in Rust and in C++. | A scripted "down, down, OK" replay matches a golden image. |
| ⬜ | 4d · GPU backend | A tiny `gpu_*` interface with an OpenGL ES backend. The software canvas stays as the reference. | Both renderers draw the same scene within a tolerance. |
| ⬜ | 5 · Input | Keyboard and gamepad events, plus an input thread feeding a lock-free ring buffer. | A multi-threaded stress test passes under TSan, plus an input replay test. |
| ⬜ | 6 · Networking | Non-blocking sockets, `poll()`, a hand-written HTTP/1.1 client, async callbacks to the guest. | Tests against a local server, and the response parser gets fuzzed. |
| ⬜ | 7 · Debugging toolkit | A crash handler with a backtrace, GDB and `perf` habits, and a bug diary. | At least 5 real diary entries, each with a regression test. |
| ⬜ | 8 · Linux kernel driver | `/dev/mim_pad`: a kernel module with a ring buffer of gamepad events. MIM reads input from it. | CI boots QEMU, loads the module, runs the test, and the kernel log stays clean (lockdep + KASAN). |
| ⬜ | 9 · Bare metal | The same `.wasm` apps on `qemu-system-aarch64 -M virt`: an assembly boot stub, UART, timer and `ramfb` drivers. | CI boots the image and finds the app's log lines on the serial port. |
| ⬜ | 10 · Extras (optional) | Boot through U-Boot, run on a Raspberry Pi, upstream a small fix. | — |

## Try it

You need CMake, Ninja, a C compiler, clang with `wasm-ld` (lld) for the C/C++ guests, Rust with
the `wasm32v1-none` target for the Rust guests, and SDL3.

```sh
rustup target add wasm32v1-none
cmake --preset debug
cmake --build build/debug

./build/debug/mim build/debug/guests/bounce_rust.wasm            # a box bouncing in a window
./build/debug/mim --headless build/debug/guests/hello_rust.wasm  # logs a few lines and quits

ctest --test-dir build/debug --output-on-failure
```

No display? `mim` says so and keeps running headless. Other presets: `clang`, `asan`, `tsan`,
`valgrind`, `fuzz`. Everything about the tests is in [docs/testing.md](docs/testing.md).

Changing what a guest can import: edit `sdk/mim.ffi`, run `cargo run -p ffigen`, and commit the
table with the files it rewrites. [tools/ffigen](tools/ffigen/README.md) has the steps.

## Where things are

```
platform/   pl.h (the whole OS interface) and its Linux version
runtime/    the host: main loop, guest loader, canvas, frame clock, memory budgets
sdk/        what an app links against: sdk/rust (the mim crate) and sdk/cpp (mim.h), and
            sdk/mim.ffi, the table every import is generated from
tools/      ffigen: writes the import glue for the host and both SDKs from sdk/mim.ffi
guests/     sample apps, plus guests/evil: hostile ones the tests throw at the host
tests/      one small executable per test, plus test_mim.sh for the real binary
extern/     wasm3, vendored with a small patch marked MIM
```
