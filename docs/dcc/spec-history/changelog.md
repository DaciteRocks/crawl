# DCC spec change log

## 2026-10-25 — Phase 0 done

Landed: the repo-root `CLAUDE.md` holds the gate, the `nul` rule, the isolation
and no-trademark rules, and the optional Debug Console route. `nul` is now in
`crawl-ref/source/.gitignore`. Findings are in `discovery.md`. The Standard
gate worked as written: Release-build `-builddb` and `-script` both run
headless. The review found no bugs. Reviewers asked for three doc cleanups,
all applied:
- `CLAUDE.md` now states the gate path once (`SRC`/`MSBUILD`), and SPEC.md
  names `CLAUDE.md` as the authoritative copy of the gate.
- The `script error:` text now cites its source in `ctest.cc` and is marked
  as observed behaviour.
- `discovery.md` now records why VS2022 is not used.

## 2026-10-25 — Phase 1a done

Landed: `GAME_TYPE_DCC` ("Crawl Live") is appended before `NUM_GAME_TYPE`.
`game_is_descent()` also returns true for it, and `game_is_dcc()` exists. It
shows in the startup menu in every build, saves to `dcc/`, can be picked with
rc `type = dcc` or `-dcc`, and uses the Descent tile. Reviewers changed:
- added a `welcome_spam crawl live` entry to `miscname.txt` (the start-of-game
  line was blank);
- merged the DCC tile case into the Descent case;
- added `-dcc` to the `-help` text (`main.cc`) and `dcc` to the `type` entry in
  `options_guide.txt`;
- added a "Merging upstream" section to `CLAUDE.md` about game-type numbering in
  saves.
Kept on purpose: DCC is not alpha-gated, the tourney prompt at `newgame.cc:430`
is untouched, and `dcc` is listed in `-gametypes-json`.

## 2026-10-25 — Phase 1b done

Landed: `source/dcc.h`/`dcc.cc` (`dcc_setup_new_game()`, called from
`_setup_generic` under `game_is_dcc()`, sets `you.props["dcc_version"] = 1`)
and `source/l-dcc.cc` (dlua table `dcc` with `is_active()` and
`enable_for_script()`), registered in `dlua.cc`/`l-libs.h` and listed in
`Makefile.obj`, `crawl.vcxproj` and its `.filters`. `scripts/dcc_smoke.lua` is
now the gate's fourth command: it checks the mode, a 12-floor Dungeon, and
generates D:1, D:6 (each with a down stair) and D:12. A broken check exits 1.
Discovery: `initialise_branch_depths()` already sets the Descent depth and both
Descent branch keys for every Descent-type game, so `enable_for_script()` just
sets the type and calls it; there was nothing extra to factor out.
Reviewers changed: `dcc.enable_for_script()` now raises a Lua error unless
`crawl_state.script` or `crawl_state.test` is set (`-script` sets both).

## 2026-10-25 — Phase 2a done

Landed: `source/dcc-clock.h`/`dcc-clock.cc`, a per-floor clock (1500 turns on
D:1-3, 1200 elsewhere) in `you.props`, started from `load_level()` and ticked
from `player_reacts()`, with dlua helpers and smoke checks. At zero the floor
collapses: a forced one-floor descent with 20% HP damage, or at a branch
bottom the Zot-touch max-HP penalty and a 100-turn retry clock.
User decision (Resolved decision 8): the clock runs only on connected-branch
floors and pauses in portals, the Abyss, Pan and Zigs.
Reviewers changed:
- the hook passes `just_created_level`; a newly created floor always gets a
  fresh clock (Pan, portals, Zigs and repeat Abyss trips used to reuse old
  clocks under one `describe()` key). The park table remains for real
  revisits on connected floors only: it never stores a spent clock and keeps
  at most 16 entries, so a collapsed floor cannot collapse again on return;
- the collapse uses `floor_transition()` directly instead of the shaft-trap
  path (no trap destroyed, no shaft messages, no shaft milestone), and HP
  loss, `dcc_collapsed_count` and the note apply only after the level has
  really changed, so a failed fall is penalised once;
- the Zot-touch penalty is now `zot_touch_max_hp()` in `zot.cc`, shared by
  both (a pure move, recorded in the CLAUDE.md isolation rule);
- `dcc.tick()` returns the collapse result, and the smoke checks a collapse,
  the D:12 no-floor penalty, and the pause in the Abyss and a portal;
- CLAUDE.md now allows the one `#include "dcc*.h"` a trunk hook needs.

## 2026-10-25 — Phase 2b done

