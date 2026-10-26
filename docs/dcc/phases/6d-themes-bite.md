# Phase 6d — Themes bite: monsters, colours, clock, quest tier

Split from the former Phase 6b (floor themes). Phase 6c assigns a theme
per floor at build time (`source/dcc-theme.cc`, `dcc_theme_for(place)`, a
table row with `mon_list`, `floor_colour`, `rock_colour`, `clock_percent`,
`quest_tier_bump`, `extra_monsters`) and announces it; this phase makes
those columns do something. Read `docs/dcc/SPEC.md` Goal and Decisions,
then this file. Gate and rules are in the repo-root `CLAUDE.md`. `source/`
means `crawl-ref/source/`.

Decisions restated:
- Theme levers are existing exported trunk functions only: the level's
  random-monster list (`set_vault_mon_list()`, mon-place.h), the level
  colours (`env.floor_colour` / `env.rock_colour`, kept by trunk's
  `_dgn_set_floor_colours()` when already set), extra monsters placed on
  arrival (`mons_place()`), the clock tunable (`dcc_floor_turns_for()`)
  and the quest band tier (`dcc_quests.lua`). No layout hook.
- Clock values stay the tunables (1500 / 1200 turns); a theme scales them
  by its percent.

## Scope

A themed floor is populated from its theme's monster list, painted in its
colours, gets its extra monsters on first arrival, runs its clock at its
percent, and offers its quest at a bumped box tier. Concretely: `crowd`
adds 8 extra band-capable monsters on arrival; `headliners` draws the
floor's random monsters from four floors deeper and runs a 120% clock with
a +1 quest tier; `reruns` draws undead versions of the floor's monsters
with grey colours; `lightning` runs a 60% clock with a +1 quest tier in
yellow and red.

Produces: `dcc_apply_theme_build()` and `dcc_apply_theme_arrival()` in
`source/dcc-theme.cc`; the clock percent in `dcc_floor_turns_for()`
(`source/dcc-clock.cc`); the tier bump in `band_for()`
(`source/dat/dlua/dcc_quests.lua`); dlua `dcc.theme_mon_list([place])`,
`dcc.theme_quest_bump([place])`, `dcc.level_colours()`,
`dcc.monster_counts()`; smoke checks.

Budget: ~5 files touched (`dcc-theme.cc`, `dcc-theme.h`, `dcc-clock.cc`,
`dat/dlua/dcc_quests.lua`, `l-dcc.cc`, `scripts/dcc_smoke.lua`); read
`mon-place.h` (`set_vault_mon_list`, `mons_place`, `mgen_data` in
`mon-place.h`/`mgen-data.h`), `dungeon.cc` 4400–4445 (`_builder_monsters`:
the `mgen_data` a level population uses), `mapdef.h` `class mons_list`
(~697), `docs/develop/levels/syntax.txt` 845–885 (`place:` specs),
`dcc-clock.cc` 60–110 (`dcc_clock_rules`, `dcc_floor_turns_for`),
`dat/dlua/dcc_quests.lua` 60–115 (`DCC_QUEST_BANDS`, `band_for`).

## Details

- `mon_list` expansion: `{here}` → `place:<Branch>:<depth>` of the current
  level (`level_id::current().describe()`); `{deeper}` →
  `place:<Branch>:<min(depth + 4, brdepth[branch])>`. Zot:5 and D:12 thus
  clamp to themselves. The expanded string is parsed with `mons_list
  ml; ml.add_mons(expanded)` (a non-empty error string is a table bug:
  `mprf(MSGCH_ERROR)` and skip); the specs of slot 0 go to
  `set_vault_mon_list(specs)`. `_builder_monsters` resolves
  `RANDOM_MONSTER` through this list for every monster whose place is the
  current level (mon-place.cc ~453), so the floor's own population and
  later spawns follow the theme. A vault placed afterwards that sets its
  own list (`dungeon.cc` ~4875) wins; accepted.
- `dcc_apply_theme_build(const dcc_theme &)`: called from
  `dcc_assign_theme()` on every build (a vetoed level is rebuilt from a
  reset `env`, so this must run each time, not only on first assignment):
  the monster list as above (skipped when `mon_list` is empty) and, when
  not BLACK, `env.floor_colour` / `env.rock_colour`. Trunk's
  `_dgn_set_floor_colours()` runs after the build and keeps non-BLACK
  values, so these stick and are saved with the level.
- `dcc_apply_theme_arrival()`: called from `dcc_enter_floor()` next to the
  6c announce call (`just_created` only): for `extra_monsters` times,
  `mgen_data mg(RANDOM_MONSTER); mg.behaviour = BEH_SLEEP; mg.flags |=
  MG_PERMIT_BANDS; mg.proximity = PROX_AWAY_FROM_PLAYER; mg.map_mask |=
  MMT_NO_MONS;` (copy whatever else `_builder_monsters` sets, such as
  `preferred_grid_feature`) then `mons_place(mg)`. Hostiles are already
  barred from safe cells by `dcc_generation_barred_at()`.
- Clock: `dcc_floor_turns_for(place)` multiplies the rule's turns by
  `dcc_theme_for(place)->clock_percent / 100` when a theme exists. The
  status light and the Announcer thresholds follow the total already.
