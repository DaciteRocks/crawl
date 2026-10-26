# Phase 6a — Boss placement

Split from the former Phase 6a (floor bosses): this phase puts the boss on
the floor; Phase 6b makes meeting and killing it mean something. Read
`docs/dcc/SPEC.md` Goal and Decisions, then this file. Gate and rules are in
the repo-root `CLAUDE.md`. `source/` means `crawl-ref/source/`.

Decisions restated (do not relitigate):
- Bosses are placed monsters, not vaults. A C++ table maps a floor to a des
  monster spec string; the boss is placed on the floor's first arrival at a
  far reachable cell and carries the monster prop `dcc_boss`. Reason: Zot:5,
  Vaults:$ and the Hell ends are encompass maps where no secondary vault
  can be placed, and a monster at a far cell never fails to place.
- A boss never blocks the way down. Its reward is the kill achievement's
  box (Phase 6b) plus whatever it carries.
- No book names anywhere (no Carl, Donut, Mordecai, etc.). Boss names are
  original studio flavour.

## Scope

Five floors of a Crawl Live game hold a boss: D:3, D:6, D:9, D:12 and
Zot:5. On the player's first arrival on such a floor, one named, buffed
monster is placed far from the safe room, awake-or-asleep like any level
monster, hostile, with `mons.props["dcc_boss"]` set to the floor's name.
Revisits place nothing. Kill quests never pick a boss as a target. Nothing
yet happens on sight or death (6b).

Produces: `source/dcc-boss.h`, `source/dcc-boss.cc`; one DCC-internal call
in `dcc_enter_floor()` (`source/dcc-clock.cc`); a boss filter in
`dcc_quest_kill_targets()` and a `vaults_ok` fallback in `dcc_quest_spot()`
(`source/dcc-quest.cc/.h`); dlua `dcc.boss_floors()`, `dcc.boss_pos()`,
`dcc.boss_count()`; build registrations; smoke checks.

Budget: ~8 files touched (`dcc-boss.h`, `dcc-boss.cc`, `dcc-clock.cc`,
`dcc-quest.cc`, `dcc-quest.h`, `l-dcc.cc`, `Makefile.obj` +
`MSVC/crawl.vcxproj` as one-line registrations, `scripts/dcc_smoke.lua`);
read `dcc-quest.cc` lines 40–140 (`_good_quest_spot`, `dcc_quest_spot`,
`dcc_quest_kill_targets`), `dcc-clock.cc` `dcc_enter_floor()` (~line 308),
`mapdef.h` `class mons_list` (~697: `add_mons`, `get_monster`),
`dungeon.h` `dgn_place_monster` (~252), `l-dgnmon.cc` `dgn_create_monster`
(the model for parse-then-place), `docs/develop/levels/syntax.txt` 975–1015
(name flags, `hd:`, `hp:`).

## Details

- `dcc-boss.h`: `#define DCC_BOSS_KEY "dcc_boss"` (monster prop, value:
  `level_id::describe()` of the floor). `struct dcc_boss { branch_type
  branch; int depth; const char *name; const char *base; const char *spec;
  const char *intro; const char *death; const char *achievement; }`.
  `const dcc_boss *dcc_boss_for(const level_id &place)`; `monster
  *dcc_find_boss()`; `bool dcc_is_boss(const monster &mons)`; `monster
  *dcc_place_boss()`; `const vector<dcc_boss> &dcc_boss_table()`.
