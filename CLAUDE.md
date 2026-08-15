# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A C++23 implementation of a POSIX-compliant shell, built as a solution to the
CodeCrafters "Build Your Own Shell" challenge. The entire implementation
currently lives in `src/main.cpp` (a single-file REPL). CMake globs all
`src/*.cpp`/`src/*.hpp`, so new files dropped in `src/` are picked up
automatically without editing `CMakeLists.txt`.

## Build and run

Requires `cmake` and `VCPKG_ROOT` set (vcpkg supplies `readline`, which the
binary links against).

```sh
./your_program.sh
```

This runs the same compile step CodeCrafters uses remotely
(`.codecrafters/compile.sh`): configures with the vcpkg toolchain file into
`build/`, builds, then execs `build/shell`. There is no separate lint or test
command in this repo — correctness is verified via CodeCrafters' remote test
stages.

To submit progress against the CodeCrafters test stages:

```sh
codecrafters submit
```

Editing `your_program.sh` or `.codecrafters/*.sh` only affects local runs vs.
the CodeCrafters-side compile/run steps respectively — the two are separate
copies and must be kept in sync manually if changed.

## Formatting

Style is defined in `.clang-format` (Google-based, 2-space indent). Format
via CMake targets (only registered if `clang-format` is found on `PATH`):

```sh
cmake --build build --target format        # reformat src/ in place
cmake --build build --target format-check   # CI-style check, fails on diff
```

or via the shortcut in `your_program.sh`, which configures the build first if
needed:

```sh
./your_program.sh format
./your_program.sh format-check
```

## Architecture notes

- The shell is a straightforward `while(true)` REPL loop in `main()`: print
  `$ `, read a line with `std::getline`, tokenize on whitespace via
  `parse_input`, dispatch to a builtin or fall through to "command not
  found".
- Builtins are dispatched by string-comparing `input[0]` against literals
  inline in `main()`, and separately tracked in the `builtsins` unordered_set
  (used only by `type` to answer "is this a builtin"). When adding a new
  builtin, both the dispatch branch in `main()` and the `builtsins` set need
  updating.
- `std::cout`/`std::cerr` are set to `unitbuf` (auto-flush after every
  output) at startup — this matters for interactive REPL behavior and should
  be preserved.
- This is early-stage scaffolding (`parse_input` is naive whitespace
  splitting with no quoting/escaping support yet); expect the tokenizer and
  builtin dispatch to need a rewrite as later CodeCrafters stages (quoting,
  redirection, `cd`, `pwd`, `$PATH` execution, pipes) are implemented.
</content>
