local cpu=manager.machine.devices[":maincpu"]
local sp=cpu.spaces["program"]
local latch={0,0,0,0,0,0,0,0}; local scroll=0
t1=sp:install_write_tap(0x7e80,0x7e87,"latch",function(o,d,m) latch[o-0x7e80+1]=d&1 end)
t2=sp:install_write_tap(0x7d00,0x7d00,"scroll",function(o,d,m) scroll=d end)
local ram=io.open("ram_trace.txt","w")
local f=0
emu.register_frame_done(function()
  f=f+1
  if f==700 then manager.machine.video:snapshot() end
  if f>=2400 and f<=2700 then
    local t={}
    for a=0x6000,0x77ff do t[#t+1]=string.format("%02x",sp:read_u8(a)) end
    ram:write(string.format("%d %d %s %s\n",f,scroll,table.concat(latch,""),table.concat(t)))
    if f==2500 or f==2501 or f==2650 then manager.machine.video:snapshot() end
  end
  if f==2701 then ram:close() manager.machine:exit() end
end)
