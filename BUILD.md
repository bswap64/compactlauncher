# Building CompactLauncher

## Requirements

| | Windows | Linux |
|---|---|---|
| Compiler | MinGW-w64 (MSYS2) | GCC / Clang |
| Qt | Qt5 or Qt6 (Core + Widgets) | Qt5 or Qt6 (Core + Widgets) |
| Build tools | CMake 3.16+, Ninja | CMake 3.16+, Ninja |
| Network | (WinINet built-in) | libcurl |
| Zip extraction | zlib | unzip |

---

## Windows (MSYS2 / MinGW-w64)

```bash
pacman -S mingw-w64-x86_64-qt6-base mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-zlib mingw-w64-x86_64-gcc
```

```bash
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

---

## Linux


```bash
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

---

## Output

| Platform | Binary |
|---|---|
| Windows | `build/CompactLauncher.exe` |
| Linux | `build/CompactLauncher` |
