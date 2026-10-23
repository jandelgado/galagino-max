#!/usr/bin/env python3
"""Marble Madness game flow model (Phase 3a, MARBLE.md "Game flow"): the race timer of the
vblank IRQ (0x28a96, cascade 0x28c38), the race end detector (0x251de tail), the race end
bonus (0x1101e state 3, 0x118d2), the next race carry, the race intro time count up, time out
(state 4) and the final bonus (state 6, 0x18a88). Marble Love TS: game-tick-timers.ts,
timer-cascade.ts, object-scan-dispatch-251de.ts, main-loop-init-1101e.ts,
player-slot-iter-118d2.ts, state-sub-18a88.ts. Frame counts of the blocking main loop parts
(intro, bonus wait, level load) are fitted to MAME traces (mame_trace.lua MARBLE_TICK=frame).

usage: marble_flow.py check TRACE...   frame traces: timer per frame, race end sequence, carry
       make_fixture.py fwrite OUT TRACE...  the host test header (write_fixture)
"""
import struct
import sys

P = 0x400018
GS, RACE, FRAMES, RNG = 0x400390, 0x400394, 0x400010, 0x4003a6
TIMER = 0x6a                 # player: word seconds, +0x6c tenths, +0x6e frames (0xff stopped)

# 0x1101e states (0x400390)
RACE_ON, ATTRACT, GAME_OVER, RACE_END, TIME_OUT, GAME_START, FINAL = 0, 1, 2, 3, 4, 5, 6

# race intro time (level-intro-banner-resume.ts): races 0/1 set it, later races add it
RACE_TIME = (60, 60, 35, 30, 20, 20)
BONUS_WAIT = 40              # 0x118d2: frames before the race sound and the level load
LOAD = (81, 80, 60, 116, 73)  # 0x16ec6 level load of race k + 1, frames (MAME, after the wait)
INIT_AFTER_BONUS = (8, 8, 8, 9, 9)  # bonus add to race k + 1 init (0x10504 player init)
ADD_FIRST = (64, 63, 63, 64, 64)    # race k + 1 init to its first +5 s
ADD_STEP, ADD_SECONDS = 10, 5
INTRO_END = 61               # last +5 s step to race start: 61 + extra - 5 * (extra / 5 - 1)
START_ARM = 130              # new game: player init to the first +5 s (60 frame scroll in)
TIME_OUT_FRAMES = 182        # 0x16a20 summary, then game over
FINAL_START = 6              # bonus add to state 6
FINAL_FIRST = 10             # state 6 to the first 250 points
FINAL_STEP, FINAL_POINTS = 2, 250
FINAL_END = 272              # last 250 points to game over (90 + 0xb4 frame waits)
FRAMES_TO_START = 6          # 0x400010 at race start = at race end + 6 (vblank acks)


def cascade(t):
    """0x28c38 on a 5 byte timer [sec_hi, sec_lo, tenths, pad, frames]: returns flags (bit 0
    seconds wrapped below 0, bit 1 a tenth passed)."""
    if t[4] == 0xff: return 0
    t[4] = (t[4] - 1) & 0xff
    if t[4] < 0x80: return 0
    t[4] = 5
    t[2] = (t[2] - 1) & 0xff
    if t[2] < 0x80: return 0
    t[2] = 9
    w = (((t[0] << 8) | t[1]) - 1) & 0xffff
    t[0], t[1] = w >> 8, w & 0xff
    return 3 if w == 0xffff else 2


TIME_BONUS = 8               # player state +0x1a: the timer pauses (0x28a96 skips it)


def timers_28a96(t, state):
    """Player timer part of 0x28a96 (one player, +0x1a state): cascade; expired: player out
    (+0x18 = 2), timer cleared and stopped, 0x400390 = 4. Returns True on expiry."""
    if state == TIME_BONUS: return False
    if cascade(t) & 1:
        t[0] = t[1] = t[2] = 0
        t[4] = 0xff
        return True
    return False


def bonus(seconds):
    """0x118d2: points for the time left (seconds clamped to 99)."""
    s = seconds - 0x10000 if seconds & 0x8000 else seconds
    return min(s, 99) * 100


