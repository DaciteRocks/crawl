# Phase 6g — Species re-pick core

Split from the former Phase 6d (floor-3 species re-pick and kits): this
phase holds the state and the species change, script-driven; Phase 6h adds
the kits; Phase 6i asks the player. Read `docs/dcc/SPEC.md` Goal and
Decisions, then this file. Gate and rules are in the repo-root
`CLAUDE.md`. `source/` means `crawl-ref/source/`.

Decisions restated:
- The re-pick is species-only (jobs cannot change in DCSS); kits stand in
  for class (6h). One re-roll per game, offered on first arrival at D:3:
  three random starting species other than the current one, applied with
  trunk's `change_species_to()` (species.h:103, the wizard tool that is
  also used for save conversion). Declining keeps everything.
- State lives in `you.props` (`dcc_` keys). No new `player` fields.

## Scope

Arriving on D:3 for the first time rolls three species choices and marks
the offer pending. A script (and in 6i, the player) can apply one of the
three: the species changes with trunk's fixups, the Announcer comments, a
note and a milestone record it, and the offer is spent. Declining spends
it too. A revisit, a reload or any other floor never re-offers.

Produces: `source/dcc-repick.h`, `source/dcc-repick.cc`; one call in
`dcc_enter_floor()` (`source/dcc-clock.cc`); dlua `dcc.repick_pending()`,
`dcc.repick_choices()`, `dcc.repick(name)`, `dcc.repick_decline()`,
`dcc.repicked()` (all script-only but `repicked` and `repick_pending`);
build registrations; smoke checks.

Budget: ~7 files touched (`dcc-repick.h`, `dcc-repick.cc`, `dcc-clock.cc`,
`l-dcc.cc`, `Makefile.obj` + `MSVC/crawl.vcxproj`, `scripts/dcc_smoke.lua`);
read `species.cc` 690–800 (`change_species_to`), `wiz-you.cc` 113–140
(`wizard_change_species`: the two follow-up steps), `species.h` 20–90
(`get_all_species`, `is_starting_species`, `name`, `from_str_loose`),
`dcc-clock.cc` `dcc_enter_floor()` (~308), `l-you.cc` `you_race` (what
the smoke reads back), `dcc-achieve.h` (key style, `dcc_check_achievements`).

## Details

- `dcc-repick.h`: `#define DCC_REPICK_OFFER_KEY "dcc_repick_offer"`
  (bool, pending), `#define DCC_REPICK_CHOICES_KEY "dcc_repick_choices"`
  (vector of species ints), `#define DCC_REPICKED_KEY "dcc_repicked"`
  (string: the new species' name, or "declined"; its presence is the
  one-shot). `void dcc_repick_arrived()`; `bool dcc_repick_pending()`;
  `vector<species_type> dcc_repick_choices()`; `bool dcc_repick_apply
  (species_type)`; `void dcc_repick_decline()`.
- `dcc_repick_arrived()`: called from `dcc_enter_floor()` in the
  `just_created && is_connected_branch(here)` block; no-op unless `here`
  is D:3 and neither `DCC_REPICKED_KEY` nor the offer exists. Pool:
  `species::get_all_species()` filtered by `species::is_starting_species
  (sp) && sp != you.species`; `shuffle_array`; keep three; store; set the
  offer.
- `dcc_repick_apply(sp)`: false unless pending and `sp` is one of the
  stored choices. Then: `change_species_to(sp)`; the follow-up
  `wizard_change_species` does (clear `env.invis_knowledge` if the player
  now sees invisible, else `sense_if_invisible()` on monsters that were in
  view); `you.props[DCC_REPICKED_KEY] = species::name(sp)`; erase the offer
  and choices; `dcc_announce("Re-rolled. Please welcome the new <Species>.
  Same contestant, different paperwork.")`; `take_note(Note(NOTE_MESSAGE,
  0, 0, "Re-rolled species to <Species>"))`; `mark_milestone("dcc.repick",
  "became a <Species>.")`; `dcc_check_achievements()`. Return true.
- `dcc_repick_decline()`: `DCC_REPICKED_KEY = "declined"`, erase the offer
  and choices, Announcer: "Keeping the original. Bold. The sponsors
  wanted a redesign."
- l-dcc.cc: `dcc.repick_pending()` → bool; `dcc.repicked()` → string or
  nil; `dcc.repick_choices()` → array of species names (script-only);
  `dcc.repick(name)` → bool (script-only; `species::from_str_loose`,
  error on `SP_UNKNOWN`); `dcc.repick_decline()` (script-only).
- Smoke (`phase6g()`): `debug.goto_place("D:3")`, regenerate,
  `dcc.enter_floor(true)` → `repick_pending()`; choices = three distinct
  names, none equal to `you.race()`; `dcc.repick("Felid")` with Felid not
  among them → false and still pending (if Felid is among them pick
  another name not in the list); `mark()`; `dcc.repick(choices[1])` →
  true, `you.race() == choices[1]`, `repicked() == choices[1]`, not
  pending, `said("Re-rolled")`, `you.hp()` max > 0; `dcc.repick(choices
  [2])` → false (spent); `dcc.enter_floor(true)` on D:3 again and on D:4
  → not pending; `dcc.save_roundtrip()` keeps `repicked()`. The decline
  path: cannot be tested in the same game after a re-pick, so test it
  first in a separate order: enter D:3, `dcc.repick_decline()` →
  `repicked() == "declined"`, then `dcc.reset_repick()` (script-only
  helper that erases the three keys) before the apply checks above.
  Leave the player as a species the later sections tolerate (the 4b box
  checks assume nothing about species; the kill checks need XL 26, which
  `change_species_to` keeps).

## Gate

Standard gate (`CLAUDE.md`).

## Verification

None visible yet beyond the smoke (6i adds the prompt). In wizard mode
after reaching D:3, `&` Lua `dcc.repick_pending()` is unavailable (script-
only), so the smoke is the check for this phase.

## Risks

- `change_species_to()` ends with `redraw_screen()`, `init_player_doll()`
  and `update_screen()`; headless, these are the same calls the save-
  conversion path makes, so they should be inert. If a tiles call asserts
  in script mode, guard the doll call inside `dcc_repick_apply` only by
  `crawl_state.script` — never edit species.cc.
- An Octopode or Felid re-pick makes worn items "fall away" (unequipped,
  kept in the pack): accepted, the Announcer line is the warning. 6i's
  prompt names the species so the player can decline.
- Mutations: `change_species_to` re-applies innate mutations for the new
  species up to the current XL; temporary and non-innate ones survive.

Status: not started
