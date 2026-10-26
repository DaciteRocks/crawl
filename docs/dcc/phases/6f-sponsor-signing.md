# Phase 6f — Sponsor signing: join hook and Gold box

Split from the former Phase 6c (sponsors). Phase 6e built the booth: three
altars on `props_marker { dcc_sponsor = "1" }` cells in the D:3 safe room,
`dcc_sponsor_altars()` in `source/dcc-room.cc`. This phase makes signing
there pay. Read `docs/dcc/SPEC.md` Goal and Decisions, then this file.
Gate and rules are in the repo-root `CLAUDE.md`. `source/` means
`crawl-ref/source/`.

Decisions restated:
- Joining a god from a booth altar awards one Gold box, once per game,
  plus the existing `signed` achievement (Bronze, any god anywhere). Gods
  keep their DCSS names and rules; the Announcer alone calls them sponsors.
- A trunk file may gain one guarded line plus its `#include`; the callee
  re-checks `game_is_dcc()`. Record every new hook in `CLAUDE.md`.
- Boxes are `you.props["dcc_boxes"]` tier ints, awarded with
  `dcc_award_box(tier)` and opened at a kiosk.

## Scope

On first arrival at D:3 the Announcer names the three sponsors at the
booth. Joining a god while standing on a booth altar makes the Announcer
announce the deal and awards a Gold box, with a note and a milestone; the
`signed` achievement unlocks at once rather than on the next turn. Joining
anywhere else, or a second god later, gets no box.

Produces: `source/dcc-sponsor.h`, `source/dcc-sponsor.cc`; a guarded hook at
the end of `join_religion()` (`source/religion.cc`); one call in
`dcc_enter_floor()` (`source/dcc-clock.cc`); dlua `dcc.join_god(name)`
(script-only) and `dcc.sponsor()`; build registrations; smoke checks; a
`CLAUDE.md` line recording the hook.

Budget: ~9 files touched, three of them one-liners (`dcc-sponsor.h`,
`dcc-sponsor.cc`, `religion.cc`, `dcc-clock.cc`, `l-dcc.cc`, `Makefile.obj`
+ `MSVC/crawl.vcxproj`, `scripts/dcc_smoke.lua`, `CLAUDE.md`); read
`religion.cc` 3900–3995 (`join_religion`), `dcc-boxes.cc` 178–184
(`dcc_at_kiosk`), `dcc-achieve.h` (`dcc_award_box`, `dcc_check_achievements`,
`dcc_unlock_batch`), `dcc-room.h` (`dcc_sponsor_altars`), `l-dcc.cc`
`dcc_set_god_lua` (~907: how the smoke sets a god today).

## Details

- `dcc-sponsor.h`: `#define DCC_SPONSOR_PROP "dcc_sponsor"` (the marker
  prop), `#define DCC_SPONSOR_GOD_KEY "dcc_sponsor_god"` (you.props: the
  god signed at the booth, int; its presence is the one-shot). `bool
  dcc_at_sponsor_booth()` (`game_is_dcc()`, `is_dcc_safe_cell(you.pos())`,
  `env.markers.property_at(you.pos(), MAT_ANY, DCC_SPONSOR_PROP) == "1"`,
  `feat_is_altar(env.grid(you.pos()))`); `void dcc_god_joined(god_type)`;
  `void dcc_sponsor_intro()`; `god_type dcc_sponsor_god()`.
- Hook: last line of `join_religion()` (religion.cc), after
  `learned_something_new(HINT_CONVERT);`: `if (crawl_state.game_is_dcc())
  dcc_god_joined(which_god);` with `#include "dcc-sponsor.h"`.
- `dcc_god_joined(god)`: open a `dcc_unlock_batch`; if
  `dcc_at_sponsor_booth()` and no `DCC_SPONSOR_GOD_KEY`: set it,
  `dcc_announce("<God> has signed you. Welcome to the family; legal says
  the contract is binding. A Gold box for the signature.")`,
  `dcc_award_box(DCC_BOX_GOLD)`, `take_note(Note(NOTE_MESSAGE, 0, 0,
  "Signed with sponsor <God>"))`, `mark_milestone("dcc.sponsor", "signed
  with " + god_name(god) + ".")`. In every case `dcc_check_achievements()`
  (unlocks `signed` now).