def final_bonus(seconds, deaths):
    """0x18a88: 20000 + 1000 per second left (max 99) - 1000 per death (max 20)."""
    s = seconds - 0x10000 if seconds & 0x8000 else seconds
    return 20000 + min(s, 99) * 1000 - min(deaths, 20) * 1000


def race_end(race, seconds, score, deaths, frames10):
    """Frame offsets from the 0x400390 = 3 frame and values of the race end sequence."""
    ev = {"race+1": 2}
    after = race + 1
    ev["bonus"] = ev["race+1"] + BONUS_WAIT + (LOAD[race] if after < 6 else 0)
    score += bonus(seconds)
    ev["score"] = score
    if after == 6:
        ev["final"] = ev["bonus"] + FINAL_START
        fb = final_bonus(seconds, deaths)
        steps = (fb + FINAL_POINTS - 1) // FINAL_POINTS
        ev["final_score"] = score + fb
        ev["final_last"] = ev["final"] + FINAL_FIRST + FINAL_STEP * (steps - 1)
        ev["game_over"] = ev["final_last"] + FINAL_END
        return ev
    ev["init"] = ev["bonus"] + INIT_AFTER_BONUS[race]
    base = 0 if after < 2 else seconds
    n = RACE_TIME[after] // ADD_SECONDS
    ev["adds"] = [ev["init"] + ADD_FIRST[race] + ADD_STEP * k for k in range(n)]
    ev["seconds"] = base + RACE_TIME[after]
    ev["race_on"] = ev["init"] + ADD_FIRST[race] - 1 + INTRO_END + RACE_TIME[after]
    ev["frames10"] = frames10 + FRAMES_TO_START
    return ev


# ---------------------------------------------------------------- MAME frame trace check

REC = 8 + 0x2000


def frames(path):
    with open(path, "rb") as fh:
        while True:
            b = fh.read(REC)
            if len(b) < REC: return
            if b[:4] != b"FRAM": raise SystemExit("%s: not a frame trace" % path)
            yield struct.unpack(">I", b[4:8])[0], b[8:]


def rw(r, a): return (r[a - 0x400000] << 8) | r[a - 0x3fffff]
def rl(r, a): return struct.unpack(">I", r[a - 0x400000:a - 0x3ffffc])[0]
def rb(r, a): return r[a - 0x400000]


def timer_bytes(r): return [rb(r, P + TIMER + k) for k in range(5)]


def check(path, tol=1):
    """Every race frame: the IRQ timer step equals the trace (except frames a logic tick
    stopped or changed it: goal, time bonus). Race end: the sequence of race_end within
    tol frames, values exact. Time out: 0x400390 = 4 on the expiry frame."""
    fr = list(frames(path))
    ok = True
    n_timer = 0
    for (f0, r0), (f1, r1) in zip(fr, fr[1:]):
        if rw(r0, GS) != RACE_ON or rw(r1, GS) not in (RACE_ON, TIME_OUT): continue
        if rb(r1, P + 0x1a) == 6: continue                            # goal stops it
        if abs(rw(r1, P + TIMER) - rw(r0, P + TIMER)) > 1: continue   # MARBLE_POKE
        t = timer_bytes(r0)
        st = rb(r0, P + 0x1a)
        expired = timers_28a96(t, st)
        if st == TIME_BONUS:                                          # its +1 s: logic tick
            w = ((t[0] << 8) | t[1]) + (rw(r1, P + TIMER) - rw(r0, P + TIMER))
            t[0], t[1] = w >> 8, w & 0xff
        got = timer_bytes(r1)
        if t[:3] + t[4:] != got[:3] + got[4:] or expired != (rw(r1, GS) == TIME_OUT):
            print("%s frame %d: timer %s, trace %s (expired %d)" % (path, f1, t, got, expired))
            ok = False
            break
        n_timer += 1
    print("%s: %d race frames, timer equal" % (path, n_timer) if ok else "")

    end = measure_end(fr)
    if end is None: return ok
    race, inp, got = end["race"], end["inputs"], end["got"]
    ev = race_end(race, *inp)
    bad = []
    for k, v in ev.items():
        g = got.get(k)
        if g is None: bad.append("%s missing" % k); continue
        if k == "adds":
            if len(g) != len(v) or any(abs(a - b) > tol for a, b in zip(g, v)): bad.append("adds %s/%s" % (g, v))
        elif k in EXACT:
            if g != v: bad.append("%s %d/%d" % (k, g, v))
        elif abs(g - v) > tol: bad.append("%s %d/%d" % (k, g, v))
    print("%s: race %d end %s" % (path, race, "equal (tol %d)" % tol if not bad else "differs: " + "; ".join(bad)))
    if "race_on" in got: ok &= carry_check(end)
    return ok and not bad


