# Phase 6i — Live re-pick offer

Split from the former Phase 6d (floor-3 species re-pick and kits). Phase
6g holds the re-pick state and `dcc_repick_apply()` /
`dcc_repick_decline()`; Phase 6h holds the kits and `dcc_give_kit()`; this
phase asks the player. Read `docs/dcc/SPEC.md` Goal and Decisions, then
this file. Gate and rules are in the repo-root `CLAUDE.md`. `source/` means
`crawl-ref/source/`.

Decisions restated:
- Prompts that need the player's attention are asked from the main loop,
  before the next command, never from inside a view update or a delay.
  Phase 5b set the pattern: a guarded one-line hook in `main.cc`
  `_input()` right after `ASSERT(!you.turn_is_over);` calls
  `dcc_quest_before_command()`, which refuses while keys are queued or
  the player is running or delayed.
- The re-roll is one bundle: accept → choose a species, then a kit;
  decline → nothing, and the offer is spent. Escape puts it off.
- A trunk file keeps one guarded line per hook; changing what that line
  calls is still one line. Record the change in `CLAUDE.md`.

## Scope

The first command after the first arrival on D:3 (while the player stands
in the safe room, with control) brings the Announcer's pitch and one
prompt: "(a) X, (b) Y, (c) Z, (n) keep what you have, Esc: later". A letter
changes the species (6g) and asks a second prompt for the kit
("(1) Steel, (2) Arcana, (3) Supplies"); `n` declines everything; Escape
puts the offer off until the player has left the safe room and come back
onto a safe cell. The quest steward's offer still works (it comes after).

Produces: `dcc_before_command()` in `source/dcc.cc/.h` (calls
`dcc_repick_before_command()` then `dcc_quest_before_command()`); the
`main.cc` hook line now calls it; `dcc_repick_before_command()` and the
prompts in `source/dcc-repick.cc/.h`; a smoke check that scripts are never
asked; `CLAUDE.md` updated.

Budget: ~8 files touched (`dcc.h`, `dcc.cc`, `main.cc`, `CLAUDE.md`,
`dcc-repick.h`, `dcc-repick.cc`, `l-dcc.cc`, `scripts/dcc_smoke.lua`); read
`dcc-quest.cc` from `dcc_ask_quest_offer` to `dcc_quest_before_command`
(~lines 330–420: the guards, the `yesno` keymap trick, `has_pending_input`),
`main.cc` ~1228–1235 (the hook), `CLAUDE.md` isolation list (the 5b
bullet naming `dcc_quest_before_command`), `prompt.h` (`yesno`), `macro.h`
(`getchm`), `dcc-repick.h` (6g/6h API).

## Details

- `dcc.cc`: `void dcc_before_command()` — `game_is_dcc()` check, then
  `dcc_repick_before_command(); dcc_quest_before_command();`. `main.cc`'s
  guarded line changes from `dcc_quest_before_command()` to
  `dcc_before_command()` (include `dcc.h` instead of `dcc-quest.h` if that
  is what the include line was). `CLAUDE.md`'s 5b bullet names the new
  function.
- `dcc_repick_before_command()`: return unless `dcc_repick_pending()`;
  return in script/test mode; return while `has_pending_input()`,
  `crawl_state.is_repeating_cmd()`, `crawl_state.is_replaying_keys()`,
  `you.running` or `you_are_delayed()` (the same list as the quest offer);
  return unless `is_dcc_safe_cell(you.pos())` (asked in the safe room
  only); a static `_put_off` flag, cleared whenever this runs with the
  player off a safe cell, returns while set.
  Pitch: `dcc_announce("One-time offer from the studio: a re-roll. Three
  species on the table, and a kit to go with the new look. Decline and
  you keep everything you walked in with.")`. Prompt with
  `mprf(MSGCH_PROMPT, "Re-roll as (a) %s, (b) %s, (c) %s, or (n) keep
  what you have? (Esc: later)")` and a `getchm()` loop: `a`–`c` →
  `dcc_repick_apply(choice)`, then the kit prompt `"Kit: (1) Steel, (2)
  Arcana, (3) Supplies"` with its own `getchm()` loop (no escape from the
  kit: a re-rolled contestant gets a kit; Escape there picks nothing and
  asks again next command until chosen — keep a `DCC_KIT_PENDING` flag in
  `you.props` so a reload mid-choice still gets the kit); `n` →
  `dcc_repick_decline()`; Escape, `Ctrl-G` or any other key → `_put_off =
  true`, `canned_msg(MSG_OK)`. `clear_messages()` and `redraw_screen()` as
  the quest offer does after answering.
- Ordering with the steward: on D:3 arrival both offers can fall on the
  same command; the re-pick asks first, the quest second. Two prompts in
  a row is accepted.
- l-dcc.cc: `dcc.before_command()` (script-only) runs
  `dcc_before_command()` so the smoke can show it is inert headless.
- Smoke (`phase6i()`): with a fresh D:3 arrival pending (reuse 6g's
  setup after `dcc.reset_repick()`): `dcc.before_command()` →
  `repick_pending()` still true, no messages since `mark()`, no kit
  (`dcc.kit()` unchanged). Kit-pending state: `dcc.repick(choices[1])`
  then `dcc.kit_pending()` (expose it) is true until `dcc.give_kit(id)`
  (6h) clears it; `dcc.save_roundtrip()` keeps it pending.

