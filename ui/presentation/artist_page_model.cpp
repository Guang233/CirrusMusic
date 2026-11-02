#include "artist_page_model.h"

#include "app.h"
#include "collection_list_model.h"
#include "formatter.h"
#include "song_list_model.h"

#include <QVariantMap>

namespace cirrus::ui
{

namespace
{
constexpr int kSongPage = 100;
constexpr int kAlbumPage = 30;
} // namespace

ArtistSongsModel::ArtistSongsModel(QObject* parent)
    : PagedModel(parent), m_songs(new SongListModel(this))
{
    setPageSize(kSongPage);
}

void ArtistSongsModel::setArtistKey(const QString& key)
{
    m_key = key;
    reset();
}

void ArtistSongsModel::setByTime(bool byTime)
{
    if (byTime == m_byTime)
        return;
    m_byTime = byTime;
    emit byTimeChanged();
    if (state() != loadstate::Idle)
        reload();
}

void ArtistSongsModel::ensureLoaded()
{
    if (state() == loadstate::Idle && !m_key.isEmpty())
        reload();
}

void ArtistSongsModel::clear()
{
    resetPaging();
    m_songs->clear();
}

int ArtistSongsModel::loadedCount() const { return m_songs->rowCount(); }

Request ArtistSongsModel::loadPage(PageRequest page)
{
    auto* catalog = App::instance()->catalog();
    if (page.offset == 0)
        return catalog->artistSongs(
            m_key, m_byTime, page,
            handle<ResultPage<SongInfo>>([this](ResultPage<SongInfo> result) {
                QStringList keys;
                for (const SongInfo& song : std::as_const(result.items))
                    keys << song.key;
                m_songs->reset(keys, result.items);
                pageArrived(int(result.items.size()), result.hasMore);
                return result.items.isEmpty();
            }));
    return catalog->artistSongs(
        m_key, m_byTime, page,
        handleMore<SongInfo>([this](QList<SongInfo> songs) { m_songs->append(songs); }));
}

ArtistAlbumsModel::ArtistAlbumsModel(QObject* parent)
    : PagedModel(parent), m_items(new CollectionListModel(this))
{
    setPageSize(kAlbumPage);
}

void ArtistAlbumsModel::setArtistKey(const QString& key)
{
    m_key = key;
    reset();
}

void ArtistAlbumsModel::ensureLoaded()
{
    if (state() == loadstate::Idle && !m_key.isEmpty())
        reload();
}

void ArtistAlbumsModel::clear()
{
    resetPaging();
    m_items->reset({});
}

int ArtistAlbumsModel::loadedCount() const { return m_items->rowCount(); }

Request ArtistAlbumsModel::loadPage(PageRequest page)
{
    auto* catalog = App::instance()->catalog();
    if (page.offset == 0)
        return catalog->artistAlbums(
            m_key, page,
            handle<ResultPage<CollectionItem>>([this](ResultPage<CollectionItem> result) {
                m_items->reset(result.items);
                pageArrived(int(result.items.size()), result.hasMore);
                return result.items.isEmpty();
            }));
    return catalog->artistAlbums(m_key, page,
                                 handleMore<CollectionItem>([this](QList<CollectionItem> items) {
                                     m_items->append(items);
                                 }));
}

ArtistIntroductionModel::ArtistIntroductionModel(QObject* parent) : LoadableModel(parent) {}

void ArtistIntroductionModel::setArtistKey(const QString& key)
{
    m_key = key;
    reset();
}

void ArtistIntroductionModel::ensureLoaded()
{
    if (state() == loadstate::Idle && !m_key.isEmpty())
        reload();
}

QVariantList ArtistIntroductionModel::sections() const
{
    QVariantList sections;
    for (const ArtistIntroduction::Section& section : m_intro.sections)
        sections << QVariantMap{{QStringLiteral("title"), section.title},
                                {QStringLiteral("text"), section.text}};
    return sections;
}

Request ArtistIntroductionModel::load()
{
    return App::instance()->catalog()->artistIntroduction(
        m_key, handle<ArtistIntroduction>([this](ArtistIntroduction intro) {
            m_intro = std::move(intro);
            emit contentChanged();
            return m_intro.brief.isEmpty() && m_intro.sections.isEmpty();
        }));
}

void ArtistIntroductionModel::clear()
{
    m_intro = {};
    emit contentChanged();
}

SimilarArtistsModel::SimilarArtistsModel(QObject* parent)
    : LoadableModel(parent), m_items(new CollectionListModel(this))
{
    followAccount();
}

void SimilarArtistsModel::setArtistKey(const QString& key)
{
    m_key = key;
    reset();
}

void SimilarArtistsModel::ensureLoaded()
{
    if (state() == loadstate::Idle && !m_key.isEmpty())
        reload();
}

Request SimilarArtistsModel::load()
{
    return App::instance()->catalog()->similarArtists(
        m_key, handle<QList<CollectionItem>>([this](QList<CollectionItem> items) {
            m_items->reset(items);
            return items.isEmpty();
        }));
}

void SimilarArtistsModel::clear() { m_items->reset({}); }

ArtistPageModel::ArtistPageModel(QObject* parent)
    : LoadableModel(parent), m_topSongs(new SongListModel(this)),
      m_songs(new ArtistSongsModel(this)), m_albums(new ArtistAlbumsModel(this)),
      m_introduction(new ArtistIntroductionModel(this)), m_similar(new SimilarArtistsModel(this))
{
    followAccount();
}

void ArtistPageModel::setArtistKey(const QString& key)
{
    if (m_key == key)
        return;
    m_key = key;
    emit artistKeyChanged();
    m_songs->setArtistKey(key);
    m_albums->setArtistKey(key);
    m_introduction->setArtistKey(key);
    m_similar->setArtistKey(key);
    if (key.isEmpty())
        reset();
    else
        reload();
}

QString ArtistPageModel::aliasText() const { return m_info.aliases.join(tr(" / ")); }

QString ArtistPageModel::countText() const
{
    QStringList parts;
    if (m_info.songCount >= 0)
        parts << tr("单曲 %1").arg(Formatter::count(m_info.songCount));
    if (m_info.albumCount >= 0)
        parts << tr("专辑 %1").arg(Formatter::count(m_info.albumCount));
    return parts.join(tr(" · "));
}

void ArtistPageModel::setFollowed(bool followed)
{
    if (followed == m_info.followed || m_info.key.isEmpty())
        return;
    m_info.followed = followed;
    emit followedChanged();
    m_followRequest = App::instance()->librarySource()->setArtistFollowed(
        m_key, followed, [this, followed](Outcome<bool> outcome) {
            m_followRequest.release();
            if (outcome.value) {
                emit libraryChanged();
                return;
            }
            m_info.followed = !followed;
            emit followedChanged();
            const bool signedOut = outcome.failure && outcome.failure->kind == failure::SignedOut;
            emit notice(signedOut  ? tr("登录后才能关注歌手")
                        : followed ? tr("没能关注，请稍后再试")
                                   : tr("没能取消关注，请稍后再试"));
        });
}

Request ArtistPageModel::load()
{
    return App::instance()->catalog()->artist(m_key, handle<ArtistPage>([this](ArtistPage page) {
                                                  m_info = page.info;
                                                  emit headerChanged();
                                                  emit followedChanged();
                                                  QStringList keys;
                                                  for (const SongInfo& song :
                                                       std::as_const(page.topSongs))
                                                      keys << song.key;
                                                  m_topSongs->reset(keys, page.topSongs);
                                                  // An artist without songs still has a page
                                                  // (albums, introduction).
                                                  return false;
                                              }));
}

void ArtistPageModel::clear()
{
    m_followRequest.cancel();
    m_info = {};
    emit headerChanged();
    emit followedChanged();
    m_topSongs->clear();
}

} // namespace cirrus::ui
