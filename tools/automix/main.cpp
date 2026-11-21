// The automix tuning tool (docs/tasks/automix.md §9): renders the transition between two
// local files the way the engine plays it, to listen to and compare. Development only.
//
//   cirrus_automix analyse a.flac [b.mp3 ...]
//   cirrus_automix beats a.flac [-o clicks.wav]
//   cirrus_automix report a.flac b.mp3 [c.flac ...]
//   cirrus_automix mix a.flac b.mp3 -o out.wav [--mode smart|crossfade|gapless]
//                  [--crossfade 6] [--context 60] [--rate 48000] [--no-analysis]
//
// `analyse` prints what the analysis finds in each file (and how long it took).
// Writes A's last `context` seconds into B's first, through the transition, as 32-bit
// float WAVE. Without the service's loudness gains: no normalization.
#include "infrastructure/media/audio_decoder.h"
#include "infrastructure/media/automix/analysis.h"
#include "infrastructure/media/automix/deck.h"
#include "infrastructure/media/automix/mix.h"
#include "infrastructure/media/automix/transition.h"
#include "infrastructure/media/byte_source.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

using namespace cirrus;
using namespace cirrus::media;
using domain::TransitionMode;

namespace
{
constexpr qsizetype kChunkFrames = 4096;

void complain(const QString& message)
{
    std::fprintf(stderr, "cirrus_automix: %s\n", qPrintable(message));
}

double seconds(qint64 ms) { return double(ms) / 1000; }

// From the name, or for the cache's files (`<md5>-<size>.audio`) from the first bytes.
QString formatOf(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("mp3") || suffix == QLatin1String("flac"))
        return suffix;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray head = file.read(10);
    if (head.startsWith("ID3") && head.size() == 10) {
        // A tag first (FLAC files may have one too): what follows it says.
        qint64 size = 10 + ((qint64(head[6] & 0x7f) << 21) | (qint64(head[7] & 0x7f) << 14) |
                            (qint64(head[8] & 0x7f) << 7) | qint64(head[9] & 0x7f));
        if (head[5] & 0x10)
            size += 10; // a footer
        if (!file.seek(size))
            return {};
        head = file.read(4);
        return head.startsWith("fLaC") ? QStringLiteral("flac") : QStringLiteral("mp3");
    }
    if (head.startsWith("fLaC"))
        return QStringLiteral("flac");
    if (head.size() >= 2 && quint8(head[0]) == 0xff && (quint8(head[1]) & 0xe0) == 0xe0)
        return QStringLiteral("mp3");
    return {};
}

std::unique_ptr<AudioDecoder> openTrack(const QString& path)
{
    const QString format = formatOf(path);
    if (format.isEmpty()) {
        complain(QStringLiteral("%1: not MP3 or FLAC").arg(path));
        return nullptr;
    }
    auto decoder = std::make_unique<AudioDecoder>();
    const auto file = LocalFileSource::open(path);
    if (!file || !decoder->open(file, format)) {
        complain(QStringLiteral("%1: can't decode it").arg(path));
        return nullptr;
    }
    return decoder;
}

// A WAVE file of interleaved stereo 32-bit float; the sizes go in at the end.
class WaveFile
{
  public:
    bool open(const QString& path, int rate)
    {
        m_rate = rate;
        m_file.setFileName(path);
        return m_file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
               m_file.write(header()) == kHeaderBytes;
    }
    bool write(const float* frames, qsizetype count)
    {
        static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN, "WAVE samples are little-endian");
        m_frames += count;
        const qint64 bytes = qint64(count) * 8;
        return m_file.write(reinterpret_cast<const char*>(frames), bytes) == bytes;
    }
    bool finish() { return m_file.seek(0) && m_file.write(header()) == kHeaderBytes; }
    [[nodiscard]] qint64 frames() const { return m_frames; }

  private:
    static constexpr qint64 kHeaderBytes = 12 + 26 + 12 + 8;
    [[nodiscard]] QByteArray header() const
    {
        QByteArray bytes;
        const auto u16 = [&bytes](quint16 value) {
            char le[2];
            qToLittleEndian(value, le);
            bytes.append(le, 2);
        };
        const auto u32 = [&bytes](quint32 value) {
            char le[4];
            qToLittleEndian(value, le);
            bytes.append(le, 4);
        };
        const auto data = quint32(m_frames * 8);
        bytes.append("RIFF");
        u32(quint32(kHeaderBytes - 8) + data);
        bytes.append("WAVE");
        bytes.append("fmt ");
        u32(18);
        u16(3); // IEEE float
        u16(2);
        u32(quint32(m_rate));
        u32(quint32(m_rate) * 8);
        u16(8);
        u16(32);
        u16(0);
        bytes.append("fact");
        u32(4);
        u32(quint32(m_frames));
        bytes.append("data");
        u32(data);
        return bytes;
    }

    QFile m_file;
    int m_rate = 0;
    qint64 m_frames = 0;
};

