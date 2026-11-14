/*
    Offline checks for DelayEngine: things that are hard to hear reliably or
    dangerous to get wrong by ear (Runaway's bound, Tapestop's exact return).

        cmake --build build --target EngineTests
        ./build/EngineTests_artefacts/Debug/EngineTests

    Exits non-zero if any check fails.
*/

#include <juce_dsp/juce_dsp.h>
#include "DelayEngine.h"
#include <cstdio>
#include <functional>

namespace
{
constexpr float sr = 48000.0f;
constexpr int blockSize = 32;
int failures = 0;

void check (const char* name, bool ok, const char* detail)
{
    std::printf ("%-58s %s  %s\n", name, ok ? "ok  " : "FAIL", detail);
    if (! ok) ++failures;
}

struct Setup
{
    int channels = 2;
    float delayMs = 300.0f, feedback = 0.5f, wear = 0.0f, runawayDrive = 0.5f, diffusion = 0.0f, shimmer = 0.0f;
    int style = 0, shimmerSemitones = 12, modType = 0;
    bool pingPong = false, filters = false, modInLoop = true;
    int outputChannel = 0;
};

using Events = std::function<void (DelayEngine&, float seconds)>;
using Input  = std::function<float (long sample)>;

/** Runs the engine with mix at 100% and returns the left output (or Setup::outputChannel). */
std::vector<float> run (const Setup& s, float seconds, const Input& input, const Events& events = {})
{
    DelayEngine e;
    e.prepare (sr, blockSize, s.channels, 5000.0f, 100.0f);
    e.setDelayMs (s.delayMs); e.setDelaySmoothingMs (0); e.setFeedback (s.feedback); e.setMix (1.0f);
    e.setLowCutHz (s.filters ? 200.0f : 0.0f); e.setHighCutHz (s.filters ? 3000.0f : 0.0f);
    e.setFiltersInFeedbackLoop (true); e.setFrozen (false); e.setFreezeFadeMs (20); e.setInputSend (1.0f);
    e.setPingPong (s.pingPong); e.setStereoWidthMs (0); e.setWear (s.wear);
    e.setRunaway (false); e.setRunawayDrive (s.runawayDrive); e.setTapestop (false); e.setTapestopTimeMs (500);
    e.setReverse (false); e.setDiffusion (s.diffusion);
    e.setStyle (s.style); e.setShimmer (s.shimmer); e.setShimmerSemitones ((float) s.shimmerSemitones);
    e.setModulationType (s.modType); e.setModulationRateHz (0.6f); e.setModulationDepth (0.7f); e.setModulationInFeedbackLoop (s.modInLoop);
    e.setGroove (0.0f); e.setFeelMs (0.0f);
    e.reset();

    std::vector<float> out;
    juce::AudioBuffer<float> buffer (s.channels, blockSize);
    long n = 0;

    for (int b = 0; b < (int) (seconds * sr / blockSize); ++b)
    {
        if (events)
            events (e, (float) n / sr);

        for (int i = 0; i < blockSize; ++i, ++n)
            for (int ch = 0; ch < s.channels; ++ch)
                buffer.setSample (ch, i, input (n));

        e.process (buffer);

        for (int i = 0; i < blockSize; ++i)
            out.push_back (buffer.getSample (s.outputChannel, i));
    }

    return out;
}

Input sine()  { return [] (long n) { return 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) n / sr); }; }
Input dc (float v) { return [v] (long) { return v; }; }

Input noiseBurst()
{
    auto random = std::make_shared<juce::Random> (1);
    return [random] (long n) { return n < (long) (0.2f * sr) ? random->nextFloat() * 2.0f - 1.0f : 0.0f; };
}

Input loudNoise()
{
    auto random = std::make_shared<juce::Random> (2);
    return [random] (long) { return random->nextFloat() * 2.0f - 1.0f; };
}

/** Peak from `from` seconds on; non-finite samples count as huge. */
float peak (const std::vector<float>& v, float from = 0.0f)
{
    float m = 0.0f;
    for (auto i = (size_t) (from * sr); i < v.size(); ++i) m = std::isfinite (v[i]) ? std::max (m, std::abs (v[i])) : 1e9f;
    return m;
}

float rms (const std::vector<float>& v, float from, float to)
{
    double sum = 0.0; int count = 0;
    for (auto i = (size_t) (from * sr); i < (size_t) (to * sr) && i < v.size(); ++i, ++count)
        sum += (double) v[i] * v[i];
    return (float) std::sqrt (sum / std::max (1, count));
}

float maxDiff (const std::vector<float>& a, const std::vector<float>& b, float from)
{
    float m = 0.0f;
    for (auto i = (size_t) (from * sr); i < a.size(); ++i) m = std::max (m, std::abs (a[i] - b[i]));
    return m;
}

float maxStep (const std::vector<float>& v)
{
    float m = 0.0f;
    for (size_t i = 1; i < v.size(); ++i) m = std::isfinite (v[i]) ? std::max (m, std::abs (v[i] - v[i - 1])) : 1e9f;
    return m;
}

/** Level of `v` at one frequency between `from` and `to` seconds (a single DFT bin). */
float toneLevel (const std::vector<float>& v, float hz, float from, float to)
{
    double re = 0.0, im = 0.0; int count = 0;
    for (auto i = (size_t) (from * sr); i < (size_t) (to * sr) && i < v.size(); ++i, ++count)
    {
        const auto w = juce::MathConstants<double>::twoPi * hz * (double) i / sr;
        re += v[i] * std::cos (w); im -= v[i] * std::sin (w);
    }
    return (float) (std::sqrt (re * re + im * im) / std::max (1, count));
}

