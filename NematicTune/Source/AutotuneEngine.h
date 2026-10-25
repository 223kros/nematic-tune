// Nematic Tune - motor de autotune (DSP puro, sin dependencias de JUCE)
//
//  1) Detección de pitch: YIN sobre señal diezmada + refinamiento a frecuencia completa.
//  2) Cuantización a tonalidad/escala con histéresis, zona muerta, cantidad y velocidad de retune.
//  3) Cambio de tono con PSOLA simplificado (granos de 2 períodos, solapados con Hann).
//     Como cada grano se copia sin remuestrear, los formantes de la voz NO se mueven
//     (la voz no suena "ardilla" cuando corrige varios semitonos).
//
//  Latencia fija: se informa al host con getLatency().

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace nematic
{

struct EngineParams
{
    int   scaleMask = 0xFFF;   // bit i = clase de nota i permitida (0 = Do/C)
    float retuneMs  = 0.0f;    // tiempo de retune (0 = instantáneo)
    float amount    = 1.0f;    // 0..1 cuánto se corrige el error
    float deadZone  = 0.0f;    // semitonos que se dejan sin tocar (naturalidad)
    float wet       = 1.0f;    // 0..1
    float outGain   = 1.0f;    // lineal
};

class AutotuneEngine
{
public:
    static constexpr float kMinHz = 80.0f;
    static constexpr float kMaxHz = 900.0f;

    // Telemetría para la interfaz (escrita desde el hilo de audio)
    float telemetryInMidi  = 60.0f;
    float telemetryOutMidi = 60.0f;
    bool  telemetryVoiced  = false;

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        nch = std::max (1, std::min (numChannels, kMaxChannels));

        decim   = std::max (1, (int) std::floor (fs / 12000.0));
        fsD     = fs / decim;
        tauMaxD = (int) std::ceil (fsD / kMinHz) + 2;
        tauMinD = std::max (2, (int) std::floor (fsD / kMaxHz));
        winY    = (int) std::ceil (0.016 * fsD);
        winY    = std::max (winY, tauMaxD + 8);
        hop     = std::max (64, (int) std::lround (0.0055 * fs));

        tMax = std::ceil (fs / kMinHz);
        tMin = std::floor (fs / kMaxHz);
        latency = (int) std::ceil (2.5 * tMax) + 24;

        const int ring = 1 << 15;           // 32768 muestras por canal
        ringMask = ring - 1;
        inRing.assign ((size_t) nch, std::vector<float> ((size_t) ring, 0.0f));
        acc.assign ((size_t) nch, std::vector<float> ((size_t) ring, 0.0f));
        normRing.assign ((size_t) ring, 0.0f);
        monoRing.assign ((size_t) ring, 0.0f);

        decRing.assign (4096, 0.0f);
        decMask = 4095;

        xs.assign ((size_t) (winY + tauMaxD + 4), 0.0f);
        dBuf.assign ((size_t) (tauMaxD + 4), 0.0f);
        dFull.reserve ((size_t) (2 * decim + 16));
        cmnd.assign ((size_t) (tauMaxD + 4), 0.0f);

        reset();
    }

    void reset()
    {
        for (auto& v : inRing) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : acc)    std::fill (v.begin(), v.end(), 0.0f);
        std::fill (normRing.begin(), normRing.end(), 0.0f);
        std::fill (monoRing.begin(), monoRing.end(), 0.0f);
        std::fill (decRing.begin(), decRing.end(), 0.0f);

        W = 0;
        decCount = 0; decAccum = 0.0f; decPhase = 0; hopCounter = 0;
        trackCount = 0;
        haveLast = false; lastWasVoiced = false; jumpCount = 0; unvoicedRun = 0;
        lastMidi = 60.0; lastT = 0.0;

        started = false;
        Tuse = std::min (tMax, std::max (tMin, std::floor (fs * 0.005)));
        corr = 0.0; oFloat = 0.0; sPrev = 0.0; lastPout = Tuse; nextLeft = -1.0e18;
        noteValid = false; curNote = 60;

        wetSm = params.wet; gainSm = params.outGain;
        telemetryVoiced = false;
    }

    int getLatency() const { return latency; }

    void setParams (const EngineParams& p)
    {
        params = p;
        if (params.scaleMask == 0) params.scaleMask = 0xFFF;
    }

    // Procesa en el lugar. channels = punteros a cada canal.
    void process (float* const* channels, int numSamples)
    {
        const int nc = nch;
        const float smoothCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) fs));

        for (int n = 0; n < numSamples; ++n)
        {
            const long long idx = W & ringMask;

            float mono = 0.0f;
            for (int c = 0; c < nc; ++c)
            {
                float x = channels[c][n];
                if (! std::isfinite (x)) x = 0.0f;
                inRing[(size_t) c][(size_t) idx] = x;
                mono += x;
            }
            mono /= (float) nc;
            monoRing[(size_t) idx] = mono;

            // diezmado (promedio simple) para el detector
            decAccum += mono;
            if (++decPhase >= decim)
            {
                decRing[(size_t) (decCount & decMask)] = decAccum / (float) decim;
                ++decCount;
                decAccum = 0.0f; decPhase = 0;
            }

            if (++hopCounter >= hop)
            {
                hopCounter = 0;
                analyze();
            }

            // colocar granos que ya toca poner
            int guard = 0;
            while (nextLeft <= (double) W + kMargin && guard++ < 8)
                placeGrain();

            // salida
            wetSm  += smoothCoef * (params.wet - wetSm);
            gainSm += smoothCoef * (params.outGain - gainSm);

            const float nrm = normRing[(size_t) idx];
            const float invN = nrm > 1.0e-3f ? 1.0f / nrm : 0.0f;
            const long long dryIdx = (W - latency) & ringMask;

            for (int c = 0; c < nc; ++c)
            {
                float wetS = acc[(size_t) c][(size_t) idx] * invN;
                float dryS = (W >= latency) ? inRing[(size_t) c][(size_t) dryIdx] : 0.0f;
                float y = gainSm * (wetSm * wetS + (1.0f - wetSm) * dryS);
                if (! std::isfinite (y)) y = 0.0f;
                channels[c][n] = y;
                acc[(size_t) c][(size_t) idx] = 0.0f;
            }
            normRing[(size_t) idx] = 0.0f;

            ++W;
        }
    }

