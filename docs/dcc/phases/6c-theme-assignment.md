# Phase 6c — Theme assignment and Announcer blurb

Split from the former Phase 6b (floor themes): this phase picks and
announces a theme per floor; Phase 6d makes the theme change the floor.
Read `docs/dcc/SPEC.md` Goal and Decisions, then this file. Gate and rules
are in the repo-root `CLAUDE.md`. `source/` means `crawl-ref/source/`.

Decisions restated:
- A theme is one row of a C++ table in `dcc-theme.cc`: id, name, Announcer
  blurb, random-monster list spec, floor and rock colours, clock percent,
  quest tier bump, extra monster count. Four rows. No layout changes.
- The theme is chosen at build time (so 6d can act during the build) with
  the builder's RNG, stored in `you.props["dcc_themes"]` (hash: floor name
  → theme id). D:1 has no theme. Consecutive assignments never repeat.
- Themes apply to connected-branch floors only, like the clock and the
  safe room.

## Scope

Every connected-branch floor after D:1 is assigned one of four themes when
it is built, and on the player's first arrival the Announcer says
"Tonight's floor: <name>. <blurb>", with a note. The time/status command's
clock summary line also names the theme. The assignment is stable across
rebuilds and saves. Nothing else changes yet (the table's other columns
are data for 6d).

Produces: `source/dcc-theme.h`, `source/dcc-theme.cc`; one call at the top
of `dcc_place_safe_room()` (`source/dcc-room.cc`); one call in
`dcc_enter_floor()` and one line in `dcc_floor_clock_summary()`
(`source/dcc-clock.cc`); dlua `dcc.theme_for([place])`,
`dcc.theme_table()`, `dcc.set_theme(place, id)` and `dcc.reset_themes()`
(the last two script-only); build registrations; smoke checks.

Budget: ~8 files touched (`dcc-theme.h`, `dcc-theme.cc`, `dcc-room.cc`,
`dcc-clock.cc`, `l-dcc.cc`, `Makefile.obj` + `MSVC/crawl.vcxproj` as
one-line registrations, `scripts/dcc_smoke.lua`); read `dcc-room.cc`
`_wants_safe_room()` and `dcc_place_safe_room()` (~lines 70–80 and
~455–500), `dcc-clock.cc` `dcc_enter_floor()` (~308) and
`dcc_floor_clock_summary()`, `dcc-achieve.h` (the `you.props` key style),
`l-dcc.cc` 1382–1431 (registration list) and one `LUAFN` that reads a
`you.props` hash.

## Details

- `dcc-theme.h`: `#define DCC_THEMES_KEY "dcc_themes"`, `#define
  DCC_LAST_THEME_KEY "dcc_last_theme"`. `struct dcc_theme { const char
  *id; const char *name; const char *blurb; const char *mon_list; colour_t
  floor_colour; colour_t rock_colour; int clock_percent; int
  quest_tier_bump; int extra_monsters; }`. `const vector<dcc_theme>
  &dcc_theme_table()`; `const dcc_theme *dcc_theme_by_id(const string &)`;
  `const dcc_theme *dcc_theme_for(const level_id &place)` (null if none);
  `void dcc_assign_theme()`; `void dcc_announce_theme()`.
- Rows (names and blurbs are the Announcer's; the other columns are
  tuned in 6d, fill them now with these defaults):
  - `crowd` "Crowd Work": blurb "The studio packed the floor. Try to
    stay charismatic." `mon_list` `"place:{here} band w:30 / place:{here}
    w:10"`, colours LIGHTGREY/BROWN (BLACK = leave trunk's), clock 100,
    bump 0, extra 8.
  - `headliners` "Headliners": "Tonight's guests came up from four floors
    down. They were promised a bigger room." `mon_list`
    `"place:{deeper}"`, colours BLACK/BLACK, clock 120, bump 1, extra 0.
  - `reruns` "Reruns": "Nothing on this floor is new. Most of it is not
    even alive." `mon_list` `"place:{here} zombie w:5 / place:{here}
    skeleton w:3 / place:{here} spectre w:1"`, colours DARKGREY floor /
    LIGHTGREY rock, clock 100, bump 0, extra 0.
  - `lightning` "Lightning Round": "Half the time, twice the prize money.
    The sponsors insisted." `mon_list` `""`, colours YELLOW floor / RED
    rock, clock 60, bump 1, extra 0.
- `dcc_assign_theme()`: no-op unless `game_is_dcc()`, the current level is
  a connected branch and not D:1. If `you.props[DCC_THEMES_KEY]` already
  has the place, keep it (a vetoed-and-rebuilt level, or a smoke
  regeneration, keeps its theme). Else pick uniformly among rows whose id
  differs from `DCC_LAST_THEME_KEY` (`random_choose_weighted` or a simple
  filtered `random2`), store both keys. Called as the first line of
  `dcc_place_safe_room()` after its `ASSERT`, before the
  `_wants_safe_room() || _level_has_safe_room()` early return (D:1's room
  is the arrival vault, so the early return fires there; the D:1 rule is
  inside `dcc_assign_theme` anyway).
- `dcc_announce_theme()`: `dcc_announce("Tonight's floor: " + name + ". "
  + blurb)` and `take_note(Note(NOTE_MESSAGE, 0, 0, "Floor theme: " +
  name))`. Called from `dcc_enter_floor()` in the existing `just_created
  && is_connected_branch(here)` block. `dcc_floor_clock_summary()` appends
  " Theme: <name>." when the floor has one.
- l-dcc.cc: `dcc.theme_for([place])` → id or nil; `dcc.theme_table()` →
  array of the rows as tables (every column, colours as names via
  `colour_to_str`); `dcc.set_theme(place, id)` writes the hash entry
  (script-only; errors on an unknown id); `dcc.reset_themes()` clears both
  keys (script-only).
- Smoke (`phase6c()`): `dcc.reset_themes()`; D:1 regenerate →
  `theme_for("D:1") == nil`; D:2 then D:3 then D:4 regenerate → each id is
  in `theme_table()`, `theme_for("D:3") ~= theme_for("D:2")` and
  `theme_for("D:4") ~= theme_for("D:3")`; regenerate D:3 again → same id;
  `dcc.save_roundtrip()` → same; `mark()`; D:3 `dcc.enter_floor(true)` →
  `said("Tonight's floor")`; `mark()`; D:1 `enter_floor(true)` → not
  said. Off the connected branches (`Abyss:1` as the Phase 2a checks do):
  `theme_for` nil. `dcc.set_theme("D:5", "reruns")` then regenerate D:5 →
  `theme_for("D:5") == "reruns"` (the hook keeps a pre-set theme; 6d
  relies on this).

## Gate

Standard gate (`CLAUDE.md`).

## Verification

Start Crawl Live, descend to D:2: the Announcer names the floor's theme in
LIGHTCYAN; the time command (`@` or `Ctrl-X`'s status line, wherever the
floor clock summary prints) shows "Theme: ...". D:1 said nothing.

## Risks

- `dcc_place_safe_room()` runs inside the builder's RNG scope; the theme
  draw consumes RNG and shifts the rest of the build. Harmless, but
  3b's "repeat builds of the hardest floors" smoke must still pass; if a
  floor starts vetoing, the cause is elsewhere (the draw is one call).
- `you.props` hash tables: use `get_table()` on the key as other DCC
  code does for parked clocks (`DCC_FLOOR_CLOCKS_KEY` in dcc-clock.cc).

Status: not started