// Where rendered audio goes: a file, or memory.
using Sink = std::function<void(const float* frames, qsizetype count)>;

// Up to `frames` frames from `deck`; fewer only at its end, or when decoding failed.
qsizetype pullAll(automix::Deck& deck, float* out, qsizetype frames)
{
    qsizetype got = 0;
    while (got < frames) {
        got += deck.pull(out + got * 2, frames - got, false);
        if (got == frames || deck.ended() || !deck.decoderEnded())
            break;
        deck.finish(); // the resampler's last frames
    }
    return got;
}

// `deck` alone up to its track's media time `untilMs` (or its end).
void playUntil(automix::Deck& deck, qint64 untilMs, const Sink& out)
{
    std::vector<float> chunk(2 * kChunkFrames);
    for (qint64 left = deck.framesUntil(untilMs); left > 0;) {
        const auto want = qsizetype(std::min<qint64>(kChunkFrames, left));
        const qsizetype got = pullAll(deck, chunk.data(), want);
        out(chunk.data(), got);
        if (got < want)
            return;
        left -= got;
    }
}

// A to its end and B straight on through the same resampler: the engine's gapless joint.
void renderGapless(automix::Deck& deck, std::unique_ptr<AudioDecoder> next, qint64 untilMs,
                   const Sink& out)
{
    std::vector<float> chunk(2 * kChunkFrames);
    while (true) {
        const qsizetype got = deck.pull(chunk.data(), kChunkFrames, false);
        out(chunk.data(), got);
        if (got == kChunkFrames)
            continue;
        if (!deck.decoderEnded()) {
            complain(QStringLiteral("decoding A stopped"));
            return;
        }
        break;
    }
    deck.follow(std::move(next), 1.0f);
    playUntil(deck, untilMs, out);
}

void renderOverlap(const automix::Plan& plan, automix::Deck& a, std::unique_ptr<AudioDecoder> next,
                   int rate, qint64 untilMs, const Sink& out)
{
    playUntil(a, plan.outMs, out);
    automix::Deck b(std::move(next), rate, 1.0f, false);
    if (plan.inMs > 0 && !b.seek(plan.inMs)) {
        complain(QStringLiteral("can't seek B"));
        return;
    }
    std::vector<float> chunk(2 * kChunkFrames), other(2 * kChunkFrames);
    const qint64 overlap = plan.overlapMs * rate / 1000;
    automix::Mixer mixer(plan, rate);
    for (qint64 offset = 0; offset < overlap;) {
        const auto want = qsizetype(std::min<qint64>(kChunkFrames, overlap - offset));
        const qsizetype gotA = pullAll(a, chunk.data(), want);
        std::fill(chunk.begin() + gotA * 2, chunk.begin() + want * 2, 0.0f);
        std::fill(other.begin(), other.begin() + want * 2, 0.0f);
        const qsizetype silent = mixer.silentIn(want);
        pullAll(b, other.data() + silent * 2, want - silent);
        mixer.mix(chunk.data(), other.data(), chunk.data(), want);
        out(chunk.data(), want);
        offset += want;
    }
    playUntil(b, untilMs, out);
}

