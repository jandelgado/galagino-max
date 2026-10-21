-- Marble sound capture (MARBLE.md "Sound"). Race 0 forced, coin + start, then from frame
-- START every WIN frames: command 0x00 (music off), 2 frames later the next command of
-- CMDS; every YM2151 / POKEY write of the sound CPU is logged with its time. One file per
-- command: $OUT/cmd_XX.txt:
--   init ym <reg> <val>     YM registers written since boot, before the command
--   init pokey <reg> <val>  POKEY registers 0-8
--   ym <t> <reg> <val>      t: 60 Hz frames since the command (float)
--   pokey <t> <reg> <val>
--   game <t> <cmd> 0        a command the game sent itself (its own sounds start there)
-- MODE=log instead logs the main CPU's commands (f <frame> cmd <hex>) and 0x400390 changes
-- (f <frame> phase <n>) to $OUT/log.txt. MAME drops taps when the slapstic remaps memory:
-- all taps are reinstalled every frame.
local MODE = os.getenv("MODE") or "capture"
local OUT = os.getenv("OUT")
local FRAMES = tonumber(os.getenv("FRAMES") or "6000")
local START = tonumber(os.getenv("START") or "1400")
local WIN = tonumber(os.getenv("WIN") or "150")
local COIN = 1000
local CMDS = {}
for h in string.gmatch(os.getenv("CMDS") or "", "%x+") do CMDS[#CMDS + 1] = tonumber(h, 16) end

local main = manager.machine.devices[":maincpu"].spaces["program"]
local snd = manager.machine.devices[":audiocpu"].spaces["program"]
local ports = manager.machine.ioport.ports
local frame, taps, log = 0, {}, {}
local ym_reg, ym_shadow, pk_shadow = 0, {}, {}
local cur = nil
local frame_t0, frame_len = 0, 1 / 60
local last_phase = -1

local function now_frames()
  if not cur then return 0 end
  local dt = manager.machine.time:as_double() - frame_t0
  return (frame - cur.at) + dt / frame_len
end

local function on_cmd(o, d, m)
  if (m & 0xff) == 0 then return end
  local c = d & 0xff
  if MODE == "log" then
    log[#log + 1] = string.format("f %d cmd %02x", frame, c)
  elseif cur and c ~= 0x03 and c ~= 0x07 and not cur.injecting then
    cur.lines[#cur.lines + 1] = string.format("game %.4f %d 0", now_frames(), c)
  end
end

local function on_chip(o, d, m)
  local a = o & ~0x278e
  if a == 0x1800 then ym_reg = d; return end
  if a == 0x1801 then
    ym_shadow[ym_reg] = d
    if cur then cur.lines[#cur.lines + 1] = string.format("ym %.4f %d %d", now_frames(), ym_reg, d) end
    return
  end
  local p = o & ~0x2780
  if p >= 0x1870 and p <= 0x187f then
    pk_shadow[p - 0x1870] = d
    if cur then cur.lines[#cur.lines + 1] = string.format("pokey %.4f %d %d", now_frames(), p - 0x1870, d) end
  end
end

local function retap()
  for _, t in ipairs(taps) do t:remove() end
  taps = {main:install_write_tap(0xfe0000, 0xfe0001, "cmd", on_cmd),
          snd:install_write_tap(0x1000, 0x3fff, "chips", on_chip),
          MODE == "capture" and main:install_write_tap(0x400394, 0x400395, "race", function(o, d, m) return 0 end) or nil}
end
retap()

local function flush()
  if not cur then return end
  local f = io.open(string.format("%s/cmd_%02x.txt", OUT, cur.cmd), "w")
  f:write(table.concat(cur.lines, "\n"), "\n")
  f:close()
  cur = nil
end

local function begin(cmd)
  cur = {cmd = cmd, at = frame, lines = {}}
  for r = 0, 255 do
    if ym_shadow[r] then cur.lines[#cur.lines + 1] = string.format("init ym %d %d", r, ym_shadow[r]) end
  end
  for r = 0, 8 do cur.lines[#cur.lines + 1] = string.format("init pokey %d %d", r, pk_shadow[r] or 0) end
  cur.injecting = true
  main:write_u8(0xfe0001, cmd)
  cur.injecting = false
end

-- NOCOIN=1: another script (mame_trace.lua) inserts coin and start
local function press(port, field, on)
  if os.getenv("NOCOIN") == "1" then return end
  local p = ports[port]
  if p and p.fields[field] then p.fields[field]:set_value(on and 1 or 0) end
end

emu.register_frame_done(function()
  local t = manager.machine.time:as_double()
  frame_len = t - frame_t0
  frame_t0 = t
  frame = frame + 1
  retap()
  press(":1820", "Coin 1", frame >= COIN and frame < COIN + 15)
  press(":F60000", "1 Player Start", frame >= COIN + 300 and frame < COIN + 315)
  local ph = main:read_u16(0x400390)
  if MODE == "log" and ph ~= last_phase then
    log[#log + 1] = string.format("f %d phase %d", frame, ph)
    last_phase = ph
  end
  if MODE == "capture" then
    local k = frame - START
    if k >= 0 and k % WIN == 0 then
      flush()
      if k // WIN < #CMDS then main:write_u8(0xfe0001, 0x00) end
    elseif k >= 0 and k % WIN == 2 and k // WIN < #CMDS then
      begin(CMDS[k // WIN + 1])
    end
  end
  if frame >= FRAMES then
    flush()
    if MODE == "log" then
      local f = io.open(OUT .. "/log.txt", "w"); f:write(table.concat(log, "\n"), "\n"); f:close()
    end
    manager.machine:exit()
  end
end)