## Gate

Standard gate (`CLAUDE.md`).

## Verification

Start Crawl Live, reach D:3 by stairs. On the first keypress the pitch
prints and the prompt names three species. Press Esc: nothing changes;
step out of the room and back in: it asks again. Press `b`: the species
changes (title line), the kit prompt follows, `2` drops the Arcana kit at
your feet, then the steward's quest offer appears as usual. In a second
game press `n`: nothing changes, and it never asks again.

## Note from Phase 6h (orchestrator, 2026-10-25)

Offer only the kits that suit the picked species: a Felid or an Octopode
should not be offered the Steel kit (it can wear none of it). How is 6i's
call: the item generator's useful/can-wear checks (`is_useless_item`,
`can_wear_armour`) on the kit's items, or a simple per-kit species filter
in the kit table. If every kit is filtered out, decide what the player
gets (probably the Supplies kit, which suits everyone).

## Risks

- `getchm()` inside `_input()` before the command is read: the quest
  offer's `yesno()` runs at the same point, so the terminal state is
  known good. Do not call `interrupt_activity` from inside the prompt.
- A reload between the species change and the kit choice: the
  `DCC_KIT_PENDING` flag covers it (the species is already saved by
  `change_species_to`).
- Local tiles: the species change redraws the doll inside
  `change_species_to`; `tiles.layout_statcol()` as `wizard_change_species`'s
  caller does may be needed for the stat column. Verify by eye.

## Design change at review (orchestrator, 2026-10-26)

The text above has the owed kit asked before every command, anywhere,
with Escape deferring it one keystroke. Both reviewers rated that the
worst UX problem: mid-fight the prompt came back on every command,
`clear_messages()` wiped the combat text each time, and a jammed delivery
re-announced (with a forced interrupt) on every command. The rule now:
the kit, like the re-roll, is asked only while the player stands on a
safe cell. Escape (or a jam) puts it off with the re-roll's mechanism,
and it is asked again once the player has stepped off the safe cells and
come back onto one, which includes arriving in the next floor's safe
room. The prompt reads "(Esc: ask next time you're in a safe room)". The
kit stays owed across a save and reload (`dcc_kit_pending`).

## As built (deviations from the text above)

- Keys are read with `ui::getch(KMC_CONFIRM)`, as `yesno()` (and so the
  quest offer) does, not `getchm()`: the same keymap context and redraw
  handling as the prompt the quest offer already uses at this point.
- Only Escape (or Ctrl-G), Enter, space or a hangup puts a question off
  (review fix). Any other unrecognised key asks again; `CK_REDRAW` (a
  resized terminal) redraws the screen and asks again. The same rule
  holds for the kit prompt.
- Kits that suit the new body (the 6h note): `dcc_kit_suits()` requires
  every weapon, staff, armour and jewellery item of a kit to be usable
  (`is_useless_item()` on the spec's base and sub type); potions,
  scrolls and books do not count. A Felid or an Octopode is offered
  Supplies only (Steel's scale mail and Arcana's robe are body armour); a
  Mummy may still take Steel. If no kit suits, Supplies is offered. Keys
  keep the table numbers, so `2` is always Arcana.
- The put-off flag is a static, cleared per turn when the player is off
  the safe cells (`dcc_repick_turn()`, from `dcc_tick_clock()`, so travel
  and rests count), by a fresh roll, and (review fix) on every load but a
  visit (`dcc_repick_level_loaded()` from `dcc_start_stair()`, which
  `load_level()` already calls): a new or restored game, or a level
  change, never inherits another game's Escape. No new trunk hook.
- `DCC_KIT_PENDING_KEY` (`dcc_kit_pending`) is set by
  `dcc_repick_apply()` (not by the prompt), so the script re-pick owes a
  kit too; `dcc_give_kit()` erases it; `dcc.reset_repick()` erases it.
- A god refusal in `dcc_repick_apply()` (6g) brings the species prompt
  back; the Announcer has said why.
- Kit equipment specs gained `no_uniq` (Phase 6h's smoke failed 1 run in
  5 when the ring rolled as a randart).
- Script hooks beyond the text: `dcc.kit_offer()` (ids on offer now),
  `dcc.repick_put_off([set])` (script-only), plus `dcc.kit_pending()` and
  `dcc.before_command()` from the text.

## Manual checks (no headless way to answer a prompt)

- D:3 by stairs: the first command brings the pitch and the prompt
  naming three species. A typo asks again; Esc changes nothing; step out
  of the room and back in: it asks again.
- Resize the window at the prompt: it redraws and asks again.
- Press a letter: the title line and the tiles stat column and doll show
  the new species, the kit prompt follows (a Felid or an Octopode sees
  Supplies only), the kit lands at your feet, then the steward's quest
  offer.
- Esc at the kit prompt: nothing until you leave the room's cells and
  come back (or reach the next floor's room). Save and reload before
  choosing: the kit is still asked in the room.
- A second game, `n`: nothing changes, no kit, never asked again.
- With a macro, a repeated command or travel running, no prompt appears
  until the player has control.

Status: done 2026-10-26
