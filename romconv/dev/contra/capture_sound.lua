-- MAME contra sound commands, two modes:
--   MODE=log   attract mode, every write to the sound latch (001c) to
--              sound_log.txt: "<frame> <command hex>", until LAST
--   MODE=sweep demo sounds off; per command (CMDS=hex,..., default 00-ff):
--              stop (00), then the command through latch + sound IRQ,
--              SECONDS apart (default 4); stamps in sweep.txt. Record audio
--              with -wavwrite sweep.wav -samplerate 24000 (-sound none off)
local MODE = os.getenv("MODE") or "log"
local LAST = tonumber(os.getenv("LAST") or "3300")
local START = 600
local STEP = 60 * tonumber(os.getenv("SECONDS") or "4")
local CMDS = {}
for hex in string.gmatch(os.getenv("CMDS") or "", "%x+") do CMDS[#CMDS + 1] = tonumber(hex, 16) end
if #CMDS == 0 then for c = 0, 255 do CMDS[#CMDS + 1] = c end end

local sp = manager.machine.devices[":maincpu"].spaces["program"]
local f, tap, out = 0, nil, nil

local function send(cmd)
  sp:write_u8(0x001c, cmd)   -- latch
  sp:write_u8(0x001a, 0)     -- sound CPU IRQ
end

if MODE == "sweep" then
  manager.machine.ioport.ports[":DSW2"].fields["Demo Sounds"].user_value = 0x80
  out = io.open("sweep.txt", "w")
else
  out = io.open("sound_log.txt", "w")
end

SUB = emu.add_machine_frame_notifier(function()
  f = f + 1
  if MODE == "log" then
    if f == 1 then
      tap = sp:install_write_tap(0x001c, 0x001c, "latch", function(offset, data, mask)
        out:write(string.format("%d %02x\n", f, data & 0xff))
      end)
    end
    if f == LAST then out:close() manager.machine:exit() end
    return
  end

  if f < START then return end
  local i = (f - START) // STEP + 1
  local phase = (f - START) % STEP
  if i > #CMDS then out:close() manager.machine:exit() return end
  if phase == 0 then send(0x00) end
  if phase == 2 then
    send(CMDS[i])
    out:write(string.format("%d %02x\n", f, CMDS[i]))
  end
end)
