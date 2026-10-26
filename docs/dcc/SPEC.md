# DCC mode for Dungeon Crawl Stone Soup — spec

Repo: `DungeonCrawlStoneSoup`, branch `dcc` (forked from upstream `master`,
0.35-a0). Reference fork (read-only, borrow ideas, never merge):
`C:\Users\Connor Wardell\Desktop\PLUTO2\Hobbies\Programming\dcst-reference`
("Dungeon Crawl Souper Turbo"). All paths below are relative to the repo root
unless absolute. `source/` means `crawl-ref/source/`.

## Goal

Add a new game mode, **"Crawl Live"** (code name `dcc`), that layers Dungeon
Crawler Carl-style mechanics on DCSS's existing one-way Descent mode: a visible
per-floor countdown that collapses the floor at zero, a guaranteed monster-free
safe room on every floor holding services, achievements announced by a snarky
"Announcer", tiered loot boxes that open only in safe rooms, per-floor quests,
and later floor bosses, floor themes, sponsors and a floor-3 species re-pick.

## Non-goals

- No changes to how normal DCSS, Sprint, Descent, Tutorial or Arena play.
- No WebTiles support for the new mode (local tiles and console only).
- No save compatibility between DCC phases while the mode is in development.
- No use of trademarked names from the books (no Carl, Donut, Mordecai,
  Syndicate, Borant, etc.). Flavour yes, names no.
- No upstream-facing refactors. Trunk files get one-line guarded hooks only.

## Decisions (do not relitigate)

Naming
- Mode label "Crawl Live"; identifier `dcc` everywhere (enum
  `GAME_TYPE_DCC`, `crawl_state.game_is_dcc()`, rc/CLO value `dcc`, save dir
  `dcc/`, files `source/dcc*.cc`, Lua table `dcc`, des dir `source/dat/des/dcc/`,
  des tag `dcc_only`). One name, greppable, no collisions.
- The System AI is **"the Announcer"**. Its lines go on a new message channel
  `MSGCH_DCC` (rc name `dcc`) so players can colour or mute them.
- Box tiers keep the generic words: Bronze, Silver, Gold, Platinum, Legendary,
  Celestial. Safe rooms are "safe rooms". Sponsors are "sponsors".

Architecture
- **DCC is a Descent variant.** `game_state::game_is_descent()` returns true
  for `GAME_TYPE_DCC` as well, so all ~60 existing Descent guards (one-way
  stairs, stair peek, 12-floor Dungeon, branch order, no temple, voucher) apply
  for free. `game_is_dcc()` gates only the new mechanics. Reason: Descent is a
  complete, winnable one-way game; rebuilding that plumbing would be the whole
  budget.
- **Isolation rule.** New behaviour lives in new files (`source/dcc*.cc`,
  `source/l-dcc.cc`, `source/dat/des/dcc/*.des`, `source/dat/dlua/dcc*.lua`,
  `source/scripts/dcc_*.lua`). A trunk file may gain a one-line call guarded by
  `crawl_state.game_is_dcc()`, an enum member appended before the `NUM_` entry,
  or a `case` in an existing switch. Nothing else. Reason: upstream merges.
- **State lives in `you.props`** (CrawlHashTable, saved automatically, keys
  prefixed `dcc_`) and, for per-floor Lua state, in `dgn.persist` (saved with
  the level, cleared when a level is built). No new fields on `player`, no
  `tags.cc` edits. Reason: zero save-format surgery.
- **Lua for data, C++ for hooks.** Quest and box tables are Lua/des where
  feasible; C++ provides the hook points and a small `dcc.*` dlua library.
- **Headless gate is a Release-build Lua script.** `crawl.exe -script` runs in
  non-debug builds (`ctest.cc`: the `#else ASSERT(crawl_state.script)` path), so
  each phase extends `source/scripts/dcc_smoke.lua` and the gate runs it with
  the user's verified Release Tiles build. The Debug Console `-test` build is
  optional and never part of the per-phase gate; run it at feature boundaries
  (after 1b, 2b, 3b, 4b, 5b) or every few phases if Phase 0 finds it works.
  Reason: an agent cannot play the game; it can run scripts.
- **The clock replaces the Zot clock.** In DCC the Zot clock is inactive; the
  per-floor clock is the only time pressure. Values: 1500 turns on D:1–D:3,
  1200 turns on every later floor, kept as named tunables in `dcc-clock.cc`
  (one table, no magic numbers elsewhere). Reason: two clocks confuse; the
  reference fork's 1200 proved playable.
- **Collapse = forced descent, not death.** At zero the player falls (shaft)
  to the next floor, taking 20% of max HP (never lethal), landing away from the
  safe room, with an Announcer taunt and a note. Where no shaft target exists
  (bottom of a branch, Zot:5), the fallback is the trunk Zot-touch penalty
  (`3 + mhp/6` max HP lost every 100 turns). Reason: a timer that kills
  outright punishes slow play more than DCSS's design tolerates; falling
  forfeits the floor's loot, quest and safe room, which is penalty enough.
  Death-at-zero is a possible later "hardcore" option, not in scope now; keep
  the collapse behaviour in one function so it can be swapped.
- **The clock ticks inside safe rooms.** Safe rooms matter because monsters
  cannot enter them, you arrive in one (every up stair and branch exit on the
  floor is inside it), and services (shop, box kiosk, quest giver) are only
  there. Resting is still "free" in HP terms but costs floor time, as it should.
- **Safe-room monster exclusion is a cell property.** New
  `FPROP_DCC_SAFE` bit in `fprop.h`, set via des `KPROP`, checked in
  `mon_can_move_to_pos` (monsters that would attack you cannot enter) and in
  monster placement. Not Zin's sanctuary (decays, Zin-coupled), not runed doors
  (become normal once opened). No combat inside: Resolved decision 9.
- **Boxes are not inventory items.** `you.props["dcc_boxes"]` holds a vector
  of tier ints. Opening happens at a kiosk (Lua marker in the safe room vault)
  and drops items on the floor. Reason: no new `OBJ_`/`MISC_` enums, no
  item-name/tile/tags work.
