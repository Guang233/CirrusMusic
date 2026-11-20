// Transitions between tracks (docs/tasks/automix.md): the planner, the gain curves, the
// mix and the deck that feeds it, a skip's quick fade. No output device.
#include "infrastructure/media/audio_decoder.h"
#include "infrastructure/media/audio_output.h"
#include "infrastructure/media/automix/deck.h"
#include "infrastructure/media/automix/mix.h"
#include "infrastructure/media/automix/transition.h"
#include "infrastructure/media/byte_source.h"
#include <QtTest>
#include <algorithm>
#include <cmath>

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
        mixOverlap(p, a.data(), b.data(), mixed.data(), 2000, 0, rate);
        QCOMPARE(mixed[0], 0.5f);
        const float middle = (0.5f - 0.25f) * float(std::sqrt(0.5));
        QVERIFY(std::abs(mixed[2 * 1000] - middle) < 1e-3);
        QVERIFY(std::abs(mixed[2 * 1999 + 1] + 0.25f) < 2e-3);
        // A later piece of the same overlap continues the curve.
        std::vector<float> tail(2 * 10);
        mixOverlap(p, a.data(), b.data(), tail.data(), 10, 1000, rate);
        QCOMPARE(tail[0], mixed[2 * 1000]);
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
};

QTEST_GUILESS_MAIN(AutomixTest)
#include "automix_test.moc"
