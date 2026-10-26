-- Crawl Live (DCC) smoke script: the headless per-phase gate.
-- Run from crawl-ref/source:  ./crawl.exe -script dcc_smoke.lua
-- Every check error()s on failure, so a failing check exits non-zero.
-- Later phases append their checks here.

local function check(cond, what)
  if not cond then
    error("dcc_smoke FAILED: " .. what)
  end
  print("dcc_smoke ok: " .. what)
end

dcc.enable_for_script()
check(dcc.is_active(), "dcc.is_active() after enable_for_script")
check(dgn.is_descent(), "dgn.is_descent() is true in DCC")
check(dgn.br_depth("D") == 12, "the Dungeon is 12 floors deep")

-- D:12 is the bottom of the Dungeon in Descent; it may lack a plain down
-- stair, so for it only successful generation is checked.
local places = {
  { place = "D:1",  need_down = true },
  { place = "D:6",  need_down = true },
  { place = "D:12", need_down = false },
}

for _, p in ipairs(places) do
  debug.goto_place(p.place)
  local ok, err = pcall(test.regenerate_level)
  check(ok, p.place .. " generates" .. (ok and "" or (": " .. tostring(err))))
  if p.need_down then
    check(test.level_has_down_stair(), p.place .. " has a down stair")
  end
end

-- Phase 2a: the per-floor clock.
debug.goto_place("D:1")
test.regenerate_level()
dcc.start_floor_clock()
check(dcc.floor_turns_left() == 1500, "D:1 floor clock starts at 1500 turns")
dcc.tick(10)
check(dcc.floor_turns_left() == 1499, "a 10-aut tick leaves 1499 turns")
dcc.tick(5)
check(dcc.floor_turns_left() == 1499, "partial turns round up (1498.5 -> 1499)")

-- Arriving on a newly created floor starts its clock; returning to a floor
-- visited before (the Descent stair peek) resumes its parked clock.
debug.goto_place("D:4")
dcc.enter_floor(true)
check(dcc.floor_turns_left() == 1200, "D:4 floor clock starts at 1200 turns")
dcc.tick(100)
debug.goto_place("D:1")
dcc.enter_floor(false)
check(dcc.floor_turns_left() == 1499, "back on D:1 its clock resumes at 1499")
dcc.enter_floor(false)
check(dcc.floor_turns_left() == 1499, "re-entering the same floor changes nothing")
debug.goto_place("D:4")
dcc.enter_floor(false)
check(dcc.floor_turns_left() == 1190, "back on D:4 its clock resumes at 1190")

-- A newly created level always gets a fresh clock, even if a clock for the
-- same place name is parked (D:1 was parked at 1499 above).
debug.goto_place("D:1")
dcc.enter_floor(true)
check(dcc.floor_turns_left() == 1500, "a newly created D:1 gets a fresh 1500")

-- Outside connected branches the clock pauses, and resumes on return.
dcc.tick(100)
check(dcc.floor_turns_left() == 1490, "D:1 clock at 1490 before the Abyss")
debug.goto_place("Abyss:1")
dcc.enter_floor(true)
check(dcc.tick(5000) == nil, "no collapse while paused in the Abyss")
check(dcc.floor_turns_left() == 1490, "the clock is paused in the Abyss")
debug.goto_place("Sewer")
dcc.enter_floor(true)
dcc.tick(5000)
check(dcc.floor_turns_left() == 1490, "the clock is paused in a portal vault")
debug.goto_place("D:1")
dcc.enter_floor(false)
check(dcc.floor_turns_left() == 1490, "back on D:1 the paused clock resumes")

-- Reaching zero collapses the floor. With a floor below, the fall itself is
-- skipped in script mode (no real player to move between levels), but the
-- result shows the collapse was attempted and found a floor to fall to.
dcc.set_floor_turns_left(1)
check(dcc.tick(5) == nil, "half a turn left: no collapse yet")
check(dcc.tick(10) == "fall_skipped", "D:1 collapses toward the floor below")
check(dcc.floor_turns_left() == 0, "the clock stops at 0")

-- At a branch bottom there is nowhere to fall: the Zot-touch penalty applies
-- and a short retry clock starts.
debug.goto_place("D:12")
dcc.enter_floor(true)
check(dcc.floor_turns_left() == 1200, "D:12 floor clock starts at 1200 turns")
dcc.set_floor_turns_left(1)
check(dcc.tick(10) == "no_floor", "D:12 has no floor below to fall to")
check(dcc.floor_turns_left() == 100, "the retry clock restarts at 100 turns")

-- The collapsed D:1 left at 0 is never parked, so a return cannot collapse
-- it again on the spot: it gets a fresh clock instead.
debug.goto_place("D:1")
dcc.enter_floor(false)
check(dcc.floor_turns_left() == 1500, "a collapsed floor is not re-entered at 0")

dcc.set_floor_turns_left(3)
check(dcc.floor_turns_left() == 3, "set_floor_turns_left sets the clock")

-- Phase 2b: clock surface.
-- The full clock length follows the floor the clock belongs to.
debug.goto_place("D:1")
dcc.start_floor_clock()
check(dcc.floor_turns_total() == 1500, "D:1 floor clock total is 1500 turns")
debug.goto_place("D:5")
dcc.start_floor_clock()
check(dcc.floor_turns_total() == 1200, "D:5 floor clock total is 1200 turns")
check(not dcc.floor_clock_paused(), "the clock runs on D:5")

