// Transitions between tracks (docs/tasks/automix.md): the planner, the gain curves, the
// mix and the deck that feeds it, a skip's quick fade. No output device.
#include "infrastructure/media/audio_decoder.h"
#include "infrastructure/media/audio_output.h"
#include "infrastructure/media/automix/analysis.h"
#include "infrastructure/media/automix/analyst.h"
#include "infrastructure/media/automix/deck.h"
#include "infrastructure/media/automix/mix.h"
#include "infrastructure/media/automix/transition.h"
#include "infrastructure/media/byte_source.h"
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <random>

using namespace cirrus;
using namespace cirrus::media;
using namespace cirrus::media::automix;
using domain::TransitionMode;

namespace
{
constexpr qint64 ToneFrames = 22050; // tests/fixtures/audio/README.md

std::unique_ptr<AudioDecoder> tone(const char* format)
{
    auto decoder = std::make_unique<AudioDecoder>();
    const auto file =
        LocalFileSource::open(QStringLiteral(AUDIO_FIXTURES "/tone.") + QLatin1String(format));
    if (!file || !decoder->open(file, QLatin1String(format)))
        return nullptr;
    return decoder;
}

// Synthetic signals, interleaved stereo.
std::vector<float> sine(int rate, double seconds, double amplitude, double hz = 1000)
{
    std::vector<float> out(size_t(rate * seconds) * 2);
    for (size_t i = 0; i < out.size() / 2; ++i)
        out[i * 2] = out[i * 2 + 1] = float(amplitude * std::sin(2 * M_PI * hz * double(i) / rate));
    return out;
}

std::vector<float> silence(int rate, double seconds)
{
    return std::vector<float>(size_t(rate * seconds) * 2, 0.0f);
}

// `seconds` of a sine fading out by `dbPerSecond` from `amplitude`.
std::vector<float> fading(int rate, double seconds, double amplitude, double dbPerSecond)
{
    auto out = sine(rate, seconds, amplitude);
    for (size_t i = 0; i < out.size() / 2; ++i) {
        const auto gain = float(std::pow(10.0, -dbPerSecond * double(i) / rate / 20));
        out[i * 2] *= gain;
        out[i * 2 + 1] *= gain;
    }
    return out;
}

// A drum groove with chords: `bars` bars of 4/4 at `bpm` after `leadIn` seconds of silence,
// a kick on 1 and 3 (on every beat `fourOnTheFloor`), a snare on 2 and 4, a hi-hat on every
// eighth and a triad held through each bar (C, G, Am, F). Beat i at leadIn + i · 60 / bpm,
// times `drift` per beat (a band slowing down: above 1).
std::vector<float> groove(int rate, double bpm, int bars, double leadIn = 0,
                          bool fourOnTheFloor = false, double drift = 1)
{
    std::vector<double> beats;
    double at = leadIn, beat = 60 / bpm;
    for (int i = 0; i <= bars * 4; ++i, at += beat, beat *= drift)
        beats.push_back(at);
    std::vector<float> out(size_t((beats.back() + 1) * rate) * 2, 0.0f);
    std::mt19937 random(7);
    std::uniform_real_distribution<float> noise(-1, 1);
    const auto add = [&](double start, double seconds, const auto& sample) {
        const auto first = qsizetype(start * rate);
        const auto count = qsizetype(seconds * rate);
        for (qsizetype i = 0; i < count && (first + i) * 2 + 1 < qsizetype(out.size()); ++i) {
            const float v = sample(double(i) / rate);
            out[size_t(first + i) * 2] += v;
            out[size_t(first + i) * 2 + 1] += v;
        }
    };
    const auto kick = [&](double t) {
        const double phase = 2 * M_PI * (50 * t + 100 * 0.03 * (1 - std::exp(-t / 0.03)));
        return float(0.6 * std::exp(-t / 0.08) * std::sin(phase));
    };
    const auto snare = [&](double t) {
        return float(0.3 * noise(random) * std::exp(-t / 0.05) +
                     0.2 * std::sin(2 * M_PI * 190 * t) * std::exp(-t / 0.04));
    };
    float previous = 0;
    const auto hat = [&](double t) {
        const float n = noise(random), high = n - previous;
        previous = n;
        return float(0.08 * high * std::exp(-t / 0.01));
    };
    const int chords[4][3] = {{60, 64, 67}, {55, 59, 62}, {57, 60, 64}, {53, 57, 60}};
    for (int bar = 0; bar < bars; ++bar) {
        const double start = beats[size_t(bar) * 4], end = beats[size_t(bar) * 4 + 4];
        for (const int note : chords[bar % 4]) {
            const double hz = 440 * std::pow(2.0, (note - 69) / 12.0);
            add(start, end - start, [&](double t) {
                const double envelope = std::min({1.0, t / 0.02, (end - start - t) / 0.02});
                return float(0.06 * envelope * std::sin(2 * M_PI * hz * t));
            });
        }
        for (int b = 0; b < 4; ++b) {
            const double t = beats[size_t(bar) * 4 + size_t(b)];
            const double next = beats[size_t(bar) * 4 + size_t(b) + 1];
            if (fourOnTheFloor || b % 2 == 0)
                add(t, 0.15, kick);
            if (b % 2 == 1)
                add(t, 0.15, snare);
            add(t, 0.03, hat);
            add((t + next) / 2, 0.03, hat);
        }
    }
    return out;
}
// Where groove() puts its beats.
std::vector<double> grooveBeats(double bpm, int bars, double leadIn = 0, double drift = 1)
{
    std::vector<double> beats;
    double at = leadIn, beat = 60 / bpm;
    for (int i = 0; i < bars * 4; ++i, at += beat, beat *= drift)
        beats.push_back(at * 1000);
    return beats;
}
// How far each found beat is from the nearest true one, ms: the mean, and the worst.
std::pair<double, double> beatErrors(const std::vector<float>& found,
                                     const std::vector<double>& truth)
{
    double sum = 0, worst = 0;
    for (const float b : found) {
        double nearest = 1e9;
        for (const double t : truth)
            nearest = std::min(nearest, std::fabs(double(b) - t));
        sum += nearest;
        worst = std::max(worst, nearest);
    }
    return {found.empty() ? 1e9 : sum / double(found.size()), worst};
}

std::vector<float> operator+(std::vector<float> a, const std::vector<float>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Analysis analyse(const std::vector<float>& audio, int rate, bool complete = true)
{
    Analyser analyser(rate);
    const auto frames = qsizetype(audio.size() / 2);
    for (qsizetype at = 0; at < frames; at += 2048) // in a decoder's chunks
        analyser.add(audio.data() + at * 2, std::min<qsizetype>(2048, frames - at));
    return analyser.finish(complete);
}

// An analysis as the planner sees it: sound over [start, end) at `level`, opening (its
// first 2 s) at `opening`.
Analysis made(qint64 durationMs, qint64 start, qint64 end, float level = -10, float opening = -20)
{
    Analysis a;
    a.durationMs = durationMs;
    a.complete = true;
    a.loudness = level;
    a.soundStartMs = start;
    a.soundEndMs = end;
    const auto step = [](float lufs) { return quint8((lufs - Analysis::kFloor) * 2); };
    for (qint64 at = 0; at < durationMs; at += Analysis::kStepMs) {
        const bool sounding = at >= start && at < end;
        a.momentary.push_back(step(!sounding           ? Analysis::kFloor
                                   : at < start + 2000 ? opening
                                                       : level));
    }
    return a;
}

qint64 drain(Deck& deck)
{
    std::vector<float> chunk(2 * 1000);
    qint64 total = 0;
    while (true) {
        const auto got = deck.pull(chunk.data(), 1000, false);
        total += got;
        if (got < 1000) {
            if (!deck.decoderEnded() || deck.ended())
                return total;
            deck.finish();
        }
    }
}
} // namespace

class AutomixTest : public QObject
{
    Q_OBJECT
  private slots:
    void fadesKeepThePower()
    {
        const auto out = Automation::fadeOut(1, 4);
        const auto in = Automation::fadeIn(1, 4);
        QCOMPARE(out.at(0), 1.0f);
        QCOMPARE(in.at(0), 0.0f);
        QCOMPARE(out.at(5), 0.0f);
        QCOMPARE(in.at(9), 1.0f);
        for (double t = 1; t <= 5; t += 0.037) {
            const double power = std::pow(out.at(t), 2) + std::pow(in.at(t), 2);
            QVERIFY2(std::abs(power - 1) < 1e-3, qPrintable(QString::number(t)));
        }
        QVERIFY(std::abs(out.at(3) - float(std::sqrt(0.5))) < 1e-3);
        Automation steps;
        steps.add(0, 0).add(2, 1).add(2, 0.5f);
        QCOMPARE(steps.at(1), 0.5f);
        QCOMPARE(steps.at(2), 0.5f); // the jump
        QCOMPARE(steps.at(3), 0.5f);
        QCOMPARE(Automation().at(1), 1.0f);
    }