EXACT = ("score", "seconds", "final_score", "frames10")


def measure_end(fr):
    """Race end sequence of a frame trace: race, inputs of race_end (seconds, score, deaths,
    0x400010 at the 0x400390 = 3 frame), measured frame offsets and values, the RAM of that
    frame and of the frame before the next race starts. None without a race end."""
    i3 = next((i for i, (f, r) in enumerate(fr) if i and rw(r, GS) == RACE_END
               and rw(fr[i - 1][1], GS) != RACE_END), None)
    if i3 is None: return None
    f3, r3 = fr[i3]
    inputs = (rw(r3, P + TIMER), rl(r3, P + 0xbc), rw(r3, P + 0xd2), rl(r3, FRAMES))
    got = {}
    adds = []
    prev, pre = r3, None
    for f, r in fr[i3 + 1:]:
        d = f - f3
        if rw(r, RACE) != rw(prev, RACE): got["race+1"] = d
        if "race+1" in got and "bonus" not in got and rl(r, P + 0xbc) != rl(prev, P + 0xbc):
            got["bonus"] = d; got["score"] = rl(r, P + 0xbc)
        if "bonus" in got and "init" not in got and rb(r, P + 0x18) == 1 and rb(prev, P + 0x18) != 1:
            got["init"] = d
        if "init" in got and rb(r, P + 0x6e) == 0xff and rw(r, P + TIMER) == rw(prev, P + TIMER) + 5:
            adds.append(d)
        if "init" in got and rw(r, GS) == RACE_ON and rw(prev, GS) != RACE_ON:
            got["race_on"] = d; got["seconds"] = rw(r, P + TIMER); got["frames10"] = rl(r, FRAMES)
            pre = prev
        if rw(r, GS) == FINAL and rw(prev, GS) != FINAL: got["final"] = d
        if rw(r, GS) == FINAL and rl(r, P + 0xbc) != rl(prev, P + 0xbc):
            got["final_last"] = d; got["final_score"] = rl(r, P + 0xbc)
        if rw(r, GS) == GAME_OVER and rw(prev, GS) != GAME_OVER: got["game_over"] = d
        prev = r
    if adds: got["adds"] = adds
    return {"race": rw(r3, RACE), "inputs": inputs, "got": got, "end": r3, "pre": pre}


# ---------------------------------------------------------------- next race carry

CARRY = ((P + TIMER, 6), (P + 0xbc, 4), (P + 0xd2, 2))   # timer, score, death count
CARRY_TICKS = 400
KEY = tuple(range(0, 0x1c)) + (0x36, 0x56, 0x57, 0x58, 0x5a, 0x5b, 0x5c, 0x5d) + \
    tuple(range(0x6a, 0x6f)) + tuple(range(0xbc, 0xc0)) + (0xd2, 0xd3)
SLOTS = (0x400a9c, 0x401300)


def next_race(race, rng, carry, frames10):
    """The remake's next race: cleared work RAM with the RNG word, race start (marble_sim
    begin_race, all enemies), the carried player fields, 0x400010."""
    from dev.marble import marble_sim as ms, marble_enemy
    ram = bytearray(0x2000)
    ram[RNG - 0x400000:RNG - 0x3ffffe] = rng.to_bytes(2, "big")
    m = marble_enemy.setup(ms.Mem(ms.Terrain(race), ram))
    ms.begin_race(m, race)
    for (a, n), b in zip(CARRY, carry): m.ram[a - 0x400000:a - 0x400000 + n] = b
    m.wl(FRAMES, frames10)
    return m