private:
    static constexpr int    kMaxChannels = 2;
    static constexpr double kMargin = 16.0;

    struct TrackEntry
    {
        double tC = 0.0;       // instante central (muestras absolutas)
        double midi = 60.0;
        double T = 0.0;        // período en muestras
        bool   voiced = false;
    };

    // ---------------------------------------------------------------- detector
    void analyze()
    {
        const int Ntot = winY + tauMaxD;
        if (decCount < (long long) Ntot + 4) return;

        for (int i = 0; i < Ntot; ++i)
            xs[(size_t) i] = decRing[(size_t) ((decCount - Ntot + i) & decMask)];

        // energía
        double e = 0.0;
        for (int i = Ntot - winY; i < Ntot; ++i) e += (double) xs[(size_t) i] * xs[(size_t) i];
        const double rms = std::sqrt (e / winY);

        TrackEntry te;
        te.tC = (double) W - 0.5 * Ntot * decim;

        bool voicedNow = false;
        double Tmeas = 0.0;

        if (rms > 0.0025)
        {
            // función de diferencia
            for (int tau = 1; tau <= tauMaxD; ++tau)
            {
                double d = 0.0;
                const float* a = xs.data();
                const float* b = xs.data() + tau;
                for (int j = 0; j < winY; ++j)
                {
                    const float df = a[j] - b[j];
                    d += (double) df * df;
                }
                dBuf[(size_t) tau] = (float) d;
            }

            // diferencia normalizada por media acumulada
            cmnd[0] = 1.0f;
            double run = 0.0;
            for (int tau = 1; tau <= tauMaxD; ++tau)
            {
                run += dBuf[(size_t) tau];
                cmnd[(size_t) tau] = run > 0.0 ? (float) (dBuf[(size_t) tau] * tau / run) : 1.0f;
            }

            int best = -1;
            for (int tau = tauMinD; tau < tauMaxD; ++tau)
            {
                if (cmnd[(size_t) tau] < kYinThreshold)
                {
                    while (tau + 1 < tauMaxD && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau]) ++tau;
                    best = tau;
                    break;
                }
            }

            if (best > 1 && best < tauMaxD)
            {
                // interpolación parabólica en la señal diezmada
                double tauF = (double) best;
                const double d0 = dBuf[(size_t) (best - 1)], d1 = dBuf[(size_t) best], d2 = dBuf[(size_t) (best + 1)];
                const double den = d0 - 2.0 * d1 + d2;
                if (std::abs (den) > 1.0e-12) tauF += 0.5 * (d0 - d2) / den;

                double tFull = tauF * decim;

                // refinamiento a frecuencia completa
                const int tc = (int) std::lround (tFull);
                const int lo = std::max (2, tc - decim - 1);
                const int hi = tc + decim + 1;
                const int winF = winY * decim;
                if (W > (long long) (winF + hi + 8))
                {
                    const long long base = W - (winF + hi) + 1;
                    double bestD = 1.0e300; int bestT = tc;
                    std::vector<double>& dd = dFull;
                    dd.assign ((size_t) (hi - lo + 3), 0.0);
                    for (int t = lo - 1; t <= hi + 1; ++t)
                    {
                        if (t < 1) continue;
                        double d = 0.0;
                        for (int j = 0; j < winF; ++j)
                        {
                            const float a = monoRing[(size_t) ((base + j) & ringMask)];
                            const float b = monoRing[(size_t) ((base + j + t) & ringMask)];
                            const double df = (double) a - b;
                            d += df * df;
                        }
                        dd[(size_t) (t - lo + 1)] = d;
                        if (t >= lo && t <= hi && d < bestD) { bestD = d; bestT = t; }
                    }
                    const size_t k = (size_t) (bestT - lo + 1);
                    const double f0 = dd[k - 1], f1 = dd[k], f2 = dd[k + 1];
                    const double den2 = f0 - 2.0 * f1 + f2;
                    double tRef = (double) bestT;
                    if (std::abs (den2) > 1.0e-12 && bestT > lo && bestT < hi)
                        tRef += 0.5 * (f0 - f2) / den2;
                    if (std::abs (tRef - tFull) < 2.0 * decim) tFull = tRef;
                }

                if (tFull >= tMin && tFull <= tMax)
                {
                    voicedNow = true;
                    Tmeas = tFull;
                }
            }
        }

        if (voicedNow)
        {
            double midiRaw = 69.0 + 12.0 * std::log2 ((fs / Tmeas) / 440.0);

            if (haveLast && lastWasVoiced && std::abs (midiRaw - lastMidi) > 5.0 && jumpCount < 2)
            {
                // salto sospechoso (posible error de octava): mantener y esperar confirmación
                ++jumpCount;
                te.midi = lastMidi; te.T = lastT; te.voiced = true;
            }
            else
            {
                jumpCount = 0;
                if (haveLast && lastWasVoiced && std::abs (midiRaw - lastMidi) < 0.8)
                {
                    te.midi = lastMidi + 0.6 * (midiRaw - lastMidi);
                    te.T    = lastT + 0.6 * (Tmeas - lastT);
                }
                else { te.midi = midiRaw; te.T = Tmeas; }
                te.voiced = true;
                lastMidi = te.midi; lastT = te.T;
                haveLast = true; lastWasVoiced = true; unvoicedRun = 0;
            }
        }
        else
        {
            te.voiced = false;
            te.midi = lastMidi; te.T = lastT;
            if (++unvoicedRun >= 3) { lastWasVoiced = false; jumpCount = 0; }
        }

        track[(size_t) (trackCount & (kTrackSize - 1))] = te;
        ++trackCount;
    }

    TrackEntry lookup (double tq) const
    {
        TrackEntry r;
        if (trackCount == 0) return r;

        long long i = trackCount - 1;
        const long long oldest = std::max<long long> (0, trackCount - kTrackSize);
        while (i > oldest && track[(size_t) (i & (kTrackSize - 1))].tC > tq) --i;
        r = track[(size_t) (i & (kTrackSize - 1))];
        if (i + 1 < trackCount)
        {
            const TrackEntry& nx = track[(size_t) ((i + 1) & (kTrackSize - 1))];
            if (std::abs (nx.tC - tq) < std::abs (r.tC - tq)) r = nx;
        }
        return r;
    }

    // ---------------------------------------------------------------- cuantizador
    int pickNote (double m)
    {
        const int base = (int) std::floor (m);
        int nearest = base; double bestDist = 1.0e9;
        for (int n = base - 4; n <= base + 5; ++n)
        {
            const int pc = ((n % 12) + 12) % 12;
            if (! (params.scaleMask & (1 << pc))) continue;
            const double dist = std::abs (m - n);
            if (dist < bestDist) { bestDist = dist; nearest = n; }
        }

        if (noteValid)
        {
            const int pcCur = ((curNote % 12) + 12) % 12;
            const bool allowed = (params.scaleMask & (1 << pcCur)) != 0;
            if (allowed && std::abs (m - curNote) <= bestDist + 0.25 && std::abs (m - curNote) < 1.6)
                return curNote;
        }
        curNote = nearest; noteValid = true;
        return nearest;
    }

    // ---------------------------------------------------------------- granos
    static inline float hermite (const std::vector<float>& buf, long long i, float f, long long mask)
    {
        const float xm1 = buf[(size_t) ((i - 1) & mask)];
        const float x0  = buf[(size_t) (i & mask)];
        const float x1  = buf[(size_t) ((i + 1) & mask)];
        const float x2  = buf[(size_t) ((i + 2) & mask)];
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float b = w + a;
        return ((a * f - b) * f + c) * f + x0;
    }

    void placeGrain()
    {
        // 1) estimación de pitch cercana al contenido que vamos a copiar
        const double tq = (started ? oFloat + lastPout : (double) W) - latency;
        const TrackEntry info = lookup (tq);
        const bool v = info.voiced;

        if (v)
        {
            const double tt = std::min (tMax, std::max (tMin, info.T));
            Tuse = started ? Tuse + 0.6 * (tt - Tuse) : tt;
        }
        else
        {
            noteUnvoiced();
        }

        // 2) corrección deseada
        double corrTarget = 0.0;
        if (v)
        {
            const int note = pickNote (info.midi);
            const double err = (double) note - info.midi;
            const double aerr = std::abs (err);
            const double eff = aerr > params.deadZone ? aerr - params.deadZone : 0.0;
            corrTarget = (err < 0 ? -eff : eff) * (double) params.amount;
        }

        const double tauSamp = (v ? (double) params.retuneMs : 15.0) * 0.001 * fs;
        const double a = tauSamp < 1.0 ? 1.0 : 1.0 - std::exp (-lastPout / tauSamp);
        corr += a * (corrTarget - corr);
        corr = std::min (7.0, std::max (-7.0, corr));

        const double ratio = std::exp2 (corr / 12.0);
        const double Pout = Tuse / ratio;
        const int G = std::max (8, (int) std::ceil (Tuse));

        // 3) posición del grano en la salida
        const double oF = started ? oFloat + Pout : (double) W + kMargin + G;
        const long long oI = std::llround (oF);

        // 4) de dónde lo copiamos: la latencia total se mantiene fija
        const double sTarget = oF - latency;
        double s = sTarget;
        if (started)
        {
            double m = std::round ((sTarget - sPrev) / Tuse);
            m = std::min (3.0, std::max (0.0, m));
            s = sPrev + m * Tuse;
        }
        const double sMax = (double) W - G - 4.0;
        if (s > sMax) s -= std::ceil ((s - sMax) / Tuse) * Tuse;
        const double sMin = (double) W - (double) (ringMask - G - 64);
        if (s < sMin) s = sMin;

        const double sFrac = s + (double) oI - oF;

        // 5) mezcla del grano con ventana de Hann
        const long long j0 = std::max<long long> (-G, (long long) W - oI);
        const double pi = 3.14159265358979323846;
        for (long long j = j0; j <= G; ++j)
        {
            const float w = (float) (0.5 * (1.0 + std::cos (pi * (double) j / G)));
            if (w <= 0.0f) continue;
            const size_t oi = (size_t) ((oI + j) & ringMask);
            normRing[oi] += w;

            const double pos = sFrac + (double) j;
            const long long i0 = (long long) std::floor (pos);
            const float fr = (float) (pos - (double) i0);
            for (int c = 0; c < nch; ++c)
                acc[(size_t) c][oi] += w * hermite (inRing[(size_t) c], i0, fr, ringMask);
        }

        // 6) estado y telemetría
        telemetryVoiced  = v;
        telemetryInMidi  = (float) info.midi;
        telemetryOutMidi = (float) (info.midi + corr);

        started = true;
        sPrev = s;
        oFloat = oF;
        lastPout = Pout;
        nextLeft = oF + Pout - G;
    }

    void noteUnvoiced() { noteValid = false; }

    // ---------------------------------------------------------------- datos
    static constexpr float kYinThreshold = 0.22f;
    static constexpr int   kTrackSize = 256;

    EngineParams params;
    double fs = 48000.0;
    int nch = 1;

    // buffers
    long long ringMask = 0;
    std::vector<std::vector<float>> inRing, acc;
    std::vector<float> normRing, monoRing;
    long long W = 0;

    // detector
    int decim = 4, tauMaxD = 150, tauMinD = 13, winY = 192, hop = 264;
    double fsD = 12000.0;
    std::vector<float> decRing; long long decMask = 0, decCount = 0;
    float decAccum = 0.0f; int decPhase = 0, hopCounter = 0;
    std::vector<float> xs, dBuf, cmnd;
    std::vector<double> dFull;
    double tMax = 600.0, tMin = 53.0;
    int latency = 1524;

    TrackEntry track[kTrackSize];
    long long trackCount = 0;
    bool haveLast = false, lastWasVoiced = false;
    int jumpCount = 0, unvoicedRun = 0;
    double lastMidi = 60.0, lastT = 0.0;

    // síntesis
    bool started = false;
    double Tuse = 240.0, corr = 0.0, oFloat = 0.0, sPrev = 0.0, lastPout = 240.0, nextLeft = -1.0e18;
    bool noteValid = false; int curNote = 60;

    float wetSm = 1.0f, gainSm = 1.0f;
};

} // namespace nematic