    void plansACrossfade()
    {
        const Settings crossfade{TransitionMode::Crossfade, 8000};
        const auto p = plan(crossfade, {200000}, 10000, {180000}, false);
        QCOMPARE(p.style, Style::Crossfade);
        QCOMPARE(p.reason, Reason::Setting);
        QCOMPARE(p.overlapMs, 8000);
        QCOMPARE(p.outMs, 192000);
        QCOMPARE(p.inMs, 0);
        QCOMPARE(p.switchMs, 4000);
        QCOMPARE(p.gainOut.at(0), 1.0f);
        QCOMPARE(p.gainIn.at(8), 1.0f);
        // The smart mode crossfades until the analysis comes.
        const auto smart = plan({TransitionMode::Smart, 1000}, {200000}, 0, {180000}, false);
        QCOMPARE(smart.overlapMs, 6000);
        QCOMPARE(smart.reason, Reason::NoAnalysis);
    }

    void fallsBackToGapless()
    {
        const Settings crossfade{TransitionMode::Crossfade, 8000};
        const auto gapless = [](const Plan& p, Reason reason) {
            return p.style == Style::Gapless && p.overlapMs == 0 && p.reason == reason;
        };
        QVERIFY(gapless(plan({TransitionMode::Gapless, 8000}, {200000}, 0, {180000}, false),
                        Reason::Setting));
        QVERIFY(gapless(plan(crossfade, {200000, true}, 0, {180000}, false), Reason::Preview));
        QVERIFY(gapless(plan(crossfade, {200000}, 0, {180000, true}, false), Reason::Preview));
        QVERIFY(gapless(plan(crossfade, {200000}, 0, {200000}, true), Reason::SameFile));
        QVERIFY(gapless(plan(crossfade, {200000}, 0, {0}, false), Reason::NoDuration));
        // Short tracks: a third of the shorter one at most.
        QCOMPARE(plan(crossfade, {9000}, 0, {180000}, false).overlapMs, 3000);
        QVERIFY(gapless(plan(crossfade, {2400}, 0, {180000}, false), Reason::NoRoom));
        // Near the end already (a seek there): what is left, or nothing.
        const auto late = plan(crossfade, {200000}, 196000, {180000}, false);
        QCOMPARE(late.style, Style::Crossfade);
        QCOMPARE(late.overlapMs, 200000 - 196000 - kStartMarginMs);
        QCOMPARE(late.outMs + late.overlapMs, 200000);
        QVERIFY(gapless(plan(crossfade, {200000}, 199000, {180000}, false), Reason::NoRoom));
    }