// The whole file through the analyser, at its own rate.
std::optional<automix::Analysis> analyseFile(const QString& path, qint64 untilMs = 0)
{
    auto decoder = openTrack(path);
    if (!decoder)
        return std::nullopt;
    automix::Analyser analyser(decoder->sampleRate());
    const bool timing = qEnvironmentVariableIsSet("CIRRUS_AUTOMIX_TIMES");
    automix::RhythmFrontEnd frontEnd(decoder->sampleRate()); // timed alone too
    std::vector<float> chunk;
    QElapsedTimer timer;
    qint64 decoding = 0, adding = 0, rhythm = 0;
    while (true) {
        chunk.clear();
        timer.start();
        const bool more = decoder->read(chunk, kChunkFrames);
        decoding += timer.nsecsElapsed();
        if (!more || (untilMs > 0 && analyser.durationMs() >= untilMs))
            break;
        timer.start();
        analyser.add(chunk.data(), qsizetype(chunk.size() / 2));
        adding += timer.nsecsElapsed();
        if (timing) {
            timer.start();
            frontEnd.add(chunk.data(), qsizetype(chunk.size() / 2));
            rhythm += timer.nsecsElapsed();
        }
    }
    if (decoder->error()) {
        complain(QStringLiteral("%1: decoding failed").arg(path));
        return std::nullopt;
    }
    timer.start();
    auto analysis = analyser.finish(untilMs <= 0 || analyser.durationMs() < untilMs);
    if (timing)
        std::fprintf(
            stderr,
            "  decoding %lld ms, analysing %lld ms (the rhythm's part %lld ms), "
            "finishing %lld ms\n",
            static_cast<long long>(decoding / 1000000), static_cast<long long>(adding / 1000000),
            static_cast<long long>(rhythm / 1000000), static_cast<long long>(timer.elapsed()));
    return analysis;
}

int analyse(const QStringList& files)
{
    if (files.isEmpty()) {
        complain(QStringLiteral("analyse needs a file"));
        return 2;
    }
    int status = 0;
    for (const QString& path : files) {
        QElapsedTimer timer;
        timer.start();
        const auto a = analyseFile(path);
        if (!a) {
            status = 1;
            continue;
        }
        std::printf("%s\n  %.1f s, %.1f LUFS, sound %.2f-%.2f s", qPrintable(path),
                    seconds(a->durationMs), a->loudness, seconds(a->soundStartMs),
                    seconds(a->soundEndMs));
        if (a->fadeStartMs >= 0)
            std::printf(", fades out from %.1f s (-20 dB at %.1f s)", seconds(a->fadeStartMs),
                        seconds(a->fadedBy(20)));
        std::printf("\n  start %.1f LUFS over 2 s, end %.1f LUFS over 2 s; %lld ms\n",
                    a->loudnessOver(a->soundStartMs, a->soundStartMs + 2000),
                    a->loudnessOver(a->soundEndMs - 2000, a->soundEndMs),
                    static_cast<long long>(timer.elapsed()));
        const automix::Rhythm& r = a->rhythm;
        if (r.beats.empty()) {
            std::printf("  no beats\n");
            continue;
        }
        std::printf("  %.2f BPM, strength %.2f, %zu beats %.2f-%.2f s%s, bars from beat %d "
                    "(confidence %.2f)\n",
                    double(r.bpm), double(r.strength), r.beats.size(),
                    double(r.beats.front()) / 1000, double(r.beats.back()) / 1000,
                    r.steady ? " (steady)" : "", r.downbeat, double(r.barConfidence));
        const auto grid = [&r](const char* name, const automix::Grid& g) {
            if (!g.valid()) {
                std::printf("  %s: no grid\n", name);
                return;
            }
            const auto bars = automix::barPhase(r, g.first, g.first + g.count);
            std::printf("  %s: %.3f BPM over %d beats, %.1f ms rms, %.0f%% within 20 ms, bars "
                        "%.2f\n",
                        name, 60000 / g.periodMs, g.count, g.rmsMs, g.inliers * 100,
                        double(bars.confidence));
        };
        grid("first 40 s", automix::fitGrid(r.beats, r.beats.front(), r.beats.front() + 40000));
        grid("last 40 s ", automix::fitGrid(r.beats, r.beats.back() - 40000, r.beats.back() + 1));
    }
    return status;
}

