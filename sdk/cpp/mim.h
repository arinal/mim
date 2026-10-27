//===-- mim.h - The C++ guest SDK: host imports and a log line builder ----===//
//
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// For guests built with clang --target=wasm32 -nostdlib: no libc, no
// exceptions, no RTTI. mim.cpp provides operator new/delete and the
// memset/memcpy/memmove that clang emits calls to.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>

extern "C" {
__attribute__((import_module("mim"), import_name("log"))) void mim_log(int32_t level, const char * ptr, size_t len);
__attribute__((import_module("mim"), import_name("present"))) void mim_present(const uint32_t * pixels, uint32_t width, uint32_t height);
}

namespace mim {

/// The severity of a log line, as the host numbers them
enum class Level : int32_t { debug = 0,
                             info = 1,
                             warn = 2,
                             error = 3 };

/// The size every app draws at
constexpr uint32_t screen_width = 1280;
constexpr uint32_t screen_height = 720;

/// What app_tick tells the host
enum class Tick : int32_t { quit = 0,
                            keep_going = 1 };

/// A log line built on the stack; text past its capacity is dropped
class Line {
public:
    Line & operator<<(const char * s) {
        while (*s != '\0' && len_ < sizeof(buf_)) {
            buf_[len_++] = *s++;
        }
        return *this;
    }

    Line & operator<<(uint64_t n) {
        char digits[20];
        size_t count = 0;
        do {
            digits[count++] = char('0' + n % 10);
            n /= 10;
        } while (n != 0);
        while (count > 0 && len_ < sizeof(buf_)) {
            buf_[len_++] = digits[--count];
        }
        return *this;
    }

    Line & operator<<(uint32_t n) {
        return *this << uint64_t(n);
    }

    /// Writes the line to the host's log
    void send(Level level) const {
        mim_log(int32_t(level), buf_, len_);
    }

private:
    char buf_[200];
    size_t len_ = 0;
};

/// Writes one line to the host's log
inline void log(Level level, const char * msg) {
    (Line() << msg).send(level);
}

/// Shows a frame: screen_width × screen_height pixels of 0x00RRGGBB, rows top to bottom
inline void present(const uint32_t * pixels) {
    mim_present(pixels, screen_width, screen_height);
}

} // namespace mim