-- The Announcer warns as the clock crosses each threshold, once, and only
-- the most urgent line when one tick crosses several.
local function last_messages()
  return crawl.messages(3) or ""
end
dcc.set_floor_turns_left(601)
dcc.tick(10)
check(last_messages():find("Halfway there", 1, true) ~= nil,
      "the Announcer calls 50% left")
dcc.set_floor_turns_left(51)
dcc.tick(10)
check(last_messages():find("Fifty turns", 1, true) ~= nil,
      "the Announcer calls 50 turns left")
dcc.set_floor_turns_left(200)
dcc.tick(1900)
check(last_messages():find("Ten turns", 1, true) ~= nil,
      "a tick across several thresholds announces the most urgent")
check(last_messages():find("Twenty turns", 1, true) == nil,
      "and not the less urgent ones")

-- Off the connected branches the clock reports itself paused, and the
-- total still belongs to the floor the clock came from.
debug.goto_place("D:1")
dcc.enter_floor(true)
debug.goto_place("Abyss:1")
dcc.enter_floor(true)
check(dcc.floor_clock_paused(), "the clock is reported paused in the Abyss")
check(dcc.floor_turns_total() == 1500, "the paused clock keeps D:1's total")
debug.goto_place("D:1")
dcc.enter_floor(false)
check(not dcc.floor_clock_paused(), "back on D:1 the clock runs again")

-- Negative checks: the Announcer stays quiet when it has nothing to say.
-- Every message after the last mark is what one step printed.
local MARK = "-- dcc_smoke mark --"
local function mark()
  crawl.mpr(MARK)
end
local function since_mark()
  local all = crawl.messages(40) or ""
  local pos, last = 1, nil
  while true do
    local s, e = all:find(MARK, pos, true)
    if not s then break end
    last = e
    pos = e + 1
  end
  if not last then
    error("dcc_smoke FAILED: the mark is not in the message history")
  end
  return all:sub(last + 1)
end
local function quiet()
  return since_mark():match("%S") == nil
end

debug.goto_place("D:5")
dcc.start_floor_clock()
dcc.set_floor_turns_left(1000)
mark()
dcc.tick(10)
check(quiet(), "a tick that crosses no threshold announces nothing")

-- At a branch bottom the no-floor line is announced once; the retry clock
-- that follows gives no drop warnings, and later retries stay quiet.
debug.goto_place("D:12")
dcc.enter_floor(true)
dcc.set_floor_turns_left(1)
mark()
check(dcc.tick(10) == "no_floor", "D:12 collapses with no floor below")
check(since_mark():find("Nowhere left to drop you", 1, true) ~= nil,
      "the first no-floor collapse is announced")
check(dcc.floor_turns_total() == 100, "the retry clock's total is 100 turns")
mark()
dcc.tick(600)  -- 40 turns left: past 50% and 50 turns
dcc.tick(250)  -- 15 left: past 25%, 20% and 20 turns
dcc.tick(100)  -- 5 left: past 10% and 10 turns
check(quiet(), "the retry clock gives no drop warnings")
mark()
check(dcc.tick(100) == "no_floor", "the retry clock runs out again")
check(since_mark():find("Nowhere left to drop you", 1, true) == nil,
      "a repeated no-floor collapse is not re-announced")
check(dcc.floor_turns_left() == 100, "and the retry clock restarts at 100")

-- A parked retry clock stays a retry clock when the player comes back.
debug.goto_place("D:11")
dcc.enter_floor(true)
check(dcc.floor_turns_total() == 1200, "D:11 has a normal 1200-turn clock")
debug.goto_place("D:12")
dcc.enter_floor(false)
check(dcc.floor_turns_left() == 100 and dcc.floor_turns_total() == 100,
      "back on D:12 the parked retry clock resumes")
mark()
dcc.tick(600)
check(quiet(), "the resumed retry clock gives no drop warnings")

-- Phase 3a: the dcc_safe cell property (FPROP_DCC_SAFE). The test vault is
-- a 5x5 safe room (x 28..32, y 28..32) centred on (30, 30), with an open
-- door at (30, 33) and a ring of plain, unsafe floor around it (y 34 is the
-- row below the door).
dgn.reset_level()
local safe_map = dgn.map_by_name("dcc_test_safe")
check(safe_map ~= nil, "the dcc_test_safe vault exists")
dgn.with_map_anchors(30, 30, function()
  return dgn.place_map(safe_map, true, true, 30, 30)
end)
check(dgn.grid(31, 31) == dgn.find_feature_number("floor"),
      "dcc_test_safe is placed at (30, 30)")
check(dcc.is_safe_cell(31, 31), "an interior cell of the room is safe")
check(dcc.is_safe_cell(28, 28) and dcc.is_safe_cell(32, 32),
      "the room's corner cells are safe")
check(dgn.fprop_at(31, 31, "dcc_safe"), "KPROP dcc_safe sets the property")
check(not dcc.is_safe_cell(30, 33), "the doorway is not safe")
check(not dcc.is_safe_cell(30, 34), "the floor outside the room is not safe")
check(not dcc.is_safe_cell(27, 27), "the room's wall is not safe")
check(not dcc.is_safe_cell(-1, -1), "out of bounds is not safe")

you.moveto(30, 30)

-- A monster's map position. The dlua monster wrapper's x and y are
-- relative to the player, so add the player's position back.
local function mpos(m)
  local px, py = you.pos()
  return m.x + px, m.y + py