- `dcc_sponsor_intro()`: called from `dcc_enter_floor()` in the
  `just_created && is_connected_branch(here)` block when `here` is D:3;
  reads `dcc_sponsor_altars()`; with three: `dcc_announce("Three sponsors
  have booths on this floor: A, B and C. Sign with one at its altar and
  the studio throws in a Gold box.")`; with none: "The sponsors looked at
  your file and passed." (Phase 6e's Demigod case).
- l-dcc.cc: `dcc.join_god(name)` (script-only): `str_to_god`, then
  `join_religion(god)`; `dcc.sponsor()` → god name or nil; `dcc.at_booth()`
  → bool.
- Smoke (`phase6f()`): regenerate D:3, `dcc.init_markers()` as the 4b
  checks do for a regenerated level; `mark()`; `dcc.enter_floor(true)` →
  `said("Three sponsors")`; `dcc.set_god("No God")` (or however the 4b
  checks clear it); altars = `dcc.sponsor_altars()`; `you.moveto(altars
  [1].x, altars[1].y)`; `dcc.at_booth()`; boxes = `dcc.box_count()`;
  `mark()`; `dcc.join_god(altars[1].god)` → `you.god() == altars[1].god`,
  `dcc.box_count() == boxes + 1`, last of `dcc.boxes()` == "Gold",
  `said("signed you")`, `dcc.sponsor() == altars[1].god`,
  `dcc.has_achievement("signed")` (reset achievements first with
  `dcc.reset_achievements()` so the check is real). Then move to altars[2]
  and `dcc.join_god(altars[2].god)` → god changes, box count unchanged
  (one-shot). Off the booth: `dcc.set_god("No God")`, stand on a floor
  cell outside the room, `dcc.join_god("Xom")` → no box. `dcc.save_
  roundtrip()` keeps `dcc.sponsor()`. Finally restore the god the later
  sections expect (check what 4b's TSO checks assume).
- `CLAUDE.md`: record the `join_religion` hook under a "Phase 6f" bullet.

## Gate

Standard gate (`CLAUDE.md`).

## Verification

Reach D:3 with no god, read the Announcer's sponsor line, pray at one of
the booth altars: the god welcomes you, the Announcer announces the deal,
`?:` shows the Gold box, and the next kiosk opens it.

## Risks

- `join_religion()` headless: it calls `redraw_screen()`, god join
  effects and, for Vehumet, a gift; the smoke already runs delays and
  level loads headless, so this should pass. If it asserts or prompts
  (Gozag's fee, Ru's sacrifice), pick a quiet god for the smoke (Okawaru,
  Vehumet) and pass the booth altar's god only when it is one; otherwise
  move the player to the altar of a quiet god placed with `dgn.grid` on a
  booth cell (the prop is on the marker, not the feature).
- `str_to_god` wants the exact name; use the names `dcc.sponsor_altars()`
  returns.

## As built (deviations from the text above)

- A signing needs the altar under the player to be the joined god's own
  (`feat_altar_god(env.grid(you.pos())) == god`, review fix): an orc
  converting to Beogh while standing on another god's booth altar signs
  nothing. The Announcer's line starts with `uppercase_first(god_name())`
  ("The Shining One has signed you").
- Box count: unlocking `signed` awards its own Bronze box right after the
  Gold one, so a booth signing from a fresh achievement set adds two
  boxes (one Gold, one Bronze); the smoke checks one more Gold box and two
  new boxes in all, rather than "last box is Gold".
- The smoke clears the god with `dcc.set_god("")` before each later
  `dcc.join_god()` (second booth god, off-booth Xom, the wrong-god check),
  so no real excommunication runs: penance and wrath would leak into
  later smoke sections. The second booth join still shows the one-shot.
- `dcc.reset_achievements()` also erases `dcc_sponsor_god` (a fresh
  game's state), so the off-booth check starts with `signed` locked.
- The intro adapts to the altar count ("Three sponsors have booths...",
  "A sponsor has a booth...", or "passed" with none). The D:3 check sits
  inside `dcc_sponsor_intro()`, so `dcc_enter_floor()` calls it
  unconditionally in its first-arrival block.
- The "no intro off D:3" check looks for the intro's own phrases, not the
  word "sponsor": the lightning theme's blurb says "The sponsors insisted."
- Extra smoke checks: joining a non-booth god on a booth altar pays
  nothing; an emptied booth gets the "passed" line; D:4 gets no intro; a
  booth signing survives a save round trip.
- `dcc-room.cc` uses the header's `DCC_SPONSOR_PROP` instead of its own
  copy of the prop name.

Status: done 2026-10-25