Landed: the "Floor (N)" status light (`STATUS_DCC_FLOOR`, coloured by the
share of the clock left, DARKGREY "paused" off the connected branches), the
Announcer channel `MSGCH_DCC` (rc name `dcc`, LIGHTCYAN) with `dcc_announce()`
warnings at 50%, 25%, 10%, 50, 20 and 10 turns left and on collapse, the Zot
clock off in DCC, and dlua `dcc.floor_turns_total()` /
`dcc.floor_clock_paused()`.
Reviewers changed:
- the clock's full length is stored in `dcc_floor_aut_total` wherever the
  clock is set (start, resume, retry; parked entries keep it too) instead of
  being re-parsed from the place each turn; older saves derive it;
- a `dcc_floor_retry` flag marks the branch-bottom retry clock: no drop
  warnings on it, the light is RED (LIGHTMAGENTA for the last 10 turns), and
  the no-floor Announcer line is said once, not every 100 turns. A parked
  retry clock stays one on return;
- the collapse line is announced only after the fall really happened;
- the Zot clock is off through a guarded early return in `zot_immune()`
  (replacing the inline condition in `_zot_clock_active_in`), which also
  hides the overview's Zot columns and the Ziggurat "hunt you nevermore"
  line; the time/status command shows the floor clock instead of Zot;
- the running clock now sets `short_text`, so it appears in the character
  screen and dump status lists;
- smoke gained negative checks (a quiet tick says nothing; retries do not
  re-announce or warn);
- CLAUDE.md's isolation rule now allows data-table entries that parallel an
  extended enum and records the `zot_immune()` exception.

## 2026-10-25 — Phase 3a done

Landed: `FPROP_DCC_SAFE` (des KPROP `dcc_safe`) in `dcc-safe.cc`: monsters
that would attack the player cannot walk onto, be placed on, or be
translocated onto a safe cell; dlua `dcc.is_safe_cell`,
`dcc.monster_can_step`, `dcc.monster_can_land`; test vault `dcc_test_safe`.
User decision (Resolved 9): no combat in a safe room. `could_harm()` refuses
harm by or to anything on a safe cell; monsters drop foes across the edge
(`_is_valid_foe()`); the player's melee, fire/throw, harmful spells and
evocations are refused with an Announcer line.
Reviewers changed:
- monsters no longer melee the player across the edge via the raw step
  (`dcc_adjust_monster_step` cancels such steps; attacks on allies too);
- a hostile on a safe cell may only head for the way out (cells of the same
  room strictly closer to the exit, or off the safe cells), walks out on its
  own, and can never land on another safe cell or room;
- flopping aquatic monsters respect safe cells; confused moves were already
  checked through `mon_can_move_to_pos`;
- "dragged along with you" / "arrives with you" now re-pick a spot outside
  instead of printing over a refused move;
- arriving followers queue outside the door (`find_home_near_place()`
  searches through safe cells);
- `is_dcc_safe_cell` moved into `dcc-safe.cc`; the lurker hook was removed
  (`find_newmons_square` already skips safe cells for lurkers);
- the BEH_CHARMED finding was rejected: `place_monster()` gives BEH_CHARMED
  no charm, so generation and movement already agree (comment added);
- smoke: adjacent hostile at the door over AI turns (no HP loss), player
  refusals, harm checks, drag spots, a turned hostile walking out. The old
  AI checks read player-relative monster coordinates and gave no energy, so
  they passed vacuously; fixed, and `dcc_test_safe` is pinned against
  rotation and mirroring (see discovery.md).
Second review (of the no-combat design) changed:
- a refused melee never costs time: the quiver's melee action refuses
  before marking the turn over, and the `player_fight()` refusal clears it;
- the trunk spot finders no longer skip safe cells at all, so allies
  (recall, summons, companions) may land inside; hostiles get a spot outside
  through `create_monster()` and lurker re-searches, and the drag/follow
  hooks (live again) re-pick outside;
- utility spells aimed at items or places (Apportation, Dig) are allowed;
- known harmful scrolls (torment, immolation, poison, fear, vulnerability)
  are refused before being read; unknown scrolls are not, so nothing leaks;
- documented that launcher and throwable quiver entries are disabled inside
  and that the Announcer line prints only on an actual attempt;
- smoke: no-time refusal, Apportation allowed, ally recall and friendly
  summons may land inside, hostile summons land outside.

## 2026-10-25 — Phase 3b done

Landed: every connected-branch floor except the Tomb gets one safe-room
vault (`dat/des/dcc/safe_rooms.des`, four variants). It holds all of the
floor's up stairs, one door, the Commissary general shop at normal prices
and an inactive loot box kiosk. On D:1 it is the arrival vault.
`dcc-room.cc` digs the room into wall first, then into deep water or lava,
then into open floor. Up stairs and exits outside the room are removed
before trunk's connectivity checks. `dcc_only` guards in `maps.cc` and
`dungeon.cc` keep the maps out of other game types. Reviewers changed:
- a Delver now starts inside the D:2 safe room (`dcc_start_stair()`, a
  guarded hook in `files.cc` `load_level()`);
- `dcc.arrival_spot()` runs the real `_place_player()` through one wrapper
  in `files.cc` (recorded in `CLAUDE.md`), instead of a copy of
  `_get_dest_stair_type()`;
