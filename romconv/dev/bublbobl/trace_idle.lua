-- Find Bubble Bobble CPU idle loops: trace maincpu/subcpu for 60 frames
-- of gameplay into main.log / sub.log (MAME collapses loops into
-- "(loops for N instructions)" lines). Run from a dir holding
-- boblbobl.zip + bublbobl.zip, go.txt containing "go":
--   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy XDG_CONFIG_HOME=$PWD \
--   XDG_STATE_HOME=$PWD XDG_DATA_HOME=$PWD mame -rompath . boblbobl \
--   -noautosave -nofilter -video none -sound none -nothrottle \
--   -skip_gameinfo -debug -debugger none -debugscript go.txt \
--   -autoboot_script $PWD/trace_idle.lua
-- Result 2026-10-06: main idles at $01ED (jr $01ED), sub at $000A
-- (jp $000A); both leave only through their vblank IRQ.
local frames = 0
local in0 = manager.machine.ioport.ports[":IN0"]
SUB = emu.add_machine_frame_notifier(function()
  frames = frames + 1
  if frames == 300 then in0.fields["Coin 1"]:set_value(1) end
  if frames == 306 then in0.fields["Coin 1"]:set_value(0); in0.fields["1 Player Start"]:set_value(1) end
  if frames == 312 then in0.fields["1 Player Start"]:set_value(0) end
  if frames == 600 then
    manager.machine.debugger:command("trace main.log,maincpu")
    manager.machine.debugger:command("trace sub.log,subcpu")
  end
  if frames == 660 then
    manager.machine.debugger:command("trace off,maincpu")
    manager.machine.debugger:command("trace off,subcpu")
    manager.machine:exit()
  end
end)