end
local function on_safe(m)
  return dcc.is_safe_cell(mpos(m))
end
-- One AI action: top up the energy run_ai() needs (handle_monster_move()
-- does nothing without action energy), then act.
local function act(m)
  if m.energy < 100 then
    m.add_energy(100 - m.energy)
  end
  m.run_ai()
end

-- Placement: hostiles are refused on safe cells, allies are not.
check(dgn.create_monster(31, 31, "orc") == nil,
      "a hostile monster cannot be placed on a safe cell")
local ally = dgn.create_monster(29, 29, "orc att:friendly")
check(ally ~= nil, "an ally can be placed on a safe cell")
check(dcc.monster_can_step(29, 29, 29, 28),
      "an ally can walk about inside the safe room")
check(dcc.monster_can_land(29, 29, 32, 32),
      "an ally can land anywhere in the safe room")

-- A hostile in the doorway can neither walk nor be moved into the room.
local orc = dgn.create_monster(30, 33, "generate_awake orc")
check(orc ~= nil, "a hostile monster can stand in the doorway")
check(not dcc.monster_can_step(30, 33, 30, 32),
      "the hostile cannot step into the safe room")
check(not dcc.monster_can_step(30, 33, 31, 32),
      "the hostile cannot step diagonally into the safe room")
check(dcc.monster_can_step(30, 33, 30, 34),
      "the hostile can still step back outside")
check(not dcc.monster_can_land(30, 33, 31, 31),
      "the hostile cannot blink, teleport or be pushed into the room")
check(dcc.monster_can_land(30, 33, 31, 34),
      "the hostile can land outside the room")

-- A hostile already on a safe cell (an ally that turned) is not trapped: it
-- may leave, but only by heading for the way out (never sideways or deeper),
-- while a hostile outside may not step onto those cells.
local turned = dgn.create_monster(32, 34, "orc")
check(turned ~= nil, "a second hostile stands outside the room")
dgn.fprop_changed(32, 34, "dcc_safe")
dgn.fprop_changed(31, 34, "dcc_safe")
check(dcc.is_safe_cell(32, 34) and dcc.is_safe_cell(31, 34),
      "two outside cells are made safe under the second hostile")
check(dcc.monster_can_step(32, 34, 33, 34),
      "a hostile on a safe cell can leave it")
check(not dcc.monster_can_step(32, 34, 31, 34),
      "a hostile on a safe cell cannot move sideways to another safe cell")
check(not dcc.monster_can_step(30, 33, 31, 34),
      "a hostile off the safe cells still cannot step onto one")
dgn.fprop_changed(32, 34, "dcc_safe")
dgn.fprop_changed(31, 34, "dcc_safe")
check(not dcc.is_safe_cell(32, 34) and not dcc.is_safe_cell(31, 34),
      "fprop_changed toggles the property back off")
turned.dismiss()

-- Live behaviour: with the player inside, the hostile's own AI never takes
-- it into the room, and random teleports never land it there.
for i = 1, 10 do
  act(orc)
  check(not on_safe(orc),
        "the hostile's AI keeps it out of the safe room (turn " .. i .. ")")
end
local spots = {}
local nspots = 0
for i = 1, 20 do
  orc.random_teleport()
  check(not on_safe(orc),
        "a random teleport does not land the hostile in the room (" .. i .. ")")
  local key = orc.x .. "," .. orc.y  -- player-relative, still distinct
  if not spots[key] then
    spots[key] = true
    nspots = nspots + 1
  end
end
check(nspots > 1, "the random teleports really moved the hostile")

-- Phase 3a review: no combat in a safe room (user decision 2026-10-25).
-- The player stands just inside the door, right next to a hostile in the
-- doorway.
orc.dismiss()
you.moveto(30, 32)
local guard = dgn.create_monster(30, 33, "generate_awake orc")
check(guard ~= nil, "a hostile stands in the doorway, next to the player")
check(not dcc.can_harm(30, 33, 30, 32),
      "the hostile in the doorway cannot harm the player inside")
check(not dcc.can_harm(30, 32, 30, 33),
      "the player inside cannot harm the hostile in the doorway")
check(not dcc.can_harm(29, 29, 30, 33),
      "the ally inside cannot harm the hostile in the doorway")
check(dcc.player_attack_refused(),
      "the player cannot melee, fire or throw from inside")
check(dcc.spell_refusal("Magic Dart"):find("safe room") ~= nil,
      "a harmful spell is refused inside")
check(dcc.spell_refusal("Blink"):find("safe room") == nil,
      "a harmless spell is not refused inside")
check(dcc.spell_refusal("Apportation"):find("safe room") == nil,
      "Apportation (aimed at an item) is not refused inside")
check(not dcc.melee_takes_turn(30, 33),
      "a refused melee takes no turn, even with the turn already marked over")
local hp = you.hp()
for i = 1, 10 do
  act(guard)
  check(you.hp() == hp,
        "the hostile next to the room does not hurt the player (turn " .. i .. ")")
  check(not on_safe(guard),
        "the hostile next to the room stays outside (turn " .. i .. ")")
end

-- A monster dragged along with the player lands outside the room.
for i = 1, 10 do
  local x, y = dcc.spot_near_you(mpos(guard))
  check(x ~= nil, "a hostile dragged along with the player finds a spot (" .. i .. ")")
  check(not dcc.is_safe_cell(x, y),
        "a hostile dragged along with the player lands outside (" .. i .. ")")
