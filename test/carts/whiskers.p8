pico-8 cartridge // http://www.pico-8.com
version 41
__lua__
-- whiskers
-- a cat to try shaders with
-- ⬅️➡️ walk, 🅾️ jump, ❎ test pattern

ground=104

function _init()
 cat={x=60,y=ground-8,dy=0,flip=false,t=0}
 fish={}
 score=0
 misses=0
 spawn=0
 over=false
 clouds={}
 for i=1,4 do
  add(clouds,{x=rnd(128),y=14+rnd(26),s=0.2+rnd(0.4)})
 end
end

function _update()
 if btnp(5) then pattern=not pattern end
 if pattern then return end

 -- clouds drift whatever happens, for smooth motion to look at
 for c in all(clouds) do
  c.x+=c.s
  if c.x>140 then c.x=-20 end
 end

 if over then
  if btnp(4) then _init() end
  return
 end

 local moving=false
 if btn(0) then cat.x-=1.5 cat.flip=true moving=true end
 if btn(1) then cat.x+=1.5 cat.flip=false moving=true end
 cat.x=mid(0,cat.x,120)
 if btnp(4) and cat.y>=ground-8 then cat.dy=-3.5 end
 cat.dy+=0.25
 cat.y+=cat.dy
 if cat.y>ground-8 then cat.y=ground-8 cat.dy=0 end
 if moving then cat.t+=1 end

 -- fish fall faster and more often as the score goes up
 spawn-=1
 if spawn<=0 then
  add(fish,{x=4+rnd(116),y=-8,dy=0.5+rnd(0.5)+score/40})
  spawn=40-min(score,25)
 end
 for f in all(fish) do
  f.y+=f.dy
  if abs(f.x-cat.x)<7 and abs(f.y-cat.y)<7 then
   del(fish,f)
   score+=1
  elseif f.y>ground-6 then
   del(fish,f)
   misses+=1
   if misses>=3 then over=true end
  end
 end
end

function draw_world()
 -- sky in bands, a sun and clouds
 local bands={1,2,13,12}
 for i=0,3 do rectfill(0,8+i*12,127,19+i*12,bands[i+1]) end
 rectfill(0,56,127,ground-1,12)
 circfill(100,30,10,10)
 circ(100,30,12,9)
 for c in all(clouds) do
  circfill(c.x,c.y,5,7)
  circfill(c.x+6,c.y+1,4,7)
  circfill(c.x-6,c.y+1,4,6)
 end

 -- ground and grass
 rectfill(0,ground,127,127,4)
 for x=0,127,4 do line(x,ground,x+2,ground-3,11) end
 line(0,ground,127,ground,3)

 for f in all(fish) do spr(3,f.x,f.y) end

 local frame=1
 if cat.y<ground-8 or cat.t%8>=4 then frame=2 end
 spr(frame,cat.x,cat.y,1,1,cat.flip)

 -- every colour along the top
 for i=0,15 do rectfill(i*8,0,i*8+7,7,i) end

 print("score "..score,2,110,7)
 print("misses "..misses.."/3",80,110,8)
 print("❎ test pattern",2,120,6)

 if over then
  rectfill(24,50,103,74,0)
  rect(24,50,103,74,7)
  print("game over",46,55,8)
  print("🅾️ to play again",33,65,7)
 end
end

function draw_pattern()
 cls(0)
 -- colour bars
 for i=0,15 do rectfill(i*8,0,i*8+7,23,i) end
 -- a one pixel checkerboard and one pixel columns
 for y=24,39 do
  for x=0,63 do
   if (x+y)%2==0 then pset(x,y,7) end
  end
 end
 for x=64,127,2 do line(x,24,x,39,7) end
 -- one pixel rows
 for y=40,55,2 do line(0,y,127,y,7) end
 -- a dither ramp from black to white
 local ramp={0x0000,0x8020,0xa5a5,0x7fdf,0xffff}
 for i=0,4 do
  fillp(ramp[i+1])
  rectfill(i*26,56,i*26+25,71,0x70)
 end
 fillp()
 -- rings and discs, for curves and edges
 for r=2,14,4 do circ(32,92,r,10) end
 circfill(96,92,14,8)
 circfill(96,92,8,12)
 line(52,76,76,108,11)
 line(52,108,76,76,11)
 print("whiskers ♥ shaders",2,114,7)
 print("❎ back to the cat",2,121,6)
end

function _draw()
 if pattern then
  draw_pattern()
 else
  draw_world()
 end
end
__gfx__
0000000000000909000009090000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0000000090000999900009990000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0000000090000191900001910c0cccc000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
00000000900009e9900009e90cccc7c000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0000000009999990099999900c0cccc000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0000000009999990099999900000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0000000009000090009009000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
0000000009000090009009000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
