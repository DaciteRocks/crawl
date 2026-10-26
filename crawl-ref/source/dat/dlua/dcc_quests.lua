------------------------------------------------------------------------------
-- dcc_quests.lua: Crawl Live (DCC) floor quests.
--
-- Every connected-branch floor of a Crawl Live game has one quest. The safe
-- room's epilogue (dat/des/dcc/safe_rooms.des) calls dcc_quest_setup() on
-- the player's first arrival, once the floor is fully built and the player
-- is on it: it picks the quest kind by depth (DCC_QUEST_BANDS), the
-- Announcer states it, and a DccQuestMarker at the kiosk listens for the
-- dungeon events that advance it. Completing a quest awards a loot box.
-- Accepting is automatic (every quest is active on arrival); Phase 5b adds
-- the Floor Steward's offer.
--
-- Kinds:
--   kill    kill N of a monster type that lives on this floor, by the player
--           or an ally (DET_MONSTER_DIED; dcc.quest_kill_name() says whether
--           a death counts, by the achievements' kill rule)
--   fetch   pick up a flagged item placed on a far floor cell
--           (DET_ITEM_PICKUP at that cell); if the item went astray (a
--           monster picked it up), holding it when the player arrives on a
--           new floor still completes the quest, and otherwise it expires
--           silently then (dcc_quest_floor_left())
--   reach   stand on a far floor cell (DET_PLAYER_MOVED at that cell)
--   survive spend N turns off the safe room's cells without HP dropping
--           below half; dropping below half starts the count over
--           (DET_TURN_ELAPSED)
--
-- State: dgn.persist.dcc_quests[place], place being the floor's
-- level_id::describe() ("D:3"). dgn.persist is saved with the player, not
-- the level, and is cleared only for a new game, so each floor keys its own
-- entry; only connected-branch floors have safe rooms, so the key is unique.
-- dcc.quest_state() (l-dcc.cc) reads the current floor's entry.
------------------------------------------------------------------------------

-- Tunables. A floor's quest comes from the first band whose max_depth is at
-- least its absolute depth (you.absdepth()). weights pick the kind; a kind
-- that cannot be set up on the floor (no monsters to hunt, no far floor
-- cell) gives way to the next one drawn, and survive always works.
DCC_QUEST_BANDS = {
  { max_depth = 4,  tier = "Bronze",   kill = 3, survive = 150,
    weights = { kill = 3, fetch = 2, reach = 2, survive = 1 } },
  { max_depth = 8,  tier = "Silver",   kill = 4, survive = 200,
    weights = { kill = 3, fetch = 2, reach = 2, survive = 2 } },
  { max_depth = 14, tier = "Gold",     kill = 5, survive = 250,
    weights = { kill = 3, fetch = 2, reach = 2, survive = 2 } },
  { max_depth = 99, tier = "Platinum", kill = 6, survive = 300,
    weights = { kill = 3, fetch = 2, reach = 2, survive = 2 } },
}
DCC_QUEST_KINDS = { "kill", "fetch", "reach", "survive" }
-- The fetch item: a piece of jewellery, which never stacks or merges in the
-- pack, so the flagged one stays apart. (A wand would not do: picking up a
-- wand of a kind already held adds its charges to the held one, and the
-- flag goes with it.)
DCC_QUEST_FETCH_ITEM = "any jewellery"
-- How far out the targets lie, as a percentage of the farthest reachable
-- plain floor cell's walking distance from the kiosk (dcc.quest_spot()).
DCC_QUEST_FETCH_PERCENT = 50
DCC_QUEST_REACH_PERCENT = 75
-- The most aut one DET_TURN_ELAPSED event adds to a survive count. A player
-- turn is far shorter; the one long event is update_level()'s catch-up when
-- the player comes back to a floor, time the player spent elsewhere.
DCC_QUEST_SURVIVE_MAX_TICK = 100

-- The item property that flags the fetch item; its value is the place.
DCC_QUEST_ITEM_PROP = "dcc_quest_item"
-- The marker property naming the place whose quest a marker tracks.
DCC_QUEST_MARKER_PROP = "dcc_quest"

local function quest_table()
  dgn.persist.dcc_quests = dgn.persist.dcc_quests or { }
  return dgn.persist.dcc_quests
end

-- The quest state of a floor (by place name), or nil.
function dcc_quest_state_at(place)
  return quest_table()[place]
end

local function band_for(depth)
  for _, band in ipairs(DCC_QUEST_BANDS) do
    if depth <= band.max_depth then
      return band
    end
  end
  return DCC_QUEST_BANDS[#DCC_QUEST_BANDS]
end

-- The kinds in the order to try them: a weighted draw without replacement.
local function kind_order(weights)
  local pool = { }
  for _, kind in ipairs(DCC_QUEST_KINDS) do
    if (weights[kind] or 0) > 0 then
      table.insert(pool, kind)
    end
  end
  local order = { }
  while #pool > 0 do
    local total = 0
    for _, kind in ipairs(pool) do
      total = total + weights[kind]
    end
    local roll = crawl.random2(total)
    for i, kind in ipairs(pool) do
      roll = roll - weights[kind]
      if roll < 0 then
        table.insert(order, kind)
        table.remove(pool, i)
        break
      end
    end
  end
  return order
end

-- "northeast", "south", ... for an offset (y grows to the south).
local function compass(dx, dy)
  local ns, ew = "", ""
  if dy < 0 and -dy * 2 >= math.abs(dx) then
    ns = "north"
  elseif dy > 0 and dy * 2 >= math.abs(dx) then
    ns = "south"
  end
  if dx > 0 and dx * 2 >= math.abs(dy) then
    ew = "east"
  elseif dx < 0 and -dx * 2 >= math.abs(dy) then
    ew = "west"
  end
  return ns .. ew
end

local function where_from(kx, ky, x, y)
  local dx, dy = x - kx, y - ky
  return "about " .. math.max(math.abs(dx), math.abs(dy))
         .. " squares to the " .. compass(dx, dy)
end

-- The fetch item in the player's pack for a floor's quest, or nil.
local function held_quest_item(place)
  for _, it in ipairs(items.inventory()) do
    if dgn.item_property(it, DCC_QUEST_ITEM_PROP, "s") == place then
      return it
    end
  end
  return nil
end

-- Builders: each returns a new quest state, or nil if this floor cannot
-- hold that kind of quest.
local builders = { }

function builders.kill(band, kx, ky, place)
  local targets = dcc.quest_kill_targets()
  if #targets == 0 then
    return nil
  end
  local target = targets[crawl.random2(#targets) + 1]
  local need = math.min(target.count, band.kill)
  local what = need == 1 and ("one " .. target.name)
               or (need .. " " .. target.plural)
  return { kind = "kill", target = target.name, plural = target.plural,
           need = need, have = 0,
           text = "Floor quest: the locals are getting cocky. Kill " .. what
                  .. " and a " .. band.tier .. " box is yours." }
end

function builders.fetch(band, kx, ky, place)
  local x, y = dcc.quest_spot(kx, ky, DCC_QUEST_FETCH_PERCENT)
  if not x then
    return nil
  end
  dgn.create_item(x, y, DCC_QUEST_FETCH_ITEM)
  local it = dgn.items_at(x, y)[1]
  if not it then
    return nil
  end
  dgn.item_property_set(it, DCC_QUEST_ITEM_PROP, "s", place)
  local name = it.name("a")
  return { kind = "fetch", x = x, y = y, item = name,
           text = "Floor quest: somebody dropped " .. name .. " "
                  .. where_from(kx, ky, x, y) .. " of the safe room. Pick it"
                  .. " up and a " .. band.tier .. " box is yours." }
end

function builders.reach(band, kx, ky, place)
  local x, y = dcc.quest_spot(kx, ky, DCC_QUEST_REACH_PERCENT)
  if not x then
    return nil
  end
  return { kind = "reach", x = x, y = y,
           text = "Floor quest: the cameras want a shot of you standing on"
                  .. " a spot " .. where_from(kx, ky, x, y) .. " of the safe"
                  .. " room. Get there and a " .. band.tier
                  .. " box is yours." }
end

function builders.survive(band, kx, ky, place)
  return { kind = "survive", need = band.survive, have_aut = 0,
           text = "Floor quest: stay out of the safe room and above half"
                  .. " health for " .. band.survive .. " turns. Pull it off"
                  .. " and a " .. band.tier .. " box is yours." }
end

local function complete_quest(q, place)
  q.done = true
  if q.kind == "fetch" then
    local it = held_quest_item(place)
    if it then
      dgn.item_property_remove(it, DCC_QUEST_ITEM_PROP)
    end
  end
  dcc.announce("Quest complete! One " .. q.tier .. " box, coming right up."
               .. " Open it at any safe room's kiosk.")
  dcc.quest_reward(q.tier)
end

------------------------------------------------------------------------------
-- DccQuestMarker: tracks one floor's quest. It holds only the place name;
-- the state lives in dgn.persist (see the top of this file).

DccQuestMarker = { CLASS = "DccQuestMarker" }
DccQuestMarker.__index = DccQuestMarker

function DccQuestMarker:new(place)
  local m = { place = place }
  setmetatable(m, self)
  return m
end

function DccQuestMarker:state()
  return dcc_quest_state_at(self.place)
end

function DccQuestMarker:activate(marker, verbose)
  local q = self:state()
  if not q or q.done or q.expired then
    return
  end
  if q.kind == "kill" then
    dgn.register_listener(dgn.dgn_event_type("monster_dies"), marker)
  elseif q.kind == "survive" then
    dgn.register_listener(dgn.dgn_event_type("turn"), marker)
  elseif q.kind == "reach" then
    dgn.register_listener(dgn.dgn_event_type("player_move"), marker,
                          q.x, q.y)
  elseif q.kind == "fetch" then
    dgn.register_listener(dgn.dgn_event_type("item_pickup"), marker,
                          q.x, q.y)
  end
end

function DccQuestMarker:event(marker, ev)
  local q = self:state()
  if not q or q.done or q.expired then
    return
  end
  local t = ev:type()
  if q.kind == "kill" and t == dgn.dgn_event_type("monster_dies") then
    if dcc.quest_kill_name(ev:arg1()) ~= q.target then
      return
    end
    q.have = q.have + 1
    if q.have >= q.need then
      complete_quest(q, self.place)
    else
      dcc.announce(q.have .. " of " .. q.need .. " " .. q.plural
                   .. " down. Keep going.")
    end
  elseif q.kind == "survive" and t == dgn.dgn_event_type("turn") then
    local hp, mhp = you.hp()
    if hp * 2 < mhp then
      if q.have_aut > 0 then
        q.have_aut = 0
        dcc.announce("Below half health! Your survival count starts over.")
      end
      return
    end
    local x, y = you.pos()
    if dcc.is_safe_cell(x, y) then
      return
    end
    q.have_aut = q.have_aut + math.min(ev:ticks(), DCC_QUEST_SURVIVE_MAX_TICK)
    if q.have_aut >= q.need * 10 then
      complete_quest(q, self.place)
    end
  elseif q.kind == "reach" and t == dgn.dgn_event_type("player_move") then
    local x, y = ev:pos()
    if x == q.x and y == q.y then
      complete_quest(q, self.place)
    end
  elseif q.kind == "fetch" and t == dgn.dgn_event_type("item_pickup") then
    -- arg2 is -1 when the player picked something up (a monster's index
    -- otherwise).
    if ev:arg2() == -1 and held_quest_item(self.place) then
      complete_quest(q, self.place)
    end
  end
end

function DccQuestMarker:property(marker, pname)
  if pname == DCC_QUEST_MARKER_PROP then
    return self.place
  end
  return ""
end

function DccQuestMarker:describe(marker)
  local q = self:state()
  return "dcc quest " .. self.place .. ": " .. (q and q.kind or "none")
end

function DccQuestMarker:write(marker, th)
  file.marshall(th, self.place)
end

function DccQuestMarker:read(marker, th)
  self.place = file.unmarshall_string(th)
  setmetatable(self, DccQuestMarker)
  return self
end

-- The marker tracking a floor's quest on the current level, or nil.
local function quest_marker(place)
  return dgn.find_markers_by_prop(DCC_QUEST_MARKER_PROP, place)[1]
end

------------------------------------------------------------------------------
-- Entry points.

-- Set up the current floor's quest: called from the safe room's epilogue on
-- the first arrival. e is the map environment (unused for now). A forced
-- kind (dcc_quest_debug().setup()) is tried first; survive is the fallback
-- when it cannot be set up.
function dcc_quest_setup(e, forced_kind)
  if not dcc.is_active() then
    return
  end
  local place = you.where()
  local kx, ky = dgn.find_marker_position_by_prop("dcc_kiosk", "1")
  if not kx then
    return
  end
  -- A repeated setup (scripts) replaces the floor's quest and marker.
  local old = quest_marker(place)
  if old then
    local old_q = dcc_quest_state_at(place)
    dgn.remove_listener(old)
    if old_q and old_q.x then
      dgn.remove_listener(old, old_q.x, old_q.y)
    end
    old:remove()
  end

  local band = band_for(you.absdepth())
  local order = forced_kind and { forced_kind } or kind_order(band.weights)
  local q
  for _, kind in ipairs(order) do
    q = builders[kind](band, kx, ky, place)
    if q then
      break
    end
  end
  q = q or builders.survive(band, kx, ky, place)
  q.tier = band.tier
  q.done = false
  q.expired = false
  quest_table()[place] = q

  local m = DccQuestMarker:new(place)
  dgn.register_lua_marker(kx, ky, m)
  -- This arrival's markers are already active, so start this one here.
  m:activate(quest_marker(place), false)
  dcc.announce(q.text)
end

-- The player has arrived on a new floor, leaving place behind
-- (dcc_quest_floor_left() in dcc-quest.cc): settle a fetch quest left open
-- there. Holding the item completes it; otherwise it expires silently.
function dcc_quest_floor_left(place)
  local q = dcc_quest_state_at(place)
  if not q or q.kind ~= "fetch" or q.done or q.expired then
    return
  end
  if held_quest_item(place) then
    complete_quest(q, place)
  else
    q.expired = true
  end
end

-- Script helpers for the current floor's quest.
function dcc_quest_debug()
  local place = you.where()
  return {
    state = function() return dcc_quest_state_at(place) end,
    setup = function(kind) dcc_quest_setup(nil, kind) end,
    force_complete = function()
      local q = dcc_quest_state_at(place)
      if q and not q.done then
        complete_quest(q, place)
        return true
      end
      return false
    end,
    marker = function() return quest_marker(place) end,
  }
end