    void mixesWithTheGains()
    {
        const auto p = plan({TransitionMode::Crossfade, 2000}, {60000}, 0, {60000}, false);
        constexpr int rate = 1000; // a frame a millisecond
        std::vector<float> a(2 * 2000, 0.5f), b(2 * 2000, -0.25f), mixed(2 * 2000);
        Mixer whole(p, rate);
        whole.mix(a.data(), b.data(), mixed.data(), 2000);
        QCOMPARE(mixed[0], 0.5f);
        const float middle = (0.5f - 0.25f) * float(std::sqrt(0.5));
        QVERIFY(std::abs(mixed[2 * 1000] - middle) < 1e-3);
        QVERIFY(std::abs(mixed[2 * 1999 + 1] + 0.25f) < 2e-3);
        // In pieces, the same.
        Mixer pieces(p, rate);
        std::vector<float> first(2 * 1000), second(2 * 10);
        pieces.mix(a.data(), b.data(), first.data(), 1000);
        pieces.mix(a.data(), b.data(), second.data(), 10);
        QCOMPARE(pieces.offset(), 1010);
        QCOMPARE(second[0], mixed[2 * 1000]);
    }

    void aBassCutTakesTheBass()
    {
        constexpr int rate = 44100;
        Plan p;
        p.overlapMs = 2000;
        p.gainOut = Automation(1);
        p.gainIn = Automation(0);
        p.bassCutOut = Automation(1);
        const auto rms = [&](double hz) {
            const auto in = sine(rate, 2, 0.5, hz);
            std::vector<float> out(in.size()), none(in.size(), 0.0f);
            Mixer(p, rate).mix(in.data(), none.data(), out.data(), qsizetype(in.size() / 2));
            double sum = 0;
            for (size_t i = in.size() / 2; i < in.size(); ++i) // after it settled
                sum += double(out[i]) * out[i];
            return std::sqrt(sum / double(in.size() / 2)) / (0.5 / std::sqrt(2));
        };
        QVERIFY2(rms(60) < 0.05, qPrintable(QString::number(rms(60))));
        QVERIFY2(rms(2000) > 0.97, qPrintable(QString::number(rms(2000))));
    }

    void anEchoRepeats()
    {
        constexpr int rate = 1000; // a frame a millisecond
        Plan p;
        p.overlapMs = 500;
        p.gainOut = Automation(0); // only the echo is heard
        p.gainIn = Automation(0);
        p.echoMs = 100;
        p.echoFeedback = 0.5f;
        p.echoSend.add(0, 1).add(0.001, 1).add(0.002, 0);
        std::vector<float> in(2 * 500, 0.0f), none(2 * 500, 0.0f), out(2 * 500);
        in[0] = in[1] = 0.5f;
        Mixer(p, rate).mix(in.data(), none.data(), out.data(), 500);
        QCOMPARE(out[0], 0.0f);
        QCOMPARE(out[2 * 100], 0.5f);
        QCOMPARE(out[2 * 200], 0.25f);
        QCOMPARE(out[2 * 300 + 1], 0.125f);
        QCOMPARE(out[2 * 150], 0.0f);
    }

    void theNextWaitsForItsDelay()
    {
        Plan p;
        p.overlapMs = 1000;
        p.inDelayMs = 300;
        Mixer m(p, 1000);
        QCOMPARE(m.silentIn(200), 200);
        std::vector<float> a(2 * 200, 0.0f), b(2 * 200, 0.0f), out(2 * 200);
        m.mix(a.data(), b.data(), out.data(), 200);
        QCOMPARE(m.silentIn(200), 100);
        m.mix(a.data(), b.data(), out.data(), 200);
        QCOMPARE(m.silentIn(200), 0);
    }

    void beatsAreMixedOnTheBeat()
    {
        // Both 125 BPM and steady, A ending at full: 16 bars of B's start over A's last 16,
        // their beats and bar lines together, the bass swapped in the middle.
        const int rate = 22050;
        const auto a = analyse(groove(rate, 125, 40, 0.5), rate);
        const auto b = analyse(groove(rate, 125, 24, 0.5), rate, false);
        const Track out{a.durationMs, false, &a}, in{b.durationMs, false, &b};
        const Plan p = plan({}, out, 0, in, false);
        QCOMPARE(p.style, Style::BeatMix);
        QCOMPARE(p.bars, 16);
        const double beat = 60000 / 125.0;
        QVERIFY(std::fabs(double(p.overlapMs) - 64 * beat) < 30);
        // B's first beat (at 0.5 s) lands on one of A's bar lines, 16 bars before its end.
        const double barLine = double(p.outMs) + (500 - double(p.inMs));
        const double intoBar = std::fmod(barLine - 500, 4 * beat);
        QVERIFY2(std::min(intoBar, 4 * beat - intoBar) < 5, qPrintable(QString::number(intoBar)));
        QVERIFY(std::fabs(double(p.switchMs) - 32 * beat) < 5);
        QCOMPARE(p.bassCutIn.at(0), 1.0f);
        QCOMPARE(p.bassCutIn.at(double(p.switchMs) / 1000 + 0.01), 0.0f);
        QCOMPARE(p.bassCutOut.at(double(p.switchMs) / 1000 + 0.01), 1.0f);
    }