- **Achievements are predicates evaluated at hook points** (floor entry,
  monster kill, box open, quest complete), reading `you` state, one-shot per
  game, stored as a set in `you.props["dcc_achievements"]`. No counters that
  reward grinding; the floor clock bounds everything anyway.
- **Quest giver "talks" via LOS trigger + `crawl.yesno`.** DCSS has no
  dialogue; a friendly, non-following NPC in the safe room offers the floor's
  one quest when first seen. Accepting is optional; rewards are boxes.
- **Reuse the Descent startup tile** (`TILEG_STARTUP_IRONSOUP`). No new art.

Standard gate (every phase; run from Git Bash; all must exit 0)
The repo-root `CLAUDE.md` holds the authoritative copy of this gate (Phase 0
verified it); if the two differ, `CLAUDE.md` wins.
```
cd "/c/Users/Connor Wardell/Desktop/PLUTO2/Hobbies/Programming/DungeonCrawlStoneSoup/crawl-ref/source/MSVC" && MSYS_NO_PATHCONV=1 "/c/Program Files/Microsoft Visual Studio/18/Insiders/MSBuild/Current/Bin/MSBuild.exe" crawl-ref.sln /m "/p:Configuration=Release Tiles" /p:Platform=x64 /p:PlatformToolset=v145 /p:WindowsTargetPlatformVersion=10.0 /v:minimal
rm -f "/c/Users/Connor Wardell/Desktop/PLUTO2/Hobbies/Programming/DungeonCrawlStoneSoup/crawl-ref/source/nul"
cd "/c/Users/Connor Wardell/Desktop/PLUTO2/Hobbies/Programming/DungeonCrawlStoneSoup/crawl-ref/source" && ./crawl.exe -builddb -reset-cache
cd "/c/Users/Connor Wardell/Desktop/PLUTO2/Hobbies/Programming/DungeonCrawlStoneSoup/crawl-ref/source" && ./crawl.exe -script dcc_smoke.lua
```
The last command exists from Phase 1b on. Phase 0 confirms the second and
third work at all and amends this block if they do not. The build may leave an
empty `source/nul`; always delete it, never commit it. `git status` must show
no `nul`, no `crawl.exe`, no `dat/des` cache changes before committing.

## Phase list

| # | Phase | Status |
|---|-------|--------|
| 0 | Gate discovery and repo CLAUDE.md | done 2026-10-25 |
| 1a | `GAME_TYPE_DCC` exists and is selectable | done 2026-10-25 |
| 1b | `dcc.cc` module, `dcc` dlua library, smoke script | done 2026-10-25 |
| 2a | Per-floor clock core and collapse | done 2026-10-25 |
| 2b | Clock surface: status light, Announcer channel, Zot clock off | done 2026-10-25 |
| 3a | `FPROP_DCC_SAFE` monster-exclusion property | done 2026-10-25 |
| 3b | Safe-room vault on every floor | done 2026-10-25 |
| 4a | Achievement table, hooks, Announcer lines | done 2026-10-25 |
| 4b | Loot boxes and the kiosk | done 2026-10-25 |
| 5a | Quest engine (Lua, four objective kinds) | not started |
| 5b | Quest giver NPC and offer flow | not started |
| 6a | Floor bosses | not started |
| 6b | Floor themes | not started |
| 6c | Sponsors (built on gods) | not started |
| 6d | Floor-3 species re-pick and kits | not started |

Phases 1, 2, 3, 4 were each split from one agreed feature into two commits
because the first half changes trunk plumbing and the second adds the new
module; they have different risk profiles.

---

## Phase 0 — Gate discovery and repo CLAUDE.md

Scope. Establish exactly which headless commands work on this machine with the
Release Tiles build, and write them into a repo-root `CLAUDE.md` so every
later implementer has the gate without rediscovering it. No gameplay code.
Produces: `CLAUDE.md` (repo root), `docs/dcc/spec-history/discovery.md`
(findings), `source/.gitignore` gains `nul`, and this spec's Standard gate
block corrected if needed.

Budget: ~4 files touched; read 2 (`crawl-ref/docs/develop/testing.md`,
`source/ctest.cc` lines 1–110).

Steps.
1. Run the MSBuild command from the Standard gate. Confirm
   `source/crawl.exe` is produced. Note wall time. Delete `source/nul`.
2. From `source/`: `./crawl.exe -builddb -reset-cache`. Expect exit 0 and no
   window. If a window opens or it hangs, try `./crawl.exe -headless -builddb`
   (`CLO_HEADLESS` exists for `USE_TILE_LOCAL`); record which works.
3. From `source/`: `./crawl.exe -script genlevel.lua D:1` (an existing script
   in `source/scripts/`). Expect `dump-D-1.map` written and exit 0. Delete the
   dump. If this fails, record the error verbatim: it decides whether the
   smoke-script gate is viable or whether `-test` on a Debug build is needed.
4. Time-boxed (stop after one attempt): build `Debug Console|x64` with the
   same MSBuild line but `"/p:Configuration=Debug Console"`. If it links, run
   `./crawl.exe -test youdie` from the output location (check
   `MSVC/bin/x64/Debug Console/` and whether it copies to `source/`). Record
   pass/fail and time. Do not fix build errors; record them. Whatever the
   result, this build is optional tooling, not part of the per-phase gate;
   `CLAUDE.md` lists it under "optional checks" with the recommendation to run
   it at feature boundaries (after phases 1b, 2b, 3b, 4b, 5b).
5. Write `CLAUDE.md` at the repo root: the working gate commands verbatim, the
   `nul` rule, "run from `source/`", the isolation rule from Decisions (one
   paragraph), the no-trademark rule, and a pointer to `docs/dcc/SPEC.md`.
6. Add `nul` to `source/.gitignore`. Write findings to
   `docs/dcc/spec-history/discovery.md`. If `-builddb` or `-script` needed
   different flags, edit the Standard gate block in this file to match and
   note it in the discovery file.

Gate: the commands that steps 1–3 found working, exactly as written into
`CLAUDE.md`.

Verification: a human reads `CLAUDE.md`, pastes the gate, sees exit 0.

