# DCC discovery log

## Phase 0 — gate discovery (2026-10-25)

Machine: Windows 11, Git Bash, VS 18 Insiders MSBuild, toolset v145.

0. **Why not VS2022.** VS2022 Community's `VC/Tools/MSVC/14.44.35207` has
   no `include/` directory, and `VC/Auxiliary/Build/` has no
   `Microsoft.VCToolsVersion*.props` (both re-checked on disk 2026-10-25).
   MSBuild via VS2022 failed with TRK0005 "Failed to locate CL.exe" and
   MSB8003 (VCToolsInstallDir undefined). That is the earlier session's
   result; it was not reproduced in Phase 0. VS 18 Insiders with v145 works.

1. **Release Tiles build.** The Standard gate MSBuild line works and writes
   `crawl-ref/source/crawl.exe` (about 14 MB). An incremental no-op build
   takes about 13 s. A rebuild after switching from another configuration
   takes about 283 s. Every build leaves an empty `crawl-ref/source/nul`,
   created by the pre-build step `perl util/gen_ver.pl build.h "" "nul"`.
   `nul` is now in `crawl-ref/source/.gitignore`, and the gate still deletes it.
2. **`./crawl.exe -builddb -reset-cache`** (from `source/`) runs headless in
   the Release Tiles build: exit 0, about 11 s, no output, no window, no tracked
   files changed. `-headless` was not needed.
3. **`./crawl.exe -script genlevel.lua D:1`** runs headless: exit 0, about
   1 s, writes `dump-D-1.map` (gitignored by `*.map`; deleted anyway). The
   exit code is meaningful. A script that calls `error(...)` exits 1. Observed on stderr:
   `script error: ...nCrawlStoneSoup/crawl-ref/source/scripts/zz_tmp_fail.lua:1: deliberate failure`.
   That line comes from `ctest.cc` `run_tests()`:
   `fprintf(stderr, "%s error: %s
", activity, fe.second.c_str())`, where
   `activity` is `"script"` and the message is the Lua error text. A missing script exits 1
   with `No scripts found matching ...`. So the Release-build smoke-script
   gate (`-script dcc_smoke.lua`) is viable, and the Standard gate block
   needs no amendment.
4. **Debug Console (time-boxed, optional).** The same MSBuild line with
   `"/p:Configuration=Debug Console"` built and linked: exit 0, 300 s.
   - Output goes to `source/crawl.exe` (the vcxproj `OutDir` is
     `$(SolutionDir)\..\` for every configuration), so it overwrites the
     Release Tiles exe. Restore it by deleting `crawl.exe` and rebuilding
     Release Tiles.
   - `./crawl.exe -test youdie` from Git Bash exits 0 in about 2 s, but this
     is a FALSE PASS. `libw32c.cc:_set_string_input` fails `SetConsoleMode`
     on non-console stdin, prints `Error initialising console input mode.`,
     and calls `exit(0)` before any test runs.
   - From PowerShell, `Start-Process crawl.exe -ArgumentList -test,youdie
     -WorkingDirectory <source> -PassThru` (a new real console window) exits
     0 in about 2 s. It is a real pass: the same invocation on a deliberately
     failing `test/zz_tmp_fail.lua` exited 1. Caveat: a `-test` name that
     matches nothing exits 0 (zero tests run).
   - Conclusion: the Debug Console `-test` route works, but only from a real
     console. It stays optional tooling, run at feature boundaries (after 1b,
     2b, 3b, 4b, 5b). The fallback in Phase 0's Risks (`-test dcc`) is not
     needed.

## Phase 2a — clock and collapse (2026-10-25)

- `is_valid_shaft_level(false)` (`traps.cc`) is the "is there a floor below"
  test: it is false in Sprint, in non-connected branches and at a branch
  bottom, and with `false` it ignores `no_shafts` branch flags.
- A forced descent without the shaft-trap side effects is
  `floor_transition(DNGN_TRAP_SHAFT, orig_terrain(you.pos()), dest, true,
  false, true, false)`. `shaft = true` gives the shaft arrival (random spot,
  and `load_level()` does not treat it as a Descent peek), skips the climb
  message and travel cache update; `_travel_destination()`, which destroys
  the trap, prints the shaft lines and marks the milestone, is bypassed.
- `level_id::describe()` is only unique in connected branches: every Pan level,
  repeat portals, Zigs and later Abyss trips share keys. Key per-floor state
  on it only for connected branches, and use `just_created_level` for "new".

## Phase 2b — clock surface (2026-10-25)

- `zot_immune()` (`zot.cc`) is used in exactly four places: `zot.cc`'s
  `_zot_clock_active_in`, the overview's `show_zot` (`dgn-overview.cc`), the
  Ziggurat exit line (`stairs.cc`) and the time/status command (`main.cc`).
  Making it return true in DCC switches every Zot display off with one guarded
  line; the spec's original `_zot_clock_active_in` condition missed the
  overview and the time/status command. `main.cc` needs its own guarded
  branch ahead of the `zot_immune()` test, or it would print "You are forever
  immune to Zot's power".
- `zot_touch_max_hp()` prints nothing itself (note, `dec_max_hp`, interrupt),
  so a quiet no-floor retry still prints one short line of its own.
- `crawl.messages(n)` in `-script` mode returns the stored message history,
  so a marker `crawl.mpr()` line lets the smoke check that a step printed
  nothing.