/** RT60 from the backward-integrated energy decay, fitted between two levels from `start` seconds on. */
float decayTime (const std::vector<float>& v, float start, double fromDb, double toDb)
{
    std::vector<double> decay (v.size());
    double tail = 0.0;
    for (size_t i = v.size(); i-- > 0;) { tail += (double) v[i] * v[i]; decay[i] = tail; }
    const auto first = (size_t) (start * sr);
    const auto timeAt = [&] (double db)
    {
        for (size_t i = first; i < decay.size(); ++i)
            if (10.0 * std::log10 (decay[i] / decay[first]) <= db) return (float) i / sr;
        return 1e9f;
    };
    return (float) (60.0 / (fromDb - toDb)) * (timeAt (toDb) - timeAt (fromDb));
}

void testCleanPath()
{
    // Wear 0 is the clean delay: an impulse comes back at exactly 1.0, then 0.5.
    Setup s; s.channels = 1; s.delayMs = 100.0f;
    const auto out = run (s, 0.5f, [] (long n) { return n == 0 ? 1.0f : 0.0f; });
    const auto first = out[4800], second = out[9600];
    char d[96]; std::snprintf (d, sizeof d, "echoes %.6f %.6f", first, second);
    check ("wear 0: clean impulse echoes", std::abs (first - 1.0f) < 1e-6f && std::abs (second - 0.5f) < 1e-6f, d);
}

void testRunaway()
{
    // Runaway is pressed like a key at 0.5 s, so it ramps in. Peaks are measured
    // from 1 s: after the ramp, and after echoes recorded before the press (which
    // are the clean delay's own peaks) have played out.
    char d[96];
    const auto runaway = [] (DelayEngine& e, float t) { e.setRunaway (t >= 0.5f); };

    for (int channels : { 1, 2 })
        for (bool pingPong : { false, true })
        {
            if (channels == 1 && pingPong) continue;

            for (float drive : { 0.0f, 0.5f, 1.0f })
            {
                Setup s; s.channels = channels; s.pingPong = pingPong; s.feedback = 0.35f; s.runawayDrive = drive;
                const auto out = run (s, 20.0f, noiseBurst(), runaway);
                char name[96];
                std::snprintf (name, sizeof name, "runaway ch%d pp%d drive %.1f: bounded, sustains", channels, (int) pingPong, drive);
                std::snprintf (d, sizeof d, "peak %.3f  last-second rms %.4f", peak (out, 1.0f), rms (out, 19.0f, 20.0f));
                check (name, peak (out, 1.0f) < 1.0f && rms (out, 19.0f, 20.0f) > 0.01f, d);
            }
        }

    Setup s; s.runawayDrive = 1.0f;
    auto out = run (s, 20.0f, dc (0.5f), runaway);
    std::snprintf (d, sizeof d, "peak %.3f", peak (out, 1.0f));
    check ("runaway with DC 0.5 input: bounded", peak (out, 1.0f) < 1.0f, d);

    for (float delayMs : { 1.0f, 300.0f })
    {
        s.delayMs = delayMs;
        out = run (s, 10.0f, loudNoise(), runaway);
        std::snprintf (d, sizeof d, "peak after pressed %.3f", peak (out, 1.0f));
        check (delayMs < 2.0f ? "runaway, loud input, 1 ms delay: bounded" : "runaway, loud input, 300 ms delay: bounded",
               peak (out, 1.0f) < 1.0f, d);
    }

    // Pressing it never overshoots what the clean delay was already doing.
    s.delayMs = 1.0f;
    const auto pressed = run (s, 1.0f, loudNoise(), runaway);
    const auto clean = run (s, 1.0f, loudNoise());
    const auto peakIn = [] (const std::vector<float>& v)
    {
        return peak (std::vector<float> (v.begin() + (long) (0.5f * sr), v.end()));
    };
    std::snprintf (d, sizeof d, "peak while ramping %.3f, clean delay %.3f", peakIn (pressed), peakIn (clean));
    check ("runaway press, 1 ms delay: no overshoot", peakIn (pressed) <= peakIn (clean), d);
}

void testWear()
{
    char d[96];

    for (bool pingPong : { false, true })
    {
        Setup s; s.feedback = 0.95f; s.wear = 1.0f; s.pingPong = pingPong;
        const auto out = run (s, 30.0f, noiseBurst());
        std::snprintf (d, sizeof d, "peak %.3f  last-second rms %.4f", peak (out), rms (out, 29.0f, 30.0f));
        check (pingPong ? "wear 1, feedback 95%, ping-pong: bounded, decays" : "wear 1, feedback 95%: bounded, decays",
               peak (out) < 1.2f && rms (out, 29.0f, 30.0f) < 0.05f, d);
    }
}