Risks. `-script` may not go headless in a tiles build on Windows (the help
text in `main.cc:506` claims tiles headless supports only `-objstat`/`-mapstat`,
but `enter_headless_mode()` is called for `-script` and `tilesdl.cc` is
guarded throughout). If it truly fails and the Debug Console build works,
switch the smoke gate to `crawl.exe -test dcc` with the script moved to
`source/test/dcc.lua`; update the Standard gate and Phase 1b accordingly. If
neither works, the gate is build + `-builddb` only and each phase's
Verification step becomes mandatory; say so in `CLAUDE.md`.

Status: done 2026-10-25

---

## Phase 1a — `GAME_TYPE_DCC` exists and is selectable

Scope. A new game type appears in the startup menu as "Crawl Live", can be
started, saves to `saves/dcc/`, and plays exactly like Descent (because
`game_is_descent()` now also returns true for it). Visible in all builds, not
alpha-only. Also reachable via rc `type = dcc` and CLO `-dcc`.

Budget: ~7 files touched (`game-type.h`, `state.h`, `state.cc`,
`initfile.cc`, `startup.cc`, `ng-setup.cc`, `tilepick.cc`); read the Descent
lines in each (grep `GAME_TYPE_DESCENT` and `game_is_descent`).

Details.
- `source/game-type.h`: append `GAME_TYPE_DCC` after `GAME_TYPE_DESCENT`,
  before `NUM_GAME_TYPE` (the comment says never reorder).
- `source/state.h/.cc`: add `bool game_is_dcc() const;` (`type ==
  GAME_TYPE_DCC`). Change `game_is_descent()` to `type == GAME_TYPE_DESCENT ||
  type == GAME_TYPE_DCC`. Add `GAME_TYPE_DCC` cases: `game_type_name_for` →
  "Crawl Live"; `game_savedir_path` → "dcc/"; `game_type_qualifier` → the
  `-<str>` branch.
- `source/initfile.cc`: `modes["dcc"] = GAME_TYPE_DCC` (unconditionally, not
  under the alpha check, ~line 236); `gametype_to_str` case → "dcc" (~1277);
  new `CLO_DCC` in the `commandline_option_type` enum and `"dcc"` at the
  matching index of `cmd_ops[]` (the two lists must stay aligned); handler
  mirroring `case CLO_DESCENT:` (~6189); `_game_type_to_clo` case (~5647).
- `source/startup.cc`: add `{GAME_TYPE_DCC, "Crawl Live", "A televised,
  timed, one-way descent. The floor collapses when the clock runs out and the
  loot boxes are rigged."}` to `entries` after Descent; the alpha-hiding check
  at ~456 stays Descent-only; add `GAME_TYPE_DCC` to the `case
  GAME_TYPE_NORMAL: case GAME_TYPE_DESCENT:` groups in `on_button_focusin` and
  `menu_item_activated`.
- `source/ng-setup.cc`: `case GAME_TYPE_DCC:` alongside `GAME_TYPE_DESCENT`
  in `setup_game`'s switch (~405). `_setup_generic` already does the Descent
  work via `game_is_descent()`.
- `source/tilepick.cc`: `case GAME_TYPE_DCC: return TILEG_STARTUP_IRONSOUP;`.
- `grep -rn "GAME_TYPE_DESCENT" source/*.cc` after editing and confirm every
  remaining hit is either the alpha-hide check or a `switch` that now also has
  the DCC case. `newgame.cc:430` is under `DGAMELAUNCH && TOURNEY`; skip.

Gate: Standard gate (first three commands; the smoke script does not exist
yet).

Verification: launch `source/crawl.exe`, pick "Crawl Live", create a
character, confirm the up stairs crumble after the first descent and a save
appears under `saves/dcc/`.

Risks. MSVC may warn on switches missing the new enum; fix by adding cases,
never by adding `default:`. The save browser lists games by type; a wrong
`game_type_name_for` shows an empty label, not a crash.

Status: done 2026-10-25

---

## Phase 1b — `dcc.cc` module, `dcc` dlua library, smoke script

Scope. The home for all DCC C++ code and its Lua surface exists, is wired
into both build systems, and the Standard gate's fourth command runs green.
Produces `source/dcc.h`, `source/dcc.cc` (`dcc_setup_new_game()` called from
`ng-setup.cc`'s `_setup_generic` under `game_is_dcc()`; for now it sets
`you.props["dcc_version"] = 1`), `source/l-dcc.cc` (dlua table `dcc` with
`dcc.is_active()` and `dcc.enable_for_script()`), registration in
`source/dlua.cc` and `source/l-libs.h`, entries in `source/Makefile.obj` and
`source/MSVC/crawl.vcxproj` (the `.filters` file is optional; skip it), and
`source/scripts/dcc_smoke.lua`.

Budget: ~9 files touched (two are one-line registrations); read `l-dgn.cc`
lines 1780–1800 and ~1840–1910 (how a dlib table is declared), `dlua.cc`
300–330, `ctest.cc` 90–110, `scripts/genlevel.lua`, `dat/dlua/test.lua`.

Details.
- `dcc.enable_for_script()` sets `crawl_state.type = GAME_TYPE_DCC` and runs
  the same setup a new DCC game would need before level generation:
  `initialise_branch_depths()` (sets `brdepth[BRANCH_DUNGEON] = 12` for
  descent) and whatever sets `DESCENT_WATER_BRANCH_KEY` /
  `DESCENT_POIS_BRANCH_KEY` in `you.props` (grep `ng-init.cc`/`ng-setup.cc`;
  `dungeon.cc:4087` ASSERTs they exist when placing branch entrances in
  descent). Factor that into `dcc_setup_new_game()` so both paths share it.
- Smoke script v1: call `dcc.enable_for_script()`, assert `dcc.is_active()`
  and `dgn.is_descent()`, then for `D:1`, `D:6`, `D:12`: `debug.goto_place`,
  `test.regenerate_level()`, assert `test.level_has_down_stair()` (D:12 is
  the branch-choice floor in descent; if it has no plain down stair, assert
  only that generation did not error). Print one line per check.
- Scripts run via `dlua.execfile` with `script.simple_args()` available; see
  `genlevel.lua`. The script must `error()` on failure so the exit code is
  non-zero; confirm by temporarily asserting false once.

Gate: Standard gate, all four commands.

