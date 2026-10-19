-- MAME Taito SJ (elevatorb, jungleh) autoboot: at each frame of FRAMES dump the video state of
-- that frame and the one before (<frame>.txt, hex, one line each: tilemaps
-- 1-3, colscroll, sprite RAM, palette, scroll, colorbank, video mode,
-- priority), character RAM of the frame (third line), plus a snapshot.
-- Attract mode only, no input.
local shares = manager.machine.memory.shares
-- env FRAMES: comma separated frame numbers (default: elevatorb's)
local FRAMES, LAST = {}, 0
for n in (os.getenv("FRAMES") or "690,1200,1500,2100"):gmatch("%d+") do
  FRAMES[tonumber(n)] = true
  LAST = math.max(LAST, tonumber(n))
end

local function hex(name, n)
  local s = shares[":" .. name]
  local t = {}
  for a = 0, n - 1 do t[#t + 1] = string.format("%02x", s:read_u8(a)) end
  return table.concat(t)
end

local function dump()
  return hex("videoram_1", 0x400) .. hex("videoram_2", 0x400) .. hex("videoram_3", 0x400) ..
         hex("colscrolly", 0x60) .. hex("spriteram", 0x80) .. hex("paletteram", 0x80) ..
         hex("scroll", 6) .. hex("colorbank", 2) .. hex("video_mode", 1) .. hex("video_priority", 1)
end

local prev = nil
local frame = 0
emu.register_frame_done(function()
  frame = frame + 1
  if FRAMES[frame + 1] then prev = dump() end
  if FRAMES[frame] then
    manager.machine.video:snapshot()
    local f = io.open(string.format("%s/%d.txt", os.getenv("OUT"), frame), "w")
    f:write(dump() .. "\n" .. prev .. "\n" .. hex("characterram", 0x3000) .. "\n")
    f:close()
  end
  if frame == LAST then manager.machine:exit() end
end)