void testTapestop()
{
    char d[128];

    for (bool filters : { false, true })
    {
        Setup s; s.filters = filters;
        const auto ref = run (s, 6.0f, sine());
        const auto full = run (s, 6.0f, sine(), [] (DelayEngine& e, float t) { e.setTapestop (t >= 1.0f && t < 3.0f); });
        const auto early = run (s, 6.0f, sine(), [] (DelayEngine& e, float t) { e.setTapestop (t >= 1.0f && t < 1.2f); });
        const auto stepLimit = maxStep (ref) * 1.05f;
        const auto tag = filters ? ", filters on" : "";
        char name[96];

        std::snprintf (name, sizeof name, "tapestop full stop%s: silent while stopped", tag);
        std::snprintf (d, sizeof d, "stopped rms %.5f (reference %.3f)", rms (full, 1.6f, 3.0f), rms (ref, 1.6f, 3.0f));
        check (name, rms (full, 1.6f, 3.0f) < 1e-4f, d);

        std::snprintf (name, sizeof name, "tapestop full stop%s: exactly back, no clicks", tag);
        std::snprintf (d, sizeof d, "diff after 4 s %.2e, max step %.4f (reference %.4f)", maxDiff (full, ref, 4.0f), maxStep (full), maxStep (ref));
        check (name, maxDiff (full, ref, 4.0f) < 1e-6f && maxStep (full) <= stepLimit, d);

        std::snprintf (name, sizeof name, "tapestop early release%s: exactly back, no clicks", tag);
        std::snprintf (d, sizeof d, "diff after 3 s %.2e, max step %.4f", maxDiff (early, ref, 3.0f), maxStep (early));
        check (name, maxDiff (early, ref, 3.0f) < 1e-6f && maxStep (early) <= stepLimit, d);
    }

    {
        Setup s;
        const auto frozenRef  = run (s, 7.0f, sine(), [] (DelayEngine& e, float t) { e.setFrozen (t >= 1.0f); });
        const auto frozenStop = run (s, 7.0f, sine(), [] (DelayEngine& e, float t) { e.setFrozen (t >= 1.0f); e.setTapestop (t >= 2.0f && t < 3.5f); });
        std::snprintf (d, sizeof d, "stopped rms %.5f, diff after 4.5 s %.2e", rms (frozenStop, 2.6f, 3.5f), maxDiff (frozenStop, frozenRef, 4.5f));
        check ("tapestop while frozen: same loop comes back", rms (frozenStop, 2.6f, 3.5f) < 1e-4f
               && maxDiff (frozenStop, frozenRef, 4.5f) < 1e-6f && maxStep (frozenStop) <= maxStep (frozenRef) * 1.05f, d);
    }

    {
        Setup s; s.delayMs = 5000.0f; s.feedback = 0.9f;
        const auto ref = run (s, 20.0f, sine());
        const auto out = run (s, 20.0f, sine(), [] (DelayEngine& e, float t)
        {
            e.setTapestopTimeMs (2000);
            e.setTapestop ((t >= 6.0f && t < 12.0f) || (t >= 12.5f && t < 13.0f));
        });
        std::snprintf (d, sizeof d, "max step %.4f (reference %.4f)", maxStep (out), maxStep (ref));
        check ("tapestop 5 s delay, 2 s stop, re-press: stable", maxStep (out) <= maxStep (ref) * 1.05f, d);
    }
}