// The beats found in a file, one per line (ms, then 1 where a bar starts); with -o the
// file itself with a click on each beat (a higher one on bar starts), to listen to.
int beats(const QCommandLineParser& parser, const QStringList& args)
{
    if (args.size() != 2) {
        complain(QStringLiteral("beats needs a track"));
        return 2;
    }
    const auto a = analyseFile(args[1]);
    if (!a)
        return 1;
    const automix::Rhythm& r = a->rhythm;
    for (size_t i = 0; i < r.beats.size(); ++i)
        std::printf("%.1f %d\n", double(r.beats[i]), int((int(i) - r.downbeat) % 4 == 0));
    if (!parser.isSet(QStringLiteral("output")))
        return 0;
    auto decoder = openTrack(args[1]);
    if (!decoder)
        return 1;
    const int rate = decoder->sampleRate();
    WaveFile out;
    const QString path = parser.value(QStringLiteral("output"));
    if (!out.open(path, rate)) {
        complain(QStringLiteral("%1: can't write it").arg(path));
        return 1;
    }
    std::vector<float> chunk;
    qint64 frame = 0;
    size_t next = 0;
    std::vector<std::pair<qint64, bool>> clicks; // start frame, bar start
    for (size_t i = 0; i < r.beats.size(); ++i)
        clicks.push_back(
            {qint64(double(r.beats[i]) * rate / 1000), (int(i) - r.downbeat) % 4 == 0});
    const qint64 clickFrames = rate / 50; // 20 ms
    while (true) {
        chunk.clear();
        if (!decoder->read(chunk, kChunkFrames))
            break;
        const auto frames = qsizetype(chunk.size() / 2);
        for (qsizetype i = 0; i < frames; ++i, ++frame) {
            while (next < clicks.size() && clicks[next].first + clickFrames < frame)
                ++next;
            float click = 0;
            for (size_t c = next; c < clicks.size() && clicks[c].first <= frame; ++c) {
                const qint64 t = frame - clicks[c].first;
                if (t >= clickFrames)
                    continue;
                const double hz = clicks[c].second ? 2000 : 1000;
                click += float(0.3 * std::sin(2 * M_PI * hz * double(t) / rate) *
                               (1 - double(t) / double(clickFrames)));
            }
            chunk[size_t(i) * 2] = chunk[size_t(i) * 2] * 0.7f + click;
            chunk[size_t(i) * 2 + 1] = chunk[size_t(i) * 2 + 1] * 0.7f + click;
        }
        out.write(chunk.data(), frames);
    }
    if (!out.finish()) {
        complain(QStringLiteral("%1: can't write it").arg(path));
        return 1;
    }
    return 0;
}

