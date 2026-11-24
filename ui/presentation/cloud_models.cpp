#include "cloud_models.h"

#include "app.h"
#include "formatter.h"
#include "logging/log.h"
#include "notice.h"
#include "song_list_model.h"
#include "sources.h"

#include <QFileInfo>
#include <QSettings>

namespace cirrus::ui
{

namespace
{
constexpr int kPage = 100;
constexpr int kMostSongs = 20000; // a local bound on reading page after page
} // namespace

CloudPageModel::CloudPageModel(QObject* parent)
    : LoadableModel(parent), m_songs(new SongListModel(this))
{
    followAccount();
    reload();
    // A finished upload is a new song: read again (cloud.md §5).
    if (App* app = App::instance())
        connect(app->cloudUploads(), &CloudUploads::uploaded, this, [this]() {
            if (state() != loadstate::Idle)
                refresh();
        });
}

QString CloudPageModel::countText() const
{
    const qint64 count = m_total >= 0 ? m_total : m_songs->rowCount();
    return count > 0 ? tr("%1 首").arg(count) : QString();
}

QString CloudPageModel::usageText() const
{
    if (m_used < 0)
        return {};
    if (m_max <= 0)
        return tr("已用 %1").arg(Formatter::bytes(m_used));
    return tr("已用 %1 / 共 %2").arg(Formatter::bytes(m_used), Formatter::bytes(m_max));
}

double CloudPageModel::usage() const
{
    return m_used >= 0 && m_max > 0 ? std::clamp(double(m_used) / double(m_max), 0.0, 1.0) : -1;
}

Request CloudPageModel::load()
{
    auto all = std::make_shared<CloudPage>();
    auto current = std::make_shared<Request>();
    readFrom(0, all, current,
             handle<CloudPage>([this](CloudPage page) { return apply(std::move(page)); }));
    return Request([current] { current->cancel(); });
}

void CloudPageModel::readFrom(int offset, std::shared_ptr<CloudPage> all,
                              std::shared_ptr<Request> current, Reply<CloudPage> done)
{
    *current = App::instance()->librarySource()->cloudSongs(
        {offset, kPage}, [this, offset, all, current, done](Outcome<CloudPage> outcome) {
            current->release();
            if (!outcome.value) {
                done(std::move(outcome));
                return;
            }
            const CloudPage& page = *outcome.value;
            all->songs += page.songs;
            all->entries += page.entries;
            all->total = page.total;
            all->usedBytes = page.usedBytes;
            all->maxBytes = page.maxBytes;
            if (page.hasMore && !page.songs.isEmpty() && all->songs.size() < kMostSongs)
                readFrom(offset + int(page.songs.size()), all, current, done);
            else
                done(Outcome<CloudPage>{*all});
        });
}

bool CloudPageModel::apply(CloudPage page)
{
    m_entries.clear();
    QStringList keys;
    for (qsizetype i = 0; i < page.songs.size(); ++i) {
        SongInfo& song = page.songs[i];
        const CloudEntry& entry = page.entries.value(i);
        // The page's 添加时间 column (the table's reason column).
        song.reason = entry.addedAtMs > 0 ? Formatter::date(entry.addedAtMs) : QString();
        m_entries.insert(song.key, entry);
        keys << song.key;
    }
    m_total = page.total;
    m_used = page.usedBytes;
    m_max = page.maxBytes;
    m_songs->reset(keys, page.songs);
    emit summaryChanged();
    return page.songs.isEmpty();
}

void CloudPageModel::clear()
{
    m_entries.clear();
    m_total = m_used = m_max = -1;
    m_songs->clear();
    emit summaryChanged();
}

QStringList CloudPageModel::cloudKeysOf(const QStringList& songKeys) const
{
    QStringList keys;
    for (const auto& key : songKeys)
        for (const auto& entry : m_entries.values(key))
            if (!keys.contains(entry.cloudKey))
                keys << entry.cloudKey;
    return keys;
}

void CloudPageModel::deleteSongs(const QStringList& songKeys)
{
    const QStringList cloudKeys = cloudKeysOf(songKeys).mid(0, 100);
    if (cloudKeys.isEmpty() || writing())
        return;
    log::info(lcUi, "cloud.delete", {"count", cloudKeys.size()});
    m_write = App::instance()->librarySource()->deleteCloudSongs(
        cloudKeys, [this, songKeys](Outcome<bool> outcome) {
            m_write.release();
            emit writingChanged();
            if (outcome.value) {
                m_songs->remove(songKeys);
                for (const auto& key : songKeys)
                    m_entries.remove(key);
                if (m_total > 0)
                    m_total = std::max<qint64>(0, m_total - songKeys.size());
                emit summaryChanged();
                emit notice(noticed("cloud.deleted", tr("已从云盘删除")));
                refresh(); // the space it freed
            } else {
                emit notice(noticed("cloud.delete_failed", tr("没能从云盘删除")));
            }
        });
    emit writingChanged();
}

void CloudPageModel::match(const QString& songKey, const QString& targetKey)
{
    const QStringList cloudKeys = cloudKeysOf({songKey});
    if (cloudKeys.isEmpty() || writing())
        return;
    log::info(lcUi, "cloud.match", {"unmatch", targetKey.isEmpty()});
    m_write = App::instance()->librarySource()->matchCloudSong(
        cloudKeys.first(), targetKey, [this, targetKey](Outcome<bool> outcome) {
            m_write.release();
            emit writingChanged();
            if (outcome.value) {
                emit notice(targetKey.isEmpty() ? noticed("cloud.unmatched", tr("已取消关联"))
                                                : noticed("cloud.matched", tr("已关联")));
                refresh();
            } else {
                emit notice(targetKey.isEmpty()
                                ? noticed("cloud.unmatch_failed", tr("没能取消关联"))
                                : noticed("cloud.match_failed", tr("没能关联这首歌")));
            }
        });
    emit writingChanged();
}

void CloudPageModel::unmatch(const QString& songKey) { match(songKey, {}); }

QString CloudPageModel::searchTextFor(const QString& songKey) const
{
    for (int row = 0; row < m_songs->rowCount(); ++row) {
        const auto index = m_songs->index(row);
        if (index.data(SongListModel::KeyRole).toString() != songKey)
            continue;
        const QString title = index.data(SongListModel::TitleRole).toString();
        const QString artists = index.data(SongListModel::ArtistTextRole).toString();
        return artists.isEmpty() || artists == tr("未知歌手") ? title : title + u' ' + artists;
    }
    return {};
}

// --- CloudUploads ---

CloudUploads::CloudUploads(QObject* parent) : QAbstractListModel(parent) {}

CloudUploads::~CloudUploads() = default;

int CloudUploads::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant CloudUploads::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Item& item = m_items.at(index.row());
    switch (role) {
    case NameRole:
        return item.name;
    case StateRole:
        return item.state;
    case ProgressRole:
        return item.state == Transferring && item.total > 0 ? double(item.done) / item.total
               : item.state == Done                         ? 1.0
                                                            : -1.0;
    case StatusRole:
        return status(item);
    case InstantRole:
        return item.instant;
    default:
        return {};
    }
}

