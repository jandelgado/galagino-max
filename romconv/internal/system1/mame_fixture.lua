-- MAME System 1 ROT0 games (flickys2, pitfall2u): at the frames in env
-- FRAMES dump video RAM, palette RAM and sprite RAM (System 1 draws sprites
-- of the same frame, VIDEO_ALWAYS_UPDATE), plus a snapshot each. Coin at
-- 200, start at 260, then the stick cycles right / left / idle every 180
-- frames with FIRE every 40. Output dir: env OUT.
local frames, last = {}, 0
for f in string.gmatch(os.getenv("FRAMES"), "%d+") do
  frames[tonumber(f)] = true
  last = math.max(last, tonumber(f))
end
local out = os.getenv("OUT")
local cpu = manager.machine.devices[":maincpu"].spaces["program"]
local n = 0

local function field(name)
  for _, p in pairs(manager.machine.ioport.ports) do
    for fname, fl in pairs(p.fields) do
      if fname == name then return fl end
    end
  end
end
local coin, start = field("Coin 1"), field("1 Player Start")
local right, left, fire = field("P1 Right"), field("P1 Left"), field("P1 Button 1")

local function dump(a, len)
  local t = {}
  for i = 0, len - 1 do t[#t + 1] = string.format("%02x", cpu:read_u8(a + i)) end
  return table.concat(t)
end

emu.register_frame_done(function()
  n = n + 1
  local phase = math.floor((n - 400) / 180) % 3
  coin:set_value((n >= 200 and n < 210) and 1 or 0)
  start:set_value((n >= 260 and n < 270) and 1 or 0)
  right:set_value((n > 400 and phase == 0) and 1 or 0)
  left:set_value((n > 400 and phase == 1) and 1 or 0)
  fire:set_value((n > 400 and n % 40 < 20) and 1 or 0)
  if frames[n] then
    local f = io.open(string.format("%s/%d.txt", out, n), "w")
    f:write(dump(0xe000, 0x1000), "\n", dump(0xd800, 0x600), "\n", dump(0xd000, 0x800), "\n")
    f:close()
    manager.machine.video:snapshot()
  end
  if n > last then manager.machine:exit() end
end)
