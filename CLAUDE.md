# CLAUDE.md — DungeonCrawlStoneSoup, branch `dcc` ("Crawl Live")

This branch adds the "Crawl Live" game mode (identifier `dcc`). The spec is
`docs/dcc/SPEC.md`; read its Goal, Non-goals and Decisions before any phase.
Gate discovery notes live in `docs/dcc/spec-history/discovery.md`.

## Gate (every phase; run from Git Bash; every command must exit 0)

This block is the authoritative copy of the gate; `docs/dcc/SPEC.md` points
here. Paste it whole: the first line sets the paths the rest use.

```
SRC="/c/Users/Connor Wardell/Desktop/PLUTO2/Hobbies/Programming/DungeonCrawlStoneSoup/crawl-ref/source"; MSBUILD="/c/Program Files/Microsoft Visual Studio/18/Insiders/MSBuild/Current/Bin/MSBuild.exe"
cd "$SRC/MSVC" && MSYS_NO_PATHCONV=1 "$MSBUILD" crawl-ref.sln /m "/p:Configuration=Release Tiles" /p:Platform=x64 /p:PlatformToolset=v145 /p:WindowsTargetPlatformVersion=10.0 /v:minimal
rm -f "$SRC/nul"
cd "$SRC" && ./crawl.exe -builddb -reset-cache
cd "$SRC" && ./crawl.exe -script dcc_smoke.lua
```

- The last line runs `source/scripts/dcc_smoke.lua` (added in Phase 1b). Each
  phase appends its checks there; every check `error()`s on failure, so a
  failing check exits 1. New `.cc` files go in `source/Makefile.obj` and in
  `source/MSVC/crawl.vcxproj` (plus `crawl.vcxproj.filters`).
- Use the VS 18 Insiders MSBuild exactly as above. Do NOT use VS2022; its C++
  install on this machine is broken (details in
  `docs/dcc/spec-history/discovery.md`).
- `-builddb` and `-script` run headless with the Release Tiles build: no
  window, exit 0 on success. Observed: a Lua error or a missing script exits 1
  and prints a `script error: ...` line to stderr (`ctest.cc` `run_tests()`,
  `fprintf(stderr, "%s error: %s
", activity, ...)` with activity
  `"script"`), so the exit code is a real signal.
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

Build (about 5 minutes, verified working 2026-10-25; set `SRC` and `MSBUILD`
as in the gate first):
```
cd "$SRC/MSVC" && MSYS_NO_PATHCONV=1 "$MSBUILD" crawl-ref.sln /m "/p:Configuration=Debug Console" /p:Platform=x64 /p:PlatformToolset=v145 /p:WindowsTargetPlatformVersion=10.0 /v:minimal
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
by `crawl_state.game_is_dcc()` (plus the single `#include "dcc*.h"` line that
call needs; `ng-setup.cc` set the precedent), an enum member appended before
the `NUM_` entry (or before the enum's terminator/alias, re-pointing the alias
as `STATUS_LAST_STATUS` needed), a `case` in an existing switch, or one entry
appended to a trunk data table or array that parallels an enum we extended
(the `"dcc"` name in `initfile.cc` `message_channel_names[]`,
`STATUS_DCC_FLOOR` in `status.cc` `important_statuses`). A guarded call may
lead an existing `if`/`else` chain (`main.cc` time/status command). Nothing
else. The DCC callee re-checks `game_is_dcc()` itself; that check is the
authoritative one, the call-site guard is there so every trunk hook is
visibly DCC-only. Recorded exceptions:
- Phase 2a extracted `zot_touch_max_hp()` in `zot.cc` (a pure move of
  existing code) so the floor clock shares Zot's max-HP penalty instead of
  copying it.
