#!/usr/bin/env python3
"""Analiza las salidas de engine_test: afinación, vibrato, formantes, transparencia, ruido."""
import sys
import numpy as np
from scipy.linalg import solve_toeplitz
from scipy.signal import freqz

D = sys.argv[1]
FS = {"phrases": 44100, "hard_steady_96k": 96000}


def load(name, kind):
    return np.fromfile(f"{D}/{name}_{kind}.f32", dtype=np.float32).astype(np.float64)


def fs_of(name):
    return FS.get(name, 48000)


def f0_track(x, fs, lo=70, hi=900, frame=0.04, hop=0.01, thr=0.5):
    """Autocorrelación normalizada por cuadro; devuelve (tiempos, f0 o nan)."""
    N = int(frame * fs); H = int(hop * fs)
    tmin = int(fs / hi); tmax = int(fs / lo)
    ts, fs0 = [], []
    for s in range(0, len(x) - N - tmax - 4, H):
        seg = x[s:s + N + tmax + 3]
        a = seg[:N]
        if np.sqrt(np.mean(a ** 2)) < 1e-3:
            ts.append((s + N / 2) / fs); fs0.append(np.nan); continue
        e0 = np.dot(a, a)
        best, bt = -1, 0
        r = np.empty(tmax + 2)
        for t in range(tmin - 1, tmax + 2):
            b = seg[t:t + N]
            r[t] = np.dot(a, b) / np.sqrt(e0 * np.dot(b, b) + 1e-12)
        # primer pico local cuyo valor es >= 90% del máximo global (evita saltos de octava)
        rmax = np.max(r[tmin:tmax])
        t = tmin
        found = False
        while t < tmax:
            if r[t] > thr and r[t] >= 0.9 * rmax and r[t] >= r[t - 1] and r[t] >= r[t + 1]:
                found = True; break
            t += 1
        if not found:
            ts.append((s + N / 2) / fs); fs0.append(np.nan); continue
        den = r[t - 1] - 2 * r[t] + r[t + 1]
        tf = t + (0.5 * (r[t - 1] - r[t + 1]) / den if abs(den) > 1e-9 else 0)
        ts.append((s + N / 2) / fs); fs0.append(fs / tf)
    return np.array(ts), np.array(fs0)


def cents(f, ref):
    return 1200 * np.log2(f / ref)


def midi(f):
    return 69 + 12 * np.log2(f / 440.0)


def window(ts, f, t0, t1):
    m = (ts >= t0) & (ts <= t1) & ~np.isnan(f)
    return f[m]


def report_steady(name, target, t0=1.0, t1=3.5):
    fs = fs_of(name)
    ti, fi = f0_track(load(name, "in"), fs); to, fo = f0_track(load(name, "out"), fs)
    a = window(ti, fi, t0, t1); b = window(to, fo, t0 + 0.05, t1)
    print(f"[{name}] entrada {np.median(a):7.2f} Hz ({cents(np.median(a), target):+6.1f} c vs {target} Hz) | "
          f"salida {np.median(b):7.2f} Hz ({cents(np.median(b), target):+6.1f} c)  dispersión salida {np.std(cents(b, np.median(b))):.2f} c")


def lpc_peak(x, fs, order=14, lo=400, hi=1000):
    x = x * np.hanning(len(x))
    r = np.correlate(x, x, "full")[len(x) - 1:len(x) + order]
    a = solve_toeplitz(r[:order], r[1:order + 1])
    w, h = freqz([1.0], np.concatenate(([1.0], -a)), worN=4096, fs=fs)
    m = (w >= lo) & (w <= hi)
    return w[m][np.argmax(np.abs(h[m]))]


print("=== Afinación en nota fija ===")
report_steady("hard_steady", 220.0)
report_steady("boom_steady", 220.0)
report_steady("hard_steady_96k", 220.0, 1.0, 2.8)
report_steady("transparent", 440.0, 1.0, 2.5)
report_steady("low_voice", 98.0)          # G2 = 98 Hz
report_steady("high_voice", 587.33)       # D5 = 587.33 Hz (en C mayor, 600 Hz -> D5)

print("\n=== Vibrato +-50 c en G3 (Hard debería aplanarlo) ===")
ti, fi = f0_track(load("hard_vibrato", "in"), 48000); to, fo = f0_track(load("hard_vibrato", "out"), 48000)
a = window(ti, fi, 1, 3.8); b = window(to, fo, 1.05, 3.8)
print(f"desviación entrada {np.std(cents(a, 196.0)):.1f} c -> salida {np.std(cents(b, 196.0)):.1f} c, media salida {np.mean(cents(b, 196.0)):+.1f} c")

print("\n=== Glissando 170 -> 300 Hz (C mayor): distancia de la salida a la nota de la escala más cercana ===")
to, fo = f0_track(load("hard_glide", "out"), 48000)
m = ~np.isnan(fo) & (to > 0.7) & (to < 4.8)
mm = midi(fo[m])
scale = {0, 2, 4, 5, 7, 9, 11}
d = []
for v in mm:
    cands = [n for n in range(int(v) - 3, int(v) + 4) if n % 12 in scale]
    d.append(min(abs(v - n) for n in cands) * 100)
d = np.array(d)
print(f"error medio {d.mean():.1f} c | mediana {np.median(d):.1f} c | p90 {np.percentile(d, 90):.1f} c | cuadros {len(d)}")
print("notas visitadas (MIDI):", sorted(set(np.round(mm).astype(int))))

