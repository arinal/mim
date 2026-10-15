# wasm3

The WebAssembly interpreter, from https://github.com/wasm3/wasm3.

- Version: v0.9.0 (commit 0cd38327f0c721e75172f4f1eeb55854dc0517af)
- Copied: `LICENSE` and the core of `source/` (no `m3_api_*` WASI/libc bindings, `extensions/`, `extra/`)
- Built by `src/guest/CMakeLists.txt`, not by wasm3's own build files

## Local changes

Every change is marked `MIM` (`grep -rn MIM source`). With `d_m3HostAllocator` set (the
default is 0, which behaves exactly like upstream):

- `m3_core.c` doesn't define `m3_Malloc_Impl`, `m3_Realloc_Impl` and `m3_Free_Impl`: the host does.
- Linear memory goes through `m3_LinearResize_Impl` / `m3_LinearFree_Impl`, declared in
  `m3_core.h` and called from `ResizeMemory` and `Runtime_Release` in `m3_env.c`, so the host can
  keep it apart from wasm3's other allocations.

To upgrade: copy the same files from the new release, then reapply these changes.