// Each track into the next as the engine plans it (the next one's first minute analysed,
// planned a minute before the end, the smart mode). The plans on the beat are rendered
// (10 s either side of the overlap) and analysed again: how closely the overlap keeps to
// one beat grid says whether the two tracks' beats met.
int report(const QStringList& files)
{
    if (files.size() < 2) {
        complain(QStringLiteral("report needs two tracks or more"));
        return 2;
    }
    constexpr int rate = 44100;
    constexpr qint64 aroundMs = 10000;
    std::vector<std::optional<automix::Analysis>> whole(size_t(files.size())),
        start(size_t(files.size()));
    std::vector<qint64> durations(size_t(files.size()), 0);
    for (qsizetype i = 0; i < files.size(); ++i) {
        if (auto decoder = openTrack(files[i]))
            durations[size_t(i)] = decoder->durationMs();
        if (i + 1 < files.size())
            whole[size_t(i)] = analyseFile(files[i]);
        if (i > 0)
            start[size_t(i)] = analyseFile(files[i], 60000);
    }
    QMap<QString, int> styles;
    int onBeat = 0, kept = 0, skipped = 0;
    double worst = 0;
    for (qsizetype i = 0; i + 1 < files.size(); ++i) {
        const auto& a = whole[size_t(i)];
        const auto& b = start[size_t(i) + 1];
        if (!a || !b)
            continue;
        const qint64 durationA = durations[size_t(i)], durationB = durations[size_t(i) + 1];
        // A file only partly cached (never-fetched ranges): the engine wouldn't plan it.
        const bool brokenA = durationA - a->durationMs > 1000;
        if (brokenA || std::min<qint64>(durationB, 60000) - b->durationMs > 1000) {
            ++skipped;
            std::printf("%-12s -> %-12s skipped: %s stops decoding at %.1f of %.1f s\n",
                        qPrintable(QFileInfo(files[i]).baseName().left(12)),
                        qPrintable(QFileInfo(files[i + 1]).baseName().left(12)),
                        brokenA ? "A" : "B", seconds(brokenA ? a->durationMs : b->durationMs),
                        seconds(brokenA ? durationA : durationB));
            continue;
        }
        const automix::Plan plan =
            automix::plan({}, {durationA, false, &*a}, std::max<qint64>(0, durationA - 60000),
                          {durationB, false, &*b}, false);
        styles[automix::logName(plan.style)] += 1;
        std::printf("%-12s -> %-12s %s (%s), overlap %.1f s",
                    qPrintable(QFileInfo(files[i]).baseName().left(12)),
                    qPrintable(QFileInfo(files[i + 1]).baseName().left(12)),
                    qPrintable(automix::logName(plan.style)),
                    qPrintable(automix::logName(plan.reason)), seconds(plan.overlapMs));
        if (plan.bars)
            std::printf(", %d bars", plan.bars);
        const auto& beatsA = a->rhythm.beats;
        const double beatA = beatsA.size() > 1 ? 60000 / double(a->rhythm.bpm) : 0;
        if (plan.onBeat && !beatsA.empty() && double(beatsA.back()) < double(plan.outMs) - beatA) {
            ++onBeat; // its beat had stopped: nothing to meet (the grid still holds)
            std::printf(", on the beat (A's beat stopped %.1f s before)",
                        seconds(plan.outMs - qint64(beatsA.back())));
        } else if (plan.onBeat && plan.style != automix::Style::Gapless) {
            ++onBeat;
            auto deckDecoder = openTrack(files[i]);
            auto next = openTrack(files[i + 1]);
            if (!deckDecoder || !next)
                return 1;
            automix::Deck deck(std::move(deckDecoder), rate, 1.0f, false);
            const qint64 from = std::max<qint64>(0, plan.outMs - aroundMs);
            std::vector<float> audio;
            if (deck.seek(from))
                renderOverlap(plan, deck, std::move(next), rate,
                              plan.inMs + (plan.overlapMs - plan.inDelayMs) + aroundMs,
                              [&audio](const float* frames, qsizetype count) {
                                  audio.insert(audio.end(), frames, frames + count * 2);
                              });
            // The beats as found (not snapped to a grid): over the overlap, or for a short
            // one (a natural end) from 8 s before it to 8 s after, both tracks' beats.
            automix::RhythmFrontEnd frontEnd(rate);
            frontEnd.add(audio.data(), qsizetype(audio.size() / 2));
            const auto found = automix::findRhythm(frontEnd.features(), false);
            const double overlapAt = double(plan.outMs - from);
            const double around = plan.overlapMs < 8000 ? 8000 : 0;
            const auto g = automix::fitGrid(found.beats, overlapAt - around,
                                            overlapAt + double(plan.overlapMs) + around);
            if (g.valid()) {
                ++kept;
                worst = std::max(worst, g.rmsMs);
                std::printf(", on the beat: %.1f ms rms, %.0f%% on one grid", g.rmsMs,
                            g.inliers * 100);
            } else {
                std::printf(", on the beat: no grid in the overlap");
            }
        }
        std::printf("\n");
    }
    std::printf("\n%lld transitions (%d skipped):",
                static_cast<long long>(files.size() - 1 - skipped), skipped);
    for (auto it = styles.cbegin(); it != styles.cend(); ++it)
        std::printf(" %s %d", qPrintable(it.key()), it.value());
    std::printf("; %d on the beat, %d measured, worst %.1f ms rms\n", onBeat, kept, worst);
    return 0;
}