void testDiffusion()
{
    char d[128];

    {
        // An impulse comes back as a smooth reverb tail: find RT60 from the
        // backward-integrated energy decay, fitted between -5 and -35 dB. The
        // network is set to 2 s at low frequencies and 0.8 s at the top; white
        // noise's energy is mostly high, so broadband lands in between.
        Setup s; s.channels = 1; s.delayMs = 100.0f; s.feedback = 0.0f; s.diffusion = 1.0f;
        const auto out = run (s, 8.0f, [] (long n) { return n == 0 ? 1.0f : 0.0f; });
        std::vector<double> decay (out.size());
        double tail = 0.0;
        for (size_t i = out.size(); i-- > 0;) { tail += (double) out[i] * out[i]; decay[i] = tail; }
        const auto timeAt = [&] (double db)
        {
            for (size_t i = 0; i < decay.size(); ++i)
                if (10.0 * std::log10 (decay[i] / decay[0]) <= db) return (float) i / sr;
            return 1e9f;
        };
        const auto rt60 = 2.0f * (timeAt (-35.0) - timeAt (-5.0));
        std::snprintf (d, sizeof d, "echo peak %.3f, energy %.3f, RT60 %.2f s", peak (out), decay[0], rt60);
        check ("diffusion 1: impulse becomes a reverb tail", peak (out) < 0.5f && decay[0] > 0.5 && decay[0] < 2.0
               && rt60 > 0.6f && rt60 < 3.0f, d);
    }

    {
        // Repeats blur together. The level (dB per 10 ms window) over delay
        // periods 4-11 is detrended (the wash decays smoothly overall). A repeat
        // shows up as extra level just after each period starts, so this is the
        // average level in the first 50 ms of each period minus the rest.
        const auto pulseDb = [] (float diffusion, float delayMs)
        {
            Setup s; s.delayMs = delayMs; s.feedback = 0.7f; s.diffusion = diffusion;
            const auto burst = [] (long n) { return n < (long) (0.05f * sr) ? juce::Random (7 + n * 7919).nextFloat() * 2.0f - 1.0f : 0.0f; };
            const auto out = run (s, 13.0f * delayMs * 0.001f + 1.0f, burst);
            const auto window = (size_t) (0.01f * sr);
            const auto binsPerPeriod = (size_t) (delayMs / 10.0f);
            const size_t firstPeriod = 4, numPeriods = 8, onsetBins = 5;

            std::vector<double> level;
            for (size_t w = 0; w < binsPerPeriod * numPeriods; ++w)
            {
                const auto start = firstPeriod * binsPerPeriod * window + w * window;
                double e = 1e-20;
                for (size_t i = start; i < start + window; ++i) e += (double) out[i] * out[i];
                level.push_back (10.0 * std::log10 (e));
            }

            // Least-squares straight line through the dB curve.
            const auto n = (double) level.size();
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            for (size_t i = 0; i < level.size(); ++i) { sx += (double) i; sy += level[i]; sxx += (double) i * (double) i; sxy += (double) i * level[i]; }
            const auto slope = (n * sxy - sx * sy) / (n * sxx - sx * sx), intercept = (sy - slope * sx) / n;

            double onset = 0.0, rest = 0.0;
            for (size_t i = 0; i < level.size(); ++i)
            {
                const auto residual = level[i] - (intercept + slope * (double) i);
                if (i % binsPerPeriod < onsetBins) onset += residual; else rest += residual;
            }

            onset /= (double) (onsetBins * numPeriods);
            rest  /= (double) ((binsPerPeriod - onsetBins) * numPeriods);
            return (float) std::min (60.0, onset - rest);
        };

        for (float delayMs : { 150.0f, 300.0f, 800.0f })
        {
            const auto clean = pulseDb (0.0f, delayMs), half = pulseDb (0.5f, delayMs), full = pulseDb (1.0f, delayMs);
            std::snprintf (d, sizeof d, "onset lift per repeat: clean %.1f dB, 50%% %.1f dB, 100%% %.1f dB", clean, half, full);
            char name[96];
            std::snprintf (name, sizeof name, "diffusion 1, %d ms delay: repeats blur together", (int) delayMs);
            check (name, full < 1.0f, d);
        }
    }

    {
        // At full diffusion, Feedback sets how long the reverb rings: RT60 should
        // follow 3 x delay / -log10(feedback), within 2 s to 20 s. Measured on a
        // low tone, since the top end is meant to die away faster.
        for (float feedback : { 0.5f, 0.7f, 0.9f })
        {
            Setup s; s.channels = 1; s.feedback = feedback; s.diffusion = 1.0f;
            const auto target = juce::jlimit (2.0f, 20.0f, 3.0f * 0.3f / -std::log10 (feedback));
            const auto tone = [] (long n) { return n < (long) (0.3f * sr) ? 0.5f * std::sin (juce::MathConstants<float>::twoPi * 120.0f * (float) n / sr) : 0.0f; };
            const auto out = run (s, 2.5f * target + 1.0f, tone);

            std::vector<double> decay (out.size());
            double tail = 0.0;
            for (size_t i = out.size(); i-- > 0;) { tail += (double) out[i] * out[i]; decay[i] = tail; }
            const auto start = (size_t) (1.0f * sr); // after the tone and the first pass
            const auto timeAt = [&] (double db)
            {
                for (size_t i = start; i < decay.size(); ++i)
                    if (10.0 * std::log10 (decay[i] / decay[start]) <= db) return (float) i / sr;
                return 1e9f;
            };
            const auto rt60 = 3.0f * (timeAt (-25.0) - timeAt (-5.0));
            char name[96];
            std::snprintf (name, sizeof name, "diffusion 1, feedback %d%%: reverb length follows", (int) (feedback * 100.0f));
            std::snprintf (d, sizeof d, "RT60 %.2f s, target %.2f s", rt60, target);
            check (name, rt60 > 0.65f * target && rt60 < 1.35f * target, d);
        }
    }

    {
        // Level: at full diffusion a noise burst comes back as reverb at about
        // the level it went in (make-up gain). Sweeping the knob fast stays
        // bounded. The noise is the same on both channels, so the left output
        // is compared with one channel's input.
        const auto white = [] (long n)
        {
            // Hash of n (murmur finaliser), so every sample is independent and both channels match.
            auto h = (juce::uint32) n * 0x9E3779B1u;
            h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
            return (float) h / 2147483648.0f - 1.0f;
        };
        const auto burst = [white] (long n) { return n < (long) (2.0f * sr) ? white (n) : 0.0f; };
        double inEnergy = 0.0;
        for (long n = 0; n < (long) (2.0f * sr); ++n) inEnergy += (double) burst (n) * burst (n);

        for (bool sweep : { false, true })
        {
            Setup s; s.feedback = 0.0f; s.diffusion = sweep ? 0.0f : 1.0f;
            const auto out = run (s, 10.0f, burst, [sweep] (DelayEngine& e, float t)
            {
                if (sweep) e.setDiffusion ((int) (t * 20.0f) % 2 == 0 ? 1.0f : 0.0f);
            });
            double outEnergy = 0.0;
            for (auto x : out) outEnergy += (double) x * x;
            const auto ratioDb = 10.0 * std::log10 (outEnergy / inEnergy);
            std::snprintf (d, sizeof d, "level %+.1f dB, peak %.3f", ratioDb, peak (out));
            check (sweep ? "diffusion swept 0-1 every 50 ms: bounded" : "diffusion 1: comes out at about input level",
                   sweep ? peak (out) < 2.0f : std::abs (ratioDb) < 3.0, d);
        }
    }

    for (bool pingPong : { false, true })
    {
        // Smeared repeats overlap and stack like a reverb tail, so peaks can pass
        // the input's. It must still decay, and stay within what the clean delay
        // reaches with the same feedback and sustained input.
        Setup s; s.feedback = 0.95f; s.diffusion = 1.0f; s.pingPong = pingPong;
        const auto out = run (s, 30.0f, noiseBurst());
        Setup clean = s; clean.diffusion = 0.0f;
        const auto sustained = run (clean, 30.0f, loudNoise());
        std::snprintf (d, sizeof d, "peak %.3f (clean, sustained %.3f), rms %.4f -> %.4f",
                       peak (out), peak (sustained), rms (out, 10.0f, 11.0f), rms (out, 29.0f, 30.0f));
        check (pingPong ? "diffusion 1, feedback 95%, ping-pong: decays" : "diffusion 1, feedback 95%: decays",
               peak (out) < peak (sustained) && rms (out, 29.0f, 30.0f) < 0.05f
               && rms (out, 29.0f, 30.0f) < rms (out, 10.0f, 11.0f), d);
    }

    {
        // The reverb is outside the loop, so Runaway's loop is exactly as bounded
        // as without it; the reverb only reshapes the wall (same level, but
        // reverb-like peaks above its flat, saturated ones).
        Setup s; s.diffusion = 1.0f; s.runawayDrive = 1.0f;
        const auto press = [] (DelayEngine& e, float t) { e.setRunaway (t >= 0.5f); };
        const auto out = run (s, 10.0f, loudNoise(), press);
        Setup dry = s; dry.diffusion = 0.0f;
        const auto plain = run (dry, 10.0f, loudNoise(), press);
        const auto levelDb = 20.0f * std::log10 (rms (out, 2.0f, 10.0f) / rms (plain, 2.0f, 10.0f));
        std::snprintf (d, sizeof d, "level vs no diffusion %+.1f dB, peak %.3f", levelDb, peak (out, 1.0f));
        check ("diffusion 1 with runaway, loud input: bounded", std::abs (levelDb) < 3.0f && peak (out, 1.0f) < 2.0f, d);
    }
}

