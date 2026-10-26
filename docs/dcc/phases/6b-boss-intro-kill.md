# Phase 6b — Boss intro, kill achievement and box

Split from the former Phase 6a (floor bosses). Phase 6a placed the boss
(`source/dcc-boss.cc`, monster prop `dcc_boss` holding the floor's name,
table row with `intro`, `death` and `achievement` strings); this phase makes
it talk and pay. Read `docs/dcc/SPEC.md` Goal and Decisions, then this file.
Gate and rules are in the repo-root `CLAUDE.md`. `source/` means
`crawl-ref/source/`.

Decisions restated:
- Achievements are one-shot predicates in the C++ table in
  `dcc-achieve.cc`; unlocking announces on `MSGCH_DCC`, takes a note, awards
  the row's box tier. A counted kill is one trunk gives XP for, by the
  player or an ally (banished, timed-out or reset monsters are no kill).
- A trunk file may gain one guarded line (`if (crawl_state.game_is_dcc())
  dcc_...;`) plus the `#include` it needs; the callee re-checks
  `game_is_dcc()`. Record every new hook in `CLAUDE.md`'s isolation list.
- Boss box tiers by depth: D:3 Gold, D:6 Gold, D:9 Platinum, D:12
  Platinum, Zot:5 Legendary.

## Scope

The first time the player sees a boss, the Announcer introduces it (its
table `intro` line), a note is taken and a milestone marked. When a boss
dies as a counted kill (player or ally), the Announcer says its `death`
line and the floor's `boss_*` achievement unlocks, awarding its box. Five
new achievement rows exist. Dying by other means (banishment, another
monster) pays nothing, as for every kill.

Produces: a guarded one-line hook in `source/player-notices.cc`
`seen_monster()`; `dcc_monster_seen()` and `dcc_boss_killed()` in
`source/dcc-boss.cc/.h`; five rows plus one call in `source/dcc-achieve.cc`;
dlua `dcc.see_monster(x, y)` (script-only); smoke checks; a `CLAUDE.md`
line recording the hook.

Budget: ~7 files touched (`player-notices.cc`, `dcc-boss.h`, `dcc-boss.cc`,
`dcc-achieve.cc`, `l-dcc.cc`, `scripts/dcc_smoke.lua`, `CLAUDE.md`); read
`player-notices.cc` 599–625 (`seen_monster`), `dcc-achieve.cc` 60–140 (the
table) and 462–504 (`dcc_monster_killed`), `dcc-achieve.h` (kinds and
`dcc_unlock`), `hiscores.h` `mark_milestone`, the Phase 5b smoke section's
`mark()`/`said()` helpers (`scripts/dcc_smoke.lua` from ~1676).

## Details

- The boss's row is found from its prop, never from the current level:
  `dcc_boss_row(const monster &)` parses `mons.props["dcc_boss"]` with
  `level_id::parse_level_id()` and calls `dcc_boss_for()`. A boss that
  followed the player downstairs keeps its identity.
- Hook: in `seen_monster()` (player-notices.cc), right after `mons->flags
  |= MF_SEEN;` (so good-neutral and already-seen monsters are already
  filtered): `if (crawl_state.game_is_dcc()) dcc_monster_seen(*mons);` with
  `#include "dcc-boss.h"`.
- `dcc_monster_seen(const monster &mons)`: `game_is_dcc()` and
  `dcc_is_boss(mons)` and no `mons.props["dcc_boss_seen"]` yet: set it,
  `dcc_announce(intro)`, `take_note(Note(NOTE_MESSAGE, 0, 0, "Met " +
  name))`, `mark_milestone("dcc.boss", "met " + name + ".")`. The flag is
  on the monster (saved with the level), so a reload cannot re-introduce.
- `dcc_boss_killed(const monster &mons)`: `dcc_announce(death)`,
  `dcc_unlock(achievement)`, note, `mark_milestone("dcc.boss", "defeated "
  + name + ".")`. Called from `dcc_monster_killed()` (dcc-achieve.cc)
  after the `counted`, `YOU_KILL(killer) || pet_kill` and pacified checks,
  before `dcc_unlock("first_blood")`, inside the existing
  `dcc_unlock_batch` (one interrupt for the whole kill).
- Achievement rows (kind `dcc_ach_kind::event`, threshold 0), appended
  before the table's end; `ASSERT(n <= 64)` in `dcc_check_achievements`
  still holds (25 rows): `boss_d3` "Opening Night" Gold; `boss_d6` "Mid-
  Season Twist" Gold; `boss_d9` "Sweeps Week" Platinum; `boss_d12` "Season
  Finale" Platinum; `boss_zot5` "Series Finale" Legendary. Lines in the
  Announcer's voice, e.g. boss_d3: "The Opening Act has left the stage.
  Feet first. A Gold box for the headliner."
- Intro lines (table `intro`), e.g. D:3: "And here comes the Opening Act!
  Contractually obliged to hit you until one of you stops." Death lines
  likewise; dry, PG-13, no book names.
- l-dcc.cc: `dcc.see_monster(x, y)` (script-only via `_dcc_script_only`)
  calls `seen_monster(mons)` on the monster at the level cell.
- Smoke (`phase6b()`): on a fresh D:3 with the boss placed
  (`dcc.enter_floor(true)`, `dcc.boss_pos()`): `mark()`;
  `dcc.see_monster(x, y)` → `said(name)` (the intro); `mark()`; again →
  nothing said; `dcc.save_roundtrip()`; `see_monster` → still nothing;
  `mark()`; boxes = `dcc.box_count()`; `dcc.kill(x, y)` →
  `dcc.has_achievement("boss_d3")`, `dcc.box_count() == boxes + 1`, last of
  `dcc.boxes()` is "Gold", `said("Opening Act")` (the death line). Zot:5:
  the same kill gives `boss_zot5` and a "Legendary" box. D:6:
  `dcc.remove_monster(x, y, "banish")` → no `boss_d6`, box count unchanged.
  The kills run at XL 26 like the 4a checks (no level-up prompt).
- `CLAUDE.md`: add the `seen_monster` hook to the isolation list (one
  line, under a "Phase 6b" bullet).

## Gate

Standard gate (`CLAUDE.md`).

## Verification

Wizard-skip to D:3, find the Opening Act: the Announcer's intro prints in
LIGHTCYAN once. Kill it: the death line, "Opening Night" unlock and a Gold
box, visible in `?:` notes; open the box at the next kiosk.

## Risks

- `seen_monster()` is reached from several places (view updates,
  polymorph); the per-monster `dcc_boss_seen` flag makes repeats harmless.
- A polymorphed boss keeps `mons.props`, so it still counts; accepted.

Status: not started
