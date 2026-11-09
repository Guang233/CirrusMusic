#include "podcast_page_model.h"

#include "app.h"
#include "song_list_model.h"

namespace cirrus::ui
{

namespace
{
constexpr int kVoicePage = 50;
} // namespace

PodcastVoicesModel::PodcastVoicesModel(QObject* parent)
    : PagedModel(parent), m_songs(new SongListModel(this))
{
    setPageSize(kVoicePage);
}

void PodcastVoicesModel::setPodcastKey(const QString& key)
{
    m_key = key;
    if (key.isEmpty())
        reset();
    else
        reload();
}

void PodcastVoicesModel::setOldestFirst(bool oldestFirst)
{
    if (oldestFirst == m_oldestFirst)
        return;
    m_oldestFirst = oldestFirst;
    emit oldestFirstChanged();
    if (!m_key.isEmpty())
        reload();
}

void PodcastVoicesModel::clear()
{
    resetPaging();
    m_songs->clear();
}

int PodcastVoicesModel::loadedCount() const { return m_songs->rowCount(); }

Request PodcastVoicesModel::loadPage(PageRequest page)
{
    auto* catalog = App::instance()->catalog();
    if (page.offset == 0)
        return catalog->podcastVoices(
            m_key, m_oldestFirst, page,
            handle<ResultPage<SongInfo>>([this](ResultPage<SongInfo> result) {
                QStringList keys;
                for (const SongInfo& voice : std::as_const(result.items))
                    keys << voice.key;
                m_songs->reset(keys, result.items);
                setTotal(result.total);
                pageArrived(int(result.items.size()), result.hasMore);
                return result.items.isEmpty();
            }));
    return catalog->podcastVoices(
        m_key, m_oldestFirst, page,
        handleMore<SongInfo>([this](QList<SongInfo> voices) { m_songs->append(voices); }));
}

PodcastPageModel::PodcastPageModel(QObject* parent)
    : LoadableModel(parent), m_voices(new PodcastVoicesModel(this))
{
    followAccount();
}

void PodcastPageModel::setPodcastKey(const QString& key)
{
    if (m_key == key)
        return;
    m_key = key;
    emit podcastKeyChanged();
    m_voices->setPodcastKey(key);
    if (key.isEmpty())
        reset();
    else
        reload();
}

QString PodcastPageModel::countText() const
{
    return m_info.voiceCount >= 0 ? tr("%1 期").arg(m_info.voiceCount) : QString();
}

void PodcastPageModel::setSubscribed(bool subscribed)
{
    if (subscribed == m_info.subscribed || m_info.key.isEmpty())
        return;
    m_info.subscribed = subscribed;
    emit subscribedChanged();
    m_subscribeRequest = App::instance()->librarySource()->setPodcastSubscribed(
        m_key, subscribed, [this, subscribed](Outcome<bool> outcome) {
            m_subscribeRequest.release();
            if (outcome.value) {
                emit libraryChanged();
                return;
            }
            m_info.subscribed = !subscribed;
            emit subscribedChanged();
            const bool signedOut = outcome.failure && outcome.failure->kind == failure::SignedOut;
            emit notice(signedOut             ? tr("登录后才能收藏播客")
                        : maybeSaved(outcome) ? tr("可能没有保存成功，刷新看看")
                        : subscribed          ? tr("没能收藏，请稍后再试")
                                              : tr("没能取消收藏，请稍后再试"));
        });
}

Request PodcastPageModel::load()
{
    return App::instance()->catalog()->podcast(m_key, handle<PodcastInfo>([this](PodcastInfo info) {
                                                   m_info = std::move(info);
                                                   emit headerChanged();
                                                   emit subscribedChanged();
                                                   return false;
                                               }));
}

void PodcastPageModel::clear()
{
    m_subscribeRequest.cancel();
    m_info = {};
    emit headerChanged();
    emit subscribedChanged();
}

} // namespace cirrus::ui
