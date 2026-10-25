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
