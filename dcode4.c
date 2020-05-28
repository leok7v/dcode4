/* Copyright 2020 "Leo" Dmitry Kuznetsov https://leok7v.github.io/
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

// Very simple delta coding `compression` suitable for specific subset of
// grey scale images with known property of low gradient illumination increase/decrease.
//
// Partially inspired by: 
//      https://en.wikipedia.org/wiki/Elias_omega_coding
//      https://en.wikipedia.org/wiki/Elias_delta_coding
//      https://en.wikipedia.org/wiki/Elias_gamma_coding
//      https://en.wikipedia.org/wiki/Even%E2%80%93Rodeh_coding
//      https://en.wikipedia.org/wiki/Fibonacci_coding
//
// 1. For each 'pixel' delta [-255..+255] of the previous pixel is calculated. 
// 2. small deltas in a range of [-6..+6] are codded as a single hex with 
//    value of [0x0..0xC]
// 3. deltas in a range [-19..+19] have 6 subtracted from them and encoded as 
//    sign hex (0xD for negative and 0xE for positive) with [0x0..0xD] 
//    absolute value in the following hex.
// 4. for larger deltas:
//    first hex hex in a steam is 0xF following by sign hex 
//    (0xD|0xE but could be 0x0|0x1 or any other two disticnt values)
//    following by two hex of absolute value of delta
// All streams of hex in little endian format
// Decoder assumes caller has knowledge of original byte size of the data
// (because of possible odd hex count of encoder)
//
// Code is not optimized for performance in a sake of simplisity. 
// It is trivial to implement much faster table driven optimized decoder. 

#define null NULL // beautification of code
#define byte uint8_t
#define countof(a) (sizeof(a) / sizeof((a)[0]))

static void hexdump(byte* data, int bytes) {
    for (int i = 0; i < bytes; i++) { printf("%02X", data[i]); }
    printf("\n");
}

#define hex_out(output, hexpos, hex) \
    if (hexpos % 2 == 0) { \
        output[hexpos / 2] = (byte)((hex) & 0xF); \
    } else { \
        output[hexpos / 2] |= (byte)(((hex) & 0xF) << 4); \
    } \
    hexpos++;

int encode4(byte* data, int bytes, byte* output, int count) {
    int last = 0;
    int hexpos = 0;
    for (int i = 0; i < bytes; i++) {
        uint16_t out = 0;
        int bits = 0;
        int delta = last - (int)data[i];
        last = (int)data[i];
        assert(-255 <= delta && delta <= 255);
        if (-6 <= delta && delta <= 6) {
            bits = 4;
            out = (uint16_t)(delta + 6);
            assert(0 <= out && out <= 0xC); // 13 values
        } else if (-19 <= delta && delta <= 19) { // (abs(delta) - 6) in [0..12]
            byte a = (byte)(abs(delta) - 6);
            assert(0 <= a && a <= 0xD);
            out = (delta < 0 ? 0xD : 0xE) | a << 4;
            bits = 8;
        } else {
            out = 0xF | ((delta < 0 ? 0xD : 0xE) << 4) | (abs(delta) << 8);
            bits = 16;
        }
        if (hexpos + bits / 4 >= count * 2) { return -1; } // overflow of output buffer
        if (bits == 4) {
            hex_out(output, hexpos, out);
        } else if (bits == 8) {
            hex_out(output, hexpos, out >> 0);
            hex_out(output, hexpos, out >> 4);
        } else if (bits == 16) {
            hex_out(output, hexpos, out >>  0);
            hex_out(output, hexpos, out >>  4);
            hex_out(output, hexpos, out >>  8);
            hex_out(output, hexpos, out >> 12);
        } else {
            assert(false);
        }
    }
    return (hexpos + 1) / 2; // bytes
}

#define hex_in(hex, data, hexpos) \
    byte hex = (byte)((data[hexpos / 2] >> ((hexpos % 2) * 4)) & 0xF); hexpos++; \

int decode4(byte* data, int bytes, byte* output, int count) {
    int i = 0;
    int last = 0;
    int hexpos = 0;
    while (hexpos < bytes * 2 && i < count) {
        hex_in(hex0, data, hexpos);
        int delta = 0;
        if (hex0 <= 0xC) {
            delta = (int)hex0 - 6;
            assert(-6 <= delta && delta <= +6);
        } else {
            assert(hexpos < bytes * 2);
            hex_in(hex1, data, hexpos);
            if (hex0 == 0xD || hex0 == 0xE) {
                int v = hex1 + 6;
                delta = hex0 == 0xD ? -v : +v;
                assert(-19 <= delta && delta <= +19);
            } else {
                assert(hex0 == 0xF);
                assert(hex1 == 0xD || hex1 == 0xE); // 0xF is unused
                assert(hexpos < bytes * 2 - 1);
                hex_in(hex2, data, hexpos);
                hex_in(hex3, data, hexpos);
                int v = (hex2 | (hex3 << 4));
                delta = hex1 == 0xD ? -v  : +v;
            }
        }
        int value = last - delta;
        assert(0 <= value && value <= 0xFF);
        output[i] = (byte)value;
        last = value;
        i++;
    }
    return i;
}

static void delta_test() {
    byte data[513] = {255, 0};
    int i = 1;
    for (int delta = -255; delta <= 255; delta++) {
        data[i] = data[i - 1]  + delta;
        i++;
    }
    byte coded[countof(data) * 2] = {};
    int k = encode4(data, countof(data), coded, countof(coded));    
    byte decoded[countof(data)] = {};
    int n = decode4(coded, k, decoded, countof(decoded));
    assert(n == countof(data));
    assert(memcmp(decoded, data, n) == 0);
}

static void random_test() {
    byte data[1024] = {};
    for (int pass = 0; pass < 100000; pass++) {
        for (int i = 0; i < countof(data); i++) {
            data[i] = (byte)((256LL * random()) / RAND_MAX);
        }
        for (int m = countof(data); m > countof(data) - 16; m--) {
            byte coded[countof(data) * 2] = {};
            int k = encode4(data, m, coded, countof(coded));
            byte decoded[countof(data)] = {};
            int n = decode4(coded, k, decoded, m);
            assert(n == m);
            assert(memcmp(decoded, data, n) == 0);
        }
        if (pass % 1000 == 0) { printf("."); }
    }
    printf("\n");
}

static void image_test() {
    static const char* fn = "greyscale.128x128.bin.pgm";
    int fd = open(fn, O_RDONLY);
    assert(fd >= 0);
    struct stat sb = {};
    int r = fstat(fd, &sb);
    assert(r == 0);
    int bytes = (int)sb.st_size;
    void* data = mmap(null, bytes, PROT_READ, MAP_PRIVATE, fd, 0);
    byte encoded[bytes * 2];
    int k = encode4(data, bytes, encoded, bytes * 2);
    byte decoded[bytes];
    int n = decode4(encoded, k, decoded, bytes);
    assert(n == bytes);
    assert(memcmp(data, decoded, n) == 0);
    printf("%s %d compressed to %d bytes %.1f%c\n", fn, bytes, k, k * 100.0 / bytes, '%');
    munmap(data, bytes);
}

int main(int argc, const char* argv[]) {
    setbuf(stdout, null);
    byte data[] = { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFF, 0xFF, 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00 };
    byte coded[countof(data) * 2] = {};
    int k = encode4(data, countof(data), coded, countof(coded));    
    hexdump(data, countof(data));
    hexdump(coded, k);
    byte decoded[countof(data)] = {};
    int n = decode4(coded, k, decoded, countof(decoded));
    assert(n == countof(data));
    assert(memcmp(decoded, data, n) == 0);
    delta_test();
    random_test();
    image_test();
    return 0;
}