-- MAME chopliftu (System 2, ROT0, per-row scroll): at the frames in env
-- FRAMES dump video RAM e000-efff (VRAM bank 0 = pages 0 and 1, the only
-- ones Choplifter uses), palette RAM and sprite RAM, plus a snapshot each.
-- Bot from the Task 1 probe: from frame 3000 coin + start every 500
-- frames, fly Up (Down 60 of every 150 frames), Left for 700 frames then
-- Right, FIRE every 25 frames, turn (BUTTON2) every 400. Output dir: env OUT.
local frames, last = {}, 0
for f in string.gmatch(os.getenv("FRAMES"), "%d+") do
  frames[tonumber(f)] = true
  last = math.max(last, tonumber(f))
end
local out = os.getenv("OUT")
local cpu = manager.machine.devices[":maincpu"].spaces["program"]
local ports = manager.machine.ioport.ports
local n = 0

local function set(port, name, v) ports[port].fields[name]:set_value(v) end

local function dump(a, len)
  local t = {}
  for i = 0, len - 1 do t[#t + 1] = string.format("%02x", cpu:read_u8(a + i)) end
  return table.concat(t)
end

emu.register_frame_done(function()
  n = n + 1
  if n >= 3000 then
    local t = n - 3000
    local r, l, u, d, b1, b2 = 0, 0, 0, 0, 0, 0
    set(":SYSTEM", "Coin 1", (t % 500 < 10) and 1 or 0)
    set(":SYSTEM", "1 Player Start", (t % 500 >= 60 and t % 500 < 70) and 1 or 0)
    if t >= 100 then
      local c = (t - 100) % 1400
      u = 1
      if c % 150 > 90 then u, d = 0, 1 end
      if c < 700 then l = 1 else r = 1 end
      if t % 25 < 5 then b1 = 1 end
      if t % 400 < 4 then b2 = 1 end
    end
    set(":P1", "P1 Right", r); set(":P1", "P1 Left", l)
    set(":P1", "P1 Up", u); set(":P1", "P1 Down", d)
    set(":P1", "P1 Button 1", b1); set(":P1", "P1 Button 2", b2)
  end
  if frames[n] then
    local f = io.open(string.format("%s/%d.txt", out, n), "w")
    f:write(dump(0xe000, 0x1000), "\n", dump(0xd800, 0x600), "\n", dump(0xd000, 0x800), "\n")
    f:close()
    manager.machine.video:snapshot()
  end
  if n > last then manager.machine:exit() end
end)
