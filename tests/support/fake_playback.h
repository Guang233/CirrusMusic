#pragma once
#include "application/ports/playback.h"
#include <QList>

namespace cirrus::test
{
class FakeStreamResolver final : public application::IStreamResolver
{
  public:
    struct Request {
        quint64 id;
        domain::SessionToken context;
        domain::StreamQuery query;
    };
    QList<Request> requests;
    QList<quint64> canceled;
    quint64 resolve(domain::SessionToken context, domain::StreamQuery query) override
    {
        const auto id = quint64(requests.size() + 1);
        requests.append({id, context, std::move(query)});
        return id;
    }
    void cancel(quint64 id) override { canceled.append(id); }
    void finish(int index, application::StreamResult result)
    {
        const auto request = requests[index];
        emit completed(request.id, request.context, std::move(result));
    }
};
class FakePlaybackBackend final : public application::IPlaybackBackend
{
  public:
    QList<domain::LoadToken> loads;
    QList<domain::LoadToken> preloads; // an empty token: canceled
    QList<domain::LoadToken> refreshes;
    QList<qint64> seeks;
    int plays = 0, pauses = 0, stops = 0;
    float volume = 0.5f;
    void load(domain::MediaSource, domain::LoadToken token) override { loads.append(token); }
    void play() override { ++plays; }
    void pause() override { ++pauses; }
    void stop() override { ++stops; }
    void seek(qint64 value) override { seeks.append(value); }
    void setVolume(float value) override { volume = value; }
    void preload(domain::MediaSource, domain::LoadToken token) override { preloads.append(token); }
    void refresh(domain::LoadToken token, domain::MediaSource) override { refreshes.append(token); }
};
} // namespace cirrus::test