Verification: `./crawl.exe -script dcc_smoke.lua` prints the checks and exits
0; `./crawl.exe -script dcc_smoke.lua` after deliberately breaking an assert
exits non-zero.

Risks. Level generation in script mode without a full `newgame` may trip
ASSERTs that a real game never sees (player species unset, etc.). Fix inside
`dcc.enable_for_script()` by calling the same helpers `debug.reset_player_data`
/ `debug.dungeon_setup` use (see `l-debug.cc`), not by weakening trunk ASSERTs.
If `-script` proved unviable in Phase 0, this phase instead creates
`source/test/dcc.lua` and the gate uses the Debug build's `-test dcc`.

Status: done 2026-10-25

---

## Phase 2a — Per-floor clock core and collapse

Scope. In DCC, entering a new connected-branch floor starts a floor clock
(1500 turns on D:1–3, 1200 elsewhere, stored as auts in
`you.props["dcc_floor_aut_left"]`); every player turn decrements it by
`you.time_taken`; at zero the floor collapses: a forced one-floor descent with
20% max-HP damage (min 1, never lethal), or at a branch bottom the Zot-touch
fallback. In portal vaults, the Abyss, Pandemonium and Ziggurats (any branch
that is not `is_connected_branch()`) the clock pauses and resumes on return
to a real floor (Resolved decision 8). No UI yet beyond plain `mpr`
messages at entry and collapse. Produces `source/dcc-clock.h/.cc`, hooks in
`files.cc` (next to `decr_zot_clock()` ~2508) and `player-reacts.cc` (next to
`incr_zot_clock()` ~1435), dlua `dcc.floor_turns_left()`,
`dcc.set_floor_turns_left(n)`, `dcc.tick(aut)`, and smoke checks.

Budget: ~8 files touched (`dcc-clock.h`, `dcc-clock.cc`, `files.cc`,
`player-reacts.cc`, `l-dcc.cc`, `Makefile.obj`, `crawl.vcxproj`,
`scripts/dcc_smoke.lua`); read `zot.cc` (whole, 411 lines: the model),
`stairs.h` (`down_stairs` signature), `files.cc` 2240–2270 and ~2500–2515.

Details.
- `dcc_enter_floor(just_created)`: called from the `files.cc` hook on every
  arrival that makes changes, passing `just_created_level` (the Zot clock's
  condition). Outside connected branches it does nothing (the clock pauses).
  A newly created floor always gets a fresh clock from the tunables table
  (`NOTE_MESSAGE` "Floor clock started: N turns"). A floor visited before
  (Descent stair peek, Hell exit to the Vestibule, Abyss exit onto an older
  floor) resumes its clock from `you.props["dcc_floor_clocks"]`; a spent
  clock is never parked, and the table is capped (oldest dropped). The clock
  starts once per floor, not once per peek.
- `dcc_tick_clock()`: no-op unless `game_is_dcc()` and in a connected branch;
  subtract `you.time_taken`; call `dcc_collapse_floor()` when ≤ 0.
- `dcc_collapse_floor()`: if `is_valid_shaft_level(false)` (connected, not a
  branch bottom): message, forced `floor_transition()` to `depth + 1` with the
  shaft arrival (no trap path, no shaft messages or milestone); only once the
  level has changed: `dec_hp(max(1, hp_max / 5), false)`,
  `you.props["dcc_collapsed_count"]++`, note. Else (or if the fall did not
  happen) the shared `zot_touch_max_hp()` from `zot.cc` and a 100-turn
  retry clock.
- Smoke: enable DCC, goto D:1, regenerate, call `dcc_enter_floor` indirectly
  is not possible without a level change, so expose `dcc.start_floor_clock()`
  for scripts; assert `floor_turns_left() == 1500`; `dcc.tick(10)`; assert
  1499. Collapse in script mode skips only the fall itself;
  `dcc.tick()` returns the collapse result (`fall_skipped`, `no_floor`,
  `fell`), so the smoke checks a collapse on D:1, the no-floor penalty on
  D:12, and that the clock is paused in the Abyss and a portal vault.

Gate: Standard gate.

Verification: start a Crawl Live game, note "Floor clock started: 1500
turns", use `&^T` wizard time-skip or just rest; at zero you fall to D:2
injured.

Risks. The collapse `floor_transition()` from inside `player_reacts()` may be
unsafe mid-turn; if the Descent stair code asserts, defer the collapse to the
next turn start with a `you.props` flag checked at the top of
`dcc_tick_clock()`. Shafts in Descent land you without the peek; confirm the
stairs still crumble on arrival (`DESCENT_STAIRS_KEY` logic).

Status: done 2026-10-25

---

## Phase 2b — Clock surface: status light, Announcer channel, Zot clock off

Scope. The clock is visible and the Zot clock is gone in DCC. A status light
"Floor (N)" (WHITE >50%, YELLOW >25%, RED >10%, LIGHTMAGENTA ≤10%) appears
next to Zot's slot; Announcer messages fire at 50%, 25%, 10%, 50, 20 and 10
turns left and on collapse, each interrupting travel/rest; the new channel
`MSGCH_DCC` (rc name `dcc`) carries them. `zot_clock_active()` returns false
in DCC.

Budget: ~7 files touched (`status.h`, `status.cc`, `mpr.h`, `initfile.cc`,
`message.cc`, `zot.cc`, `dcc-clock.cc`); read `status.cc` 340–380 and
1150–1185 (`STATUS_ZOT` pattern) and `status.cc` ~1500–1520 (the display
order list), `initfile.cc` 1110–1150 (`message_channel_names`, has a
`COMPILE_CHECK` on length), `mpr.h` 20–60.

Details.
- `STATUS_DCC_FLOOR` appended in `status.h` before the terminator; a case in
  `fill_status_info` calling `_describe_dcc_floor(inf)` which pulls from
  `dcc-clock.h` (`dcc_floor_turns_left()`, `dcc_floor_turns_total()`); add it
  to the status order list near `STATUS_ZOT`.
- `MSGCH_DCC` appended before `NUM_MESSAGE_CHANNELS`; `"dcc"` appended to
  `message_channel_names`; default colour in `message.cc`'s channel-colour
  switch: LIGHTCYAN.