    void aJointKeepsItsTempo_data()
    {
        QTest::addColumn<double>("next");
        QTest::addColumn<bool>("continuous");
        QTest::newRow("the same tempo: a mix going on") << 125.0 << true;
        QTest::newRow("another tempo: two songs") << 140.0 << false;
    }
    void aJointKeepsItsTempo()
    {
        // A runs to its very end at full level, B starts at once: one recording going on
        // only if the beat does.
        QFETCH(double, next);
        QFETCH(bool, continuous);
        const int rate = 22050;
        auto audio = groove(rate, 125, 24);
        audio.resize(audio.size() - size_t(rate) * 2); // the second of silence after it
        const auto a = analyse(audio, rate);
        const auto b = analyse(groove(rate, next, 16), rate, false);
        QVERIFY(a.durationMs - a.soundEndMs < 50 && b.soundStartMs < 50);
        const Plan p = plan({}, {a.durationMs, false, &a}, 0, {b.durationMs, false, &b}, false);
        QCOMPARE(p.reason == Reason::Continuous, continuous);
        if (!continuous)
            QVERIFY(p.style != Style::Gapless);
    }

    void aFadeOutEndsOnTheBeat()
    {
        // A fades out over its last 10 s (3 dB a second): still a natural end, but B's first
        // bar line comes on one of A's.
        const int rate = 22050;
        auto audio = groove(rate, 125, 40, 0.5);
        const auto frames = qsizetype(audio.size() / 2);
        const auto fadeFrom = frames - qsizetype(11 * rate); // the groove has 1 s of silence after
        for (qsizetype i = fadeFrom; i < frames; ++i) {
            const auto gain = float(std::pow(10.0, -3.0 * double(i - fadeFrom) / rate / 20));
            audio[size_t(i) * 2] *= gain;
            audio[size_t(i) * 2 + 1] *= gain;
        }
        const auto a = analyse(audio, rate);
        const auto b = analyse(groove(rate, 125, 24, 0.5), rate, false);
        QVERIFY(a.fadeStartMs >= 0);
        const Plan p = plan({}, {a.durationMs, false, &a}, 0, {b.durationMs, false, &b}, false);
        QCOMPARE(p.style, Style::NaturalEnd);
        QVERIFY(p.onBeat);
        const double beat = 60000 / 125.0;
        const double barLine = double(p.outMs) + (500 - double(p.inMs)); // B's first beat
        const double intoBar = std::fmod(barLine - 500, 4 * beat);
        QVERIFY2(std::min(intoBar, 4 * beat - intoBar) < 5, qPrintable(QString::number(intoBar)));
        QVERIFY(std::fabs(double(p.outMs) - double(a.fadedBy(20))) < 2 * beat);
    }

    void closeTemposMixForLess_data()
    {
        QTest::addColumn<double>("bpm");
        QTest::addColumn<int>("bars");
        QTest::newRow("0.16 % apart: 8 bars") << 125.2 << 8;
        QTest::newRow("0.5 % apart: none, a filter out") << 125.6 << 0;
    }
    void closeTemposMixForLess()
    {
        QFETCH(double, bpm);
        QFETCH(int, bars);
        const int rate = 22050;
        const auto a = analyse(groove(rate, 125, 40, 0.5), rate);
        const auto b = analyse(groove(rate, bpm, 24, 0.5), rate, false);
        const Plan p = plan({}, {a.durationMs, false, &a}, 0, {b.durationMs, false, &b}, false);
        if (bars) {
            QCOMPARE(p.style, Style::BeatMix);
            QCOMPARE(p.bars, bars);
            return;
        }
        // Its beats would drift apart: A's last 4 beats swept away into an echo, B coming in
        // as A stops.
        QCOMPARE(p.style, Style::FilterOut);
        QVERIFY(std::fabs(double(p.switchMs) - 4 * 60000 / 125.0) < 30);
        QVERIFY(p.echoMs > 470 && p.echoMs < 490);
        QVERIFY(p.sweepOut.at(0) < 30);
        QVERIFY(p.sweepOut.at(double(p.switchMs) / 1000) > 1000);
        QCOMPARE(p.gainOut.at(double(p.switchMs) / 1000 + 0.01), 0.0f);
        QVERIFY(p.inDelayMs > 0 && p.inDelayMs <= p.switchMs);
        QVERIFY(p.overlapMs > p.switchMs + 2000);
    }

    void singingStaysApartOnTheBeat()
    {
        // A sings until 6 bars before its end: done by the middle of 8 bars, not of 16.
        const int rate = 22050;
        const auto a = analyse(groove(rate, 125, 40, 0.5), rate);
        const auto b = analyse(groove(rate, 125, 24, 0.5), rate, false);
        const double beat = 60000 / 125.0;
        const auto lastLine = qint64(500 + (40 - 6) * 4 * beat);
        const Track out{a.durationMs, false, &a, {{lastLine - 3000, lastLine}}};
        const Plan p = plan({}, out, 0, {b.durationMs, false, &b}, false);
        QCOMPARE(p.style, Style::BeatMix);
        QCOMPARE(p.bars, 8);
        QVERIFY(double(p.outMs) + double(p.switchMs) > double(lastLine));
    }

