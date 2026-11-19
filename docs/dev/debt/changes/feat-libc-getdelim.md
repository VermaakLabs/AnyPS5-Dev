# libc

## add Functional

- [getline and getdelim](../../../core/libs/prx/libc/src/StandardStreams.cpp) (libc) are not measured on a console, and whether the console exports them as working functions is unverified (its `fgetln` export is a `hlt` instruction). They follow FreeBSD 11.4 [getdelim.c](https://github.com/freebsd/freebsd-src/blob/release/11.4.0/lib/libc/stdio/getdelim.c): the buffer grows through `realloc` to the next power of two, a null line or capacity pointer sets `EINVAL` and the stream error, end of file before any byte leaves an empty string and returns -1, and the delimiter is compared as an unsigned char. A null stream throws