- Table (tune `hd:`/`hp:` freely; keep names original; `intro`/`death`/
  `achievement` are used by 6b, fill them now):
  - D:3 "the Opening Act": `ogre hd:7 hp:70 name:Opening_Act n_rpl n_the
    col:lightred`, achievement `boss_d3`.
  - D:6 "the Studio Enforcer": `orc knight hd:11 hp:110
    name:Studio_Enforcer n_rpl n_the col:lightmagenta`, `boss_d6`.
  - D:9 "the Camera Operator": `cyclops hd:15 hp:170 name:Camera_Operator
    n_rpl n_the col:yellow`, `boss_d9`.
  - D:12 "the Season Finale": `stone giant hd:19 hp:230 name:Season_Finale
    n_rpl n_the col:white`, `boss_d12`.
  - Zot:5 "the Series Finale": `golden dragon hd:25 hp:330
    name:Series_Finale n_rpl n_the col:lightcyan`, `boss_zot5`.
  Renamed monsters have no `monsters.txt` description (trunk file; Phase
  5b's steward set the precedent).
- `dcc_place_boss()`: `game_is_dcc()`, `dcc_boss_for(level_id::current())`
  non-null, and `dcc_find_boss()` null (a boss already on the level means a
  revisit or a rebuild; place nothing). Spot: `dcc_quest_spot(you.pos(),
  60, spot)`; if that fails, `dcc_quest_spot(you.pos(), 0, spot, true)`
  (new `bool vaults_ok = false` parameter: on encompass maps every cell is
  `MMT_VAULT`, so Zot:5 needs the relaxed search). Parse with `mons_list
  ml; const string err = ml.add_mons(spec);` (non-empty `err` is a table
  bug: `mprf(MSGCH_ERROR, ...)`, return null); `mons_spec ms =
  ml.get_monster(0); monster *m = dgn_place_monster(ms, spot, true);` then
  `m->props[DCC_BOSS_KEY] = place.describe()`. Return `m`.
- Call site: in `dcc_enter_floor()` (dcc-clock.cc), inside the existing
  `if (just_created && is_connected_branch(here) ...)` block, add
  `dcc_place_boss();`. Bosses, like safe rooms, exist only on connected
  floors.
- `dcc_quest_kill_targets()` (dcc-quest.cc ~153): a boss is placed outside
  vaults, so the vault filter does not exclude it; add `|| dcc_is_boss(mons)`
  to the skip condition. The kill quest must never ask for the boss.
- l-dcc.cc (all callable in scripts; none script-only): `dcc.boss_floors()`
  → array of `{ place=, name=, base= }` from the table (`base` is the spec's
  first words, e.g. "ogre", for the smoke's kill-target check);
  `dcc.boss_pos()` → `x, y, name` of `dcc_find_boss()` or nil;
  `dcc.boss_count()` → monsters on the level with the prop.
- Smoke (append a `phase6a()` section; helpers `check`, `mark`, `spawn`,
  `test.regenerate_level` are at the top of the script): for each entry of
  `dcc.boss_floors()`: `debug.goto_place(place)`, `test.regenerate_level()`,
  record `dcc.quest_kill_targets()`'s count for `base`, `dcc.enter_floor
  (true)`, then: `dcc.boss_pos()` non-nil and `name` matches; the cell is
  not `dcc.is_safe_cell`; `dcc.boss_count() == 1`; the kill-target count for
  `base` is unchanged (the boss is not a target); `dcc.enter_floor(false)`
  → still 1. D:2 and D:4: `boss_pos()` nil, `boss_count() == 0`. Zot:5 is
  in the list, so the encompass fallback is exercised every run.
  `dcc.save_roundtrip()` on D:3 → `boss_count()` still 1 and the prop
  survives (`boss_pos()` non-nil).

## Gate

Standard gate (`CLAUDE.md`).

## Verification

Start Crawl Live, wizard-skip to D:3 (`&~` then `D:3`), autoexplore: a
monster named "the Opening Act" is somewhere outside the safe room and is
hostile. Descend and return: there is still exactly one.

## Risks

- `dgn_place_monster()` may refuse the spot (occupied, a monster already
  there since the spot was chosen); retry up to 10 spots before giving up
  with a `dprf`. A floor without its boss is a missed feature, not a
  broken game.
- `hd:` re-rolls HP from hit dice unless `hp:` is given; give both. If
  `name:` with `n_rpl` prints oddly ("the the Opening Act"), drop `n_the`
  and put "the" in the name string instead; check with `dcc.boss_pos()`'s
  returned name in the smoke.
- On Zot:5 the relaxed spot search can land next to the Orb chamber's
  guardians; accepted (it is the last floor).

## As built (deviations from the text above)

- The `dcc_place_boss()` call in `dcc_enter_floor()` sits in its own
  `if (just_created && is_connected_branch(here))`, not inside the quest
  block, whose extra `old_place != here` test would skip a newly built floor
  with the same name as the one left. `dcc_place_boss()` itself places
  nothing on a level that already holds a boss.
- `dcc.boss_pos()` returns the name as seen (`name(DESC_THE, true)`) even
  when the player cannot see the boss; unseen it would read "something".
- The `vaults_ok` fallback keeps the 60% rule (`DCC_BOSS_FAR_PERCENT` in
  `dcc-boss.h`); there is no further fallback, since `dcc_quest_spot()` with
  any percent fails only when no spot is reachable at all.
- A boss kill gives no kill-quest credit (`dcc_quest_kill_counted()`).
- Zot:5 is an encompass map only for some orb-chamber layouts, so the
  smoke cannot rely on it for the `vaults_ok` path: it checks that a boss
  stands on a vault cell exactly when the level has no far spot outside
  vaults, and tests the `vaults_ok` search directly on Dis:7.

Status: done 2026-10-25
