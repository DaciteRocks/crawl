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
-- may leave, and move between safe cells, while a hostile outside may not
-- step onto those cells.
local turned = dgn.create_monster(32, 34, "orc")
check(turned ~= nil, "a second hostile stands outside the room")
dgn.fprop_changed(32, 34, "dcc_safe")
dgn.fprop_changed(31, 34, "dcc_safe")
check(dcc.is_safe_cell(32, 34) and dcc.is_safe_cell(31, 34),
      "two outside cells are made safe under the second hostile")
check(dcc.monster_can_step(32, 34, 33, 34),
      "a hostile on a safe cell can leave it")
check(dcc.monster_can_step(32, 34, 31, 34),
      "a hostile on a safe cell can move to another safe cell")
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
  orc.run_ai()
  check(not dcc.is_safe_cell(orc.x, orc.y),
        "the hostile's AI keeps it out of the safe room (turn " .. i .. ")")
end
local spots = {}
local nspots = 0
for i = 1, 20 do
  orc.random_teleport()
  check(not dcc.is_safe_cell(orc.x, orc.y),
        "a random teleport does not land the hostile in the room (" .. i .. ")")
  local key = orc.x .. "," .. orc.y
  if not spots[key] then
    spots[key] = true
    nspots = nspots + 1
  end
end
check(nspots > 1, "the random teleports really moved the hostile")