    void anOutroCrossfadeIsOnTheBeat()
    {
        // Both 125 BPM and steady, but A sings until a bar and a half before its end (no
        // beat mix) and ends quieter (no filter out): the crossfade after its singing, with
        // B's first beat on one of A's.
        const int rate = 22050;
        auto audio = groove(rate, 125, 40, 0.5);
        const double beat = 60000 / 125.0;
        const auto from = qsizetype((0.5 + 39 * 4 * beat / 1000) * rate); // its last bar
        for (qsizetype i = from; i < qsizetype(audio.size() / 2); ++i) {
            audio[size_t(i) * 2] *= 0.4f; // −8 dB: no fade, not full either
            audio[size_t(i) * 2 + 1] *= 0.4f;
        }
        const auto a = analyse(audio, rate);
        const auto b = analyse(groove(rate, 125, 24, 0.5), rate, false);
        QVERIFY(a.fadeStartMs < 0);
        const auto lastLine = qint64(500 + (40 - 1.5) * 4 * beat);
        const Track out{a.durationMs, false, &a, {{lastLine - 3000, lastLine}}};
        const Plan p = plan({}, out, 0, {b.durationMs, false, &b}, false);
        QCOMPARE(p.style, Style::Crossfade);
        QCOMPARE(p.reason, Reason::Outro);
        QVERIFY(p.onBeat);
        QVERIFY(p.outMs >= lastLine + 500);
        const double landing = double(p.outMs) + (500 - double(p.inMs)); // B's first beat
        const double intoBeat = std::fmod(landing - 500, beat);
        QVERIFY2(std::min(intoBeat, beat - intoBeat) < 5, qPrintable(QString::number(intoBeat)));
    }

    void aTrackerOnTheOffbeatsStillFindsTheClick()
    {
        // A fast punk beat (200 BPM: kicks on the beats, snares on the offbeats) with its
        // offbeats accented for a while: the tracker follows them there, half a beat off. It
        // was still made to a click: its beats are that grid, on the kicks.
        RhythmFeatures f;
        f.frameMs = 1000.0 * 128 / 11025;
        const double beat = 300, length = 90000;
        const auto n = size_t(length / f.frameMs);
        f.onset.assign(n, 0.0f);
        f.kick.assign(n, 0.0f);
        f.snare.assign(n, 0.0f);
        f.bass.assign(n, -60.0f);
        const auto frame = [&](double ms) { return size_t(std::lround(ms / f.frameMs)); };
        for (int i = 0; 500 + i * beat / 2 < length - 500; ++i) {
            const double t = 500 + i * beat / 2;
            // Most onsets off the beat in all: only the kicks say where it is.
            const bool accented = (t > 20000 && t < 45000) || (t > 55000 && t < 80000);
            f.onset[frame(t)] = i % 2 == 0 ? (accented ? 3.0f : 6.0f) : (accented ? 9.0f : 3.0f);
            (i % 2 == 0 ? f.kick : f.snare)[frame(t)] = 10.0f;
        }
        const Rhythm found = findRhythm(f, false);
        QVERIFY(!found.beats.empty());
        const Grid g = fitGrid(found.beats, 0, length);
        QVERIFY2(g.inliers < 0.8, qPrintable(QString::number(g.inliers))); // it did slip
        const Rhythm r = findRhythm(f);
        QVERIFY(r.steady);
        QVERIFY(double(r.beats.size()) > 0.9 * (length - 1000) / (2 * beat));
        for (const float b : r.beats) {
            const double intoBeat = std::fmod(double(b) - 500 + 1000 * beat, beat);
            QVERIFY2(std::min(intoBeat, beat - intoBeat) < 8, qPrintable(QString::number(b)));
        }
    }

    void everyQueuedAnalysisComesBack()
    {
        // Jobs queued back to back (the current track's and the next one's): the second
        // starts before the first's result reaches this thread, which still gets it. A
        // canceled one doesn't come back.
        Analyst analyst;
        QList<quint64> back;
        connect(&analyst, &Analyst::analysed, this,
                [&back](quint64 id, std::shared_ptr<const Analysis> analysis, qint64) {
                    if (analysis && analysis->complete)
                        back.append(id);
                });
        const auto file = [](const char* format) {
            return LocalFileSource::open(QStringLiteral(AUDIO_FIXTURES "/tone.") +
                                         QLatin1String(format));
        };
        analyst.analyse(1, file("flac"), QStringLiteral("flac"));
        analyst.analyse(2, file("mp3"), QStringLiteral("mp3"));
        analyst.analyse(3, file("flac"), QStringLiteral("flac"));
        analyst.analyse(4, file("mp3"), QStringLiteral("mp3"));
        analyst.cancel(3);
        QTRY_COMPARE(back.size(), 3);
        std::sort(back.begin(), back.end());
        QCOMPARE(back, (QList<quint64>{1, 2, 4}));
    }

    void limitsOnlyThePeaks()
    {
        QCOMPARE(softLimit(0.5f), 0.5f);
        QCOMPARE(softLimit(-0.8f), -0.8f);
        QVERIFY(softLimit(1.6f) < 1.0f && softLimit(1.6f) > 0.95f);
        QVERIFY(softLimit(-3.0f) >= -1.0f); // never past full scale
        QVERIFY(softLimit(0.95f) < 0.95f && softLimit(0.95f) > 0.93f);
    }

    void aDeckPullsExactly()
    {
        for (const char* format : {"flac", "mp3"}) {
            auto decoder = tone(format);
            QVERIFY(decoder);
            Deck deck(std::move(decoder), 44100, 1.0f, false);
            std::vector<float> chunk(2 * 4000);
            QCOMPARE(deck.pull(chunk.data(), 4000, false), 4000);
            QCOMPARE(deck.positionMs(), 4000 * 1000 / 44100);
            QCOMPARE(4000 + drain(deck), ToneFrames);
            QVERIFY(deck.ended());
        }
    }

