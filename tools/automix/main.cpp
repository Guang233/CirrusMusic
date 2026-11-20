// The automix tuning tool (docs/tasks/automix.md §9): renders the transition between two
// local files the way the engine plays it, to listen to and compare. Development only.
//
//   cirrus_automix analyse a.flac [b.mp3 ...]
//   cirrus_automix mix a.flac b.mp3 -o out.wav [--mode smart|crossfade|gapless]
//                  [--crossfade 6] [--context 60] [--rate 48000]
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
#include <QtEndian>
#include <algorithm>
#include <cstdio>

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
void playUntil(automix::Deck& deck, qint64 untilMs, WaveFile& out)
{
    std::vector<float> chunk(2 * kChunkFrames);
    for (qint64 left = deck.framesUntil(untilMs); left > 0;) {
        const auto want = qsizetype(std::min<qint64>(kChunkFrames, left));
        const qsizetype got = pullAll(deck, chunk.data(), want);
        out.write(chunk.data(), got);
        if (got < want)
            return;
        left -= got;
    }
}

// A to its end and B straight on through the same resampler: the engine's gapless joint.
void renderGapless(automix::Deck& deck, std::unique_ptr<AudioDecoder> next, qint64 untilMs,
                   WaveFile& out)
{
    std::vector<float> chunk(2 * kChunkFrames);
    while (true) {
        const qsizetype got = deck.pull(chunk.data(), kChunkFrames, false);
        out.write(chunk.data(), got);
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
                   int rate, qint64 untilMs, WaveFile& out)
{
    playUntil(a, plan.outMs, out);
    automix::Deck b(std::move(next), rate, 1.0f, false);
    if (plan.inMs > 0 && !b.seek(plan.inMs)) {
        complain(QStringLiteral("can't seek B"));
        return;
    }
    std::vector<float> chunk(2 * kChunkFrames), other(2 * kChunkFrames);
    const qint64 overlap = plan.overlapMs * rate / 1000;
    for (qint64 offset = 0; offset < overlap;) {
        const auto want = qsizetype(std::min<qint64>(kChunkFrames, overlap - offset));
        const qsizetype gotA = pullAll(a, chunk.data(), want);
        std::fill(chunk.begin() + gotA * 2, chunk.begin() + want * 2, 0.0f);
        const qsizetype gotB = pullAll(b, other.data(), want);
        std::fill(other.begin() + gotB * 2, other.begin() + want * 2, 0.0f);
        automix::mixOverlap(plan, chunk.data(), other.data(), chunk.data(), want, offset, rate);
        out.write(chunk.data(), want);
        offset += want;
    }
    playUntil(b, untilMs, out);
}

// The whole file through the analyser, at its own rate.
std::optional<automix::Analysis> analyseFile(const QString& path)
{
    auto decoder = openTrack(path);
    if (!decoder)
        return std::nullopt;
    automix::Analyser analyser(decoder->sampleRate());
    std::vector<float> chunk;
    while (true) {
        chunk.clear();
        if (!decoder->read(chunk, kChunkFrames))
            break;
        analyser.add(chunk.data(), qsizetype(chunk.size() / 2));
    }
    if (decoder->error()) {
        complain(QStringLiteral("%1: decoding failed").arg(path));
        return std::nullopt;
    }
    return analyser.finish(true);
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
    }
    return status;
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
    const qint64 startMs = std::max<qint64>(0, a->durationMs() - contextMs);
    const automix::Plan plan =
        automix::plan(settings, {a->durationMs()}, startMs, {b->durationMs()}, false);
    std::printf("plan  %s (%s): out %.1f s, in %.1f s, overlap %.1f s, B current after %.1f s\n",
                qPrintable(automix::logName(plan.style)), qPrintable(automix::logName(plan.reason)),
                seconds(plan.outMs), seconds(plan.inMs), seconds(plan.overlapMs),
                seconds(plan.switchMs));

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
    if (plan.style == automix::Style::Gapless)
        renderGapless(deck, std::move(b), contextMs, out);
    else
        renderOverlap(plan, deck, std::move(b), rate, plan.inMs + contextMs, out);
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
    });
    parser.process(app);
    const QStringList args = parser.positionalArguments();
    if (args.value(0) == QLatin1String("analyse"))
        return analyse(args.mid(1));
    if (args.value(0) == QLatin1String("mix"))
        return mix(parser, args);
    parser.showHelp(2);
}
