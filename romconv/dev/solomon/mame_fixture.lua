-- MAME solomon: dump video RAM, palette and the previous frame's sprite RAM
-- (snapshots draw sprites one frame behind) at the frames in env FRAMES,
-- plus a snapshot each. Coin at 2000, start at 2100. Output dir: env OUT.
local frames, last = {}, 0
for f in string.gmatch(os.getenv("FRAMES"), "%d+") do
  frames[tonumber(f)] = true
  last = math.max(last, tonumber(f))
end
local out = os.getenv("OUT")
local cpu = manager.machine.devices[":maincpu"].spaces["program"]
local sys = manager.machine.ioport.ports[":SYSTEM"]
local n, prev_spr = 0, ""

local function dump(a, len)
  local t = {}
  for i = 0, len - 1 do t[#t + 1] = string.format("%02x", cpu:read_u8(a + i)) end
  return table.concat(t)
end

emu.register_frame_done(function()
  n = n + 1
  sys.fields["Coin 1"]:set_value((n >= 2000 and n < 2006) and 1 or 0)
  sys.fields["1 Player Start"]:set_value((n >= 2100 and n < 2106) and 1 or 0)
  if frames[n] then
    local f = io.open(string.format("%s/%d.txt", out, n), "w")
    f:write(dump(0xd000, 0x1000), "\n", dump(0xe400, 0x200), "\n", prev_spr, "\n")
    f:close()
    manager.machine.video:snapshot()
  end
  prev_spr = dump(0xe000, 0x80)
  if n > last then manager.machine:exit() end
end)
