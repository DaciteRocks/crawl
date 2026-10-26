# Phase 6h — Kits

Split from the former Phase 6d (floor-3 species re-pick and kits). Phase
6g holds the re-pick state in `source/dcc-repick.cc`; this phase adds the
kits that stand in for a class change; Phase 6i offers both to the player.
Read `docs/dcc/SPEC.md` Goal and Decisions, then this file. Gate and rules
are in the repo-root `CLAUDE.md`. `source/` means `crawl-ref/source/`.

Decisions restated:
- Kits stand in for class (jobs cannot change in DCSS). A kit is a short
  list of des item specs, dropped at the player's feet with trunk's
  `dgn_place_multiple_items()` (dungeon.h), never written into the pack
  (`newgame_make_item()` assumes an empty new-game inventory). One kit per
  game, and only as part of an accepted re-roll (6i); declining the
  re-roll gets no kit.
- Items are "good" but not acquirement-grade; boxes are the loot
  mechanic.

## Scope

Three kits exist as a table in `dcc-repick.cc`: `steel` (weapon, armour,
shield), `arcana` (a starter spellbook, a robe, a ring of magical power)
and `supplies` (healing, blinking, teleportation, haste). A script can
give one; the items land on the player's cell and around it, with an
Announcer line, a note and the one-shot recorded. Unknown ids and a
second kit are refused.

Produces: the kit table, `dcc_kit_table()`, `dcc_give_kit(id, force)` in
`source/dcc-repick.cc/.h`; dlua `dcc.kits()`, `dcc.give_kit(id[, force])`
(script-only), `dcc.kit()`; smoke checks.

Budget: ~4 files touched (`dcc-repick.h`, `dcc-repick.cc`, `l-dcc.cc`,
`scripts/dcc_smoke.lua`); read `mapdef.h` `class item_list` (~600:
`add_item`, `get_item`, `size`), `dungeon.h` `dgn_place_multiple_items`
and `dgn_place_item` (~255), `docs/develop/levels/syntax.txt` ITEM
section (search "ITEM:" and "good_item", "ego:", "q:"), `dcc-boxes.cc`
`_announce_jam` and the free-slot check (`dcc_free_item_slots`, so a
cluttered level jams the kit the way it jams a box).

## Details

- `dcc-repick.h`: `#define DCC_KIT_KEY "dcc_kit"` (string: the kit id;
  presence is the one-shot). `struct dcc_kit { const char *id; const char
  *name; const char *blurb; vector<const char *> items; }`.
  `const vector<dcc_kit> &dcc_kit_table()`; `bool dcc_give_kit(const
  string &id, bool force = false)`.
- Table (specs are des `ITEM:` syntax; tune freely, keep each kit to 3–5
  lines):
  - `steel` "Steel": "A sword, a shirt, and something to hide behind."
    `"long sword good_item"`, `"scale mail good_item"`, `"kite shield"`,
    `"potion of might q:2"`.
  - `arcana` "Arcana": "Spells, a robe, and the ring the budget allowed."
    `"book of flames"` (or any `book of` that exists: check
    `item_list::add_item`'s error string in the smoke), `"robe
    ego:resistance"`, `"ring of magical power"`, `"potion of magic q:3"`.
  - `supplies` "Supplies": "For the contestant who plans to run."
    `"potion of heal wounds q:3"`, `"scroll of blinking q:2"`, `"scroll of
    teleportation q:3"`, `"potion of haste q:2"`.
- `dcc_give_kit(id, force)`: `game_is_dcc()`; unknown id → false; already
  given and not `force` → false. Build an `item_list`, `add_item(spec)`
  for each line (a non-empty error string is a table bug:
  `mprf(MSGCH_ERROR)`, skip that line). If `dcc_free_item_slots()` is
  below the line count, announce a jam like the kiosk does and return
  false without consuming the one-shot. Else
  `dgn_place_multiple_items(list, you.pos())` (it puts every item on that
  one cell and does not link them; see As built); set `DCC_KIT_KEY`; `dcc_announce("Kit delivered:
  <name>. <blurb>")`; `take_note(Note(NOTE_MESSAGE, 0, 0, "Received the
  <name> kit"))`. Return true.
- l-dcc.cc: `dcc.kits()` → array of `{ id=, name=, blurb=, items= }`;
  `dcc.give_kit(id[, force])` → bool (script-only); `dcc.kit()` → id or
  nil.
- Smoke (`phase6h()`): on a generated floor with the player on a plain
  floor cell outside the safe room (the 4b section's `outside` cell
  helper or `dcc.quest_spot`): for each kit in `dcc.kits()`: count
  `dgn.items_at` within radius 1 before, `dcc.give_kit(id, true)` → true,
  `said("Kit delivered")`, count after ≥ before + #items − 1 (stackables
  may merge; potions of the same kind in one spec are one stack). Then
  `dcc.give_kit("steel")` without force → false (spent);
  `dcc.give_kit("nope", true)` → false; `dcc.kit()` is the last id;
  `dcc.save_roundtrip()` keeps it. Every spec line parses: `dcc.kits()`
  exposes a `valid` boolean per kit computed by `add_item` at table read,
  and the smoke asserts all true (this is the check that catches a bad
  item name).

## Gate

Standard gate (`CLAUDE.md`).

## Verification

Smoke only until 6i; in wizard mode there is no way to call
`dcc_give_kit` (script-only Lua).

## Risks

- `dgn_place_multiple_items` places at `where` and lets trunk's item
  placement spill; if everything stacks on one cell that is fine too.
- Item names in specs drift with trunk (books get renamed); the `valid`
  check in the smoke fails the gate when one does, which is the point.

## As built (deviations from the text above)

- `dgn_place_multiple_items()` does not spread items: it puts every item
  on the one cell it is given and leaves them unlinked, so
  `dcc_give_kit()` calls `link_items()` after it (as trunk's
  `dgn.create_item` does). The whole kit lands on one cell.
- Where it lands (`_kit_spot()`, review fix): the player's cell if it is
  plain floor, else the nearest plain floor cell in sight, else the
  nearest cell in sight where items stay reachable (not solid, not lava,
  not deep water the species cannot swim). Never an item-destroying
  cell. If nothing qualifies (a flyer over a lake of lava) it is a jam.
- Jams: a full item table or no cell to land on refuses the kit without
  spending the one-shot. The Announcer line is "The <Name> kit is stuck
  in the chute: ..." with the reason (too cluttered / nowhere here to put
  it down).
- Kit items arrive identified (`identify_item()`, review fix): the item
  slots free before `dgn_place_multiple_items()` and filled after it are
  the kit's. Their types become known, as with any identified item.
- Steel uses `long sword plus:2`, `scale mail plus:2`, `kite shield
  plus:1` instead of `good_item`: in the smoke, `good_item` rolled a
  randart scale mail with a Bane malus, which is neither "good" nor
  below acquirement grade.
- Extra helpers beyond the text: `dcc_find_kit(id)` (C++), and the
  script-only `dcc.reset_kit()`.
- The smoke also checks both jams (one-shot kept), every kit item's real
  name, and a kit given over lava landing on plain floor next to it.

Status: done 2026-10-25