QHash<int, QByteArray> CloudUploads::roleNames() const
{
    return {{NameRole, "name"},
            {StateRole, "uploadState"},
            {ProgressRole, "progress"},
            {StatusRole, "status"},
            {InstantRole, "instant"}};
}

int CloudUploads::remaining() const
{
    int count = 0;
    for (const auto& item : m_items)
        count += item.state < Done;
    return count;
}

QString CloudUploads::status(const Item& item) const
{
    switch (item.state) {
    case Waiting:
        return tr("等待中");
    case Reading:
    case Checking:
        return tr("校验中");
    case Transferring:
        return item.total > 0 ? tr("上传中 %1%").arg(item.done * 100 / item.total) : tr("上传中");
    case Finishing:
        return tr("处理中");
    case Done:
        return item.alreadyThere ? tr("已在云盘里") : item.instant ? tr("已秒传") : tr("已上传");
    case Canceled:
        return tr("已取消");
    case Failed:
        switch (item.error) {
        case CloudUploadDone::Unreadable:
            return tr("失败：文件读不了或格式不支持");
        case CloudUploadDone::TooLarge:
            return tr("失败：文件太大");
        case CloudUploadDone::SignedOut:
            return tr("失败：没有登录");
        case CloudUploadDone::Network:
            return tr("失败：网络中断");
        case CloudUploadDone::Rejected:
            return tr("失败：服务器拒绝");
        default:
            return tr("失败：不确定是否已上传");
        }
    }
    return {};
}

QString CloudUploads::summary() const
{
    if (m_running >= 0) {
        const Item& item = m_items.at(m_running);
        return tr("上传 %1 首：%2 %3").arg(remaining()).arg(item.name, status(item));
    }
    if (remaining() > 0)
        return tr("上传 %1 首：等待中").arg(remaining());
    if (m_failedInRun > 0)
        return tr("已上传 %1 首，%2 首失败").arg(m_doneInRun).arg(m_failedInRun);
    return m_doneInRun > 0 ? tr("已上传 %1 首").arg(m_doneInRun) : QString();
}

double CloudUploads::progress() const
{
    if (m_running < 0)
        return -1;
    const Item& item = m_items.at(m_running);
    return item.state == Transferring && item.total > 0 ? double(item.done) / item.total : -1;
}