end
guard.dismiss()

-- Allies may land inside (recall, summons); hostile summons appear outside.
local ally_inside = false
for i = 1, 20 do
  local x, y = dcc.spot_near_you(29, 29)
  if x and dcc.is_safe_cell(x, y) then
    ally_inside = true
  end
end
check(ally_inside, "an ally recalled to the player may land in the safe room")
local summoned_inside = false
for i = 1, 20 do
  local x, y = dcc.summon_near_you("orc", false)
  if x and dcc.is_safe_cell(x, y) then
    summoned_inside = true
  end
end
check(summoned_inside, "a friendly summon may appear in the safe room")
local hostile_placed = false
for i = 1, 10 do
  local x, y = dcc.summon_near_you("orc", true)
  if x then
    hostile_placed = true
    check(not dcc.is_safe_cell(x, y),
          "a hostile summon appears outside the safe room (" .. i .. ")")
  end
end
check(hostile_placed, "hostile summons near the room find a spot outside")

-- An ally deep in the room turns hostile: it heads straight for the way
-- out, never deeper, and never hurts the player on the way.
local turncoat = dgn.create_monster(31, 29, "orc att:friendly")
check(turncoat ~= nil, "an ally stands deep in the safe room")
turncoat.add_ench("hexed", 1000)
check(not turncoat.wont_attack, "the hexed ally has turned hostile")
check(dcc.monster_can_step(31, 29, 31, 30),
      "the turned hostile can step towards the way out")
check(not dcc.monster_can_step(31, 29, 31, 28),
      "the turned hostile cannot step deeper into the room")
check(not dcc.monster_can_step(31, 29, 32, 29),
      "the turned hostile cannot step sideways in the room")
check(not dcc.monster_can_land(31, 29, 28, 28),
      "the turned hostile cannot blink deeper into the room")
local left = false
for i = 1, 15 do
  act(turncoat)
  check(you.hp() == hp,
        "the turned hostile does not hurt the player (turn " .. i .. ")")
  if not on_safe(turncoat) then
    left = true
    break
  end
end
check(left, "the turned hostile walks out of the safe room")
turncoat.dismiss()

you.moveto(30, 34)
check(not dcc.player_attack_refused(), "outside the room the player may attack")

-- Phase 3b: a safe-room vault on every connected-branch floor. It holds all
-- of the floor's up stairs and branch exits, so every arrival lands inside,
-- plus the Commissary shop and the (inactive) loot box kiosk.
local room_map = dgn.map_by_name("dcc_safe_room_a")
check(room_map ~= nil, "the dcc_safe_room_a vault exists")
check(dgn.has_tag(room_map, "dcc_only"),
      "the safe room is tagged dcc_only, so no other game type places it")
check(dgn.has_tag(room_map, "allow_dup"),
      "the safe room may be placed on every floor (allow_dup)")

-- Every way up: stone up stairs, branch exits and up hatches. The Zot:$
-- dungeon exit is the way out with the Orb, not an arrival.
local function ways_up()
  local ups = {}
  for y = 1, dgn.GYM - 2 do
    for x = 1, dgn.GXM - 2 do
      local name = dgn.feature_name(dgn.grid(x, y))
      if (name:find("^stone_stairs_up") or name:find("^exit_")
          or name == "escape_hatch_up")
         and not (name == "exit_dungeon" and you.branch() == "Zot") then
        table.insert(ups, { x = x, y = y, name = name })
      end
    end
  end
  return ups
end

local function safe_rooms_placed()
  local _, n = (debug.vault_names() or ""):gsub("dcc_safe_room", "")
  return n
end

-- Up stairs a floor keeps (dungeon.cc _fixup_stone_stairs()): one on the
-- first floor of a branch and in the Hell branches, three elsewhere. Trunk
-- turns the X:1 one into the branch exit.
local hell_branches = { Dis = true, Geh = true, Coc = true, Tar = true }
local function expected_ups()
  if you.depth() == 1 or hell_branches[you.branch()] then
    return 1
  end
  return 3
end

-- The room's door: the one door next to a safe cell.
local function room_doors()
  local doors = {}
  for y = 1, dgn.GYM - 2 do
    for x = 1, dgn.GXM - 2 do
      if dgn.feature_name(dgn.grid(x, y)):find("door") then
        local by_room = false
        for dy = -1, 1 do
          for dx = -1, 1 do
            by_room = by_room or dcc.is_safe_cell(x + dx, y + dy)
          end
        end
        if by_room then
          table.insert(doors, { x = x, y = y })
        end
      end
    end
  end
  return doors
end