    void aDeckFollowsGaplessly()
    {
        Deck deck(tone("flac"), 44100, 1.0f, false);
        std::vector<float> chunk(2 * 30000);
        const auto first = deck.pull(chunk.data(), 30000, false);
        QCOMPARE(first, ToneFrames);
        QVERIFY(deck.decoderEnded());
        const qint64 joint = deck.follow(tone("mp3"), 1.0f);
        QCOMPARE(joint, ToneFrames);
        QCOMPARE(deck.positionMs(), 0);
        QCOMPARE(deck.pull(chunk.data(), 1000, false), 1000);
        QCOMPARE(deck.positionMs(), 1000 * 1000 / 44100);
        QCOMPARE(1000 + drain(deck), ToneFrames);
    }

    void aDeckResamplesAndSeeks()
    {
        Deck deck(tone("flac"), 48000, 1.0f, false);
        const qint64 all = drain(deck);
        QVERIFY2(std::abs(all - ToneFrames * 48000 / 44100) <= 2, qPrintable(QString::number(all)));
        QVERIFY(deck.seek(250));
        QCOMPARE(deck.positionMs(), 250);
        QVERIFY(std::abs(drain(deck) - all / 2) <= 2);
    }

    void aDeckRampsItsGain()
    {
        Deck deck(tone("flac"), 44100, 0.5f, true);
        std::vector<float> chunk(2 * 100);
        deck.pull(chunk.data(), 100, true);
        std::vector<float> plain(2 * 100);
        Deck reference(tone("flac"), 44100, 1.0f, false);
        reference.pull(plain.data(), 100, false);
        // On from the first frame when it starts normalized.
        QVERIFY(std::abs(chunk[2 * 50] - plain[2 * 50] * 0.5f) < 1e-6);
        // Switched off: back to 1, sliding.
        deck.pull(chunk.data(), 100, false);
        reference.pull(plain.data(), 100, false);
        QVERIFY(std::abs(chunk[2 * 99]) < std::abs(plain[2 * 99]) || plain[2 * 99] == 0);
    }
    void aSkipFadesOut()
    {
        // What was queued fades from full gain to silence over as much as fits.
        AudioRing ring(1000);
        const std::vector<float> half(2 * 1000, 0.5f);
        ring.write(half.data(), 1000);
        FadeTail tail(800);
        tail.take(ring, 1000);
        QCOMPARE(ring.readable(), 200); // the flush drops the rest
        std::vector<float> out(2 * 1000, 0.0f);
        tail.mix(out.data(), 1000, 1.0f, false);
        QCOMPARE(out[0], 0.5f);
        for (int i = 1; i < 800; ++i)
            QVERIFY(out[size_t(i) * 2] < out[size_t(i - 1) * 2]);
        QVERIFY(out[799 * 2] < 0.01f);
        QCOMPARE(out[800 * 2], 0.0f);
        QVERIFY(!tail.active());
    }

    void skipsInARowStayContinuous()
    {
        // The second skip carries the first one's tail on from the gain it had reached.
        AudioRing ring(1000);
        const std::vector<float> half(2 * 1000, 0.5f);
        ring.write(half.data(), 1000);
        FadeTail tail(1000);
        tail.take(ring, 1000);
        std::vector<float> out(2 * 400, 0.0f);
        tail.mix(out.data(), 400, 1.0f, false);
        const std::vector<float> silence(2 * 300, 0.0f); // the next track not started yet
        ring.write(silence.data(), 300);
        tail.take(ring, 300);
        std::vector<float> rest(2 * 1000, 0.0f);
        tail.mix(rest.data(), 1000, 1.0f, false);
        QVERIFY(std::abs(rest[0] - out[399 * 2]) < 0.01f);
        QVERIFY(rest[599 * 2] < 0.01f); // the 600 frames left of the first one
        QCOMPARE(rest[600 * 2], 0.0f);
    }

    void aSkipUnderNewAudioIsLimited()
    {
        AudioRing ring(100);
        const std::vector<float> loud(2 * 100, 0.9f);
        for (const bool limit : {false, true}) {
            ring.write(loud.data(), 100);
            FadeTail tail(100);
            tail.take(ring, 100);
            std::vector<float> out = loud; // the next track, as loud
            tail.mix(out.data(), 100, 1.0f, limit);
            const float peak = *std::max_element(out.begin(), out.end());
            QVERIFY(limit ? peak <= 1.0f : peak > 1.7f);
            QVERIFY(*std::min_element(out.begin(), out.end()) >= 0.9f);
        }
    }
    void measuresLoudness()
    {
        // BS.1770: a 1 kHz sine 20 dB under full scale in both channels is −20 LUFS, at
        // any rate; silence is gated out.
        for (const int rate : {22050, 44100, 48000, 96000}) {
            const auto a = analyse(sine(rate, 5, 0.1), rate);
            QVERIFY2(std::abs(a.loudness + 20) < 0.1f, qPrintable(QString::number(a.loudness)));
            // (The 400 ms blocks over the edge count: a little under; ungated, −24.8.)
            QVERIFY(std::abs(analyse(sine(rate, 5, 0.1) + silence(rate, 10), rate).loudness + 20) <
                    0.3f);
        }
        QCOMPARE(analyse(silence(44100, 2), 44100).loudness, Analysis::kFloor);
        const auto a = analyse(sine(44100, 2, 0.1) + sine(44100, 2, 0.01), 44100);
        QVERIFY(std::abs(a.momentaryAt(500) + 20) < 0.5f);
        QVERIFY(std::abs(a.momentaryAt(3000) + 40) < 0.5f);
        QVERIFY(std::abs(a.loudnessOver(2500, 3500) + 40) < 0.5f);
    }

