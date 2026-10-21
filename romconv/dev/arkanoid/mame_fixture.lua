-- MAME arkangc autoboot: at each frame of FRAMES dump video RAM e000-e83f
-- and the d008 latch of that frame and the one before (<frame>.txt, hex,
-- one line each), plus a snapshot. Attract mode only, no input.
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local FRAMES = { [400] = true, [600] = true, [1300] = true, [2100] = true }
local LAST = 2100
local d008 = 0
local tap = mem:install_write_tap(0xd008, 0xd008, "d008", function(offset, data, mask) d008 = data end)
local prev = nil

local function dump()
  local t = {}
  for a = 0xe000, 0xe83f do t[#t + 1] = string.format("%02x", mem:read_u8(a)) end
  t[#t + 1] = string.format("%02x", d008)
  return table.concat(t)
end

local frame = 0
emu.register_frame_done(function()
  frame = frame + 1
  if FRAMES[frame + 1] then prev = dump() end
  if FRAMES[frame] then
    manager.machine.video:snapshot()
    local f = io.open(string.format("%d.txt", frame), "w")
    f:write(dump() .. "\n" .. prev .. "\n")
    f:close()
  end
  if frame == LAST then tap:remove(); manager.machine:exit() end
end)