-- Generates the floor, then checks the room, its contents, every way up,
-- the way out, and where each kind of arrival lands (dcc.arrival_spot()
-- runs the real arrival placement).
local function check_safe_room_floor(place, arrivals)
  debug.goto_place(place)
  local ok, err = pcall(test.regenerate_level)
  check(ok, place .. " generates" .. (ok and "" or (": " .. tostring(err))))
  check(safe_rooms_placed() == 1, place .. " has exactly one safe room")

  local ups = ways_up()
  check(#ups == expected_ups(), place .. " has " .. expected_ups()
        .. " way(s) up (found " .. #ups .. ")")
  for _, u in ipairs(ups) do
    check(dcc.is_safe_cell(u.x, u.y),
          place .. ": " .. u.name .. " at " .. u.x .. "," .. u.y
          .. " is inside the safe room")
  end

  local shops, kiosks = {}, 0
  for y = 1, dgn.GYM - 2 do
    for x = 1, dgn.GXM - 2 do
      if dcc.is_safe_cell(x, y) then
        if dgn.feature_name(dgn.grid(x, y)) == "enter_shop" then
          table.insert(shops, { x = x, y = y })
        end
        if dgn.feature_desc_at(x, y, "plain"):find("loot box kiosk", 1, true)
        then
          kiosks = kiosks + 1
        end
      end
    end
  end
  check(#shops == 1, place .. ": the safe room holds the shop")
  check(kiosks == 1, place .. ": the safe room holds the kiosk")
  local name, full, general, default_price =
    dcc.shop_at(shops[1].x, shops[1].y)
  check(name == "Commissary" and full:find("Commissary", 1, true),
        place .. ": the shop is the Commissary (" .. tostring(full) .. ")")
  check(general, place .. ": the Commissary is a general store")
  check(default_price, place .. ": the Commissary charges default prices")

  local doors = room_doors()
  check(#doors == 1, place .. ": the safe room has one door")
  local door = doors[1]
  check(not dcc.is_safe_cell(door.x, door.y),
        place .. ": the door cell is not a safe cell")
  local any, reached = dcc.way_on_from(door.x, door.y)
  check(not any or reached,
        place .. ": a way on is reachable from the safe room's door")

  for _, how in ipairs(arrivals) do
    for i = 1, 3 do
      local x, y = dcc.arrival_spot(how)
      check(dcc.is_safe_cell(x, y),
            place .. ": arriving by " .. how .. " lands inside the safe room")
    end
  end
  -- A shaft, and a floor collapse (the same fall), lands outside the room.
  for i = 1, 3 do
    local x, y = dcc.arrival_spot("shaft")
    check(not dcc.is_safe_cell(x, y),
          place .. ": falling in lands outside the safe room")
  end
end

local down_ways = { "stairs_i", "stairs_ii", "stairs_iii" }
check_safe_room_floor("D:1", { "new_game", "delver_start" })
-- A Delver starts on D:2 in Descent; in Crawl Live they start in its room.
check_safe_room_floor("D:2", { "delver_start", "stairs_i", "stairs_ii",
                               "stairs_iii" })
for d = 3, 12 do
  check_safe_room_floor("D:" .. d, down_ways)
end
-- Branch tops (arrival on the branch exit), branch ends, and the encompass
-- maps where nothing fits beside the main vault: Vaults:$, the Vestibule of
-- Hell, and the mostly-water Cocytus end.
for _, place in ipairs({ "Lair:1", "Orc:1", "Elf:1", "Vaults:1", "Depths:1",
                         "Zot:1", "Hell", "Coc:1", "Temple" }) do
  check_safe_room_floor(place, { "branch" })
end
for _, place in ipairs({ "Lair:5", "Orc:2", "Elf:3", "Shoals:4", "Swamp:4",
                         "Snake:4", "Spider:4", "Slime:5", "Vaults:5",
                         "Crypt:3", "Depths:4", "Zot:5", "Coc:7", "Dis:7" }) do
  check_safe_room_floor(place, down_ways)
end
-- Placement is random: build the hardest floors a few more times so an
-- intermittent failure shows up.
for _, place in ipairs({ "D:2", "Vaults:5", "Slime:5", "Zot:5", "Coc:7",
                         "Dis:7", "Hell" }) do
  for i = 1, 3 do
    check_safe_room_floor(place, {})
  end
end

-- Other game types never see the safe rooms: map selection skips them and
-- placing one directly is vetoed.
local selection, vetoed = dcc.dcc_only_rejected_outside()
check(selection, "outside Crawl Live no safe-room map is selected")
check(vetoed, "outside Crawl Live placing a safe room is vetoed")

-- Off the connected branches there is no safe room.
debug.goto_place("Sewer")
test.regenerate_level()
check(safe_rooms_placed() == 0, "a portal vault gets no safe room")

-- Phase 4a: achievements, the Announcer's unlock lines, and the box stub.
local ach_table = dcc.achievement_table()
check(#ach_table >= 16, "the achievement table has at least 16 entries ("
      .. #ach_table .. ")")
local seen_ids = {}
local tiers = { Bronze = true, Silver = true, Gold = true, Platinum = true,
                Legendary = true, Celestial = true }
-- Names from the books that must never appear in game text.
local banned = { "carl", "donut", "mordecai", "syndicate", "borant",
                 "dungeon crawler" }
for _, a in ipairs(ach_table) do
  check(not seen_ids[a.id], "achievement id " .. a.id .. " is unique")
  seen_ids[a.id] = true
  check(a.name ~= "" and a.line ~= "", a.id .. " has a name and a line")
  check(tiers[a.tier], a.id .. " awards a known box tier (" .. a.tier .. ")")
  local text = (a.name .. " " .. a.line):lower()
  for _, word in ipairs(banned) do
    check(not text:find(word, 1, true),
          a.id .. " uses no trademarked name (" .. word .. ")")
  end
end
for _, id in ipairs({ "first_blood", "floor_2", "floor_4", "floor_6",
                      "floor_8", "floor_10", "floor_12", "unique_1",
                      "unique_3", "unique_5", "big_game", "kill_streak",
                      "quick_exit", "fell_through", "signed", "xl_10",
                      "xl_20", "rune_1", "rune_3", "orb" }) do
  check(seen_ids[id], "the seed achievement " .. id .. " is in the table")
end

-- A level to fight on, with the player outside the safe room.
debug.goto_place("D:3")
test.regenerate_level()
dcc.start_floor_clock()
dcc.reset_achievements()

local cursor = 0
local function next_cell()
  while true do
    cursor = cursor + 1
    local x = 2 + cursor % (dgn.GXM - 4)
    local y = 2 + math.floor(cursor / (dgn.GXM - 4)) % (dgn.GYM - 4)
    if dgn.feature_name(dgn.grid(x, y)) == "floor"
       and not dcc.is_safe_cell(x, y) then
      return x, y
    end
  end
end
local px, py = next_cell()
you.moveto(px, py)
-- Place a monster on the next free outside cell; returns its x, y.
-- Only empty cells, and the position is read back from the monster itself:
-- the level's own monsters must never be the ones a check kills.
local function spawn(spec)
  for i = 1, 200 do
    local x, y = next_cell()
    if not (x == px and y == py) and dgn.mons_at(x, y) == nil then
      local m = dgn.create_monster(x, y, spec)
      if m then
        return mpos(m)
      end
    end
  end
  error("dcc_smoke FAILED: could not place " .. spec)
end
-- Kill a fresh monster, by the player or by a fresh killer_spec monster.
local function kill(spec, killer_spec)
  local x, y = spawn(spec)
  if killer_spec then
    local kx, ky = spawn(killer_spec)
    check(dcc.kill(x, y, kx, ky), spec .. " is killed by " .. killer_spec)
  else
    check(dcc.kill(x, y), spec .. " is killed by the player")
  end
end

-- Unlocking is one-shot, announced, and awards the achievement's box.
mark()
check(dcc.unlock("first_blood"), "unlocking first_blood succeeds")
check(since_mark():find("Achievement unlocked: First Blood", 1, true) ~= nil,
      "the Announcer calls the unlock")
check(since_mark():find("First blood. Try to look like you meant it.", 1, true)
      ~= nil, "with the achievement's line")
check(since_mark():find("Bronze box", 1, true) ~= nil,
      "and names the Bronze box")
mark()
check(not dcc.unlock("first_blood"), "unlocking it again does nothing")
check(quiet(), "a repeated unlock announces nothing")
check(#dcc.achievements() == 1, "first_blood is recorded once")
check(dcc.has_achievement("first_blood"), "has_achievement sees it")
check(not dcc.has_achievement("orb"), "has_achievement is false for orb")
check(dcc.box_count() == 1, "one box after one unlock")
check(dcc.boxes()[1] == "Bronze", "the box is Bronze")
check(not pcall(dcc.unlock, "no_such_achievement"),
      "unlocking an unknown id raises an error")

-- The state survives a save and reload, and stays one-shot afterwards.
check(dcc.save_roundtrip() > 0, "the DCC state goes through save marshalling")
check(dcc.has_achievement("first_blood"), "first_blood survives a reload")
check(dcc.box_count() == 1 and dcc.boxes()[1] == "Bronze",
      "the box survives a reload")
check(not dcc.unlock("first_blood"),
      "after a reload first_blood is still one-shot")
check(#dcc.achievements() == 1 and dcc.box_count() == 1,
      "and nothing was added")

-- Which kills count: the player's and a player ally's, not other monsters',
-- not summons, not your own allies. The kills give XP, and a level-up that
-- earns a stat point would prompt (and hang a headless script), so the
-- player fights at XL 26, far from XL 27. That also meets xl_10 and xl_20.
check(you.set_xl(26), "the player is set to XL 26 for the fights")
dcc.reset_achievements()
kill("orc", "orc")
check(not dcc.has_achievement("first_blood"),
      "a kill by a hostile monster does not count")
kill("orc sum:shadow_creatures dur:2")
check(not dcc.has_achievement("first_blood"),
      "killing a summoned monster does not count")
kill("orc att:friendly")
check(not dcc.has_achievement("first_blood"),
      "killing your own ally does not count")
check(dcc.box_count() == 0, "no box from kills that do not count")
mark()
kill("orc", "orc att:friendly")
check(dcc.has_achievement("first_blood"), "a kill by an ally counts")
check(since_mark():find("First Blood", 1, true) ~= nil,
      "the Announcer calls first_blood on the kill")
check(dcc.has_achievement("xl_10") and dcc.has_achievement("xl_20"),
      "the check after a counted kill unlocks xl_10 and xl_20 at XL 26")
check(dcc.box_count() == #dcc.achievements(),
      "every achievement awarded exactly one box")

dcc.reset_achievements()
kill("orc", "orc att:friendly sum:shadow_creatures dur:2")
check(dcc.has_achievement("first_blood"), "a kill by your summon counts")

dcc.reset_achievements()
kill("orc")
check(dcc.has_achievement("first_blood"), "a kill by the player counts")

-- A charmed hostile still gives trunk XP, so killing it counts.
dcc.reset_achievements()
kill("orc ench:charm:500")
check(dcc.has_achievement("first_blood"),
      "killing a charmed (temporarily friendly) hostile counts")

-- Banishing a monster, or one timing out or being reset, is no kill.
dcc.reset_achievements()
for _, how in ipairs({ "banished", "timeout", "reset" }) do
  local x, y = spawn("orc")
  dcc.remove_monster(x, y, how)
  check(dgn.mons_at(x, y) == nil, "the orc is gone (" .. how .. ")")
  check(not dcc.has_achievement("first_blood") and dcc.box_count() == 0,
        "a " .. how .. " monster awards nothing")
end

-- Kills count in the Abyss and in portal vaults too (only the floor clock
-- pauses there).
for _, place in ipairs({ "Abyss:1", "Sewer" }) do
  dcc.reset_achievements()
  debug.goto_place(place)
  kill("orc")
  check(dcc.has_achievement("first_blood"), "a kill in " .. place .. " counts")
end
debug.goto_place("D:3")

-- Uniques: killed by the player or an ally, counted at 1, 3 and 5.
-- Levels generated earlier in this run may already hold some uniques, and
-- trunk places each only once, so take six that are still unplaced.
local uniques = {}
for _, name in ipairs({ "Grinder", "Sigmund", "Ijyb", "Jessica", "Terence",
                        "Edmund", "Eustachio",
                        "Prince Ribbit", "Menkaure", "Crazy Yiuf", "Erica",
                        "Josephine", "Maurice", "Harold", "Joseph",
                        "Gastronok", "Nergalle", "Louise" }) do
  if #uniques < 6 and not you.uniques(name) then
    table.insert(uniques, name)
  end
end
check(#uniques == 6, "six unplaced uniques to fight")
dcc.reset_achievements()
kill(table.remove(uniques, 1), "orc")
check(not dcc.has_achievement("unique_1"),
      "a unique killed by a hostile monster does not count")
for i, name in ipairs(uniques) do
  if i == 2 then
    kill(name, "orc att:friendly")
  else
    kill(name)
  end
  check(dcc.has_achievement("unique_1") == (i >= 1)
        and dcc.has_achievement("unique_3") == (i >= 3)
        and dcc.has_achievement("unique_5") == (i >= 5),
        "unique achievements after " .. i .. " unique kill(s)")
end
check(dcc.uniques_killed() == 5, "five distinct uniques counted")

-- A unique that comes back (Boris; Natasha revives the same way) counts once.
-- (A level generated earlier may already hold Boris; trunk will not place a
-- second copy, so the check needs him unplaced. Killing him unplaces him.)
dcc.reset_achievements()
if you.uniques("Boris") then
  crawl.stderr("dcc_smoke: Boris already placed this run; re-kill check skipped\n")
else
  kill("Boris")
  check(dcc.uniques_killed() == 1, "killing Boris counts one unique")
  check(not you.uniques("Boris"), "a dead Boris can be placed again")
  kill("Boris")
  check(dcc.uniques_killed() == 1, "killing Boris again still counts one")
  check(dcc.has_achievement("unique_1") and not dcc.has_achievement("unique_3"),
        "and earns no more unique boxes")
end

-- big_game: a monster with HD at least XL + 5.
dcc.reset_achievements()
kill("orc hd:1")
check(not dcc.has_achievement("big_game"), "an HD 1 orc is not big game")
kill("orc hd:" .. (you.xl() + 5))
check(dcc.has_achievement("big_game"), "an orc with HD = XL + 5 is big game")

-- kill_streak: ten counted kills within 50 turns.
dcc.reset_achievements()
for i = 1, 9 do
  kill("orc")
end
check(not dcc.has_achievement("kill_streak"), "nine quick kills are no streak")
dcc.advance_time(510)
kill("orc")
check(not dcc.has_achievement("kill_streak"),
      "ten kills spread over 51 turns are no streak")
for i = 1, 8 do
  kill("orc")
end
check(not dcc.has_achievement("kill_streak"),
      "the window slides: nine kills inside it are still no streak")
mark()
kill("orc")
check(dcc.has_achievement("kill_streak"), "ten kills within 50 turns: streak")
check(since_mark():find("Combo Meter", 1, true) ~= nil,
      "the Announcer calls the streak")
for i = 1, 10 do
  kill("orc")
end
local streaks = 0
for _, id in ipairs(dcc.achievements()) do
  if id == "kill_streak" then
    streaks = streaks + 1
  end
end
check(streaks == 1, "another ten quick kills award no second streak")

check(dcc.box_count() == #dcc.achievements(),
      "after all the fights, still one box per achievement")

-- Each unlock is one Announcer line naming its box, even in a burst.
local function count(hay, needle)
  local n, at = 0, 1
  while true do
    local s = hay:find(needle, at, true)
    if not s then
      return n
    end
    n, at = n + 1, s + 1
  end
end
dcc.reset_achievements()
mark()
kill("orc hd:" .. (you.xl() + 5))
-- The history wraps long lines; join them back up.
local burst = since_mark():gsub("%s+", " ")
check(#dcc.achievements() >= 3,
      "one big kill unlocks several achievements at once ("
      .. #dcc.achievements() .. ")")
check(count(burst, "Achievement unlocked:") == #dcc.achievements()
      and count(burst, "box is added to your winnings") == #dcc.achievements(),
      "one line per unlock, each naming its box")

-- The real per-turn path (player_reacts() -> dcc_tick_clock()) runs the
-- state checks: XL 26 meets xl_10 and xl_20 on the next turn.
dcc.reset_achievements()
check(not dcc.has_achievement("xl_10"), "xl_10 is not held after a reset")
dcc.player_turn(10)
check(dcc.has_achievement("xl_10") and dcc.has_achievement("xl_20"),
      "the per-turn hook unlocks xl_10 and xl_20")
local held = #dcc.achievements()
dcc.player_turn(10)
check(#dcc.achievements() == held, "the next turn unlocks nothing more")

-- quick_exit: a deliberate descent (down stairs or a branch entrance) from a
-- floor with a counted kill on it, no Abyss or portal visit from it, and
-- more than 60% of its clock left.
dcc.reset_achievements()
debug.goto_place("D:1")
dcc.enter_floor(true)
check(not dcc.has_achievement("quick_exit"),
      "arriving with no kill anywhere is no quick exit")
debug.goto_place("D:2")
dcc.enter_floor(true)
check(not dcc.has_achievement("quick_exit"),
      "stair-diving D:1 with no kill on it is no quick exit")
kill("orc")
debug.goto_place("D:3")
dcc.enter_floor(true)
check(dcc.has_achievement("quick_exit"),
      "a kill, then the stairs down with the whole clock left: quick_exit")

dcc.reset_achievements()
kill("orc")
debug.goto_place("D:4")
dcc.enter_floor(true, "shaft")
check(not dcc.has_achievement("quick_exit"),
      "falling down a shaft (or a collapse) is no quick exit")

dcc.reset_achievements()
kill("orc")
debug.goto_place("D:5")
dcc.enter_floor(true, "hatch")
check(not dcc.has_achievement("quick_exit"),
      "dropping down a hatch is no quick exit")

dcc.reset_achievements()
kill("orc")
debug.goto_place("Abyss:1")
dcc.enter_floor(true, "abyss")
debug.goto_place("D:6")
dcc.enter_floor(true, "abyss_exit")
check(not dcc.has_achievement("quick_exit"),
      "banished, then out of the Abyss onto a new floor: no quick exit")

dcc.reset_achievements()
kill("orc")
debug.goto_place("Sewer")
dcc.enter_floor(true)
debug.goto_place("D:6")
dcc.enter_floor(false)
debug.goto_place("D:7")
dcc.enter_floor(true)
check(not dcc.has_achievement("quick_exit"),
      "a portal detour from the floor rules out quick_exit")

dcc.reset_achievements()
kill("orc")
dcc.set_floor_turns_left(480)  -- 40% of D:7's 1200
debug.goto_place("D:8")
dcc.enter_floor(true)
check(not dcc.has_achievement("quick_exit"),
      "leaving with 40% of the clock left is no quick exit")

dcc.reset_achievements()
kill("orc")
debug.goto_place("Lair:1")
dcc.enter_floor(true, "branch")
check(dcc.has_achievement("quick_exit"),
      "a kill, then a branch entrance with the clock left: quick_exit")

-- Floor entry: floors reached, counted as floors descended.
check(you.set_xl(1), "the player is back to XL 1")
dcc.reset_achievements()
debug.goto_place("D:1")
dcc.enter_floor(true)
check(dcc.floors_reached() == 1, "a new game starts on floor 1")
check(not dcc.has_achievement("floor_2"), "D:1 is not floor 2")
debug.goto_place("D:2")
test.regenerate_level()
dcc.enter_floor(true)
check(dcc.has_achievement("floor_2"), "arriving on D:2 unlocks floor_2")
check(not dcc.has_achievement("floor_4"), "D:2 is not floor 4")
check(#dcc.boxes() == 1, "one box: floor_2")

dcc.reset_achievements()
debug.goto_place("D:3")
dcc.enter_floor(true)
check(dcc.has_achievement("floor_2"), "floor_2 is met again after a reset")
debug.goto_place("D:6")
dcc.enter_floor(true)
check(dcc.has_achievement("floor_4") and dcc.has_achievement("floor_6"),
      "skipping ahead to D:6 unlocks floor_4 and floor_6")
check(not dcc.has_achievement("floor_8"), "D:6 is not floor 8")

-- Another branch adds one floor, not its absolute depth (Lair:1 is absolute
-- depth 11); a fall within a branch adds the floors fallen.
dcc.reset_achievements()
debug.goto_place("D:4")
dcc.enter_floor(true)
debug.goto_place("Lair:1")
dcc.enter_floor(true, "branch")
check(dcc.floors_reached() == 5, "D:4 then Lair:1 is floor 5 ("
      .. dcc.floors_reached() .. ")")
check(not dcc.has_achievement("floor_6"), "Lair:1 from D:4 is not floor 6")
debug.goto_place("Lair:3")
dcc.enter_floor(true, "shaft")
check(dcc.floors_reached() == 7 and dcc.has_achievement("floor_6"),
      "a two-floor fall to Lair:3 is floor 7")
debug.goto_place("Lair:2")
dcc.enter_floor(false)
check(dcc.floors_reached() == 7, "stepping back up adds nothing")

-- Off the connected branches depth says nothing: no floor achievements.
dcc.reset_achievements()
debug.goto_place("Abyss:1")
dcc.enter_floor(true)
check(#dcc.achievements() == 0, "arriving in the Abyss unlocks nothing")
debug.goto_place("D:12")
dcc.enter_floor(true)
check(dcc.has_achievement("floor_12") and dcc.has_achievement("floor_10"),
      "arriving on D:12 unlocks floor_10 and floor_12")

-- One-shot across a reload: re-entering after a reload adds nothing.
local before_ids = #dcc.achievements()
local before_boxes = dcc.box_count()
dcc.save_roundtrip()
dcc.enter_floor(true)
dcc.check_achievements()
check(#dcc.achievements() == before_ids and dcc.box_count() == before_boxes,
      "after a reload the same floor unlocks nothing new")
