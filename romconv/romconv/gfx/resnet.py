"""MAME video/resnet.cpp compute_resistor_weights, shared by converters."""


# nets = [(resistances, pulldown, pullup)]
def resistor_weights(minval, maxval, scaler, nets):
    w = []
    for res, pd, pu in nets:
        ws = []
        for n in range(len(res)):
            r0 = 1e-12 if pd == 0 else 1.0 / pd
            r1 = 1e-12 if pu == 0 else 1.0 / pu
            for j, rj in enumerate(res):
                if j == n:
                    r1 += 1.0 / rj
                else:
                    r0 += 1.0 / rj
            r0, r1 = 1.0 / r0, 1.0 / r1
            vout = (maxval - minval) * r0 / (r1 + r0) + minval
            ws.append(min(max(vout, minval), maxval))
        w.append(ws)
    scale = scaler if scaler >= 0.0 else maxval / max(sum(ws) for ws in w)
    return [[x * scale for x in ws] for ws in w], scale


def combine(weights, *bits):
    return int(sum(wt * b for wt, b in zip(weights, bits)) + 0.5)