void testReverse()
{
    char d[128];

    {
        // With no feedback the buffer holds the input exactly, so the reversed
        // output can be checked sample by sample. At the centre of head A's window
        // (phase L/2, reached at n = L/2 - 1 + kL from the press), the output is
        // the input from 2 delay times ago, stepping backwards.
        Setup s; s.channels = 1; s.delayMs = 100.0f; s.feedback = 0.0f;
        const auto input = loudNoise();
        std::vector<float> in;
        for (long n = 0; n < (long) (1.0f * sr); ++n) in.push_back (input (n));
        const auto out = run (s, 1.0f, [&in] (long n) { return in[(size_t) n]; },
                              [] (DelayEngine& e, float) { e.setReverse (true); });

        const long segment = 4800, centre = segment / 2 - 1 + 3 * segment;
        float worst = 0.0f;
        for (long j = -5; j <= 5; ++j)
            worst = std::max (worst, std::abs (out[(size_t) (centre + j)] - in[(size_t) (centre - 2 * segment - j)]));
        std::snprintf (d, sizeof d, "worst error %.2e", worst);
        check ("reverse: plays the buffer exactly backwards", worst < 1e-3f, d);
    }

    for (bool filters : { false, true })
    {
        Setup s; s.filters = filters;
        const auto ref = run (s, 4.0f, sine());
        const auto out = run (s, 4.0f, sine(), [] (DelayEngine& e, float t) { e.setReverse (t >= 1.0f && t < 2.0f); });
        const auto tol = filters ? 1e-3f : 1e-6f;
        std::snprintf (d, sizeof d, "diff after 2.5 s %.2e, max step %.4f (reference %.4f)", maxDiff (out, ref, 2.5f), maxStep (out), maxStep (ref));
        check (filters ? "reverse, filters on: exactly back, no clicks" : "reverse: exactly back, no clicks",
               maxDiff (out, ref, 2.5f) < tol && maxStep (out) <= maxStep (ref) * 1.1f, d);
    }

    {
        Setup s;
        const auto frozenRef = run (s, 6.0f, sine(), [] (DelayEngine& e, float t) { e.setFrozen (t >= 1.0f); });
        const auto out = run (s, 6.0f, sine(), [] (DelayEngine& e, float t) { e.setFrozen (t >= 1.0f); e.setReverse (t >= 3.0f && t < 4.0f); });
        std::snprintf (d, sizeof d, "diff after 4.5 s %.2e, max step %.4f", maxDiff (out, frozenRef, 4.5f), maxStep (out));
        check ("reverse while frozen: same loop comes back", maxDiff (out, frozenRef, 4.5f) < 1e-6f
               && maxStep (out) <= maxStep (frozenRef) * 1.1f, d);
    }

    {
        Setup s; s.delayMs = 5000.0f; s.feedback = 0.9f;
        const auto ref = run (s, 20.0f, sine());
        const auto out = run (s, 20.0f, sine(), [] (DelayEngine& e, float t)
        {
            e.setReverse (t >= 5.0f && t < 15.0f);
            e.setTapestop ((t >= 7.0f && t < 9.0f) || (t >= 14.5f && t < 16.0f));
        });
        std::snprintf (d, sizeof d, "max step %.4f (reference %.4f)", maxStep (out), maxStep (ref));
        check ("reverse with tapestop, 5 s delay: stable", maxStep (out) <= maxStep (ref) * 1.1f, d);
    }
}

