-- Marble physics oracle: start race $MARBLE_LEVEL (0-5), drive the trackball from a
-- script and dump work RAM + playfield RAM at every logic tick of the player marble.
--
-- Tick trigger: the object physics update 0x121b8 starts with
-- move.l $c(a2),$400684 (object x). A write tap on 0x400684 whose data equals the
-- player's x high word (0x400024) snapshots RAM once per logic tick, at the entry
-- of the player's physics update (after input 0x17f66 and speed cap 0x26196).
-- At most one snapshot per frame: the player slot is the first object updated.
--
-- Trackball: read taps on 0xf20000-0xf20003 return two free-running 8 bit counters
-- (MAME trakball_r: port 0 = x + y, port 2 = x - y of the 45 degree rotated ball).
-- $MARBLE_INPUT: comma separated "frames:d0:d1" segments, counter delta per 60 Hz
-- frame for port 0 and port 2, starting at frame $MARBLE_INPUT_FRAME (default 700).
--
-- Output $MARBLE_OUT/trace.bin, records of 4 + 4 + 8192 + 12288 bytes:
--   "TICK", frame (u32 BE), work RAM 0x400000-0x401fff, video RAM 0xa00000-0xa02fff
--   (playfield RAM + motion object RAM, both hold terrain cells, see MARBLE.md).
-- Exits after $MARBLE_FRAMES frames.
-- Level objects (MARBLE.md "Level objects"): work RAM holds the slots 0x400a9c, dynamic
-- terrain codes 0x40076e and the occlusion mask entries (object +0x38); video RAM holds the
-- motion object banks 0xa02000 (0) and 0xa02200 (1, the game builds into one while the
-- other is shown, 0x4003ae).
-- $MARBLE_CAMERA "from:frames": from frame `from` (0 = off) a read tap on the marble screen
-- y (0x400038) returns 0 for `frames` frames (camera scrolls down the race at full speed,
-- level objects spawn), then the real value. Readers other than the camera see it too
-- (projection history +0x22, occlusion masks): use for slot traces only.
-- $MARBLE_SHOTS "from:to:step": PNG snapshot (-snapshot_directory) at those frames, file
-- n = n-th listed frame; shots.txt maps them, shots.bin holds a trace record of each
-- (frame end, the state the PNG shows).
--
-- shots_pal.bin holds palette RAM 0xb00000-0xb007ff of each shot (same order).
-- $MARBLE_PLACE "frame:x:y:z[:zone],...": at that frame put player 1 at pixel (x, y), height
-- z (z word - 0x4000, e.g. -120), velocity 0, last ground = there, optional zone +0x1b/+0x1d
-- (teleport next to a level object for render checks; z from tools/marble_sim.py terrain).
-- usage (absolute script path, scratch dir):
--   MARBLE_OUT=$PWD MARBLE_LEVEL=0 MARBLE_FRAMES=1500 MARBLE_INPUT=120:0:0,60:4:0 \
--   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy mame marble -rompath romszip -video none \
--     -sound none -nothrottle -noautosave -skip_gameinfo -cfg_directory "$PWD/cfg" \
--     -nvram_directory "$PWD/nv" -autoboot_script /abs/path/mame_trace.lua
local out_dir = os.getenv("MARBLE_OUT")
local frames = tonumber(os.getenv("MARBLE_FRAMES") or "1500")
local level = tonumber(os.getenv("MARBLE_LEVEL") or "0")
local input_frame = tonumber(os.getenv("MARBLE_INPUT_FRAME") or "700")
local script = os.getenv("MARBLE_INPUT") or ""
local shot_from, shot_to, shot_step = (os.getenv("MARBLE_SHOTS") or "0:-1:1"):match("(%d+):(%-?%d+):(%d+)")
shot_from, shot_to, shot_step = tonumber(shot_from), tonumber(shot_to), tonumber(shot_step)
local cam_from, cam_frames = (os.getenv("MARBLE_CAMERA") or "0:0"):match("(%d+):(%d+)")
cam_from, cam_frames = tonumber(cam_from), tonumber(cam_frames)

local RACE = 0x400394
local TICK_ADDR = 0x400684
local PLAYER_X = 0x400024
local COIN_FRAME, START_FRAME = 300, 400

