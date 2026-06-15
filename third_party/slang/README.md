# Slang Vendor Package

Version: `2026.10.2`

Source: `https://github.com/shader-slang/slang/releases/tag/v2026.10.2`

Installed packages:

- `slang-2026.10.2-windows-x86_64.tar.gz`
- `slang-macos-dist-aarch64.zip`

The vendored packages intentionally omit:

```text
third_party/slang/bin/win64/slang-llvm.dll
third_party/slang/bin/macos/libslang-llvm.dylib
```

That library is the LLVM backend and is over 100 MB. `sob.c` only invokes
`slangc` for SPIR-V and Metal shader output, which uses the adjacent Slang
compiler/glslang runtime files, so the LLVM backend is not needed for the
current build.

`sob.c` looks for vendored shader compilers at:

```text
third_party/slang/bin/win64/slangc.exe
third_party/slang/bin/macos/slangc
```

The release binaries are committed here, minus the LLVM backend, so `slangc`
can find its adjacent runtime files without requiring a system install or PATH
entry. Restore the omitted LLVM library from the packages above if future work
needs LLVM-backed Slang targets.