- Announcer helper `dcc_announce(const string&)` in `dcc-clock.cc` (moves to
  `dcc.cc` later if more callers appear): `mprf(MSGCH_DCC, ...)` then
  `interrupt_activity(activity_interrupt::force)`. Threshold crossing is
  detected old-vs-new like `incr_zot_clock` does with `bezotting_level`.
  Announcer tone: dry, mocking, PG-13. Example at 10%: "Ten percent left.
  The structural engineers have stopped taking your calls."
- `zot.cc`: `_zot_clock_active_in` gains `&& !crawl_state.game_is_dcc()`.

Gate: Standard gate (smoke: assert `dcc.floor_turns_total()` for D:1 is 1500
and D:5 is 1200 via `set`/`start` helpers; nothing UI-visible is scriptable).

Verification: in a Crawl Live game the "Floor (1500)" light shows, counts
down, changes colour, and the Zot light never appears.

Risks. `COMPILE_CHECK` on the channel-name array will fail the build if the
two lists disagree — that is the point. Status order list may be an array
with a fixed size; follow whatever `STATUS_GEM` did when it was added
(`git log -S STATUS_GEM -- source/status.cc`).

Status: done 2026-10-25

---

## Phase 3a — `FPROP_DCC_SAFE` monster-exclusion property

Scope. A cell property that monsters hostile to the player will not enter or
be placed on, and inside which there is no combat (Resolved decision 9).
Produces: `FPROP_DCC_SAFE` bit in `source/fprop.h`, `is_dcc_safe_cell(coord)`
in `dcc-safe.cc` (with its own `game_is_dcc()` check), the des `KPROP` name `dcc_safe`
(whatever `str_to_fprop` reads), a guard in `mon-act.cc`
`mon_can_move_to_pos` (~3277, mirror the sanctuary clause: refuse if target is
DCC-safe, mover is not, and `!mons->wont_attack()`), a guard in monster
placement in `mon-place.cc` (the function that validates a generation spot;
summons included), dlua `dcc.is_safe_cell(x, y)`, and a tiny test vault
`source/dat/des/dcc/test_safe.des` (tag `dcc_only unrand`, WEIGHT 0, a 5x5
room with `KPROP: . = dcc_safe`) used by the smoke script via
`dgn.place_map(dgn.map_by_name(...))` + `dcc.is_safe_cell` on an interior
cell.

Budget: ~8 files touched; read `fprop.h`, `fprop.cc` (small),
`mon-act.cc` 3277–3320, the sanctuary callers list (`grep -n is_sanctuary
source/mon-place.cc source/mon-act.cc`), `docs/develop/levels/syntax.txt`
KPROP section (~1420–1440).

Gate: Standard gate.

Verification: in wizard mode (`&`) place the test vault with `&L`, summon a
hostile outside the room, stand inside: it does not enter and cannot hurt
you, even standing in the doorway next to you; you cannot melee, fire, throw
or cast a harmful spell from inside (the Announcer refuses).

No combat (added 2026-10-25 by the user's decision): `could_harm()` refuses
any harm by or to an actor on a safe cell (melee, beams, explosions,
clouds); `_is_valid_foe()` drops foes across the edge; the monster step is
cancelled when it would melee across it; the player's melee, fire/throw,
spell and evocation checks refuse with an Announcer line. Hooks and their
isolation exceptions are listed in `CLAUDE.md`.

Risks. `env.pgrid` bit width: confirm there is a free bit; if `FPROP_` uses all
32, this needs the next type up and a `tags.cc` touch (then stop and record an
open question rather than widening). Monsters already inside a safe cell (a
friendly that turned hostile) must still be able to leave: they may only move
toward the way out (cells of the same room strictly closer to it, or off the
safe cells), and walk out on their own.

Status: done 2026-10-25

---

## Phase 3b — Safe-room vault on every floor

Scope. Every DCC level outside the Abyss places exactly one safe-room vault
containing all three up stairs (`{ ( [`, so every arrival — stairs or branch
exit, which trunk converts from `{` at branch tops — lands inside), one door,
a general shop, floor cells `KPROP: dcc_safe`, `no_monster_gen` and
`no_tele_into`, and a placeholder kiosk cell `K` (plain floor for now, marked
with a feature description "a loot box kiosk (inactive)" via `lm_pdesc`). On
D:1 it also serves as the arrival vault. Produces
`source/dat/des/dcc/safe_rooms.des` (2–3 layout variants, all tagged
`dcc_safe_room dcc_only`), a `dcc_only` tag check in `maps.cc` (~905, next to
`no_descent`) and `dungeon.cc` (~4817), a `_place_dcc_safe_room()` call in
`dungeon.cc`'s level build (before random vaults, using
`random_map_for_tag("dcc_safe_room")` + `_build_secondary_vault` +
`dgn_ensure_vault_placed`), and the D:1 arrival selection (~3256) using the
safe room when `game_is_dcc()`.

Budget: ~5 files touched (`safe_rooms.des`, `dungeon.cc`, `maps.cc`,
`scripts/dcc_smoke.lua`, delete `test_safe.des` if superseded); read
`dungeon.cc` around 3240–3270 (arrival), 2950–2975 (descent fixups) and the
`_fixup_stone_stairs` comment block (~2000–2140), `syntax.txt` shop section
(~1300–1400) and `dat/des/builder/shops.des` header.

Details.
- Shop line: `KFEAT: S = general shop count:8 name:Commissary type:Store`
  (adjust to the syntax doc). It charges gold at normal DCSS prices; no free
  picks, no voucher-style vending (decided 2026-10-25).
- Smoke: for D:1..D:12 generate the level and assert a vault whose name
  starts with `dcc_safe_room` is in `debug.vault_names()` (check the exact
  return shape in `l-debug.cc`), and that every `stone_stairs_up_*` feature
  found via `dgn.find_feature`/`test.find_feature` lies on a `dcc.is_safe_cell`
  cell. Also run one non-DCC level (`debug.goto_place("D:3")` after a fresh
  `crawl_state` is not possible in-process, so instead assert
  `dgn.map_by_name("dcc_safe_room_a")` has the `dcc_only` tag and trust the
  `maps.cc` guard; add a C++ `ASSERT` in `_place_dcc_safe_room` that it is only
  reached under `game_is_dcc()`).