void testTaps()
{
    char d[128];
    const auto impulse = [] (long n) { return n == 0 ? 1.0f : 0.0f; };

    const auto tap = [] (int uid, float ms, float gain = 1.0f, float pan = 0.0f, float semitones = 0.0f, bool reverse = false)
    {
        DelayEngine::TapSettings t;
        t.uid = uid; t.delayMs = ms; t.gain = gain; t.pan = pan; t.semitones = semitones; t.reverse = reverse;
        return t;
    };

    // Sets the taps once, at the start.
    const auto withTaps = [] (std::vector<DelayEngine::TapSettings> taps, bool multiTap = true) -> Events
    {
        return [taps, multiTap] (DelayEngine& e, float t)
        {
            if (t == 0.0f) { e.setTaps (taps); e.setMultiTap (multiTap); }
        };
    };

    {
        // A centred tap reads the mono sum at -3 dB per side; hard left is silent on the right.
        Setup s; s.delayMs = 1000.0f; s.feedback = 0.0f;
        const auto centre = run (s, 0.5f, impulse, withTaps ({ tap (1, 300.0f) }));
        s.outputChannel = 1;
        const auto leftOnly = run (s, 0.5f, impulse, withTaps ({ tap (1, 300.0f, 1.0f, -1.0f) }));
        const auto expected = std::sqrt (0.5f);
        std::snprintf (d, sizeof d, "centre %.6f (expected %.6f), hard left on the right %.2e", centre[14400], expected, peak (leftOnly));
        check ("tap at 300 ms, 0 dB: impulse at -3 dB, pan law", std::abs (centre[14400] - expected) < 1e-5f
               && peak (leftOnly) < 1e-4f && peak (centre, 0.31f) < 1e-6f, d);
    }

    {
        // Multi-Tap replaces the main echo: with a 300 ms delay, 50% feedback and
        // one tap at 100 ms, an impulse comes back at 100 ms, then 400 ms at half
        // level (the tap hearing the loop's repeat), and never at 300 ms.
        Setup s; s.channels = 1; s.delayMs = 300.0f; s.feedback = 0.5f;
        const auto out = run (s, 0.5f, impulse, withTaps ({ tap (1, 100.0f) }));
        const auto at = [&out] (int ms) { return out[(size_t) (ms * 48)]; };
        std::snprintf (d, sizeof d, "100 ms %.6f, 300 ms %.2e, 400 ms %.6f", at (100), at (300), at (400));
        check ("multi-tap on: main echo off, taps repeat with feedback", std::abs (at (100) - 1.0f) < 1e-5f
               && std::abs (at (300)) < 1e-6f && std::abs (at (400) - 0.5f) < 1e-5f, d);
    }

    {
        // Multi-Tap off is the plain delay, exactly, both before it's ever used and after it fades out.
        Setup s; s.filters = true;
        const auto ref = run (s, 3.0f, sine());
        const auto off = run (s, 3.0f, sine(), withTaps ({ tap (1, 120.0f), tap (2, 240.0f, 0.5f, 0.5f, 7.0f) }, false));
        const auto onThenOff = run (s, 3.0f, sine(), [&] (DelayEngine& e, float t)
        {
            if (t == 0.0f) e.setTaps ({ tap (1, 120.0f), tap (2, 240.0f, 0.5f, 0.5f, 7.0f) });
            e.setMultiTap (t < 1.0f);
        });
        std::snprintf (d, sizeof d, "never on %.2e, after switching off %.2e", maxDiff (off, ref, 0.0f), maxDiff (onThenOff, ref, 1.1f));
        check ("multi-tap off: bit-exact plain delay", maxDiff (off, ref, 0.0f) == 0.0f && maxDiff (onThenOff, ref, 1.1f) == 0.0f, d);
    }

    {
        // Main echo at 5 s, so up to then only the tap is heard.
        Setup s; s.delayMs = 5000.0f; s.feedback = 0.0f;
        const auto plain   = run (s, 4.0f, sine(), withTaps ({ tap (1, 300.0f) }));
        const auto octave  = run (s, 4.0f, sine(), withTaps ({ tap (1, 300.0f, 1.0f, 0.0f, 12.0f) }));
        const auto down    = run (s, 4.0f, sine(), withTaps ({ tap (1, 300.0f, 1.0f, 0.0f, -12.0f) }));
        const auto levelDb = [&] (const std::vector<float>& v) { return 20.0f * std::log10 (rms (v, 1.0f, 4.0f) / rms (plain, 1.0f, 4.0f)); };
        const auto crossings = [] (const std::vector<float>& v)
        {
            int count = 0;
            for (auto i = (size_t) (1.0f * sr); i + 1 < v.size(); ++i) count += (v[i] < 0.0f) != (v[i + 1] < 0.0f);
            return (float) count;
        };
        const auto upRatio = crossings (octave) / crossings (plain), downRatio = crossings (down) / crossings (plain);
        std::snprintf (d, sizeof d, "+12: %+.1f dB x%.2f, -12: %+.1f dB x%.2f, peaks %.3f %.3f",
                       levelDb (octave), upRatio, levelDb (down), downRatio, peak (octave), peak (down));
        check ("tap pitch +-12: an octave, about the same level, bounded", std::abs (levelDb (octave)) < 3.0f && std::abs (levelDb (down)) < 3.0f
               && std::abs (upRatio - 2.0f) < 0.05f && std::abs (downRatio - 0.5f) < 0.05f
               && peak (octave) < 1.0f && peak (down) < 1.0f, d);

        const auto reversed = run (s, 4.0f, sine(), withTaps ({ tap (1, 300.0f, 1.0f, 0.0f, 0.0f, true) }));
        std::snprintf (d, sizeof d, "reversed level %+.1f dB, peak %.3f", levelDb (reversed), peak (reversed));
        check ("tap reverse on: bounded, same level", std::abs (levelDb (reversed)) < 3.0f && peak (reversed) < 1.0f, d);

        // The global Tapestop stops every tap, and they come back exactly.
        const auto stopped = run (s, 4.0f, sine(), [&] (DelayEngine& e, float t)
        {
            if (t == 0.0f) { e.setTaps ({ tap (1, 300.0f) }); e.setMultiTap (true); }
            e.setTapestop (t >= 1.0f && t < 2.0f);
        });
        std::snprintf (d, sizeof d, "stopped rms %.5f, diff after 3 s %.2e, max step %.4f (reference %.4f)",
                       rms (stopped, 1.6f, 2.0f), maxDiff (stopped, plain, 3.0f), maxStep (stopped), maxStep (plain));
        check ("global tapestop on a tap: silent, exactly back, no clicks", rms (stopped, 1.6f, 2.0f) < 1e-4f
               && maxDiff (stopped, plain, 3.0f) < 1e-6f && maxStep (stopped) <= maxStep (plain) * 1.05f, d);
    }

    {
        // Reverse with pitch: an octave up plays the segment backwards at double
        // speed. At the centre of head A's window (phase L/2) the tap reads
        // L + 3 (L/2 + j) back, so out[c + j] = in[c - 2.5 L - 2 j].
        Setup s; s.channels = 1; s.delayMs = 5000.0f; s.feedback = 0.0f;
        const auto input = loudNoise();
        std::vector<float> in;
        for (long n = 0; n < (long) (1.0f * sr); ++n) in.push_back (input (n));
        const long segment = 4800, centre = segment / 2 - 1 + 3 * segment;

        for (float semitones : { 12.0f, -12.0f })
        {
            const auto rate = 1.0f + std::pow (2.0f, semitones / 12.0f);
            const auto out = run (s, 1.0f, [&in] (long n) { return in[(size_t) n]; },
                                  withTaps ({ tap (1, 100.0f, 1.0f, 0.0f, semitones, true) }));
            float worst = 0.0f;
            for (long j = -4; j <= 4; j += 2)
            {
                const auto back = (float) segment + rate * (float) (segment / 2 + j);
                const auto readAt = (float) (centre + j) - back;
                const auto i0 = (size_t) std::floor (readAt);
                const auto frac = readAt - std::floor (readAt);
                const auto expected = in[i0] + frac * (in[i0 + 1] - in[i0]);
                worst = std::max (worst, std::abs (out[(size_t) (centre + j)] - expected));
            }
            char name[96];
            std::snprintf (name, sizeof name, "tap reverse with pitch %+d: backwards at %.1fx speed", (int) semitones, rate - 1.0f);
            std::snprintf (d, sizeof d, "worst error %.2e", worst);
            check (name, worst < 1e-3f, d);
        }
    }

    {
        // More than 16 taps: only the first 16 play.
        Setup s; s.channels = 1; s.delayMs = 5000.0f; s.feedback = 0.0f;
        std::vector<DelayEngine::TapSettings> many;
        for (int i = 1; i <= 20; ++i) many.push_back (tap (i, 10.0f * (float) i));
        const auto out = run (s, 0.5f, impulse, withTaps (many));
        const auto at = [&out] (int ms) { return out[(size_t) (ms * 48)]; };
        std::snprintf (d, sizeof d, "tap 16 %.3f, tap 17 %.3f", at (160), at (170));
        check ("taps: capped at 16", std::abs (at (160) - 1.0f) < 1e-5f && std::abs (at (170)) < 1e-6f, d);
    }
}