- a removed up stair that was the only exit from an area becomes a down
  hatch, as trunk's `_fixup_branch_stairs()` does with vault stairs; at a
  branch end it becomes floor;
- the checks for water, lava and open floor now use trunk's
  `dgn_count_disconnected_zones()`, the same count its veto uses. The
  survey runs once per level and the variant shapes are cached for the
  whole process;
- `no_wall_fixup` on the maps (the room's walls were turning slimy on
  Slime);
- found while testing: the room could be dug over vault loot, because
  items are not in `igrid` yet during the build. Fixed;
- smoke: exact up-stair counts, one door that is not a safe cell, a way
  on reachable from the door, the shop's name and default prices, shaft
  and collapse landings outside the room, the Delver start, Temple, repeat
  builds of the hardest floors, and `dcc_only` rejected in other game types.
Rejected: returning false when the vault lands off its spot. The vault is
already written by then, so a retry would add a second room. It still
vetoes, and now also clears the shape cache.

## 2026-10-25 — Phase 4a done

Landed: `dcc-achieve.cc/.h` with 20 one-shot achievements, each with its
own Announcer line and box tier. They are checked at floor arrival, after
every counted kill, on every player turn and after a collapse. Boxes are
only recorded for now (`dcc_award_box` stub until 4b). Reviewers changed:
- `unique_N` counts distinct unique types, so Boris and Natasha count once;
  the old int count from the first 4a build is read as a minimum;
- `floor_N` counts floors descended instead of absolute depth, which
  over-counts in Descent's branches; `floor_12`'s line now names a count;
- `quick_exit` needs a deliberate descent, a kill on the floor, and no
  Abyss or portal visit (orchestrator decision; see Phase 4a). The arrival
  hook in `files.cc` now also passes `stair_taken`;
- kills of charmed hostiles count, as they give trunk XP; pacified monsters
  still do not;
- each unlock is one line that names its box ("a Gold box is added to your
  winnings", no promise about safe rooms yet), and the unlocks from one hook
  interrupt once (`dcc_unlock_batch`);
- the `kill_streak` line is built from its tunables;
- the table rows carry a kind and a threshold instead of nine wrapper
  predicates. The per-turn check allocates nothing and stops early once
  every state achievement is held;
- the comment about XP timing in the kill hook is corrected: an XL unlock
  from a kill's XP waits for the next turn's check;
- smoke: banished, timed-out and reset monsters award nothing; a charmed
  kill counts; Boris killed twice counts once; the quick_exit rules; branch
  and shaft floor counting; unlocks through the per-turn hook (new
  `dcc.player_turn`); one line per unlock in a burst. `spawn()` now uses
  only empty cells and reads the monster's real position, and the unique
  checks pick uniques that are still unplaced. Before this, a check could
  kill one of the level's own monsters, so the script failed at random.

## 2026-10-25 — Phase 4b done

Landed: `dcc-boxes.cc/.h`. The safe room's `K` cell is a working kiosk (a
`function_at_spot` marker, callback in `dat/dlua/dcc.lua`). Boxes drop
their tier's items (rand / acquirement / randart / wand) on safe floor
within radius 2 of it, never into the pack, and never make trunk cull the
level's items. Every roll uses the game RNG. Reviewers changed:
- the kiosk asks only on a deliberate step; travel, autoexplore, running,
  blinks and teleports cross it silently, and it is no travel stop. The old
  build prompted mid-travel and called `stop_delay` from inside it;
- the boxes that fit the free item slots are worked out before asking; the
  prompt and delay cover just those, none fitting jams without a prompt,
  and there is one jam message instead of two;
- opening is cancellable: a box opens every 2 turns, and a keypress, a
  forced interrupt or leaving the kiosk stops it between boxes. No cap on
  boxes per sitting (2 turns a box is small next to a 1200-turn floor);
- the randart is re-checked after conversion and rerolled if the god
  forbids it or it is useless; it draws from every randart-capable class
  (staves and talismans too) and is guaranteed, with a plain acquirement
  item as the documented last resort. Rand items the god forbids are
  rerolled. The Celestial wand no longer falls through to other classes
  unless acquirement finds no usable wand;
- box items say "You won it from a loot box" (`case AQ_DCC_BOX` in
  `items.cc` `origin_desc()`);
- `dcc.kiosk_spot` and `dcc.items_at` are gone (the smoke uses
  `dgn.find_marker_position_by_prop` and `dgn.items_at`);
- "rand" stays at absolute depth + 3 (Resolved decision 10);
- smoke: running suppression, the up-front fit and jam, the live delay
  (progressive opening, interrupt, leaving the kiosk), Platinum gear,
  Celestial wand and artefact, no TSO-forbidden or useless artefact over 8
  Legendary boxes, and the kiosk marker after a level marker round trip.
