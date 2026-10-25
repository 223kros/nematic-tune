#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include "NeonLookAndFeel.h"

// Visor de pitch estilo piano-roll: muestra la nota detectada (cian) y la corregida (rosa),
// con las notas de la escala resaltadas.
class PitchDisplay : public juce::Component, private juce::Timer
{
public:
    struct Frame { float inMidi, outMidi; bool voiced; int scaleMask; };

    explicit PitchDisplay (std::function<Frame()> provider) : getFrame (std::move (provider))
    {
        setOpaque (true);
        for (auto& v : inHist)  v = kNaN;
        for (auto& v : outHist) v = kNaN;
        startTimerHz (40);
    }

    ~PitchDisplay() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const float w = area.getWidth(), h = area.getHeight();
        const float rowsVisible = 18.0f;
        const float bottomMidi = centre - rowsVisible * 0.5f;
        auto yOf = [&] (float midi) { return h - (midi - bottomMidi) / rowsVisible * h; };

        // fondo
        juce::ColourGradient bg (neon::bgTop, 0.0f, 0.0f, neon::bgDeep, 0.0f, h, false);
        g.setGradientFill (bg);
        g.fillRect (area);

        juce::Graphics::ScopedSaveState clipState (g);
        g.reduceClipRegion (getLocalBounds());

        // filas de notas
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int k0 = (int) std::floor (bottomMidi) - 1, k1 = (int) std::ceil (bottomMidi + rowsVisible) + 1;
        for (int k = k0; k <= k1; ++k)
        {
            const int pc = ((k % 12) + 12) % 12;
            const bool inScale = (frame.scaleMask & (1 << pc)) != 0;
            const float yc = yOf ((float) k);
            const float rowH = h / rowsVisible;

            if (inScale)
            {
                g.setColour (neon::violet.withAlpha (0.11f));
                g.fillRect (0.0f, yc - rowH * 0.5f + 1.0f, w, rowH - 2.0f);
                g.setColour (neon::violet.withAlpha (0.45f));
                g.drawHorizontalLine ((int) yc, 0.0f, w);

                g.setColour (neon::text.withAlpha (0.78f));
                g.setFont (neon::font (11.0f, true));
                g.drawText (juce::String (names[pc]) + juce::String ((k / 12) - 1), 6, (int) (yc - 8.0f), 38, 16,
                            juce::Justification::centredLeft);
            }
            else
            {
                g.setColour (neon::violet.withAlpha (0.06f));
                g.drawHorizontalLine ((int) yc, 0.0f, w);
            }
        }

        // verticales de tiempo (estilo grilla)
        g.setColour (neon::violet.withAlpha (0.06f));
        for (int i = 1; i < 8; ++i)
            g.drawVerticalLine ((int) (w * i / 8.0f), 0.0f, h);

        // curvas
        auto buildPath = [&] (const std::array<float, kHistory>& hist)
        {
            juce::Path p;
            bool open = false;
            for (int i = 0; i < kHistory; ++i)
            {
                const int idx = (head + i) % kHistory;           // más viejo -> más nuevo
                const float v = hist[(size_t) idx];
                if (std::isnan (v)) { open = false; continue; }
                const float x = w * (float) i / (float) (kHistory - 1);
                const float y = juce::jlimit (-6.0f, h + 6.0f, yOf (v));
                if (! open) { p.startNewSubPath (x, y); open = true; }
                else p.lineTo (x, y);
            }
            return p;
        };

        neon::glowStroke (g, buildPath (inHist), neon::cyan, 1.6f);
        neon::glowStroke (g, buildPath (outHist), neon::magenta, 2.2f);

        // punto actual
        const int last = (head + kHistory - 1) % kHistory;
        if (! std::isnan (outHist[(size_t) last]))
        {
            const float px = w - 4.0f, py = juce::jlimit (4.0f, h - 4.0f, yOf (outHist[(size_t) last]));
            g.setColour (neon::magenta.withAlpha (0.25f)); g.fillEllipse (px - 11.0f, py - 11.0f, 22.0f, 22.0f);
            g.setColour (neon::magenta.withAlpha (0.55f)); g.fillEllipse (px - 7.0f, py - 7.0f, 14.0f, 14.0f);
            g.setColour (juce::Colours::white);             g.fillEllipse (px - 3.5f, py - 3.5f, 7.0f, 7.0f);
        }

        // lectura numérica
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (46.0f, 4.0f, 160.0f, 38.0f, 8.0f);
        g.setFont (neon::font (12.0f, true));
        g.setColour (neon::cyan);
        g.drawText ("IN   " + describe (frame.inMidi, frame.voiced), 52, 6, 150, 16, juce::Justification::centredLeft);
        g.setColour (neon::pink);
        g.drawText ("OUT  " + describe (frame.outMidi, frame.voiced), 52, 22, 150, 16, juce::Justification::centredLeft);

        // marco
        g.setColour (neon::violet.withAlpha (0.8f));
        g.drawRoundedRectangle (area.reduced (0.7f), 10.0f, 1.4f);
    }

private:
    static constexpr int kHistory = 160;
    static constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    static juce::String describe (float midi, bool voiced)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        if (! voiced) return "--";
        const int n = (int) std::lround (midi);
        const int cents = (int) std::lround ((midi - (float) n) * 100.0f);
        return juce::String (names[((n % 12) + 12) % 12]) + juce::String ((n / 12) - 1)
             + (cents >= 0 ? "  +" : "  ") + juce::String (cents) + "c";
    }

    void timerCallback() override
    {
        frame = getFrame();
        const float vIn  = frame.voiced ? frame.inMidi  : kNaN;
        const float vOut = frame.voiced ? frame.outMidi : kNaN;

        inHist[(size_t) head]  = vIn;
        outHist[(size_t) head] = vOut;
        head = (head + 1) % kHistory;

        // centro del visor: sigue la mediana de lo reciente
        float sum = 0.0f; int cnt = 0;
        for (int i = 0; i < 48; ++i)
        {
            const float v = outHist[(size_t) ((head + kHistory - 1 - i) % kHistory)];
            if (! std::isnan (v)) { sum += v; ++cnt; }
        }
        if (cnt > 0) centre += 0.06f * ((sum / (float) cnt) - centre);

        repaint();
    }

    std::function<Frame()> getFrame;
    Frame frame { 60.0f, 60.0f, false, 0xFFF };
    std::array<float, kHistory> inHist, outHist;
    int head = 0;
    float centre = 60.0f;
};