def key_state(m):
    a = P - 0x400000
    return (bytes(m.ram[a + o] for o in KEY), bytes(m.ram[RNG - 0x400000:RNG - 0x3ffffe]),
            bytes(m.ram[SLOTS[0] - 0x400000:SLOTS[1] - 0x400000]))


def idle_run(m, cam, n=CARRY_TICKS):
    from dev.marble import marble_sim as ms
    out = []
    for _ in range(n):
        ms.main_tick(m, 0, 0, cam, m.rl(FRAMES) + 1)
        out.append(key_state(m))
    return out


def carry_inputs(end):
    pre = end["pre"]
    return (rw(end["end"], RNG), tuple(bytes(pre[a - 0x400000:a - 0x400000 + n]) for a, n in CARRY),
            rl(end["end"], FRAMES) + FRAMES_TO_START)


def carry_check(end):
    """Next race from the cleared RAM and the carried fields equals the MAME RAM of the frame
    before the race starts (its race start RNG call moved in) for CARRY_TICKS idle ticks:
    player key fields, RNG word, slots."""
    from dev.marble import marble_sim as ms, marble_enemy
    race = end["race"] + 1
    rng, carry, frames10 = carry_inputs(end)
    a = next_race(race, rng, carry, frames10)
    b = marble_enemy.setup(ms.Mem(ms.Terrain(race), bytearray(end["pre"])))
    b.ww(RNG, a.rw(RNG)); b.wb(0x400444, a.rb(0x400444))
    cam = b.rl(0x40097c)
    ta, tb = idle_run(a, cam), idle_run(b, cam)
    bad = next((k for k in range(CARRY_TICKS) if ta[k] != tb[k]), None)
    print("  next race %d carry: %s" % (race, "%d idle ticks equal" % CARRY_TICKS if bad is None
                                         else "differs at tick %d" % bad))
    return bad is None


def time_out(fr):
    """Frames from the race start (0x400390 0, timer armed) to the time out and from there to
    game over, the seconds at the start."""
    i0 = next(i for i, (f, r) in enumerate(fr) if rw(r, GS) == RACE_ON and rb(r, P + 0x6e) != 0xff)
    i4 = next(i for i in range(i0, len(fr)) if rw(fr[i][1], GS) == TIME_OUT)
    i2 = next(i for i in range(i4, len(fr)) if rw(fr[i][1], GS) == GAME_OVER)
    return fr[i4][0] - fr[i0][0], fr[i2][0] - fr[i4][0], rw(fr[i0][1], P + TIMER)


def key_crc(m):
    import zlib
    return zlib.crc32(b"".join(key_state(m)))