Gate: Standard gate.

Verification: start Crawl Live; you begin inside a walled room with a shop
and a door; descend; you arrive inside another. No combat happens inside
(Phase 3a): monsters following you down the stairs queue outside the door
and cannot attack you while you stand in the room. The shop and the
arrival stairs sit on `dcc_safe` cells; the door cell itself may stay
unsafe (it is the way out for a hostile that ends up inside).

Risks. Vaults containing all three up stairs: the stair fixup may refuse or
relocate them; `grated_community.des` and similar vaults with stairs are the
precedent to copy flags from. Descent's branch-choice floor places several
branch entrances; the safe room must not swallow them (it only holds up
stairs). If the builder vetoes levels too often (`dgn_veto_exception`), make
the vault `transparent` and smaller before changing C++.

Status: done 2026-10-25

---

## Phase 4a — Achievement table, hooks, Announcer lines

Scope. A C++ table of ~16 one-shot achievements, each `{id, name, announcer
line, box tier}`, with predicates evaluated at hook points: floor entry
(already hooked via `dcc_enter_floor`), monster kill (new one-line hook in
`mon-death.cc` beside `handle_monster_dies_lua`), and `dcc_check_achievements()`
callable from later phases. Unlocking announces on `MSGCH_DCC`, takes a note,
and appends the id to `you.props["dcc_achievements"]`. Box awarding is a stub
`dcc_award_box(tier)` that only records `you.props["dcc_boxes"]` (Phase 4b
makes them openable). Produces `source/dcc-achieve.h/.cc`, hook in
`mon-death.cc`, dlua `dcc.achievements()`, `dcc.has_achievement(id)`,
`dcc.unlock(id)`, `dcc.box_count()`, smoke checks.

Budget: ~8 files touched (`dcc-achieve.h`, `dcc-achieve.cc`,
`mon-death.cc`, `dcc.cc`, `l-dcc.cc`, `Makefile.obj`, `crawl.vcxproj`,
`scripts/dcc_smoke.lua`); read `mon-death.cc` 1225–1250 and 2219–2245,
`notes.h` enum, `hiscores.h` `mark_milestone` (call it too; it is a no-op
offline).

Seed table (ids; tune text freely, keep the one-shot rule):
`first_blood` (first kill, Bronze); `floor_2/4/6/8/10/12` (Bronze, Bronze,
Silver, Silver, Gold, Gold); `unique_1/3/5` (uniques killed, Silver, Gold,
Platinum); `big_game` (kill HD ≥ XL+5, Gold); `kill_streak` (10 kills within
50 turns, Silver; track last-10 kill times in `you.props`); `quick_exit`
(leave a floor with >60% clock left, Silver); `fell_through` (survived a
collapse, Bronze: "Consolation prize"); `signed` (worship a god, Bronze);
`xl_10/20` (Silver, Gold); `rune_1/3` (Gold, Platinum); `orb` (Celestial).
Announcer examples: first_blood — "First blood. Try to look like you meant
it."; fell_through — "Gravity: one. You: still breathing. A Bronze box for
the effort."

Gate: Standard gate (smoke: `dcc.unlock("first_blood")` twice → one entry;
`box_count()` == 1; generate D:2 after calling the floor-entry hook helper →
`has_achievement("floor_2")`).

Verification: kill one monster in a Crawl Live game; the Announcer line
appears in LIGHTCYAN and `?:` notes show the unlock.

Risks. Kill hook must ignore kills that are not the player's (`killer_type`
is player / player-ally) and not count summons/zombies oddly; copy the
conditions `_give_player_experience` or `xp_gain` uses.

Achievement rules as built (later phases rely on these):
- A counted kill is one trunk gives XP for (`mons_gives_xp()`, not
  pacified), by the player or an ally. Charmed hostiles count; permanent
  allies, summons and banished, timed-out or reset monsters do not.
- `unique_N` counts distinct unique types (`you.props["dcc_uniques"]`), so
  Boris coming back or Natasha reviving counts once.
- `floor_N` uses floors descended (`you.props["dcc_floors"]`): D:1 is
  floor 1; a new floor in the same branch adds its depth gain, and a new
  floor in another branch adds 1. Absolute depth over-counts in Descent
  (Lair:1 is absolute depth 11 wherever its entrance is).
- `quick_exit` (anti-grind decision, 2026-10-25): it needs a deliberate
  descent from the floor (a down stone stair or a branch entrance; not a
  shaft, trapdoor, hatch, collapse, banishment or Abyss exit), no Abyss or
  portal visit from that floor, at least one counted kill on it, and more
  than 60% of its clock left. Diving D:1 without a kill earns nothing.
- Each unlock is one `MSGCH_DCC` line that names its box, plus a note and a
  milestone. All the unlocks from one hook (an arrival, a kill, a turn)
  interrupt the player once.

Status: done 2026-10-25

---

## Phase 4b — Loot boxes and the kiosk

Scope. Boxes open. The safe room's `K` cell becomes a kiosk: a Lua
`function_at_spot` marker (see `dat/dlua/lm_trig.lua` ~460–475) whose callback
in `source/dat/dlua/dcc.lua` calls `dcc.open_boxes()`; the C++ side prompts
("Open N boxes? [y/n]"), refuses outside a `FPROP_DCC_SAFE` cell, and for each
box drops tier-appropriate items on and around the player with an Announcer
line. Produces `source/dcc-boxes.h/.cc`, kiosk marker in `safe_rooms.des`,
`dat/dlua/dcc.lua`, dlua `dcc.open_boxes()`, smoke checks.

Budget: ~8 files touched; read `acquire.h`, the `acquirement_create_item`
body (`acquire.cc`, find `AQ_SCROLL` with `grep -rn AQ_SCROLL source/*.h`
for the agent codes), `makeitem.h` (`items()` signature), `lm_trig.lua`
460–540, reference fork `dungeon.cc` 6267–6400 (`_stock_vendor_item`,
`place_vendor`: the model for "acquirement-quality free picks").

