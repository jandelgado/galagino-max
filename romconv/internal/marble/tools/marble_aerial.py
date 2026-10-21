#!/usr/bin/env python3
"""Marble Madness aerial race level objects (Phase 4c-2): 0x29cce tags 0x0a catapult,
0x0b/0x0d vacuum gates, 0x0c bounce boxes, and player state 0xa (sucked into a gate). 1:1
ports named by ROM address from Marble Love (sub-29cce.ts runCatapult0A, runGate0B/0D,
runBounce0C, refresh-frame-10fce.ts JT[10], slot-spawn-pattern-13d38.ts) on the work RAM
image of marble_sim.Mem. Facts: MARBLE.md "Aerial objects".

  marble_aerial.py aerial TRACE RACE FROM:TO...  free run main_tick (tick start traces, all
                                              modelled enemies): marble_piston.piston_diff
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from devrom import ROM, SIM_ROM, md  # noqa: F401
from romconv.games.marble.sim import marble_aerial as _model  # noqa: F401
from romconv.games.marble.sim.marble_aerial import *  # noqa: F401,F403
import marble_sim as ms
from marble_sim import s8, s16, rom_l, PROG





def cmd_aerial(trace, race, segs):
    import marble_enemy as me, marble_piston as pp
    ok = True
    for seg in segs or ["0:%d" % trace[-1][0]]:
        a, b = (int(v) for v in seg.split(":"))
        i0 = next(k for k, x in enumerate(trace) if x[0] > a and ms.live_race(x[1]))
        m = me.steelie_mem(ms.Terrain(race), trace[i0][1])
        run = [x for x in trace[i0:] if x[0] <= b]
        n = 0; states = {}; events = []
        for (fr, r, v), (fr2, r2, v2) in zip(run, run[1:]):
            m.events = []; m.sounds = []
            ms.main_tick(m, *ms.death_inputs(r, r2))
            import marble_mo as mo
            mo.boxes_and_sort(m)
            n += 1
            st = m.rb(ms.P + 0x1a); states[st] = states.get(st, 0) + 1
            events += [e for e in m.events if isinstance(e, tuple) and e[0] in
                       ("catapult", "gate catch", "gate lost", "bounce box")]
            d = pp.piston_diff(m, r2, v2)
            if d:
                print("aerial race %d: tick %d (frame %d, state %d) differs: %s %s"
                      % (race, n, fr2, st, "; ".join(d[:8]), m.events))
                ok = False; break
        else:
            print("aerial race %d: frames %d-%d, %d ticks equal, player ticks per state %s, "
                  "events %s" % (race, run[0][0], run[-1][0], n, dict(sorted(states.items())),
                                 events))
    return ok


def main(argv):
    if len(argv) >= 4 and argv[1] == "aerial":
        return 0 if cmd_aerial(ms.load_trace(argv[2]), int(argv[3]), argv[4:]) else 1
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