- Phase 2b turned the Zot clock off in DCC with a guarded early return
  `if (crawl_state.game_is_dcc()) return true;` at the top of `zot_immune()`
  in `zot.cc`. That one line makes `zot_clock_active()` false and hides every
  Zot display (status light, overview columns, the Ziggurat "hunt you
  nevermore" line); the time/status command shows the floor clock instead.
- Phase 3a (safe cells, no combat in a safe room) keeps two non-call
  lines in `fprop.cc` `str_to_fprop()` (`if (str == "dcc_safe") return
  FPROP_DCC_SAFE;`, a des-name table entry for the appended enum bit). Its
  other hooks are one-line guarded calls into `dcc-safe.cc`, several in the
  guarded early-return/`continue` form: `mon_can_move_to_pos`,
  `handle_monster_move` (step adjust) and the aquatic-flop loop in
  `_monster_move` (`mon-act.cc`); `monster_habitable_grid`,
  `_valid_monster_generation_location`, `create_monster` (`mon-place.cc`);
  the two lurker spots (`mon-lurk.cc`); `monster::move_to` and
  `find_home_near_place` (`monster.cc`); `could_harm` (`mon-util.cc`);
  `_is_valid_foe` (`mon-behv.cc`); `melee_action::trigger` (`quiver.cc`);
  `player_fight` (`fight.cc`); `fire_warn_if_impossible` (`throw.cc`);
  `spell_uselessness_reason` (`spl-util.cc`); `cannot_evoke_item_reason`
  (`evoke.cc`); `cannot_read_item_reason` (`item-name.cc`); the entangle
  drag (`player.cc`) and the "arrives with you" follow (`spl-transloc.cc`).
- Phase 3b (safe-room vault on every floor) keeps two non-call lines that
  sit beside, and mirror, the Descent `no_descent` checks: `if
  (!crawl_state.game_is_dcc() && mapdef.has_tag("dcc_only")) return false;`
  in `maps.cc` `map_selector::accept()`, and the matching veto in
  `dungeon.cc` `_build_vault_impl()`. They keep `dcc_only` maps out of
  every other game type. Its other hooks are one-line guarded calls into
  `dcc-room.cc`: `_dgn_random_map_for_place` (the D:1 arrival vault) and
  two in `_build_dungeon_level` (place the room; fix up the up stairs just
  before `_dgn_verify_connectivity`), all in `dungeon.cc`; and one at the
  top of `load_level()` in `files.cc` (`stair_taken =
  dcc_start_stair(...)`, so a Delver's new game starts on the room's `{`).
  It also keeps one non-call wrapper in `files.cc`,
  `dcc_place_player_as_arriving()` (declared in `dcc-room.h`), which runs
  the static `_place_player()` for `dcc.arrival_spot()`, so the smoke test
  uses the real arrival code instead of a copy of it.

State lives in
`you.props` (keys prefixed `dcc_`) and `dgn.persist`; no new `player` fields,
no `tags.cc` edits. The reason is upstream merges: trunk diffs must stay tiny.

## Merging upstream

- **Game type numbering.** `GAME_TYPE_DCC` is the last member of `game_type`
  (`source/game-type.h`, value 11), and the numeric game type is written into
  saves (`saved_game_type`, read at `tags.cc:2769-2772`). If upstream adds a
  `GAME_TYPE_*` member, put upstream's new members BEFORE `GAME_TYPE_DCC` in
  the merge. That renumbers DCC, so existing `saves/dcc/` games break or show
  up as the wrong type; that is accepted while the mode is in development (no
  save compatibility between DCC phases). The alternative is to pin explicit
  values on the enum and re-check the clamp at `tags.cc:2771`.
- The same applies to `CLO_DCC` in `initfile.cc`: keep it and `"dcc"` in
  `cmd_ops[]` at matching positions.

## No trademarks

No names from the Dungeon Crawler Carl books in any in-game string or content
file name (no Carl, Donut, Mordecai, Syndicate, Borant, etc.). Flavour yes,
names no. The internal identifier `dcc` is fine.
