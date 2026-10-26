------------------------------------------------------------------------------
-- dcc.lua: Crawl Live (DCC) Lua callbacks for the safe-room vaults.
--
-- Loaded by dat/des/dcc/safe_rooms.des (crawl_require), so the callbacks
-- below exist whenever a saved level's markers are read back. The dcc table
-- they call is the dlua library in l-dcc.cc.
------------------------------------------------------------------------------

-- The loot box kiosk: a function_at_spot marker on the safe room's K cell
-- calls this whenever the player moves onto it, by any means (the marker
-- fires on every player move). The C++ side does the rest
-- (dcc-boxes.cc dcc_kiosk_stepped()): only a deliberate step opens boxes,
-- asking first; travel, autoexplore, blinks and teleports pass silently.
function dcc_kiosk_activate(data, triggerable, triggerer, marker, ev)
  dcc.kiosk_stepped()
end

-- The Floor Steward: a function_in_los marker on the steward's cell in the
-- safe room calls this on every view update that has the cell in sight.
-- The C++ side (dcc-quest.cc dcc_steward_seen()) only notes it: the offer
-- (dcc_quest_offer() in dcc_quests.lua) is made from the main loop, just
-- before the player's next command, and only once per floor, so once the
-- quest has been offered (or before it is set up) there is nothing to note.
function dcc_steward_seen(data, triggerable, triggerer, marker, ev)
  local q = dcc.quest_state()
  if q and not q.offered then
    dcc.steward_seen()
  end
end
