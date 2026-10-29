# stb

Single-header libraries from https://github.com/nothings/stb, MIT or public domain (`LICENSE`).

- Commit: 003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5 (2026-10-23)
- Copied: `stb_image.h` v2.30, `stb_image_write.h` v1.16, `stb_truetype.h` v1.26, `LICENSE`
- Built by `runtime/canvas/CMakeLists.txt`, configured in `runtime/canvas/image.c` and
  `runtime/canvas/text.c`

## Local changes

One, marked `MIM` (`grep -n MIM stb_image.h`): `stbi__getn` returns at once when asked for 0
bytes. A PNG whose first `IDAT` chunk is empty got there with a NULL buffer, and
`memcpy(NULL, src, 0)` is undefined behaviour in C99; nothing was ever read or written, but UBSan
stops on it. `fuzz_image` found it.

How MIM uses them:

- `stb_image` decodes PNGs a guest hands over, so it parses untrusted bytes. Only the PNG decoder
  is built, images are capped at 4096 × 4096, every allocation comes from the canvas budget, and
  `fuzz_image` fuzzes it.
- `stb_truetype` says it does no range checking and must never see an untrusted font. It only
  ever reads the font built into `mim` (`extern/fonts`); guests can't load fonts.
- `stb_image_write` is for the tests only: it writes the frames they compare with golden images.

To upgrade: copy the same files from a newer commit.