def write_fixture(out, paths):
    """test_marble_flow/marble_flow_trace.h: race end sequences measured in MAME frame traces,
    next race carry runs (model, equal to MAME, carry_check), the time out of a trace."""
    ends, carries, tout = [], [], None
    for p in paths:
        fr = list(frames(p))
        if not check(p): raise SystemExit("%s: model differs from the trace" % p)
        end = measure_end(fr)
        if end is None:
            tout = time_out(fr)
            continue
        ends.append(end)
        if "race_on" not in end["got"]: continue
        rng, carry, frames10 = carry_inputs(end)
        m = next_race(end["race"] + 1, rng, carry, frames10)
        cam = m.rl(0x40097c)
        idle_run(m, cam)
        carries.append((end["race"] + 1, rng, carry, frames10, cam, key_crc(m)))
    with open(out, "w") as f:
        w = lambda x="": print(x, file=f)
        w("// Generated by romconv/dev/marble/make_fixture.py fwrite; do not edit.")
        w("// Marble game flow host test (MARBLE.md \"Game flow\"): MAME frame traces")
        w("// (dev/marble/mame_trace.lua MARBLE_TICK=frame), checked by marble_flow.py check.")
        w("#ifndef MARBLE_FLOW_TRACE_H\n#define MARBLE_FLOW_TRACE_H\n#include <stdint.h>\n")
        w("// Race end: inputs at the 0x400390 = 3 frame (seconds +0x6a, score +0xbc, deaths")
        w("// +0xd2, 0x400010), then MAME frame offsets from it: race index + 1, time bonus added")
        w("// (score after), next race init, first +5 s, number of +5 s, race start (seconds,")
        w("// 0x400010), after ultimate: final state 6, last 250 points (final score), game over.")
        w("// 0 = not reached.")
        w("struct MarbleRaceEnd {")
        w("  uint8_t race; uint16_t seconds; uint32_t score; uint16_t deaths; uint32_t frames;")
        w("  uint16_t next, bonus; uint32_t bonus_score; uint16_t init, first_add; uint8_t adds;")
        w("  uint16_t race_on, seconds_on; uint32_t frames_on;")
        w("  uint16_t final, final_last; uint32_t final_score; uint16_t game_over;")
        w("};")
        w("static const MarbleRaceEnd marble_race_ends[] = {")
        for e in ends:
            g = e["got"]; sec, score, deaths, fr10 = e["inputs"]
            adds = g.get("adds", [])
            w("  {%d, %d, %d, %d, 0x%x, %d, %d, %d, %d, %d, %d, %d, %d, 0x%x, %d, %d, %d, %d}," % (
                e["race"], sec, score, deaths, fr10, g["race+1"], g["bonus"], g["score"],
                g.get("init", 0), adds[0] if adds else 0, len(adds), g.get("race_on", 0),
                g.get("seconds", 0), g.get("frames10", 0), g.get("final", 0),
                g.get("final_last", 0), g.get("final_score", 0), g.get("game_over", 0)))
        w("};\n")
        w("// Next race carry (model run, equal to MAME for %d idle ticks): race, RNG word" % CARRY_TICKS)
        w("// 0x4003a6 at the race end, carried player bytes +0x6a-+0x6f (timer), +0xbc (score),")
        w("// +0xd2 (deaths), 0x400010 at the race start, camera long; crc32 of player bytes KEY,")
        w("// RNG word and slots 0x400a9c-0x4012ff after %d idle ticks (main tick as" % CARRY_TICKS)
        w("// marble_sim.py main_tick, constant camera, 0x400010 + 1 per tick).")
        w("struct MarbleCarry {")
        w("  uint8_t race; uint16_t rng; uint8_t timer[6]; uint32_t score; uint16_t deaths;")
        w("  uint32_t frames, camera, crc;")
        w("};")
        w("static const uint16_t MARBLE_CARRY_TICKS = %d;" % CARRY_TICKS)
        w("static const uint8_t marble_carry_key[] = {%s};" % ", ".join("0x%02x" % o for o in KEY))
        w("static const MarbleCarry marble_carries[] = {")
        for race, rng, carry, fr10, cam, crc in carries:
            t, sc, de = carry
            w("  {%d, 0x%04x, {%s}, %d, %d, 0x%x, 0x%x, 0x%08x}," % (
                race, rng, ", ".join("0x%02x" % b for b in t), int.from_bytes(sc, "big"),
                int.from_bytes(de, "big"), fr10, cam, crc))
        w("};\n")
        if tout:
            w("// Practice time out: frames after the race start frame (timer armed, %d s) to" % tout[2])
            w("// 0x400390 = 4, then to game over (0x16a20 summary).")
            w("static const uint16_t MARBLE_TIMEOUT_SECONDS = %d, MARBLE_TIMEOUT_FRAMES = %d;" % (tout[2], tout[0]))
            w("static const uint16_t MARBLE_TIMEOUT_GAME_OVER = %d;\n" % tout[1])
        w("#endif")
    print("%s: %d race ends, %d carries%s" % (out, len(ends), len(carries), ", time out" if tout else ""))


def main(argv):
    if len(argv) < 3 or argv[1] != "check": raise SystemExit(__doc__)
    ok = True
    for p in argv[2:]: ok &= check(p)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
