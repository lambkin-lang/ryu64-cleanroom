/*
 * MIT License
 *
 * Copyright (c) 2026 lambkin-lang
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef RYU64_WASI_IO_H
#define RYU64_WASI_IO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const void* buf;
  size_t buf_len;
} wasi_iovec_t;

__attribute__((import_module("wasi_snapshot_preview1"), import_name("fd_write")))
extern uint32_t wasi_fd_write(uint32_t fd, const wasi_iovec_t* iovs,
                              size_t iovs_len, size_t* nwritten);

static inline void write_stdout(const char* data, size_t len) {
  wasi_iovec_t iov;
  iov.buf = data;
  iov.buf_len = len;
  size_t nwritten = 0;
  (void)wasi_fd_write(1, &iov, 1, &nwritten);
}

static inline void write_line(const char* data, size_t len) {
  write_stdout(data, len);
  write_stdout("\n", 1);
}

#endif
