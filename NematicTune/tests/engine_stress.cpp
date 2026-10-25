// Pruebas de robustez: bloques variables, estéreo, cambios de parámetros, CPU.
#include "../Source/AutotuneEngine.h"
#include <chrono>
#include <cstdio>
#include <random>
using namespace nematic;

static std::vector<float> voice (double fs, double secs, double f0)
{
    std::vector<float> v ((size_t) (fs * secs));
    double ph = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        double f = f0 * std::pow (2.0, 0.3 * std::sin (2 * M_PI * 5.0 * i / fs) / 12.0);
        ph += f / fs; ph -= std::floor (ph);
        double x = 0; for (int n = 1; n <= 25; ++n) x += std::sin (2 * M_PI * n * ph) / n;
        v[i] = (float) (0.2 * x);
    }
    return v;
}

int main()
{
    const double fs = 48000; const auto in = voice (fs, 6.0, 207.0);
    EngineParams p; p.scaleMask = 0xAB5; p.retuneMs = 30; p.amount = 0.9f; p.deadZone = 0.1f;   // escala mayor

    // 1) mismo resultado con cualquier tamaño de bloque (estéreo, canales idénticos)
    auto run = [&] (std::vector<int> sizes)
    {
        AutotuneEngine e; e.prepare (fs, 2); e.setParams (p);
        std::vector<float> L = in, R = in;
        size_t pos = 0, k = 0;
        while (pos < L.size())
        {
            int n = (int) std::min<size_t> ((size_t) sizes[k++ % sizes.size()], L.size() - pos);
            float* ch[2] = { L.data() + pos, R.data() + pos };
            e.process (ch, n); pos += (size_t) n;
        }
        double diffLR = 0; for (size_t i = 0; i < L.size(); ++i) diffLR = std::max (diffLR, (double) std::abs (L[i] - R[i]));
        return std::make_pair (L, diffLR);
    };
    auto a = run ({ 480 }), b = run ({ 1, 7, 64, 513, 4096, 33, 1024 }), c = run ({ 64 });
    double d1 = 0, d2 = 0;
    for (size_t i = 0; i < a.first.size(); ++i) { d1 = std::max (d1, (double) std::abs (a.first[i] - b.first[i])); d2 = std::max (d2, (double) std::abs (a.first[i] - c.first[i])); }
    std::printf ("bloques 480 vs variables: dif max = %g | 480 vs 64: %g | dif max L-R = %g\n", d1, d2, a.second);

    // 2) cambios de parámetros agresivos mientras suena (sin NaN, sin saltos enormes)
    {
        AutotuneEngine e; e.prepare (fs, 2); e.setParams (p);
        std::vector<float> L = in, R = in; std::mt19937 rng (7);
        float pk = 0; bool bad = false;
        for (size_t pos = 0; pos + 256 <= L.size(); pos += 256)
        {
            EngineParams q = p; q.scaleMask = 1 << (rng() % 12) | 1 << (rng() % 12); q.retuneMs = (float) (rng() % 400);
            q.amount = (rng() % 101) / 100.0f; q.wet = (rng() % 101) / 100.0f; q.outGain = 0.5f + (rng() % 100) / 100.0f; q.deadZone = (rng() % 50) / 100.0f;
            e.setParams (q);
            float* ch[2] = { L.data() + pos, R.data() + pos }; e.process (ch, 256);
            for (int i = 0; i < 256; ++i) { pk = std::max (pk, std::abs (L[pos + i])); if (! std::isfinite (L[pos + i])) bad = true; }
        }
        std::printf ("parametros al azar: pico max %.3f, NaN/inf: %s\n", pk, bad ? "SI" : "no");
    }

    // 3) re-prepare con otras frecuencias de muestreo y mono
    for (double r : { 22050.0, 44100.0, 88200.0, 192000.0 })
    {
        AutotuneEngine e; e.prepare (r, 1); e.setParams (p);
        auto v = voice (r, 1.5, 150.0); float* ch[1] = { v.data() }; e.process (ch, (int) v.size());
        float pk = 0; bool bad = false; for (float x : v) { pk = std::max (pk, std::abs (x)); if (! std::isfinite (x)) bad = true; }
        std::printf ("fs=%6.0f latencia %5d (%.1f ms) pico %.3f nan=%s\n", r, e.getLatency(), 1000.0 * e.getLatency() / r, pk, bad ? "SI" : "no");
    }

    // 4) entradas extremas: DC, clip, impulsos, NaN
    {
        AutotuneEngine e; e.prepare (fs, 1); e.setParams (p);
        std::vector<float> v (48000, 0.0f);
        for (size_t i = 0; i < v.size(); ++i) v[i] = (i < 12000) ? 1.0f : (i < 24000 ? (float) ((i / 50) % 2 ? 1 : -1) : (i % 4801 == 0 ? 1.0f : 0.0f));
        v[30000] = std::numeric_limits<float>::quiet_NaN(); v[31000] = std::numeric_limits<float>::infinity();
        float* ch[1] = { v.data() }; e.process (ch, (int) v.size());
        bool bad = false; float pk = 0; for (float x : v) { if (! std::isfinite (x)) bad = true; pk = std::max (pk, std::abs (x)); }
        std::printf ("DC/cuadrada/impulsos/NaN: pico %.2f, salida no finita: %s\n", pk, bad ? "SI" : "no");
    }

    // 5) CPU (estéreo 48k): cuántas veces más rápido que tiempo real
    {
        AutotuneEngine e; e.prepare (fs, 2); e.setParams (p);
        std::vector<float> L = in, R = in;
        auto t0 = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos + 512 <= L.size(); pos += 512) { float* ch[2] = { L.data() + pos, R.data() + pos }; e.process (ch, 512); }
        double dt = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("CPU: %.1fs de audio estéreo en %.3fs -> %.0fx tiempo real (%.2f%% de un núcleo)\n", 6.0, dt, 6.0 / dt, 100.0 * dt / 6.0);
    }
}
