# Phase 6e — Sponsor booth in the D:3 safe room

Split from the former Phase 6c (sponsors): this phase builds the booth;
Phase 6f makes signing with a sponsor pay. Read `docs/dcc/SPEC.md` Goal
and Decisions, then this file. Gate and rules are in the repo-root
`CLAUDE.md`. `source/` means `crawl-ref/source/`.

Decisions restated:
- Sponsors are gods. The booth is three altars of three distinct temple
  gods inside the D:3 safe room, chosen at build time from trunk's
  `temple_god_list()` minus the player's current god and gods they cannot
  join. Gods keep their DCSS names and rules; only the Announcer calls them
  sponsors.
- The safe room is dug into wall by `dcc-room.cc` from the map lines as
  written (never rotated or mirrored, one door, outer ring all wall, every
  variant's header block the same); every cell inside is `dcc_safe`.
- `dcc_only` keeps DCC maps out of every other game type (`maps.cc`,
  `dungeon.cc`); do not touch those guards.

## Scope

On D:3 the safe room is one of four larger variants that also hold three
altar cells, each an altar of a different god; every other floor keeps the
existing variants. D:1's arrival room never uses a sponsor variant. The
altars are ordinary altars (praying joins the god as always). Nothing is
announced or rewarded yet (6f).

Produces: four maps `dcc_safe_room_sponsor_[a-d]` in
`source/dat/des/dcc/safe_rooms.des` (tag `dcc_sponsor_room`, not
`dcc_safe_room`); per-floor tag selection and the altar fill in
`source/dcc-room.cc`; dlua `dcc.sponsor_altars()`; smoke checks.

Budget: ~5 files touched (`safe_rooms.des`, `dcc-room.cc`, `dcc-room.h`,
`l-dcc.cc`, `scripts/dcc_smoke.lua`); read `safe_rooms.des` (whole, 138
lines), `dcc-room.cc` 60–120 (`_wants_safe_room`, `_level_has_safe_room`,
`dcc_arrival_map`), 140–230 (`_read_room_shape`, `_room_shapes`), 455–500
(`dcc_place_safe_room`), `dcc-boxes.cc` 178–184 (`dcc_at_kiosk`: reading a
marker prop), `dat/dlua/lm_props.lua` (`props_marker`), `religion.h`
(`temple_god_list`, `player_can_join_god`), `terrain.h` (`altar_for_god`),
the Phase 3b smoke section (`scripts/dcc_smoke.lua` ~429–600:
`check_safe_room_floor`, `safe_rooms_placed`).

## Details

- Maps: 11×7 (interior 9×5), door on a different side each (south, north,
  east, west, like a–d), same header block as the existing variants except
  `TAGS: dcc_sponsor_room dcc_only ...` in place of `dcc_safe_room`, and
  `KFEAT: A = floor`, `A` added to the `KPROP ... = dcc_safe` glyph list,
  `MARKER: A = lua:props_marker { dcc_sponsor = "1" }`. Three `A` cells,
  not adjacent to the stairs or the door, in a row along the wall opposite
  the door ("the booth"). Keep `{ ( [ S K 1` and the epilogue exactly as
  the other variants have them (the quest and steward come for free).
  Names start with `dcc_safe_room` so the 3b smoke's name checks hold.
- `dcc-room.cc`: `static string _room_tag()` → `"dcc_sponsor_room"` on
  D:3 (`level_id::current() == level_id(BRANCH_DUNGEON, 3)`), else
  `"dcc_safe_room"`. `_room_shapes(tag)` and its cache take the tag
  (cache per tag, or key the cache on tag + names); `_level_has_safe_room`
  accepts either tag; the fallback `random_map_for_tag(SAFE_ROOM_TAG,
  ...)` in `dcc_place_safe_room` uses `_room_tag()`. `dcc_arrival_map`
  stays on `dcc_safe_room` (D:1 is never D:3, but keep it explicit).
- Fill, `static void _fill_sponsor_booth()`, called at the end of every
  successful path of `dcc_place_safe_room()`: collect cells where
  `env.markers.property_at(c, MAT_ANY, "dcc_sponsor") == "1"`; if that
  finds nothing on D:3 (markers not answering during the build), read the
  placed vault's map lines instead (`vault_placement::map.map.lines()`
  offset by `vp->pos`, glyph `A`) and note which worked in the discovery
  log. Pool: `temple_god_list()`, drop `you.religion` and any god with
  `!player_can_join_god(g, false)`, `shuffle_array`, take three (fewer if
  the pool is short; a cell left over stays floor). `env.grid(c) =
  altar_for_god(g)`.
- `dcc-room.h`: nothing new is needed by trunk; expose `vector<pair
  <coord_def, god_type>> dcc_sponsor_altars()` for l-dcc.cc (cells with
  the prop whose feature is an altar: `feat_is_altar`, `feat_altar_god`).
- l-dcc.cc: `dcc.sponsor_altars()` → array of `{ x=, y=, god= }` (god via
  `god_name`).
- Smoke (`phase6e()`): regenerate D:3 three times; each time:
  `#sponsor_altars() == 3`, the three gods distinct, each cell
  `dcc.is_safe_cell`, the placed room's name (`debug.vault_names()` as the
  3b checks read it) starts with `dcc_safe_room_sponsor`. D:2 and D:4:
  `#sponsor_altars() == 0` and the room name has no "sponsor". D:1 via
  `debug.goto_place("D:1")` + regenerate: no "sponsor". `dcc.set_god
  ("Xom")` (exists, see its `LUAFN`), regenerate D:3 → Xom is not among
  the three; restore the previous god. The existing 3b loop over D:1–D:12
  still passes (door count, up stairs, shop, kiosk) with the new variants.

## Gate

Standard gate (`CLAUDE.md`).

## Verification

Start Crawl Live, wizard-skip to D:3: the arrival room is wider, with
three altars along one wall; `x` on each shows a different god. D:2 and
D:4 rooms are the old ones.

## Risks

- The larger room may fail to dig in on cramped D:3 layouts; the passes
  in `dcc_place_safe_room` then try liquid, open floor and the
  corridor-connected fallback. If the repeat-build smoke shows vetoes,
  shrink the maps to 9×7 before changing C++.
- `_read_room_shape` runs each map's Lua; the sponsor maps have no extra
  Lua, so the cache logic is unchanged apart from the tag.
- `player_can_join_god(g, false)` may reject every god for an undead
  species or a Demigod (Demigods cannot worship): then the booth is three
  floor cells and 6f's Announcer line says the sponsors passed. Smoke runs
  as whatever species the script's player is; if that is a Demigod, pick
  another with `you.set_species`-style helpers or skip the god checks
  with a message.

Status: not started
