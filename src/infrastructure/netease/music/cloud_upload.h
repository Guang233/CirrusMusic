#pragma once
#include "application/account/session_coordinator.h"
#include "domain/music/cloud_upload_types.h"
#include "infrastructure/netease/api/session_api_client.h"
#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <atomic>
#include <functional>
#include <memory>

class QNetworkReply;

namespace cirrus::netease
{
// Fills in a file's tags and bitrate (the media library's FFmpeg reader); may leave them empty.
using CloudTagReader = std::function<void(const QString& path, domain::CloudFileFacts& facts)>;

// MD5, size, name and extension of a local file, read in blocks; nullopt when it can't be read,
// is empty, has no usable extension, or `canceled` turns true. Blocking: run off the event loop.
[[nodiscard]] std::optional<domain::CloudFileFacts> readCloudFile(const QString& path,
                                                                  const std::atomic_bool& canceled);
// The name NOS allocation takes: the base name without extension, spaces dropped, dots as
// underscores (the fixed reference's cloud_upload_token.js).
[[nodiscard]] QString cloudAllocationName(const QString& fileName);

// Uploads local files to the account's cloud drive (docs/api/cloud-drive.md) on the backend
// thread: read (a pool thread), check, allocate, transfer to NOS over HTTPS (streamed from the
// file), submit the details, publish. Bound to the session and account it started in.
class CloudUploadGateway final : public QObject
{
    Q_OBJECT
  public:
    CloudUploadGateway(application::SessionCoordinator& session, SessionApiClient& client,
                       CloudTagReader tags = {}, std::unique_ptr<QNetworkAccessManager> nos = {},
                       QObject* parent = nullptr);
    ~CloudUploadGateway() override;
    [[nodiscard]] quint64 submit(domain::SessionToken context, QString accountId,
                                 domain::CloudUploadFile file);
    void cancel(quint64 id);
    static constexpr int MaximumPending = 4;

  signals:
    void progressed(quint64 id, cirrus::domain::CloudUploadProgress progress);
    void completed(quint64 id, cirrus::domain::SessionToken context,
                   cirrus::domain::CloudUploadResult result);

  private:
    struct Pending;
    void read(quint64 id);
    void readDone(quint64 id, std::optional<domain::CloudFileFacts> facts);
    void apiDone(quint64 id, const ApiResult& api);
    void transfer(quint64 id);
    void transferDone(quint64 id);
    void request(quint64 id, domain::CloudUploadStep step, ApiOperation operation,
                 QJsonObject parameters);
    void enter(quint64 id, domain::CloudUploadStep step);
    void fail(quint64 id, domain::CloudUploadError error, std::optional<int> code = {});
    void invalid(quint64 id, const char* field, const QJsonObject& root,
                 const QString& punctuation = {});
    void finish(quint64 id, domain::CloudUploadResult result);
    [[nodiscard]] bool current(const Pending& p) const;

    application::SessionCoordinator& m_session;
    QPointer<SessionApiClient> m_client;
    CloudTagReader m_tags;
    QNetworkAccessManager* m_nos;
    QHash<quint64, std::shared_ptr<Pending>> m_pending;
    QHash<quint64, quint64> m_apiIds; // API request → upload
    quint64 m_nextId = 0;
};
} // namespace cirrus::netease