int mix(const QCommandLineParser& parser, const QStringList& args)
{
    if (args.size() != 3 || !parser.isSet(QStringLiteral("output"))) {
        complain(QStringLiteral("mix needs two tracks and -o out.wav"));
        return 2;
    }
    const QString modeName = parser.value(QStringLiteral("mode"));
    automix::Settings settings;
    if (modeName == QLatin1String("crossfade"))
        settings.mode = TransitionMode::Crossfade;
    else if (modeName == QLatin1String("gapless"))
        settings.mode = TransitionMode::Gapless;
    else if (modeName != QLatin1String("smart")) {
        complain(QStringLiteral("--mode: smart, crossfade or gapless"));
        return 2;
    }
    settings.crossfadeMs =
        qint64(std::clamp(parser.value(QStringLiteral("crossfade")).toDouble(), 1.0, 12.0) * 1000);
    const qint64 contextMs =
        qint64(std::max(1.0, parser.value(QStringLiteral("context")).toDouble()) * 1000);
    const int rate = std::clamp(parser.value(QStringLiteral("rate")).toInt(), 8000, 384000);

    auto a = openTrack(args[1]);
    auto b = openTrack(args[2]);
    if (!a || !b)
        return 1;
    std::printf("A     %.1f s, %d Hz\nB     %.1f s, %d Hz\n", seconds(a->durationMs()),
                a->sampleRate(), seconds(b->durationMs()), b->sampleRate());
    // Analysed like the engine does (all of each here; it has only B's start at first).
    std::optional<automix::Analysis> analysisA, analysisB;
    if (!parser.isSet(QStringLiteral("no-analysis"))) {
        analysisA = analyseFile(args[1]);
        analysisB = analyseFile(args[2]);
    }
    const qint64 startMs = std::max<qint64>(0, a->durationMs() - contextMs);
    const automix::Plan plan =
        automix::plan(settings, {a->durationMs(), false, analysisA ? &*analysisA : nullptr},
                      startMs, {b->durationMs(), false, analysisB ? &*analysisB : nullptr}, false);
    std::printf("plan  %s (%s): out %.1f s, in %.1f s, overlap %.1f s, B current after %.1f s",
                qPrintable(automix::logName(plan.style)), qPrintable(automix::logName(plan.reason)),
                seconds(plan.outMs), seconds(plan.inMs), seconds(plan.overlapMs),
                seconds(plan.switchMs));
    if (plan.bars)
        std::printf(", %d bars", plan.bars);
    if (plan.onBeat)
        std::printf(", on the beat");
    if (plan.inDelayMs)
        std::printf(", B from %.1f s into it", seconds(plan.inDelayMs));
    std::printf("\n");

    const QString path = parser.value(QStringLiteral("output"));
    WaveFile out;
    if (!out.open(path, rate)) {
        complain(QStringLiteral("%1: can't write it").arg(path));
        return 1;
    }
    automix::Deck deck(std::move(a), rate, 1.0f, false);
    if (startMs > 0 && !deck.seek(startMs)) {
        complain(QStringLiteral("can't seek A"));
        return 1;
    }
    const Sink sink = [&out](const float* frames, qsizetype count) { out.write(frames, count); };
    if (plan.style == automix::Style::Gapless)
        renderGapless(deck, std::move(b), contextMs, sink);
    else
        renderOverlap(plan, deck, std::move(b), rate, plan.inMs + contextMs, sink);
    if (!out.finish()) {
        complain(QStringLiteral("%1: can't write it").arg(path));
        return 1;
    }
    std::printf("wrote %s: %.1f s, %d Hz\n", qPrintable(path), double(out.frames()) / rate, rate);
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Renders track transitions the way Cirrus plays them."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("analyse or mix"));
    parser.addPositionalArgument(QStringLiteral("a"), QStringLiteral("The current track."));
    parser.addPositionalArgument(QStringLiteral("b"), QStringLiteral("The next track."));
    parser.addOptions({
        {{QStringLiteral("o"), QStringLiteral("output")},
         QStringLiteral("The WAVE file to write."),
         QStringLiteral("file")},
        {QStringLiteral("mode"), QStringLiteral("smart, crossfade or gapless."),
         QStringLiteral("mode"), QStringLiteral("smart")},
        {QStringLiteral("crossfade"), QStringLiteral("The crossfade mode's length (1-12)."),
         QStringLiteral("seconds"), QStringLiteral("6")},
        {QStringLiteral("context"), QStringLiteral("How much of each track around the transition."),
         QStringLiteral("seconds"), QStringLiteral("60")},
        {QStringLiteral("rate"), QStringLiteral("The output's sample rate."), QStringLiteral("hz"),
         QStringLiteral("48000")},
        {QStringLiteral("no-analysis"), QStringLiteral("Plan as without an analysis.")},
    });
    parser.process(app);
    const QStringList args = parser.positionalArguments();
    if (args.value(0) == QLatin1String("analyse"))
        return analyse(args.mid(1));
    if (args.value(0) == QLatin1String("mix"))
        return mix(parser, args);
    if (args.value(0) == QLatin1String("beats"))
        return beats(parser, args);
    if (args.value(0) == QLatin1String("report"))
        return report(args.mid(1));
    parser.showHelp(2);
}
