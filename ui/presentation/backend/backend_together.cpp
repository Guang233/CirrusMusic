#include "backend_together.h"
#include "app_settings.h"
#include "application/backend/backend_facade.h"
#include "application/playback/listening_session.h"
#include "application/playback/together_session.h"
#include "backend_host.h"
#include "backend_mapping.h"
#include "infrastructure/runtime/backend_runtime.h"

#include <algorithm>

namespace cirrus::ui
{
using application::TogetherSession;

BackendTogether::BackendTogether(BackendHost& host, QObject* parent) : QObject(parent), m_host(host)
{
    auto& session = m_host.together();
    session.setQuality([] {
        return domain::AudioQuality(std::clamp(AppSettings().audioQuality(),
                                               int(domain::AudioQuality::Standard),
                                               int(domain::AudioQuality::HiRes)));
    });
    connect(&session, &TogetherSession::changed, this, &BackendTogether::refresh);
    connect(&session, &TogetherSession::ended, this, [this](TogetherSession::Reason reason) {
        refresh();
        if (m_ended)
            m_ended(together::End(reason));
    });
    connect(&session, &TogetherSession::listSent, this, [this] {
        if (m_listSent)
            m_listSent();
    });
    // canCreate and the queue's size follow the queue and the account.
    auto& runtime = m_host.runtime();
    connect(&runtime.listening(), &application::ListeningSession::queueChanged, this,
            &BackendTogether::refresh);
    connect(&runtime.listening(), &application::ListeningSession::fmChanged, this,
            &BackendTogether::refresh);
    connect(&runtime.facade(), &application::BackendFacade::sessionChanged, this,
            &BackendTogether::refresh);
    connect(&runtime.facade(), &application::BackendFacade::stateChanged, this,
            &BackendTogether::refresh);
    refresh();
}

void BackendTogether::setHandlers(std::function<void()> changed,
                                  std::function<void(together::End)> ended,
                                  std::function<void()> listSent)
{
    m_changed = std::move(changed);
    m_ended = std::move(ended);
    m_listSent = std::move(listSent);
}

void BackendTogether::refresh()
{
    const auto& session = m_host.together();
    const auto& listening = m_host.runtime().listening();
    TogetherSnapshot s;
    s.state = together::State(session.state());
    s.joining = session.joining();
    s.canCreate = session.canCreate();
    const auto queue = listening.queue().entries.size();
    s.queueCount = int(std::min<qsizetype>(queue, domain::kTogetherSongsMax));
    for (const auto& m : session.members())
        s.members.append({m.userId, m.nickname, mapping::coverKey(m.avatar),
                          m.userId == session.creatorId(), m.userId == session.accountId()});
    s.connected = session.connected();
    s.shareLink = session.isCreator() ? session.shareLink() : QString();
    s.hint = session.hint();
    if (s == m_snapshot)
        return;
    m_snapshot = s;
    if (m_changed)
        m_changed();
}

bool BackendTogether::create() { return m_host.together().create(); }

bool BackendTogether::join(const QString& link)
{
    const auto invite = TogetherSession::parseInvite(link);
    return invite && m_host.together().join(*invite);
}

void BackendTogether::leave() { m_host.together().leave(); }

together::Link BackendTogether::linkKind(const QString& link) const
{
    const auto invite = TogetherSession::parseInvite(link);
    if (!invite)
        return together::NoLink;
    return invite->inviterId == m_host.runtime().facade().snapshot().accountId ? together::OwnRoom
                                                                               : together::Invite;
}

} // namespace cirrus::ui
