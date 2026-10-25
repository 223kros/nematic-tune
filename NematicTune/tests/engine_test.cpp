// Banco de pruebas offline del motor: genera una "voz" sintética (pulsos glotales + 3 formantes),
// la pasa por el motor y guarda entrada/salida en archivos .f32 para analizarlos con analyze.py
//
//   g++ -O2 -std=c++17 engine_test.cpp -o engine_test && ./engine_test <carpeta_salida>

#include "../Source/AutotuneEngine.h"

#include <cstdio>
#include <functional>
#include <random>
#include <string>

using namespace nematic;

struct Resonator
{
    double a1 = 0, a2 = 0, g = 1, y1 = 0, y2 = 0;
    void set (double fs, double f, double bw)
    {
        const double r = std::exp (-M_PI * bw / fs);
        a1 = 2.0 * r * std::cos (2.0 * M_PI * f / fs);
        a2 = -r * r;
        g = 1.0 - r;
    }
    double tick (double x) { const double y = g * x + a1 * y1 + a2 * y2; y2 = y1; y1 = y; return y; }
};

struct Scenario
{
    std::string name;
    double fs = 48000.0;
    double seconds = 4.0;
    std::function<double (double)> f0;    // Hz (0 = silencio)
    EngineParams params;
    bool noiseOnly = false;
    double formantScale = 1.0;
};

static std::vector<float> synth (const Scenario& sc)
{
    const int N = (int) (sc.fs * sc.seconds);
    std::vector<float> out ((size_t) N, 0.0f);
    Resonator r1, r2, r3;
    r1.set (sc.fs, 700.0 * sc.formantScale, 110.0);
    r2.set (sc.fs, 1220.0 * sc.formantScale, 130.0);
    r3.set (sc.fs, 2600.0 * sc.formantScale, 180.0);
    std::mt19937 rng (1234);
    std::normal_distribution<double> nd (0.0, 1.0);

    double phase = 0.0, env = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const double t = i / sc.fs;
        const double f = sc.f0 (t);
        double x = 0.0;
        if (sc.noiseOnly)
            x = 0.15 * nd (rng);
        else if (f > 0.0)
        {
            phase += f / sc.fs; phase -= std::floor (phase);
            const int H = std::max (1, (int) std::min (40.0, 0.45 * sc.fs / f));
            for (int n = 1; n <= H; ++n) x += std::sin (2.0 * M_PI * n * phase) / std::pow ((double) n, 1.2);
            x *= 0.5;
        }
        const double target = (f > 0.0 || sc.noiseOnly) ? 1.0 : 0.0;
        env += (target - env) * 0.002;
        double y = r3.tick (r2.tick (r1.tick (x))) ;
        out[(size_t) i] = (float) (env * 3.0 * y + 0.0005 * nd (rng));
    }
    // normalizar a ~ -8 dBFS pico
    float pk = 0; for (float v : out) pk = std::max (pk, std::abs (v));
    if (pk > 0.01f) for (auto& v : out) v *= 0.4f / pk;
    return out;
}

static void writeF32 (const std::string& path, const std::vector<float>& v)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    std::fwrite (v.data(), sizeof (float), v.size(), f);
    std::fclose (f);
}

static int maskOf (std::initializer_list<int> pcs) { int m = 0; for (int p : pcs) m |= 1 << p; return m; }

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";

    const int cMajor = maskOf ({ 0, 2, 4, 5, 7, 9, 11 });
    const int onlyA  = maskOf ({ 9 });

    EngineParams hard;  hard.retuneMs = 0.0f;   hard.amount = 1.0f; hard.deadZone = 0.0f;
    EngineParams boom;  boom.retuneMs = 110.0f; boom.amount = 0.7f; boom.deadZone = 0.25f;

    std::vector<Scenario> list;
    auto add = [&] (std::string n, double fs, double secs, std::function<double (double)> f, EngineParams p, int mask,
                    bool noise = false, double fscale = 1.0)
    {
        Scenario s; s.name = n; s.fs = fs; s.seconds = secs; s.f0 = f; s.params = p; s.params.scaleMask = mask;
        s.noiseOnly = noise; s.formantScale = fscale; list.push_back (s);
    };

    // 1) nota fija desafinada (215 Hz, objetivo A3 = 220 Hz), modo Hard
    add ("hard_steady", 48000, 4, [] (double) { return 215.0; }, hard, cMajor);
    // 2) igual pero Boom Bap (solo correccion parcial)
    add ("boom_steady", 48000, 4, [] (double) { return 215.0; }, boom, cMajor);
    // 3) nota exacta, cromatico: transparencia
    EngineParams clean = hard; clean.amount = 1.0f;
    add ("transparent", 48000, 3, [] (double) { return 440.0; }, clean, 0xFFF);
    // 4) vibrato +-50 cents en torno a G3 (196 Hz), Hard => debe aplanarse
    add ("hard_vibrato", 48000, 4, [] (double t) { return 196.0 * std::pow (2.0, 0.5 * std::sin (2 * M_PI * 5.5 * t) / 12.0); }, hard, cMajor);
    // 5) glissando 170 -> 300 Hz en C mayor: la salida debe escalonarse en notas de la escala
    add ("hard_glide", 48000, 5, [] (double t) { return 170.0 * std::pow (300.0 / 170.0, t / 5.0); }, hard, cMajor);
    // 6) correccion grande con una sola nota permitida: C4 (261.6) -> A3 (220): -3 semitonos; formantes deben quedarse
    add ("formant", 48000, 4, [] (double) { return 261.63; }, hard, onlyA);
    // 7) frases con silencios
    add ("phrases", 44100, 6, [] (double t)
         { const double u = std::fmod (t, 1.5); return u < 1.0 ? 150.0 + 40.0 * u : 0.0; }, hard, cMajor);
    // 8) ruido y silencio
    add ("noise", 48000, 2, [] (double) { return 0.0; }, hard, cMajor, true);
    add ("silence", 48000, 1, [] (double) { return 0.0; }, hard, cMajor);
    // 9) 96 kHz
    add ("hard_steady_96k", 96000, 3, [] (double) { return 215.0; }, hard, cMajor);
    // 10) voz grave 95 Hz y aguda 600 Hz
    add ("low_voice", 48000, 3, [] (double) { return 95.0; }, hard, maskOf ({ 7 }));
    add ("high_voice", 48000, 3, [] (double) { return 600.0; }, hard, cMajor);

    for (auto& sc : list)
    {
        AutotuneEngine eng;
        eng.prepare (sc.fs, 1);
        eng.setParams (sc.params);

        std::vector<float> in = synth (sc), out = in;
        const int block = 480;
        for (size_t pos = 0; pos < in.size(); pos += block)
        {
            const int n = (int) std::min<size_t> (block, in.size() - pos);
            float* ch = out.data() + pos;
            eng.process (&ch, n);
        }
        bool bad = false; for (float v : out) if (! std::isfinite (v)) bad = true;
        writeF32 (dir + "/" + sc.name + "_in.f32", in);
        writeF32 (dir + "/" + sc.name + "_out.f32", out);
        std::printf ("%s fs=%d latency=%d samples (%.1f ms) finite=%s\n", sc.name.c_str(), (int) sc.fs,
                     eng.getLatency(), 1000.0 * eng.getLatency() / sc.fs, bad ? "NO" : "si");
    }
    return 0;
}
