#pragma once

#include "loadable_model.h"
#include "search_results.h"
#include "types.h"

#include <QAbstractListModel>
#include <QHash>
#include <QUrl>

namespace cirrus::ui
{

class SongListModel;

// The cloud drive page (docs/ui/pages/cloud.md §3): every song of the account's cloud (read
// a page after another), how much space it uses, and the writes: delete, match (纠错),
// unmatch. Rows are keyed by the song that plays them; the cloud's own keys stay here.
class CloudPageModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)
    Q_PROPERTY(QString countText READ countText NOTIFY summaryChanged)
    Q_PROPERTY(QString usageText READ usageText NOTIFY summaryChanged)
    // Used share of the space, 0–1; -1 when unknown.
    Q_PROPERTY(double usage READ usage NOTIFY summaryChanged)
    Q_PROPERTY(bool writing READ writing NOTIFY writingChanged)

  public:
    explicit CloudPageModel(QObject* parent = nullptr);

    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    [[nodiscard]] QString countText() const;
    [[nodiscard]] QString usageText() const;
    [[nodiscard]] double usage() const;
    [[nodiscard]] bool writing() const { return m_write.pending(); }

    // Deletes the cloud songs of these rows' keys (a notice tells how it went).
    Q_INVOKABLE void deleteSongs(const QStringList& songKeys);
    // Links the cloud song of `songKey` to the catalog song `targetKey`; then reads again.
    Q_INVOKABLE void match(const QString& songKey, const QString& targetKey);
    // Back to the file's own details; then reads again.
    Q_INVOKABLE void unmatch(const QString& songKey);
    // What the match dialog searches for first: "title artist".
    Q_INVOKABLE QString searchTextFor(const QString& songKey) const;

  signals:
    void summaryChanged();
    void writingChanged();
    void notice(const QString& message);

  protected:
    Request load() override;
    void clear() override;

  private:
    void readFrom(int offset, std::shared_ptr<CloudPage> all, std::shared_ptr<Request> current,
                  Reply<CloudPage> done);
    bool apply(CloudPage page);
    [[nodiscard]] QStringList cloudKeysOf(const QStringList& songKeys) const;

    SongListModel* m_songs;
    QMultiHash<QString, CloudEntry> m_entries; // by song key (keys may repeat)
    qint64 m_total = -1, m_used = -1, m_max = -1;
    Request m_write;
};

// The catalog songs the match dialog (纠错, cloud.md §4) offers: a song search of `query`,
// without the search page's history.
class CloudMatchSearch : public SongResults
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString query READ query WRITE setQueryText NOTIFY queryChanged)

  public:
    explicit CloudMatchSearch(QObject* parent = nullptr) : SongResults(parent) {}
    [[nodiscard]] QString query() const { return m_text; }
    void setQueryText(const QString& query)
    {
        const QString trimmed = query.trimmed();
        if (trimmed == m_text)
            return;
        m_text = trimmed;
        setQuery(trimmed);
        if (!trimmed.isEmpty())
            ensureLoaded();
        emit queryChanged();
    }

  signals:
    void queryChanged();

  private:
    QString m_text;
};

// The uploads to the cloud drive (cloud.md §5), one at a time in the order added; they go on
// when the cloud page closes (App.cloudUploads). Each row: a file, its state and progress.
class CloudUploads : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.cloudUploads")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // Waiting or running uploads.
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(int remaining READ remaining NOTIFY changed)
    // The collapsed line: "上传 3 首：正在上传 晴天.flac 42%", or how it ended.
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    // The running upload's progress, 0–1; -1 when none or unknown.
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    // Where the file dialog opens (the folder of the last files added).
    Q_PROPERTY(QUrl folder READ folder NOTIFY folderChanged)

  public:
    enum State { Waiting, Reading, Checking, Transferring, Finishing, Done, Failed, Canceled };
    Q_ENUM(State)
    enum Role { NameRole = Qt::UserRole + 1, StateRole, ProgressRole, StatusRole, InstantRole };

    explicit CloudUploads(QObject* parent = nullptr);
    ~CloudUploads() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool active() const { return remaining() > 0; }
    [[nodiscard]] int remaining() const;
    [[nodiscard]] QString summary() const;
    [[nodiscard]] double progress() const;
    [[nodiscard]] QUrl folder() const;

    // Local files (file dialog, drops); others are left out.
    Q_INVOKABLE void add(const QList<QUrl>& files);
    Q_INVOKABLE void cancel(int row);
    Q_INVOKABLE void cancelAll();
    Q_INVOKABLE void retry(int row);
    // Forgets the finished rows (the panel's 关闭).
    Q_INVOKABLE void clearFinished();

  signals:
    void countChanged();
    void changed();
    void folderChanged();
    // A file is in the cloud now: its page reads again.
    void uploaded();
    void notice(const QString& message);

  private:
    struct Item {
        QString path;
        QString name;
        State state = Waiting;
        qint64 done = 0, total = 0;
        bool instant = false;
        bool alreadyThere = false;
        CloudUploadDone::Error error = CloudUploadDone::None;
    };
    void next();
    void finished(const CloudUploadDone& done);
    void rowChanged(int row);
    [[nodiscard]] QString status(const Item& item) const;

    QList<Item> m_items;
    int m_running = -1; // row
    Request m_request;
    int m_doneInRun = 0, m_failedInRun = 0; // since the queue last went idle
};

} // namespace cirrus::ui
