#pragma once
#include <windows.h>
#include <cstdint>

/* Reflectively map a PE image that already lives in memory into hProc.
   Maps headers + sections, applies base relocations, resolves imports through
   the local loader, sets per-section protections, then runs DllMain(ATTACH)
   on a remote thread. Nothing is written to disk. */
bool ManualMapDll(HANDLE hProc, const void* image, SIZE_T imageSize, uintptr_t* outBase);

/* Call the mapped image's DllMain(DETACH), then release its memory. */
bool UnloadMappedDll(HANDLE hProc, uintptr_t base);

/* True if the PE at `image` has the IMAGE_FILE_DLL characteristic. */
bool ImageIsDll(const void* image, SIZE_T imageSize);
