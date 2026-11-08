// Audio engine PoC (docs/tasks/audio-engine.md §6 stage A). Not product code.
//
//   cirrus_audio_poc render OUT.f32 [--rate 48000] [--track N --ms M] FILE...
//       Decodes FILE... back to back into raw interleaved stereo float32 (no device),
//       for measuring joints and seek accuracy (tools/audio-poc/analyze.py).
//   cirrus_audio_poc play [--volume 0.5] FILE...
//       Plays on the default output. Commands on stdin: p (pause/resume), s MS (seek),
//       n (next), v X (volume 0..1), q (quit). Prints the position twice a second.

#include "chain.h"
#include "player.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSocketNotifier>
#include <QTimer>

#include <cstdio>
#include <iostream>
#include <string>

namespace
{

int render(QStringList args)
{
    const QString out = args.takeFirst();
    int rate = 48000, track = 0;
    qint64 ms = 0;
    QStringList files;
    while (!args.isEmpty()) {
        const QString a = args.takeFirst();
        if (a == QLatin1String("--rate"))
            rate = args.takeFirst().toInt();
        else if (a == QLatin1String("--track"))
            track = args.takeFirst().toInt();
        else if (a == QLatin1String("--ms"))
            ms = args.takeFirst().toLongLong();
        else
            files << a;
    }
    poc::Chain chain(rate);
    if (!chain.setTracks(files, track) || (ms > 0 && !chain.seek(track, ms)))
        return 1;
    QFile file(out);
    if (!file.open(QIODevice::WriteOnly))
        return 1;
    QElapsedTimer timer;
    timer.start();
    std::vector<float> buffer;
    qint64 total = 0;
    while (true) {
        buffer.clear();
        const qsizetype got = chain.pull(buffer, 8192);
        if (got == 0)
            break;
        file.write(reinterpret_cast<const char*>(buffer.data()),
                   qint64(buffer.size() * sizeof(float)));
        total += got;
    }
    const double seconds = double(total) / rate;
    std::printf("frames %lld (%.3f s) at %d Hz, decoded in %lld ms (%.0fx realtime)\n",
                static_cast<long long>(total), seconds, rate,
                static_cast<long long>(timer.elapsed()),
                seconds * 1000.0 / std::max<qint64>(1, timer.elapsed()));
    for (const auto& b : chain.boundaries())
        std::printf("boundary track %d at frame %lld\n", b.track,
                    static_cast<long long>(b.outFrame));
    return 0;
}

int play(QCoreApplication& app, QStringList args)
{
    float volume = 0.5f;
    QStringList files;
    while (!args.isEmpty()) {
        const QString a = args.takeFirst();
        if (a == QLatin1String("--volume"))
            volume = args.takeFirst().toFloat();
        else
            files << a;
    }
    poc::Player player(files);
    player.setVolume(volume);
    if (!player.start())
        return 1;
    QTimer status;
    QObject::connect(&status, &QTimer::timeout, &player, &poc::Player::printStatus);
    status.start(500);
    QSocketNotifier input(0, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, &app, [&] {
        std::string line;
        if (!std::getline(std::cin, line) || line == "q") {
            app.quit();
            return;
        }
        if (line == "p")
            player.togglePause();
        else if (line == "n")
            player.next();
        else if (line.rfind("s ", 0) == 0)
            player.seek(std::stoll(line.substr(2)));
        else if (line.rfind("v ", 0) == 0)
            player.setVolume(std::stof(line.substr(2)));
    });
    return app.exec();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    if (args.size() >= 3 && args.first() == QLatin1String("render"))
        return render(args.mid(1));
    if (args.size() >= 2 && args.first() == QLatin1String("play"))
        return play(app, args.mid(1));
    std::fprintf(stderr, "usage: see the comment at the top of tools/audio-poc/main.cpp\n");
    return 2;
}