void testStyles()
{
    char d[128], name[96];
    const auto impulse = [] (long n) { return n == 0 ? 1.0f : 0.0f; };

    for (int style = 0; style < DelayEngine::numStyles; ++style)
    {
        // Wear 0 is the clean delay in every style.
        Setup s; s.channels = 1; s.delayMs = 100.0f; s.style = style; s.filters = true;
        Setup clean = s; clean.style = 0;
        const auto out = run (s, 0.5f, impulse), ref = run (clean, 0.5f, impulse);
        std::snprintf (name, sizeof name, "style %d, wear 0: bit-exact clean delay", style);
        std::snprintf (d, sizeof d, "diff %.2e", maxDiff (out, ref, 0.0f));
        check (name, maxDiff (out, ref, 0.0f) == 0.0f, d);

        // Full Wear at 95% feedback stays bounded and still dies away.
        Setup w; w.feedback = 0.95f; w.wear = 1.0f; w.style = style;
        const auto worn = run (w, 30.0f, noiseBurst());
        std::snprintf (name, sizeof name, "style %d, wear 1, feedback 95%%: bounded, decays", style);
        std::snprintf (d, sizeof d, "peak %.3f  last-second rms %.4f", peak (worn), rms (worn, 29.0f, 30.0f));
        check (name, peak (worn) < 1.2f && rms (worn, 29.0f, 30.0f) < 0.05f, d);
    }

    {
        // Switching between the analogue styles with Wear up doesn't click. (Lo-Fi is
        // left out: its sample-and-hold steps are the sound.)
        Setup s; s.wear = 0.6f;
        const auto ref = run (s, 4.0f, sine());
        const auto out = run (s, 4.0f, sine(), [] (DelayEngine& e, float t)
        {
            const int cycle[] { 0, 1, 2, 4 };
            e.setStyle (cycle[(int) (t * 2.0f) % 4]);
        });
        std::snprintf (d, sizeof d, "max step %.4f (reference %.4f)", maxStep (out), maxStep (ref));
        check ("switching styles every 0.5 s: no clicks", maxStep (out) <= maxStep (ref) * 1.5f, d);
    }
}

void testShimmer()
{
    char d[128], name[96];

    {
        // Octave up: a 220 Hz tone through the reverb comes back with far more at 440 Hz than without Shimmer.
        Setup s; s.channels = 1; s.feedback = 0.5f; s.diffusion = 1.0f;
        const auto tone = [] (long n) { return n < (long) (0.3f * sr) ? 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) n / sr) : 0.0f; };
        const auto plain = run (s, 3.0f, tone);
        s.shimmer = 1.0f;
        const auto shimmered = run (s, 3.0f, tone);
        const auto liftDb = 20.0f * std::log10 (toneLevel (shimmered, 440.0f, 1.0f, 3.0f) / toneLevel (plain, 440.0f, 1.0f, 3.0f));
        std::snprintf (d, sizeof d, "440 Hz lift %+.1f dB", liftDb);
        check ("shimmer octave up: the tail climbs an octave", liftDb > 10.0f, d);
    }

    for (int semitones : { -12, 12, 24 })
    {
        // Full Shimmer at the longest tail (feedback 95%) stays bounded and still decays, at every extreme of the shift.
        Setup s; s.feedback = 0.95f; s.diffusion = 1.0f; s.shimmer = 1.0f; s.shimmerSemitones = semitones;
        const auto out = run (s, 40.0f, noiseBurst());
        std::snprintf (name, sizeof name, "shimmer 1 at %+d, feedback 95%%: bounded, decays", semitones);
        std::snprintf (d, sizeof d, "peak %.3f, rms %.4f -> %.4f", peak (out), rms (out, 10.0f, 11.0f), rms (out, 39.0f, 40.0f));
        check (name, peak (out) < 2.0f && rms (out, 39.0f, 40.0f) < 0.05f && rms (out, 39.0f, 40.0f) < 0.5f * rms (out, 10.0f, 11.0f), d);
    }

    {
        // With Shimmer on, the tail is still as long as Feedback says (as in the Diffusion test).
        Setup s; s.channels = 1; s.feedback = 0.7f; s.diffusion = 1.0f; s.shimmer = 1.0f;
        const auto target = juce::jlimit (2.0f, 20.0f, 3.0f * 0.3f / -std::log10 (0.7f));
        const auto tone = [] (long n) { return n < (long) (0.3f * sr) ? 0.5f * std::sin (juce::MathConstants<float>::twoPi * 120.0f * (float) n / sr) : 0.0f; };
        const auto out = run (s, 2.5f * target + 1.0f, tone);
        const auto rt60 = decayTime (out, 1.0f, -5.0, -25.0);
        std::snprintf (d, sizeof d, "RT60 %.2f s, target %.2f s", rt60, target);
        check ("shimmer 1, feedback 70%: reverb length still follows", rt60 > 0.65f * target && rt60 < 1.35f * target, d);
    }
}