print("\n=== Formantes: amplitud de cada armónico vs. la curva teórica de la vocal sintética ===")
from scipy.signal import resample_poly, freqz as _fz
def vowel_db(f):
    tot = 0
    for fc, bw in [(700, 110), (1220, 130), (2600, 180)]:
        r = np.exp(-np.pi * bw / 48000); a1 = 2 * r * np.cos(2 * np.pi * fc / 48000); a2 = -r * r
        _, h = _fz([1 - r], [1, -a1, -a2], worN=2 * np.pi * np.asarray(f) / 48000)
        tot = tot + 20 * np.log10(np.abs(h))
    return tot
def harmonic_residual(x, f0, t0=2.0):
    seg = x[int(48000 * t0): int(48000 * t0) + 8192] * np.hanning(8192)
    X = np.abs(np.fft.rfft(seg, 8192 * 4)); fr = np.fft.rfftfreq(8192 * 4, 1 / 48000)
    ks = [k for k in range(1, 40) if k * f0 < 3500]
    amp = []; fk = []
    for k in ks:
        m = (fr > k * f0 - 15) & (fr < k * f0 + 15)
        amp.append(20 * np.log10(X[m].max() + 1e-12)); fk.append(k * f0)
    amp = np.array(amp); fk = np.array(fk)
    model = vowel_db(fk) - 1.2 * 20 * np.log10(fk)   # envolvente absoluta: filtro + caída de la fuente
    d = amp - model; d -= d.mean()
    return np.sqrt(np.mean(d ** 2))
fin = load("formant", "in"); fout = load("formant", "out")
rs = resample_poly(fin, 1190, 1000)
print(f"desvío vs. envolvente teórica -> entrada {harmonic_residual(fin, 261.63):.2f} dB | motor (PSOLA) {harmonic_residual(fout, 220.0):.2f} dB | remuestreo clásico {harmonic_residual(rs, 261.63/1.19):.2f} dB")
ti, fi = f0_track(fin, 48000); to, fo = f0_track(fout, 48000)
print(f"pitch entrada {np.nanmedian(window(ti, fi, 1, 3.5)):.1f} Hz -> salida {np.nanmedian(window(to, fo, 1.05, 3.5)):.1f} Hz (objetivo 220 Hz)")

print("\n=== Periodicidad de la salida (1.0 = onda perfectamente periódica, sin aspereza) ===")
def periodicity(x, fs, f0, t0, t1):
    N = int(0.05 * fs); T = fs / f0
    vals = []
    for s0 in range(int(t0 * fs), int(t1 * fs) - N - int(2 * T), int(0.02 * fs)):
        a = x[s0:s0 + N]
        best = 0
        for tt in range(int(T) - 2, int(T) + 4):
            b = x[s0 + tt: s0 + tt + N]
            best = max(best, np.dot(a, b) / np.sqrt(np.dot(a, a) * np.dot(b, b) + 1e-12))
        vals.append(best)
    return np.mean(vals), np.min(vals)
for nm, f0, a, b in [("hard_steady", 220.0, 1.2, 3.5), ("formant", 220.0, 1.2, 3.5), ("boom_steady", 216.29, 1.2, 3.5), ("high_voice", 587.33, 1.2, 2.8), ("low_voice", 98.0, 1.2, 2.8)]:
    m, mn = periodicity(load(nm, "out"), 48000, f0, a, b)
    print(f"[{nm}] periodicidad media {m:.4f}, peor cuadro {mn:.4f}")

print("\n=== Transparencia (nota exacta, cromático): ¿la salida es la entrada retrasada? ===")
x = load("transparent", "in"); y = load("transparent", "out")
seg = slice(48000, 48000 * 2 + 20000)
best = None
for lag in range(1400, 1700):
    xa = x[seg.start - lag: seg.stop - lag]; ya = y[seg]
    e = np.sum((ya - xa) ** 2) / np.sum(xa ** 2)
    if best is None or e < best[1]: best = (lag, e)
print(f"mejor retardo {best[0]} muestras (latencia declarada 1524), error relativo {10*np.log10(best[1]+1e-20):.1f} dB")

print("\n=== Frases con silencios (44.1 kHz) ===")
x = load("phrases", "in"); y = load("phrases", "out")
ti, fi = f0_track(y, 44100)
print(f"cuadros con pitch en la salida: {np.sum(~np.isnan(fi))}/{len(fi)}; máx |salida| dentro de silencios largos: ", end="")
gaps = []
for k in range(4):
    t0 = 1.5 * k + 1.15 + 0.032; t1 = 1.5 * k + 1.5 - 0.02   # tras el silencio + latencia
    if t1 * 44100 < len(y): gaps.append(np.max(np.abs(y[int(t0 * 44100): int(t1 * 44100)])))
print(f"{max(gaps):.4f}")

print("\n=== Ruido (sin pitch: no debería colorearse ni cambiar de nivel) ===")
x = load("noise", "in"); y = load("noise", "out")
a = x[48000:90000]; b = y[48000 + 1524:90000 + 1524]
print(f"nivel entrada {np.sqrt(np.mean(a**2)):.4f} salida {np.sqrt(np.mean(b**2)):.4f} (ratio {np.sqrt(np.mean(b**2))/np.sqrt(np.mean(a**2)):.3f}), picos {np.max(np.abs(y)):.3f}")
y = load("silence", "out")
print(f"silencio -> máx |salida| = {np.max(np.abs(y)):.6f}")
