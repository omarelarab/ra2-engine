// iOS build only (Package.swift; the CMake builds list their sources and never
// compile this file): engine/main.cpp under another name. SwiftPM treats a
// target holding a main.* file as an executable, and an xtool app must be a
// library product, so the RA2Engine target excludes main.cpp and builds it
// through this unit. SDL2's SDL_uikit_main.c provides the process main() and
// calls main.cpp's main(), which SDL_main.h renames to SDL_main.
#include "main.cpp"
