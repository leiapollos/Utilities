# Slang Vendor Package

Version: `2026.10.2`

Source: `https://github.com/shader-slang/slang/releases/tag/v2026.10.2`

Installed package: `slang-2026.10.2-windows-x86_64.tar.gz`

The vendored Windows package intentionally omits:

```text
third_party/slang/bin/win64/slang-llvm.dll
```

That DLL is the LLVM backend and is over 100 MB. `sob.c` only invokes `slangc.exe`
for SPIR-V shader output, which uses the adjacent Slang compiler and glslang
DLLs, so the LLVM backend is not needed for the current build.

`sob.c` looks for the Windows shader compiler at:

```text
third_party/slang/bin/win64/slangc.exe
```

The release `bin` directory is committed here, minus `slang-llvm.dll`, so
`slangc.exe` can find its adjacent runtime DLLs and standard module files
without requiring a system install or PATH entry. Restore `slang-llvm.dll` from
the package above if future work needs LLVM-backed Slang targets.
