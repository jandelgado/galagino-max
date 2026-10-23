-- Dump playfield RAM (8 KB) every 30 frames to $MARBLE_OUT/pfseq.bin; exit after
-- $MARBLE_FRAMES frames (default 3600). Without $MARBLE_LEVEL: attract mode.
-- With $MARBLE_LEVEL (0-5): coin + 1 player start, race forced to that level, then the
-- camera is driven down, up and down the whole race (forward scroll covers the full
-- level in ~100 frames at max speed, so ~2200 frames suffice).
--
-- usage (absolute script path, run in a scratch dir):
--   MARBLE_OUT=$PWD MARBLE_LEVEL=2 MARBLE_FRAMES=2200 mame marble -video none -sound none \
--     -nothrottle -noautosave -skip_gameinfo -autoboot_script /abs/path/mame_pf.lua
local out_dir = os.getenv("MARBLE_OUT")
local frames = tonumber(os.getenv("MARBLE_FRAMES") or "3600")
local level = tonumber(os.getenv("MARBLE_LEVEL"))

local RACE = 0x400394        -- word: current race 0-5, read by 0x16ec6
local MARBLE_SY = 0x400038   -- word: player 1 slot (0x400018) + 0x20, screen y the camera follows
local SY_FORWARD = 0x00      -- below camera centre 0x48 - 0x30: max speed down the race
local SY_BACKWARD = 0xF0     -- above 0xa8 + 0x30: max speed up the race
local COIN_FRAME, START_FRAME, DRIVE_FRAME, PHASE = 300, 400, 700, 500

local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ports = manager.machine.ioport.ports
local coin = ports[":1820"].fields["Coin 1"]
local start = ports[":F60000"].fields["1 Player Start"]
local sy = SY_FORWARD
local f = 0
local out = io.open(out_dir .. "/pfseq.bin", "wb")

if level then
  -- taps must stay referenced, else they are garbage collected
  RACE_TAP = mem:install_write_tap(RACE, RACE + 1, "race", function() return level end)
end

SUB = emu.add_machine_frame_notifier(function()
  f = f + 1
  if level then
    coin:set_value((f >= COIN_FRAME and f < COIN_FRAME + 10) and 1 or 0)
    start:set_value((f >= START_FRAME and f < START_FRAME + 10) and 1 or 0)
    if f == DRIVE_FRAME then
      SY_TAP = mem:install_read_tap(MARBLE_SY, MARBLE_SY + 1, "sy", function() return sy end)
    end
    local phase = (f - DRIVE_FRAME) // PHASE
    sy = (phase % 2 == 1) and SY_BACKWARD or SY_FORWARD
  end
  if f % 30 == 0 then
    for a = 0xa00000, 0xa01fff do out:write(string.char(mem:read_u8(a))) end
  end
  if f == frames then out:close(); manager.machine:exit() end
end)