Loot tables (counts of items; "acq" = `acquirement_create_item` with a
random class from `shuffled_acquirement_classes(false)`, agent a new
`AQ_DCC_BOX` constant defined in `dcc-boxes.h` with a value above existing
codes; "rand" = `items(true, OBJ_RANDOM, OBJ_RANDOM, level)` with level =
absolute depth + 3, i.e. `env.absdepth0 + 4`, not `you.depth + 3`:
Resolved decision 10):
Bronze 2 rand; Silver 1 acq + 1 rand; Gold 2 acq; Platinum 3 acq (one forced
`OBJ_WEAPONS` or `OBJ_ARMOUR`); Legendary 3 acq + 1 randart (apply
`make_item_randart`); Celestial 4 acq + 1 randart + 1 `OBJ_WANDS`. Add a
`dcc_achieve` call after opening so box-related achievements can exist later.

Gate: Standard gate (smoke: on a generated D:1 with the player placed on the
kiosk cell via `you.moveto`/`debug` helper, `dcc.unlock` a Bronze and a Gold
achievement, call `dcc.open_boxes()` → `box_count()==0` and ≥ 4 items on
cells within radius 2; call again off-room → returns false).

Verification: unlock an achievement in play, walk to the kiosk in the next
safe room, answer `y`, loot appears.

Risks. `crawl.yesno` prompts in script mode block; `dcc_open_boxes` must skip
the prompt when `crawl_state.script` or take a `bool force` from Lua.
Acquirement needs a sane `you` (species, skills) to pick classes; in script
mode results may be odd but must not crash.

Kiosk rules as built (5b's LOS-triggered offer should follow the same
pattern; details in `spec-history/discovery.md`): only a deliberate step
(a move command, not running/travel/autoexplore, a blink or a teleport)
asks; the boxes that fit the level's free item slots are worked out before
asking, and none fitting jams without a prompt; opening is a cancellable
delay, one box per 2 turns. No box item is one the player's god forbids,
and Legendary/Celestial always hold an artefact.

Status: done 2026-10-25

---

## Phase 5a — Quest engine (Lua, four objective kinds)

