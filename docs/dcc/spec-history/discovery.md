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
