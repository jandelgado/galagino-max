-- Bubble Bobble sound command capture (MAME boblbobl), two modes:
--   MODE=log   play a game with scripted inputs, log every sound latch
--              ($FA00) and sound reset ($FA03) write to log.txt
--   MODE=sweep demo sounds off; per command 0x00..0xff: sound reset,
--              the driver init commands the game sends after its own
--              reset (ef, 00; without them the driver stays mute), then
--              the command; every SWEEP_FRAMES, stamps in sweep.txt; record
--              audio with -wavwrite sweep.wav -samplerate 24000
-- Optional for sweep: CMDS=07,08,... (hex, default all 256) and
-- SECONDS=n per command (default 3), e.g. 25 s clips of music commands.
-- Run from a dir with boblbobl.zip + bublbobl.zip (see trace_idle.lua
-- for the env vars), absolute -autoboot_script path, -noautosave.
local MODE = os.getenv("MODE") or "log"
local LOG_FRAMES = 12000
local SWEEP_START = 600
local CMD_AT = 30
local SWEEP_FRAMES = CMD_AT + 60 * (tonumber(os.getenv("SECONDS") or "3"))
local CMDS = {}
for hex in string.gmatch(os.getenv("CMDS") or "", "%x+") do
  CMDS[#CMDS + 1] = tonumber(hex, 16)
end
if #CMDS == 0 then
  for c = 0, 255 do CMDS[#CMDS + 1] = c end
end
local INIT_AT = 17       -- game: reset, 17 frames later ef, then 00
local INIT2_AT = 20      -- one write per frame: the latch holds one byte

local space = manager.machine.devices[":maincpu"].spaces["program"]
local in0 = manager.machine.ioport.ports[":IN0"]
local frame, log = 0, {}
local injecting = false

local function press(name, v) in0.fields[name]:set_value(v) end

local function save(name)
  local f = io.open(name, "w")
  f:write(table.concat(log, "\n"), "\n")
  f:close()
end

-- every latch / reset write; "game" marks writes not made by this script
TAP = space:install_write_tap(0xfa00, 0xfa7f, "snd", function(offset, data, mask)
  local reg = offset & 3
  if reg == 0 or reg == 3 then
    log[#log + 1] = string.format("%d %s %d %02x", frame, injecting and "inj" or "game", reg, data)
  end
end)

if MODE == "sweep" then
  manager.machine.ioport.ports[":DSW0"].fields["Demo Sounds"].user_value = 0
end

local next_cmd = 1
SUB = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  if MODE == "log" then
    if frame == 300 then press("Coin 1", 1)
    elseif frame == 306 then press("Coin 1", 0); press("1 Player Start", 1)
    elseif frame == 312 then press("1 Player Start", 0)
    elseif frame > 400 then
      -- wander and shoot: bubble, jump, left, right in turns
      local k = (frame // 15) % 6
      press("P1 Button 1", (k == 0 or k == 3) and 1 or 0)
      press("P1 Button 2", k == 1 and 1 or 0)
      press("P1 Left", (k == 2 or k == 3) and 1 or 0)
      press("P1 Right", (k == 4 or k == 5) and 1 or 0)
    end
    if frame == LOG_FRAMES then
      save("log.txt")
      manager.machine:exit()
    end
    return
  end

  if frame < SWEEP_START then
    return
  end

  local phase = (frame - SWEEP_START) % SWEEP_FRAMES
  if phase == 0 then
    if next_cmd > #CMDS then
      save("sweep.txt")
      manager.machine:exit()
      return
    end
    injecting = true
    space:write_u8(0xfa03, 1)
    space:write_u8(0xfa03, 0)
    injecting = false
    log[#log + 1] = string.format("%d reset", frame)
  elseif phase == INIT_AT then
    injecting = true
    space:write_u8(0xfa00, 0xef)
    injecting = false
  elseif phase == INIT2_AT then
    injecting = true
    space:write_u8(0xfa00, 0x00)
    injecting = false
  elseif phase == CMD_AT then
    injecting = true
    space:write_u8(0xfa00, CMDS[next_cmd])
    injecting = false
    log[#log + 1] = string.format("%d cmd %02x", frame, CMDS[next_cmd])
    next_cmd = next_cmd + 1
  end
end)