    void findsTheSilentEdges()
    {
        const int rate = 44100;
        const auto a = analyse(silence(rate, 1) + sine(rate, 2, 0.1) + silence(rate, 1.5), rate);
        QCOMPARE(a.durationMs, 4500);
        QVERIFY(std::abs(a.soundStartMs - 1000) <= 10);
        QVERIFY(std::abs(a.soundEndMs - 3000) <= 10);
        QCOMPARE(a.fadeStartMs, -1); // an abrupt end
        // All silence: no sound anywhere.
        const auto quiet = analyse(silence(rate, 2), rate);
        QCOMPARE(quiet.soundStartMs, quiet.soundEndMs);
    }

    void findsAFadeOut()
    {
        // 20 s at −14 dBFS, then 6 dB a second down to silence.
        const int rate = 44100;
        const auto audio = sine(rate, 20, 0.2) + fading(rate, 10, 0.2, 6) + silence(rate, 1);
        const auto a = analyse(audio, rate);
        QVERIFY2(a.fadeStartMs >= 19000 && a.fadeStartMs <= 21500,
                 qPrintable(QString::number(a.fadeStartMs)));
        QVERIFY2(std::abs(a.soundEndMs - 27700) < 300, qPrintable(QString::number(a.soundEndMs)));
        const qint64 quieter = a.fadedBy(20); // about 20 / 6 s in
        QVERIFY2(quieter > 22500 && quieter < 24500, qPrintable(QString::number(quieter)));
        // Only the start analysed: nothing about the end.
        QCOMPARE(analyse(audio, rate, false).fadeStartMs, -1);
        // A track that ends at full level doesn't fade.
        QCOMPARE(analyse(sine(rate, 30, 0.2), rate).fadeStartMs, -1);
    }

    void findsBeatsAndBars_data()
    {
        QTest::addColumn<int>("rate");
        QTest::addColumn<double>("bpm");
        QTest::addColumn<bool>("floor");
        QTest::newRow("44.1 kHz, 125 BPM") << 44100 << 125.0 << false;
        QTest::newRow("48 kHz, 128 BPM, four on the floor") << 48000 << 128.0 << true;
        QTest::newRow("44.1 kHz, 92 BPM") << 44100 << 92.0 << false;
    }
    void findsBeatsAndBars()
    {
        QFETCH(int, rate);
        QFETCH(double, bpm);
        QFETCH(bool, floor);
        const double leadIn = 0.5;
        const int bars = 24;
        const Rhythm r = analyse(groove(rate, bpm, bars, leadIn, floor), rate).rhythm;
        QVERIFY2(std::fabs(r.bpm - bpm) < 1, qPrintable(QString::number(r.bpm)));
        QVERIFY(r.steady);
        QVERIFY(r.strength > 0.3);
        const auto truth = grooveBeats(bpm, bars, leadIn);
        const auto [mean, worst] = beatErrors(r.beats, truth);
        QVERIFY2(worst < 5, qPrintable(QStringLiteral("mean %1 worst %2").arg(mean).arg(worst)));
        QVERIFY2(r.beats.size() >= truth.size() - 2 && r.beats.size() <= truth.size(),
                 qPrintable(QString::number(r.beats.size())));
        // The bars: the downbeat is on a bar's first beat.
        const double first = r.beats[size_t(r.downbeat)];
        const double beatMs = 60000 / bpm;
        const double intoBar =
            std::fmod(first - leadIn * 1000 + beatMs / 2, 4 * beatMs) - beatMs / 2;
        QVERIFY2(std::fabs(intoBar) < 15,
                 qPrintable(QStringLiteral("%1 ms into the bar, confidence %2")
                                .arg(intoBar)
                                .arg(r.barConfidence)));
        QVERIFY(r.barConfidence > 0.2);
        const Grid grid = fitGrid(r.beats, 0, 1e9);
        QVERIFY2(std::fabs(60000 / grid.periodMs - bpm) < 0.05,
                 qPrintable(QString::number(60000 / grid.periodMs)));
        QVERIFY(grid.inliers > 0.95);
    }

    void aDriftingBandIsNotSteady()
    {
        // From 120 BPM down by 0.1 % a beat (8 % in 80 beats): locally still a grid, not
        // one for all of it.
        const Rhythm r = analyse(groove(44100, 120, 20, 0.5, false, 1.001), 44100).rhythm;
        QVERIFY(!r.steady);
        const auto [mean, worst] = beatErrors(r.beats, grooveBeats(120, 20, 0.5, 1.001));
        QVERIFY2(worst < 25, qPrintable(QStringLiteral("mean %1 worst %2").arg(mean).arg(worst)));
        QVERIFY(fitGrid(r.beats, 0, 1e9).inliers < 0.8);
        const Grid start = fitGrid(r.beats, 0, 8000);
        QVERIFY(start.inliers > 0.9);
    }

    void aToneHasNoBeats()
    {
        const Rhythm r = analyse(sine(44100, 20, 0.2, 440), 44100).rhythm;
        QVERIFY(r.beats.empty() || r.strength < 0.2);
    }

