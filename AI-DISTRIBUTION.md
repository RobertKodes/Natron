# Distributing the AI Assistant build

## Why you cannot just hand over the .exe

The panel is compiled **into** `Natron.exe` (it is C++ under `Gui/`), not loaded as
a plug-in. There is no extension mechanism that would let someone add it to an
existing Natron install.

Dropping this `Natron.exe` into an official Natron install also fails outright:
this build links `libpython3.14.dll` plus 35 MSYS2 DLLs, while the official
installer ships Python 3.10 and its own DLL set. The executable will not load.

That is the cost of the C++ route (PRD #2, Option C). It buys grouped undo and a
native in-application panel; it is paid for at distribution time.

## What `tools/package-natron-ai.sh` produces

Run from the MSYS2 MINGW64 shell:

```bash
./tools/package-natron-ai.sh
```

Output: `dist/NatronAI/` (~408 MB) and `dist/NatronAI-win64.zip` (~136 MB).

The recipient unzips it anywhere and runs `Natron-AI.bat`. Nothing is installed;
it coexists with an official Natron install without touching it.

Verified self-contained: `NatronRenderer.exe --version` from the package succeeds
with `PATH` reduced to the package's own `bin` plus the Windows system
directories.

### What works

142 plug-ins load. All of **openfx-misc** — Grade, Merge, Transform, ChromaKeyer,
Keyer, PIK, Reformat, ColorCorrect, Crop, Shuffle, Premult, ImageStatistics, and
~130 more — plus Natron's built-ins: Roto, RotoPaint, Tracker, Group, Precomp,
Backdrop, Dot.

### What does not: file input and output

`openfx-io` does not load, so there are no concrete readers or writers. Natron's
`Read` and `Write` nodes are meta-nodes that delegate to an OFX reader/writer, so
without them images cannot be loaded or saved. **The package is a complete
processing environment but cannot open or write image files.**

The cause is a compiler ABI skew, not a packaging fault. Evidence:

- `IO.ofx` fails with `WinError 127` ("the specified procedure could not be
  found") — a missing *symbol*, not a missing DLL. Every DLL it imports is
  present and unique.
- It fails identically when loaded from `/mingw64/bin` directly, outside the
  package. Packaging is not involved.
- The local toolchain is **GCC 16.2.0**; `libOpenImageIO-2.5.dll` from the MSYS2
  repo reports `GCC: (Rev5, Built by MSYS2 project) 15.1.0`.
- `Misc.ofx`, which loads fine, imports **zero** C++ libraries. Every plug-in that
  links a C++ library built by the older GCC is the one that fails.

Two related version skews found in the same area, both pre-existing in Natron's
own Windows package repo (pinned `20250524`) versus current MSYS2:

- `avcodec-58` requires `libbluray-2.dll` and `libx265-215.dll`; MSYS2 now ships
  `libbluray-3.dll` and `libx265-217.dll`. Major soname bumps, so renaming them
  would risk crashes during decode rather than fix anything. The `IO` bundle was
  therefore rebuilt without FFmpeg (video I/O is out regardless).
- `openfx-io/OIIO/Makefile` omits `ofxsFileOpen.o`, so that sub-target does not
  link at all. Patched locally; unrelated to the ABI problem, but it hides it.

### Fixing file I/O

Pick one:

1. **Match the compiler.** Install the GCC 15.x toolchain in MSYS2 and rebuild
   `openfx-io` (and Natron) with it, so the C++ ABI matches the prebuilt
   OpenImageIO/OpenColorIO/SeExpr. Most likely to work, least invasive.
2. **Rebuild the dependencies.** Build OpenImageIO, OpenColorIO and SeExpr from
   source with GCC 16.2 so everything shares one ABI. Slow but self-consistent.
3. **Use Natron's own SDK pipeline.** `tools/jenkins/build-plugins.sh` builds the
   whole plug-in set inside their controlled SDK, which is where these version
   pins are known-good. Heaviest, and closest to what ships officially.

Until one of those is done, the package is suitable for demonstrating the AI
panel and for graph work, not for production compositing.

## The alternative worth considering

If the goal is to get this in front of someone who **already has Natron**, the
C++ fork is the expensive way to do it. A pure-Python MCP server, dropped into
their existing install, needs no rebuild and no redistribution at all — Natron
bundles `json`, `socket`, `socketserver` and `threading`, and keeps `lib-dynload`
(`tools/jenkins/zip-python-mingw.sh:64-69, 85-92`), so it runs on the stock
binary.

It loses grouped undo (`QUndoStack::beginMacro` is not exposed to Python) and the
in-application panel, but it keeps node positions, because it runs in the GUI
process rather than headless — the limitation described for the headless route in
PRD #1 does not apply there.
