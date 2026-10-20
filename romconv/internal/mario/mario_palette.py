"""Mario Bros. palette from PROM 4p, as MAME builds it (mario.cpp):
compute_res_net_all(mario_net_info), then normalize_range(), which
rescales luminance across all pens."""


def _tdiv(a, b):
    """C integer division, truncating toward zero."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


# resnet.cpp: 5V supply and bias, MB7052 PROM outputs (TTL)
_VCC, _VOL, _VOH, _TTL_HRES = 5.0, 0.05, 4.0, 50
# (minout, cut, rBias, input resistors): darlington R and G, emitter B
_CHANNELS = ((0.7, 0.0, 470, (1000, 470, 220)),
             (0.7, 0.0, 470, (1000, 470, 220)),
             (0.0, 0.7, 680, (470, 220)))


def _res_net(inputs, channel):
    """resnet.cpp compute_res_net() for this board."""
    minout, cut, rbias, rs = _CHANNELS[channel]
    g = 1 / rbias
    v = _VCC / rbias
    for i, r in enumerate(rs):
        if not inputs >> i & 1:
            g += 1 / r
            v += _VOL / r
    # low inputs alone pull above VOH: high outputs carry no current
    if v / g <= _VOH:
        for i, r in enumerate(rs):
            if inputs >> i & 1:
                g += 1 / (r + _TTL_HRES)
                v += _VOH / (r + _TTL_HRES)
    v = _VCC - max(minout, v / g - cut)  # Sanyo EZV20: inverted input
    v = min(max(0.0, v - 0.7), _VCC - 1.4) / (_VCC - 1.4) * _VCC
    return int(v * 255 / _VCC + 0.4)


def _normalize(rgb):
    """palette_t::normalize_range(0, 255) with its default 0..255 target."""
    ys = [299 * r + 587 * g + 114 * b for r, g, b in rgb]
    ymin, ymax = min(ys), max(ys)
    clamp = lambda x: min(255, max(0, x))
    out = []
    for (r, g, b), y in zip(rgb, ys):
        u = _tdiv((b - y // 1000) * 492, 1000)
        v = _tdiv((r - y // 1000) * 877, 1000)
        t = _tdiv((y - ymin) * 256, ymax - ymin)
        out.append((clamp(t + _tdiv(1140 * v, 1000)),
                    clamp(t - _tdiv(395 * u, 1000) - _tdiv(581 * v, 1000)),
                    clamp(t + _tdiv(2032 * u, 1000))))
    return out


def palette(prom):
    """256 RGB888 pens from the first 256 PROM bytes (RRRGGGBB)."""
    return _normalize([(_res_net(p >> 5 & 7, 0), _res_net(p >> 2 & 7, 1), _res_net(p & 3, 2))
                       for p in prom[:256]])
