"""Walk and skid sounds of the Mario Bros. sound board, synthesized.

The board makes them with discrete logic (MAME nl_mario.cpp): a 74123
one-shot gates two XORed 74LS629 VCOs into the RC mixer and the Q10
output stage. Returns int8 PCM at 24 kHz.
"""
import numpy as np

_RATE = 24000
_OS = 32           # the VCOs run at 2-70 kHz
_DT = 1 / (_RATE * _OS)
_VOH = 3.4         # TTL high level
_GAIN = 230        # volts to int8: walk peaks near full scale


def _vco_freq(vfc, cap, vrng=5.0):
    """74LS629 frequency, MAME's fit of the datasheet curves."""
    v = vfc
    f = (1.9904769024796283e3 + 1.2070059213983407e3 * v + 1.3266985579561108e3 * v**2
         - 1.5500979825922698e2 * v**3 + 2.8184536266938172 * v**4
         + vrng * (-2.3503421582744556e2 - 3.3836786704527788e2 * v
                   - 1.3569136703258670e2 * v**2 + 2.9914575453819188 * v**3
                   + 1.6855569086173170 * v**4))
    return f * 0.1e-6 / cap


def _charge(t, v0, v1, tau):
    return v1 + (v0 - v1) * np.exp(-t / tau)


def _square(freq, phase=0.0):
    return (phase + np.cumsum(freq) * _DT) % 1 < 0.5


def _lowpass(x, tau):
    a = 1 - np.exp(-1 / (_RATE * tau))
    y = np.empty_like(x)
    v = 0.0
    for i, s in enumerate(x):
        v += (s - v) * a
        y[i] = v
    return y


def _highpass(x, tau):
    a = np.exp(-1 / (_RATE * tau))
    y = np.empty_like(x)
    v = prev = 0.0
    for i, s in enumerate(x):
        v = a * (v + s - prev)
        prev = s
        y[i] = v
    return y


def _output(x, r_src):
    """TTL gate into the mixer node, C32 into Q10, C47 into VR1."""
    r_others = [22e3, 22e3, 22e3, 100e3]
    r_others.remove(r_src)
    g = 1 / r_src + sum(1 / r for r in r_others)
    y = x.reshape(-1, _OS).mean(axis=1) / (r_src * g)
    y = _lowpass(y, 0.022e-6 / g)          # C31
    y = _highpass(y, 1e-6 * 30e3)          # C32 into R42 || R43
    y = np.where(y > 0, 0.3 * y, y)       # Q10 idles saturated: rises compress
    y = _highpass(y, 4.7e-6 * 10e3)        # C47 into VR1
    return np.clip(np.round(y * _GAIN), -128, 127).astype(np.int8)


def _walk(r_os, c_fc, cap1, cap2, seconds=0.32):
    """SOUND0 (Mario) / SOUND1 (Luigi): one-shot R17/R18 * 4.7u, its
    output charges both VCO controls through 4.7k."""
    t = np.arange(int(seconds * _RATE) * _OS) * _DT
    on = t < 0.4 * r_os * 4.7e-6 * (1 + 700 / r_os)  # MAME 74123 timing
    fc = _charge(t, 0.0, _VOH, 4.7e3 * c_fc)
    x = on & (_square(_vco_freq(fc, cap1)) ^ _square(_vco_freq(fc, cap2)))
    return _output(_VOH * x, 22e3)


# 74123 timing would give 90 ms (R61, C41); MAME's netlist ends the skid
# after 40 ms
_SKID_PULSE = 0.040


def _skid(seconds=0.2):
    """SOUND7: the one-shot's inverted output discharges VCO 4K_A's
    control; 4K_B clocks a CD4020 (Q4 XORs with 4K_A), its own control
    follows inverted Q12."""
    n = int(seconds * _RATE) * _OS
    t = np.arange(n) * _DT
    on = t < _SKID_PULSE
    a = _square(_vco_freq(_charge(t, _VOH, 0.0, 10e3 * 3.3e-6), 0.022e-6))
    q4 = np.zeros(n, bool)
    i, count, phase, fcb = 0, 0, 0.0, _VOH
    end = int(np.count_nonzero(on))
    while i < end:
        q12 = count >> 11 & 1
        fc = _charge(np.arange(1, end - i + 1) * _DT, fcb, 0.0 if q12 else _VOH, 20e3 * 3.3e-6)
        clocks = phase + np.cumsum(_vco_freq(fc, 0.0047e-6)) * _DT
        flip = 2048 - (count & 2047)
        j = min(int(np.searchsorted(clocks, flip)), end - i - 1) + 1
        q4[i:i + j] = (count + clocks[:j].astype(int)) >> 3 & 1
        count = (count + int(clocks[j - 1])) & 0xfff
        phase = clocks[j - 1] % 1
        fcb = fc[j - 1]
        i += j
    return _output(_VOH * (on & (q4 ^ a)), 100e3)


def samples():
    return {
        "walk0": _walk(27e3, 10e-6, 3.9e-9, 22e-9),   # R17, C3, C6, C17
        "walk1": _walk(30e3, 4.7e-6, 39e-9, 6.8e-9),  # R18, C4, C5, C16
        "skid": _skid(),
    }