    void anAnalysisRoundTrips()
    {
        const auto a = analyse(silence(44100, 0.5) + sine(44100, 3, 0.1), 44100);
        const auto back = Analysis::deserialize(a.serialize());
        QVERIFY(back);
        QCOMPARE(back->durationMs, a.durationMs);
        QCOMPARE(back->loudness, a.loudness);
        QCOMPARE(back->soundStartMs, a.soundStartMs);
        QCOMPARE(back->soundEndMs, a.soundEndMs);
        QCOMPARE(back->fadeStartMs, a.fadeStartMs);
        QCOMPARE(back->momentary, a.momentary);
        const auto g = analyse(groove(44100, 125, 8), 44100);
        const auto again = Analysis::deserialize(g.serialize());
        QVERIFY(again && !g.rhythm.beats.empty());
        QCOMPARE(again->rhythm.beats, g.rhythm.beats);
        QCOMPARE(again->rhythm.bpm, g.rhythm.bpm);
        QCOMPARE(again->rhythm.steady, g.rhythm.steady);
        QCOMPARE(again->rhythm.downbeat, g.rhythm.downbeat);
        QCOMPARE(again->rhythm.accent, g.rhythm.accent);
        QByteArray other = a.serialize();
        other[3] = char(Analysis::kVersion + 1); // another version: analysed again
        QVERIFY(!Analysis::deserialize(other));
        QVERIFY(!Analysis::deserialize(a.serialize().chopped(1)));
    }
    void skipsTheSilenceAtTheEdges()
    {
        // 3 s of silence after the current track's sound, 1.5 s before the next one's: the
        // fixed crossfade ends where the sound does and starts the next at its first sound.
        const auto a = made(200000, 0, 197000), b = made(180000, 1500, 180000);
        const auto p = plan({TransitionMode::Crossfade, 6000}, {200000, false, &a}, 0,
                            {180000, false, &b}, false);
        QCOMPARE(p.style, Style::Crossfade);
        QCOMPARE(p.outMs, 197000 - 6000);
        QCOMPARE(p.inMs, 1500);
        QCOMPARE(p.overlapMs, 6000);
        // Without the next track's analysis the smart mode does the same with 6 s.
        const auto fallback = plan({}, {200000, false, &a}, 0, {180000}, false);
        QCOMPARE(fallback.reason, Reason::NoAnalysis);
        QCOMPARE(fallback.outMs, 197000 - 6000);
        QCOMPARE(fallback.inMs, 0);
    }

    void aContinuousJointStaysGapless()
    {
        // Sound to the very end and from the very start: one piece, in either mode.
        const auto a = made(200000, 0, 200000), b = made(180000, 0, 180000);
        for (const auto mode : {TransitionMode::Smart, TransitionMode::Crossfade}) {
            const auto p = plan({mode, 6000}, {200000, false, &a}, 0, {180000, false, &b}, false);
            QCOMPARE(p.style, Style::Gapless);
            QCOMPARE(p.reason, Reason::Continuous);
        }
    }

    void aFadeOutEndsNaturally()
    {
        const int rate = 44100;
        const auto a =
            analyse(sine(rate, 20, 0.2) + fading(rate, 10, 0.2, 6) + silence(rate, 1), rate);
        const auto b = made(180000, 800, 180000);
        const auto p = plan({}, {a.durationMs, false, &a}, 0, {180000, false, &b}, false);
        QCOMPARE(p.style, Style::NaturalEnd);
        QCOMPARE(p.reason, Reason::FadeOut);
        QCOMPARE(p.outMs, a.fadedBy(20)); // the next comes in once it is 20 dB down
        QCOMPARE(p.outMs + p.overlapMs, a.soundEndMs);
        QCOMPARE(p.inMs, 800);
        QCOMPARE(p.switchMs, 0);
        QCOMPARE(p.gainOut.at(0), 1.0f);
        QCOMPARE(p.gainIn.at(0.03), 1.0f); // at once, without a click
    }

    void theCrossfadeStaysOutOfTheSinging()
    {
        // The current track's last line ends 5 s before its sound; the next one sings
        // from 4 s in, after a quiet intro from 1 s.
        auto a = made(200000, 0, 198000);
        const auto b = made(180000, 1000, 180000);
        Track out{200000, false, &a, {{180000, 193000}}};
        Track in{180000, false, &b, {{4000, 8000}}};
        auto p = plan({}, out, 0, in, false);
        QCOMPARE(p.style, Style::Crossfade);
        QCOMPARE(p.reason, Reason::Outro);
        QCOMPARE(p.overlapMs, 4000 - 500 - 1000); // up to half a second before the singing
        QCOMPARE(p.outMs, 198000 - p.overlapMs);
        QVERIFY(p.outMs >= 193000 + 500); // after the last line
        QCOMPARE(p.inMs, 1000);
        // An instrumental next track: as long as the outro has room for, 8 s at most.
        in.vocals.clear();
        p = plan({}, out, 0, in, false);
        QCOMPARE(p.overlapMs, 198000 - 193000 - 500);
        out.vocals.clear();
        QCOMPARE(plan({}, out, 0, in, false).overlapMs, 8000);
    }

    void noRoomBetweenTheSingingIsShort()
    {
        // Singing to the end of one and from the start of the other: the shortest crossfade.
        const auto a = made(200000, 0, 198000), b = made(180000, 500, 180000);
        const Track out{200000, false, &a, {{190000, 197900}}};
        const Track in{180000, false, &b, {{600, 5000}}};
        const auto p = plan({}, out, 0, in, false);
        QCOMPARE(p.reason, Reason::Tight);
        QCOMPARE(p.overlapMs, kMinimumOverlapMs);
        QCOMPARE(p.outMs, 198000 - kMinimumOverlapMs);
    }

    void aLoudOpeningComesInQuicker()
    {
        const auto a = made(200000, 0, 198000);
        const auto loud = made(180000, 0, 180000, -10, -11);
        const auto p = plan({}, {200000, false, &a}, 0, {180000, false, &loud}, false);
        QCOMPARE(p.overlapMs, 4000);
    }
};

QTEST_GUILESS_MAIN(AutomixTest)
#include "automix_test.moc"
