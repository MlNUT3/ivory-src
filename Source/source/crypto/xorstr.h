#pragma once
// Runtime XOR string obfuscation. Plaintext never sits in the image - each
// literal decrypts once into its own static buffer on first use.
//   X("text")   -> const char*    (narrow strings)
//   WX("text")  -> const wchar_t* (wide strings, for Win32 W APIs)
// NEVER wrap: #pragma args, \x byte blobs, sizeof("..."), R"(...)" blocks.
#ifndef XORSTR_H
#define XORSTR_H

#include <cstddef>

#define X(str) []() -> const char* { \
    static char buf[sizeof(str)]; \
    static bool done = false; \
    if (!done) { \
        for (size_t i = 0; i < sizeof(str) - 1; ++i) \
            buf[i] = static_cast<char>(static_cast<unsigned char>(str[i]) ^ static_cast<unsigned char>((i * 7 + 0x5A) & 0xFF)); \
        buf[sizeof(str) - 1] = 0; \
        done = true; \
    } \
    return buf; \
}()

#define WX(str) []() -> const wchar_t* { \
    static wchar_t buf[sizeof(str)]; \
    static bool done = false; \
    if (!done) { \
        const size_t n = sizeof(str) / sizeof(wchar_t) - 1; \
        for (size_t i = 0; i < n; ++i) \
            buf[i] = static_cast<wchar_t>(str[i] ^ ((i * 7 + 0x5A) & 0xFF)); \
        buf[n] = 0; \
        done = true; \
    } \
    return buf; \
}()

#endif
