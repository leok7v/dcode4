# Delta Hex Coding ('Compression')

## TLDR
Here's how to compress greyscale image bytes using dcode4:
```C
    static void foo(byte* data, int bytes) {
        byte cenoded[bytes * 2];
        int k = encode4(data, bytes, coded, sizeof(encoded));    
        byte decoded[bytes];
        int n = decode4(encoded, k, decoded, sizeof(decoded));
        assert(n == bytes);
        assert(memcmp(decoded, data, n) == 0);
    }
```

## Introduction

Very simple delta coding `compression` suitable for specific subset of
grey scale images with known property of low gradient illumination increase/decrease.

Partially inspired by: 
     https://en.wikipedia.org/wiki/Elias_omega_coding
     https://en.wikipedia.org/wiki/Elias_delta_coding
     https://en.wikipedia.org/wiki/Elias_gamma_coding
     https://en.wikipedia.org/wiki/Even%E2%80%93Rodeh_coding
     https://en.wikipedia.org/wiki/Fibonacci_coding

1. For each 'pixel' delta [-255..+255] of the previous pixel is calculated. 
2. Small deltas in a range of [-6..+6] are codded as a single hex with 
   value of [0x0..0xC]
3. Deltas in a range [-19..+19] have 6 subtracted from them and encoded as 
   sign hex (0xD for negative and 0xE for positive) with [0x0..0xD] 
   absolute value in the following hex.
4. For larger deltas:
   first hex hex in a steam is 0xF following by sign hex 0xD negative 
   or 0xE positive (it could be 0x0|0x1 or any other two disticnt values)
   following by two hex of absolute value of delta.

All streams of hex in little endian format.

Decoder assumes caller has knowledge of original byte size of the data
(because of possible odd hex count of encoder).

Code is not optimized for performance in a sake of simplisity. 

It is trivial to implement much faster table driven optimized decoder. 


![alt text](https://raw.githubusercontent.com/leok7v/dcode4/master/greyscale.128x128.png "greyscale.128x128.bin.png")

greyscale.128x128.bin.pgm 16445 compressed to 9073 bytes 55.2%

It is possible to add RLE on top of delta hex coding.
