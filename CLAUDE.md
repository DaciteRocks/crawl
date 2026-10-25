# CLAUDE.md — DungeonCrawlStoneSoup, branch `dcc` ("Crawl Live")

This branch adds the "Crawl Live" game mode (identifier `dcc`). The spec is
`docs/dcc/SPEC.md`; read its Goal, Non-goals and Decisions before any phase.
Gate discovery notes live in `docs/dcc/spec-history/discovery.md`.

## Gate (every phase; run from Git Bash; every command must exit 0)

```
cd "/c/Users/Connor Wardell/Desktop/PLUTO2/Hobbies/Programming/DungeonCrawlStoneSoup/crawl-ref/source/MSVC" && MSYS_NO_PATHCONV=1 "/c/Program Files/Microsoft Visual Studio/18/Insiders/MSBuild/Current/Bin/MSBuild.exe" crawl-ref.sln /m "/p:Configuration=Release Tiles" /p:Platform=x64 /p:PlatformToolset=v145 /p:WindowsTargetPlatformVersion=10.0 /v:minimal
rm -f "/c/Users/Connor Wardell/Desktop/PLUTO2/Hobbies/Programming/DungeonCrawlStoneSoup/crawl-ref/source/nul"
cd "/c/Users/Connor Wardell/Desktop/PLUTO2/Hobbies/Programming/DungeonCrawlStoneSoup/crawl-ref/source" && ./crawl.exe -builddb -reset-cache
cd "/c/Users/Connor Wardell/Desktop/PLUTO2/Hobbies/Programming/DungeonCrawlStoneSoup/crawl-ref/source" && ./crawl.exe -script dcc_smoke.lua
```

- The last line exists from Phase 1b on. Before then, the script check is
  `./crawl.exe -script genlevel.lua D:1` (writes `dump-D-1.map`; delete it).
- Use the VS 18 Insiders MSBuild exactly as above. Do NOT use VS2022; its C++
  install on this machine is broken.
- `-builddb` and `-script` run headless with the Release Tiles build: no
  window, exit 0 on success. A Lua error or a missing script exits 1 and
  prints `script error: ...` to stderr, so the exit code is a real signal.
- Run `crawl.exe` from `crawl-ref/source/`. It finds `dat/`, `scripts/` and
  `test/` relative to the working directory.
- Run long builds in the background and redirect logs to a file OUTSIDE the
  repo (e.g. `/c/tmp/...`), never inside the working tree.

## The `nul` rule

The MSBuild pre-build step (`perl util/gen_ver.pl build.h "" "nul"`) leaves an
empty file `crawl-ref/source/nul` under Git Bash. Delete it after every build.
It is gitignored, but never commit it. Before committing, `git status` must
show no `nul`, no `crawl.exe`, no `dump-*.map`, and no `dat/des` cache files.

## Optional checks (not part of the per-phase gate)

Debug Console build plus the Lua functional tests. Run at feature boundaries
(after phases 1b, 2b, 3b, 4b, 5b), not per phase.

Build (about 5 minutes, verified working 2026-10-25):
```
cd "/c/Users/Connor Wardell/Desktop/PLUTO2/Hobbies/Programming/DungeonCrawlStoneSoup/crawl-ref/source/MSVC" && MSYS_NO_PATHCONV=1 "/c/Program Files/Microsoft Visual Studio/18/Insiders/MSBuild/Current/Bin/MSBuild.exe" crawl-ref.sln /m "/p:Configuration=Debug Console" /p:Platform=x64 /p:PlatformToolset=v145 /p:WindowsTargetPlatformVersion=10.0 /v:minimal
```
Run the tests in a REAL console window, from PowerShell:
```
$d = "C:\Users\Connor Wardell\Desktop\PLUTO2\Hobbies\Programming\DungeonCrawlStoneSoup\crawl-ref\source"
$p = Start-Process "$d\crawl.exe" -ArgumentList "-test","youdie" -WorkingDirectory $d -PassThru; $p.WaitForExit(); $p.ExitCode
```
Pitfalls, all verified:
- **The Debug Console exe overwrites `source/crawl.exe`**, the same path as
  the Release Tiles exe. Afterwards, `rm -f crawl-ref/source/crawl.exe` and
  re-run the Release Tiles build before you run the gate. Switching
  configurations triggers a near-full recompile (about 5 minutes each way).
- **Running it from Git Bash or any non-console stdin is a FALSE PASS.**
  `libw32c.cc` prints `Error initialising console input mode.` and calls
  `exit(0)` before any test runs. Use `Start-Process` as above, which opens a
  new console window. In that mode a failing test exits 1 (checked with a
  deliberately failing test).
- A `-test <name>` that matches no file runs 0 tests and exits 0. Check the
  name.

## Isolation rule

New behaviour lives in new files: `source/dcc*.cc`, `source/l-dcc.cc`,
`source/dat/des/dcc/*.des`, `source/dat/dlua/dcc*.lua`,
`source/scripts/dcc_*.lua`. A trunk file may gain only a one-line call guarded
by `crawl_state.game_is_dcc()`, an enum member appended before the `NUM_`
entry, or a `case` in an existing switch. Nothing else. State lives in
`you.props` (keys prefixed `dcc_`) and `dgn.persist`; no new `player` fields,
no `tags.cc` edits. The reason is upstream merges: trunk diffs must stay tiny.

## No trademarks

No names from the Dungeon Crawler Carl books in any in-game string or content
file name (no Carl, Donut, Mordecai, Syndicate, Borant, etc.). Flavour yes,
names no. The internal identifier `dcc` is fine.