void testModulation()
{
    char d[128], name[96];
    const char* names[] { "chorus", "flanger", "phaser" };

    for (int type = 1; type <= 3; ++type)
        for (bool inLoop : { true, false })
        {
            // Half and half with the dry signal, the gain never passes 1 at any
            // frequency, so the loop is as safe as without. Peaks can still rise
            // a little: the phaser's allpasses spread the phase, and a transient's
            // parts no longer line up as they did.
            Setup s; s.feedback = 0.95f; s.modType = type; s.modInLoop = inLoop;
            const auto out = run (s, 30.0f, noiseBurst());
            std::snprintf (name, sizeof name, "%s %s, feedback 95%%: bounded, decays", names[type - 1], inLoop ? "in the loop" : "on the output");
            std::snprintf (d, sizeof d, "peak %.3f  last-second rms %.4f", peak (out), rms (out, 29.0f, 30.0f));
            check (name, peak (out) < 1.5f && rms (out, 29.0f, 30.0f) < 0.05f, d);
        }

    {
        // Off is the plain delay, exactly: before it's ever used, and once an output-only effect has faded out.
        Setup s; s.filters = true; s.modInLoop = false;
        const auto ref = run (s, 3.0f, sine());
        const auto onThenOff = run (s, 3.0f, sine(), [] (DelayEngine& e, float t) { e.setModulationType (t < 0.5f ? 3 : 0); });
        Setup loop = s; loop.modInLoop = true;
        const auto never = run (loop, 3.0f, sine());
        std::snprintf (d, sizeof d, "never on %.2e, after switching off %.2e", maxDiff (never, ref, 0.0f), maxDiff (onThenOff, ref, 0.7f));
        check ("modulation off: bit-exact plain delay", maxDiff (never, ref, 0.0f) == 0.0f && maxDiff (onThenOff, ref, 0.7f) == 0.0f, d);
    }

    {
        // Half and half with the dry signal: on white noise that's about -3 dB, never a boost.
        Setup s; s.delayMs = 100.0f; s.feedback = 0.0f; s.modInLoop = false;
        const auto plain = run (s, 3.0f, loudNoise());
        s.modType = 1;
        const auto chorused = run (s, 3.0f, loudNoise());
        const auto levelDb = 20.0f * std::log10 (rms (chorused, 1.0f, 3.0f) / rms (plain, 1.0f, 3.0f));
        std::snprintf (d, sizeof d, "level %+.1f dB", levelDb);
        check ("chorus on the output: about -3 dB on noise", levelDb > -4.5f && levelDb < -1.5f, d);
    }

    {
        // Changing type and position mid-flight doesn't click.
        Setup s;
        const auto ref = run (s, 4.0f, sine());
        const auto out = run (s, 4.0f, sine(), [] (DelayEngine& e, float t)
        {
            e.setModulationType (1 + (int) (t * 2.0f) % 3);
            e.setModulationInFeedbackLoop ((int) (t * 1.3f) % 2 == 0);
        });
        std::snprintf (d, sizeof d, "max step %.4f (reference %.4f)", maxStep (out), maxStep (ref));
        check ("modulation type and position switching: no clicks", maxStep (out) <= maxStep (ref) * 1.5f, d);
    }
}

void testGroove()
{
    char d[160];
    const auto impulse = [] (long n) { return n == 0 ? 1.0f : 0.0f; };

    // At 120 BPM a tap half a beat in (250 ms) swings to two thirds (333 ms) at full
    // Groove, and Feel moves it by its milliseconds. The main echo is out of the way at 5 s.
    const auto tapMs = [&] (float groove, float feelMs)
    {
        Setup s; s.channels = 1; s.delayMs = 5000.0f; s.feedback = 0.0f;
        const auto out = run (s, 0.6f, impulse, [groove, feelMs] (DelayEngine& e, float t)
        {
            if (t == 0.0f)
            {
                DelayEngine::TapSettings tap; tap.uid = 1; tap.beats = 0.5f; tap.delayMs = 10.0f;
                e.setTaps ({ tap }); e.setMultiTap (true); e.setTapTempo (true, 120.0);
                e.setGroove (groove); e.setFeelMs (feelMs);
            }
        });
        size_t at = 0;
        for (size_t i = 1; i < out.size(); ++i) if (std::abs (out[i]) > std::abs (out[at])) at = i;
        return (float) at / sr * 1000.0f;
    };

    const auto straight = tapMs (0.0f, 0.0f), full = tapMs (1.0f, 0.0f), half = tapMs (0.5f, 0.0f), dragged = tapMs (0.0f, 20.0f);
    std::snprintf (d, sizeof d, "straight %.1f ms, groove 1 %.1f ms, groove 0.5 %.1f ms, feel +20 %.1f ms", straight, full, half, dragged);
    check ("groove and feel on a synced eighth tap", std::abs (straight - 250.0f) < 0.5f && std::abs (full - 333.3f) < 0.5f
           && std::abs (half - 291.7f) < 0.5f && std::abs (dragged - 270.0f) < 0.5f, d);
}

}

int main()
{
    testCleanPath();
    testRunaway();
    testWear();
    testTapestop();
    testDiffusion();
    testReverse();
    testTaps();
    testStyles();
    testShimmer();
    testModulation();
    testGroove();

    std::printf ("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