local places = {}
for fr, x, y, z, zone in (os.getenv("MARBLE_PLACE") or ""):gmatch("(%d+):(%-?%d+):(%-?%d+):(%-?%d+):?(%d*)") do
  places[tonumber(fr)] = { tonumber(x), tonumber(y), tonumber(z), tonumber(zone) }
end

local segs = {}
for n, a, b in script:gmatch("(%-?%d+):(%-?%d+):(%-?%d+)") do
  segs[#segs + 1] = { tonumber(n), tonumber(a), tonumber(b) }
end

local mem = manager.machine.devices[":maincpu"].spaces["program"]
local ports = manager.machine.ioport.ports
local coin = ports[":1820"].fields["Coin 1"]
local start = ports[":F60000"].fields["1 Player Start"]
local out = io.open(out_dir .. "/trace.bin", "wb")
local shots = io.open(out_dir .. "/shots.txt", "w")
local shots_bin = io.open(out_dir .. "/shots.bin", "wb")
local shots_pal = io.open(out_dir .. "/shots_pal.bin", "wb")
local f = 0
local cnt = { 0, 0 }

local function input_delta(frame)
  local t = frame - input_frame
  if t < 0 then return 0, 0 end
  for _, s in ipairs(segs) do
    if t < s[1] then return s[2], s[3] end
    t = t - s[1]
  end
  return 0, 0
end

local function dump(base, len, file)
  local t = {}
  for a = base, base + len - 1, 4 do
    t[#t + 1] = string.pack(">I4", mem:read_u32(a))
  end
  (file or out):write(table.concat(t))
end

-- taps must stay referenced, else they are garbage collected
RACE_TAP = mem:install_write_tap(RACE, RACE + 1, "race", function() return level end)
BALL_TAP = mem:install_read_tap(0xf20000, 0xf20003, "ball", function(offset, data, mask)
  local v = cnt[(offset & 2) == 0 and 1 or 2] & 0xff
  return (data & 0xff00) | v
end)
CAM_TAP = mem:install_read_tap(0x400038, 0x400039, "sy", function(offset, data, mask)
  if cam_from > 0 and f >= cam_from and f < cam_from + cam_frames then return 0 end
  return data
end)
local last_tick = -1
TICK_TAP = mem:install_write_tap(TICK_ADDR, TICK_ADDR + 1, "tick", function(offset, data, mask)
  -- player slot 0x400018 is updated first; later objects with the same x word are skipped
  if data ~= mem:read_u16(PLAYER_X) or last_tick == f then return end
  last_tick = f
  out:write("TICK", string.pack(">I4", f))
  dump(0x400000, 0x2000)
  dump(0xa00000, 0x3000)
end)

SUB = emu.add_machine_frame_notifier(function()
  f = f + 1
  coin:set_value((f >= COIN_FRAME and f < COIN_FRAME + 10) and 1 or 0)
  start:set_value((f >= START_FRAME and f < START_FRAME + 10) and 1 or 0)
  local p = places[f]
  if p then
    local P = 0x400018
    for o = 0, 8, 4 do mem:write_u32(P + o, 0) end
    mem:write_u32(P + 0xc, p[1] << 16); mem:write_u32(P + 0x10, p[2] << 16)
    mem:write_u32(P + 0x14, (p[3] + 0x4000) << 16); mem:write_u32(P + 0x2a, (p[3] + 0x4000) << 16)
    mem:write_u16(P + 0x32, p[1] >> 3); mem:write_u16(P + 0x34, p[2] >> 3)
    mem:write_u8(P + 0x36, 0)
    if p[4] then mem:write_u8(P + 0x1b, p[4]); mem:write_u8(P + 0x1d, p[4]) end
  end
  local d0, d1 = input_delta(f)
  cnt[1] = cnt[1] + d0
  cnt[2] = cnt[2] + d1
  if f >= shot_from and f <= shot_to and (f - shot_from) % shot_step == 0 then
    manager.machine.video:snapshot()
    shots:write(f, "\n")
    shots_bin:write("TICK", string.pack(">I4", f))
    dump(0x400000, 0x2000, shots_bin)
    dump(0xa00000, 0x3000, shots_bin)
    dump(0xb00000, 0x800, shots_pal)
  end
  if f == frames then
    out:close(); shots:close(); shots_bin:close(); shots_pal:close(); manager.machine:exit()
  end
end)