- Quest tier: `dcc.theme_quest_bump([place])` (l-dcc.cc, not script-only)
  returns the bump or 0. In `dcc_quests.lua` `band_for(depth)` returns a
  copy of the band with `tier` moved up by the bump along `{ "Bronze",
  "Silver", "Gold", "Platinum", "Legendary" }`, capped at Legendary (the
  box tier names are `dcc_box_tier_name()`'s). The quest text already
  names the tier, so the player sees it.
- l-dcc.cc: `dcc.theme_mon_list([place])` → the expanded spec string (""
  if none); `dcc.level_colours()` → floor, rock colour names
  (`colour_to_str(env.floor_colour)`); `dcc.monster_counts()` → `{ total=,
  undead=, outside_vaults=, undead_outside_vaults= }` over
  `monster_iterator` (undead: `mons->holiness() & MH_UNDEAD`; outside
  vaults: `!map_masked(pos, MMT_VAULT)`).
- Smoke (`phase6d()`): `dcc.reset_themes()`. `dcc.set_theme("D:4",
  "lightning")`, regenerate D:4, `dcc.start_floor_clock()` →
  `floor_turns_total() == 720` (1200 × 60%); `level_colours()` ==
  "yellow", "red"; `dcc_quest_debug().setup("reach")` →
  `dcc.quest_state().tier == "Silver"` (D:4 band is Bronze, +1; add
  `tier` to the state table if it is not there). `dcc.set_theme("D:5",
  "reruns")`, regenerate D:5 → `monster_counts()`:
  `undead_outside_vaults * 2 > outside_vaults` (the list is all undead;
  only vault monsters escape it); `theme_mon_list("D:5")` contains
  "place:D:5 zombie". `dcc.set_theme("D:6", "crowd")`, regenerate D:6,
  `before = monster_counts().total`, `dcc.enter_floor(true)` →
  `total >= before + 4` (placement can fail for a few).
  `dcc.set_theme("D:7", "headliners")`, regenerate D:7 →
  `theme_mon_list("D:7") == "place:D:11"`, `start_floor_clock()` →
  `floor_turns_total() == 1440`; on Zot:5 with headliners the list is
  "place:Zot:5". `dcc.set_theme("D:8", "crowd")`, regenerate →
  `floor_turns_total() == 1200` (no scaling).

## Gate

Standard gate (`CLAUDE.md`).

## Verification

Descend until the Announcer says "Reruns": the floor is grey and the
monsters are zombies and skeletons. On a "Lightning Round" floor the
status light starts at 720 and the steward offers a Silver quest on D:2–4.

## Risks

- `place:` specs with `zombie`/`skeleton`/`spectre` need a base monster
  that can be that derived type; trunk re-rolls until it finds one
  (`resolve_monster_type`, 300 tries) and may give up on floors with few
  candidates (D:2). If `reruns` yields live monsters on shallow floors,
  add `/ zombie w:5` (an unplaced random zombie) to the list.
- `band` in a random list: `mons_list` accepts it (`spec.band`), and
  `_get_vault_mon_list` carries it; if bands do not appear, drop the word
  and rely on `extra_monsters` for the crowd.
- Colour names: console only; tiles ignore `env.floor_colour`. The tile
  columns (`LFLOORTILE`-style `env.tile_default`) are out of scope; the
  Announcer line is the tiles player's cue.

## As built (deviations from the text above)

- `_concrete_specs()` expands derived specs. Trunk's
  `set_vault_mon_list()` keeps only the place of a `place:` spec and drops
  a zombie/draugr/spectre modifier (and `band`), so `place:D:5 zombie`
  would give live D:5 monsters. Each such spec becomes up to 12 concrete
  "<base> zombie" entries, bases rolled from the place with the tests
  `mons_list::get_zombie_type()` uses. Weights: every spec's weight is
  scaled by 12, and a derived spec's scaled weight is split evenly across
  its bases (at least 1 each), so Reruns keeps its 5:3:1 mix however many
  bases each kind finds.
- Reruns uses `draugr`, not `skeleton`: this trunk has no `skeleton`
  modifier (`get_zombie_type()` knows zombie, draugr, simulacrum,
  spectre), and "place:D:5 skeleton" silently meant plain D:5 monsters.
- Crowd Work's list is just `place:{here}`: the `band` token did nothing
  for a `place:` entry. Its bands come from trunk's normal
  `MG_PERMIT_BANDS` (the level builder and `mons_place()` both set it).
- A branch with no monster population (the Temple) gets no theme monster
  list and no arrival extras; `set_vault_mon_list()` would assert on it.
- A layout or encompass vault placed before the theme applies keeps its
  own monster list (`vault_mon_weights` already set) and any non-BLACK
  floor or rock colour; the theme fills in only trunk's defaults. A vault
  placed later still overrides the theme.
- Script-only dlua: `dcc.set_theme(place, nil)` clears a floor's theme;
  `dcc.apply_theme_build()` re-runs the build levers on the current level
  (the smoke uses it to check that a vault's list and colours survive).
- Earlier smoke checks made theme-proof: the clock checks reset the themes
  the opening builds gave D:6 and D:12; Phase 6a's boss arrival clears the
  floor's theme (Crowd Work extras could share the boss's kind); the
  quick_exit 40% check takes 40% of the floor's real total.

Status: done 2026-10-25