Scope. Each DCC floor gets one quest, chosen by depth from a Lua table, tracked
with dungeon-event listeners, rewarded with a box. Kinds: **kill** (N of a
monster type present on this floor, via `DET_MONSTER_DIED` global listener),
**fetch** (a flagged item placed at a random reachable floor cell, completed
on `DET_ITEM_PICKUP` at that cell), **reach** (stand on a chosen far cell,
positional `DET_PLAYER_MOVED`), **survive** (N turns with no HP drop below
50%, `DET_TURN_ELAPSED`). State in `dgn.persist.dcc_quest` (per level, which
is exactly the quest's lifetime). Produces `source/dat/dlua/dcc_quests.lua`
(a `DccQuestMarker` class modelled on `lm_timed.lua`/`lm_trig.lua`, a
`dcc_quest_setup(e)` entry called from `safe_rooms.des`'s Lua epilogue, and
`dcc_quest_debug()` for scripts), dlua `dcc.announce(text)` and
`dcc.quest_state()` in `l-dcc.cc`, a hook in `safe_rooms.des`, smoke checks.
Acceptance is automatic in this phase (every quest is active on arrival);
5b adds the offer.

Budget: ~4 files touched; read `lm_timed.lua`, `lm_trig.lua` 1–260,
`l-dgnevt.cc` (event accessors), `dat/dlua/dungeon.lua` 45–65
(`dgn.persist` lifetime), `dat/dlua/sprint.lua` (TriggerableFunction
usage), `syntax.txt` MARKER section (~1590).

Gate: Standard gate (smoke: generate D:1 as DCC, `dcc.quest_state()` returns a
table with `kind` in the four names and `done == false`; for a kill quest,
`dcc_quest_debug().force_complete()` → `box_count()` increments and
`done == true`).

Verification: on arrival the Announcer states the quest; completing it awards
a box.

Risks. A fetch item can be destroyed or carried off by a monster; make fetch
quests also complete if the item is in the player's inventory at the next
floor transition (checked in `dcc_enter_floor` via a Lua callback) and
otherwise expire silently. Kill quests must choose a monster type that
actually exists on the floor at build time (`test.level_monster_iterator`
style scan in the epilogue).

Status: not started

---

## Phase 5b — Quest giver NPC and offer flow

Scope. The safe room contains a quest giver: a friendly non-following
monster (`MONS: human name:Floor_Steward n_rpl n_des att:good_neutral`, or
whichever attitude does not follow — check `syntax.txt` ~829–835) with a
`function_in_los` trigger. On first sight the Announcer introduces them and
`crawl.yesno` offers the floor's quest; declining leaves no quest; accepting
activates the Phase 5a marker. The NPC is `M_NO_EXP_GAIN`-equivalent (use
`MonPropsMarker` to set a prop the kill hook ignores) and killing it voids
the quest with a taunt. Produces edits to `safe_rooms.des`,
`dcc_quests.lua`, `dcc.lua`, smoke checks.

Budget: ~4 files touched; read `lm_mon_prop.lua`, `lm_trig.lua` 474–540,
`mon-death.cc` kill hook from 4a.

Gate: Standard gate (smoke: after generating D:1, `dcc.quest_state().offered
== false`; call `dcc_quest_debug().accept()` → active; in script mode the
`yesno` is bypassed with the forced answer).

Verification: walk into the safe room on D:2, see the steward, get the
prompt, accept, see the quest text.

Risks. `good_neutral` monsters attack hostiles and may wander out of the
room; if so use a stationary flag via `mons_prop`/`MonPropsMarker` or
`att:fellow_slime`-style non-following attitude. Players can attack the NPC;
that is allowed (void the quest, no other penalty).

Status: not started

---

## Phase 6a — Floor bosses

Scope. On D:3, D:6, D:9, D:12 and Zot:5 a boss vault (tag `dcc_boss
dcc_only`, one per depth band, placed like the safe room but far from it)
spawns a buffed named monster (des `MONS:` with `hd:`/`hp:`/`name:` and a
`MonPropsMarker` prop `dcc_boss`). Announcer introduces it on first LOS;
killing it unlocks `boss_<depth>` (Gold → Legendary by depth) via the 4a kill
hook reading the prop. Produces `source/dat/des/dcc/bosses.des`, a
`_place_dcc_boss()` call in `dungeon.cc` beside the safe-room call, kill-hook
extension in `dcc-achieve.cc`, smoke checks.

Budget: ~5 files touched; read `dat/des/builder/uniques.des` header and one
`MONS:` example with `name:`/`hd:` syntax in `syntax.txt` (~760–900).

Gate: Standard gate (smoke: D:3 as DCC → a monster with prop `dcc_boss`
exists; D:2 → none).

Verification: reach D:3, meet the boss, kill it, get the Announcer line and
box.

Risks. Boss strength tuning is play-tested, not scripted; start at 1.5x HD
of the depth's top monster and leave a `dcc_boss_hd_mult` constant.

Status: not started

---

## Phase 6b — Floor themes

Scope. Each DCC floor announces a theme on entry and biases generation: a
small table in `dcc.cc` maps depth → theme id → (layout tag preference,
monster-set tweak via `dgn.set_random_mon_list`, Announcer blurb). Implemented
as a Lua prelude in `source/dat/des/dcc/themes.des` (one `NAME: dcc_theme_*`
map per theme with `TAGS: dcc_only layout` style hooks, following
`dat/des/builder/layout*.des` patterns) plus `dcc_enter_floor` reading the
theme for the blurb. Keep to 4 themes.

Budget: ~4 files touched; read `dat/des/builder/layout.des` header,
`l-dgn.cc` `set_random_mon_list`.

Gate: Standard gate (smoke: `dcc.theme_for("D:5")` returns one of the four
ids deterministically for a seeded game).

Verification: floor entry shows "Theme: ..." and monsters match.

Risks. Layout tags are weighted globally; a DCC-only layout must not leak
into normal games — `dcc_only` tag check from 3b covers vault selection;
confirm it also covers layouts (same `maps.cc` path).

Status: not started

---

## Phase 6c — Sponsors (built on gods)

Scope. The D:3 safe room contains a "sponsor booth": three random altars
(reusing the overflow-temple altar picker, `ng-init.cc` `initialise_temples`
pattern) tagged so Descent's no-temple rule still holds elsewhere. Joining a
god from the booth awards a Gold box and the `signed` achievement; the
Announcer frames gods as sponsors in its lines only (gods keep their DCSS
names). Produces a `safe_rooms.des` variant for D:3 (or a `SUBST` on the
depth), a small `dcc-sponsor.cc` or additions to `dcc.cc` hooking
`god_pitch`/join (find the single join point in `religion.cc`,
`join_religion`), smoke checks.

Budget: ~5 files touched; read `ng-init.cc` 60–140, `religion.cc`
`join_religion`.

Gate: Standard gate (smoke: D:3 as DCC has ≥ 3 altar features inside safe
cells).

Verification: reach D:3, pick a god at the booth, get the box.

Risks. Descent disables the temple; altars still generate elsewhere via
overflow temples — fine, the booth is additive.

Status: not started

---

## Phase 6d — Floor-3 species re-pick and kits

Scope. On first arrival at D:3 the Announcer offers a one-time
"re-roll": a menu of three random playable species (excluding the current
one) applied with `change_species_to()` (`species.h:103`), plus a kit pick
from a small table (3 starting-kit bundles: weapon+armour, spellbook+robe,
consumables; granted via `acquirement_create_item`/`items`). Declining keeps
everything. Jobs cannot change in DCSS; kits are the substitute. Produces
`source/dcc-repick.cc`, a call from `dcc_enter_floor`, a menu using the
existing `Menu` class (model: `acquirement_menu` in `acquire.cc`), smoke
checks with a forced choice.

Budget: ~5 files touched; read `species.h`, `wiz-you.cc` 110–140
(`wizard_change_species`), `acquire.cc` `acquirement_menu`.

Gate: Standard gate (smoke: `dcc.repick_species("Minotaur")` changes the
species reported by the dlua `you.race()` accessor (confirm the exact name in
`l-you.cc`); assert it changed and `you.props["dcc_repicked"]` is set).

Verification: arrive on D:3, accept the re-roll, species changes, kit drops.

Risks. `change_species_to` is a wizard tool; it may leave mutations or
equipment in odd states (e.g. a Felid holding a sword). Run the same
post-change fixups wizmode runs and drop unwearable gear to the floor.

Status: not started

---

## Resolved decisions (user answered 2026-10-25)

1. Collapse stays a forced descent plus the max-HP hit, never death.
   Death-at-zero is noted as a possible later "hardcore" option only.
2. Clock values stay 1500 turns (D:1–3) / 1200 turns (deeper), as tunables.
3. The Zot clock is fully off in DCC.
4. Names stay as proposed: "Crawl Live", the Announcer, Commissary, Floor
   Steward, and the `dcc` prefixes.
5. The safe-room shop charges gold at DCSS prices.
6. The Debug Console `-test` build is optional, not part of the per-phase
   gate; run it every few phases or at feature boundaries.
7. The floor-3 re-pick is species-only plus kits.
8. (2026-10-25) The floor clock runs only on connected dungeon-branch floors.
   In portal vaults, the Abyss, Pandemonium, Ziggurats and any other
   non-connected branch it pauses, and it resumes when the player returns to
   a real floor.
9. (2026-10-25) No combat in a safe room, as in the books. While the player
   stands on a safe cell they cannot attack (no melee, firing, throwing,
   harmful spells or evocations; the Announcer refuses), hostile attacks
   cannot reach them (no melee across the edge, no missiles, spells or
   breath aimed at them), and allies inside do not fight either.
10. (orchestrator, 2026-10-25) Loot box "rand" items roll at absolute
   depth + 3 (`env.absdepth0 + 4`), not `you.depth + 3`. Branch-relative
   depth would make a Lair:1 box roll level-4 junk; on D:N both give N + 3.

## Assumptions (proceeding under)

- Builds run from Git Bash on this machine; Python and Perl are on PATH for
  the MSBuild pre-build step (`util/gen-all.py`), as the verified build implies.
- Descent's world (D:12, then branches, then Zot) is the DCC world; no
  separate branch layout.
- Upstream merges are the user's job; this spec only keeps them feasible.
- Trademark avoidance applies to in-game strings and content file names;
  the internal `dcc` identifier is fine.
- Phase 0 may amend the Standard gate block; later phases read it as amended.