QUrl CloudUploads::folder() const
{
    const QString folder = QSettings().value(QStringLiteral("cloud/uploadFolder")).toString();
    return folder.isEmpty() ? QUrl() : QUrl::fromLocalFile(folder);
}

void CloudUploads::add(const QList<QUrl>& files)
{
    QList<Item> added;
    for (const QUrl& url : files) {
        if (!url.isLocalFile())
            continue;
        const QFileInfo info(url.toLocalFile());
        if (!info.isFile())
            continue;
        added.append({info.absoluteFilePath(), info.fileName()});
    }
    if (added.isEmpty())
        return;
    QSettings().setValue(QStringLiteral("cloud/uploadFolder"),
                         QFileInfo(added.last().path).absolutePath());
    emit folderChanged();
    if (remaining() == 0)
        m_doneInRun = m_failedInRun = 0;
    log::info(lcUi, "cloud.upload_add", {"count", added.size()});
    beginInsertRows({}, int(m_items.size()), int(m_items.size() + added.size() - 1));
    m_items += added;
    endInsertRows();
    emit countChanged();
    emit changed();
    next();
}

void CloudUploads::next()
{
    if (m_running >= 0)
        return;
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items[row].state != Waiting)
            continue;
        m_running = row;
        m_items[row].state = Reading;
        rowChanged(row);
        m_request = App::instance()->librarySource()->uploadToCloud(
            m_items[row].path,
            [this, row](int step, qint64 done, qint64 total) {
                if (row != m_running)
                    return;
                Item& item = m_items[row];
                // CloudUploadStep: Reading, Checking, Allocating, Transferring, Submitting,
                // Publishing.
                item.state = step <= 0   ? Reading
                             : step <= 2 ? Checking
                             : step == 3 ? Transferring
                                         : Finishing;
                item.done = done;
                item.total = total;
                rowChanged(row);
            },
            [this](Outcome<CloudUploadDone> outcome) {
                m_request.release();
                finished(outcome.value.value_or(CloudUploadDone{CloudUploadDone::Unknown}));
            });
        return;
    }
    // All done: one notice for the run (cloud.md §5).
    if (m_doneInRun > 0 && m_failedInRun == 0)
        emit notice(noticed("cloud.uploaded", tr("已上传 %1 首到云盘").arg(m_doneInRun)));
    else if (m_failedInRun > 0)
        emit notice(noticed("cloud.upload_failed",
                            tr("已上传 %1 首，%2 首失败").arg(m_doneInRun).arg(m_failedInRun)));
}

void CloudUploads::finished(const CloudUploadDone& done)
{
    const int row = m_running;
    m_running = -1;
    if (row >= 0 && row < m_items.size()) {
        Item& item = m_items[row];
        item.error = done.error;
        item.instant = done.error == CloudUploadDone::None && !done.transferred;
        item.alreadyThere = done.alreadyThere;
        item.state = done.error == CloudUploadDone::None ? Done : Failed;
        if (item.state == Done) {
            ++m_doneInRun;
            emit uploaded();
        } else {
            ++m_failedInRun;
        }
        rowChanged(row);
    }
    next();
}

void CloudUploads::cancel(int row)
{
    if (row < 0 || row >= m_items.size() || m_items[row].state >= Done)
        return;
    if (row == m_running) {
        m_request.cancel();
        m_running = -1;
    }
    m_items[row].state = Canceled;
    rowChanged(row);
    next();
}

void CloudUploads::cancelAll()
{
    m_request.cancel();
    m_running = -1;
    for (int row = 0; row < m_items.size(); ++row)
        if (m_items[row].state < Done) {
            m_items[row].state = Canceled;
            rowChanged(row);
        }
    emit changed();
}

void CloudUploads::retry(int row)
{
    if (row < 0 || row >= m_items.size() ||
        (m_items[row].state != Failed && m_items[row].state != Canceled))
        return;
    if (remaining() == 0)
        m_doneInRun = m_failedInRun = 0;
    else if (m_items[row].state == Failed)
        m_failedInRun = std::max(0, m_failedInRun - 1);
    m_items[row] = Item{m_items[row].path, m_items[row].name};
    rowChanged(row);
    next();
}

void CloudUploads::clearFinished()
{
    if (m_running >= 0 || remaining() > 0) {
        // Only the ended rows go; the running one keeps its place.
        return;
    }
    beginResetModel();
    m_items.clear();
    endResetModel();
    m_doneInRun = m_failedInRun = 0;
    emit countChanged();
    emit changed();
}

void CloudUploads::rowChanged(int row)
{
    const auto index = this->index(row);
    emit dataChanged(index, index);
    emit changed();
}

} // namespace cirrus::ui
