#pragma once
#include "account_library_types.h"
#include "logging/log.h"
#include <QDir>

namespace cirrus::domain
{
// Uploading a local audio file to the account's cloud drive (docs/api/cloud-drive.md):
// read it (MD5, size, tags), check, allocate, transfer to NOS, submit its details, publish.
enum class CloudUploadStep { Reading, Checking, Allocating, Transferring, Submitting, Publishing };
CIRRUS_LOG_ENUM(CloudUploadStep, Reading, Checking, Allocating, Transferring, Submitting,
                Publishing)

struct CloudUploadFile {
    static constexpr qint64 MaximumBytes = 2LL * 1024 * 1024 * 1024; // local budget
    QString path; // the file the user chose; never logged and never sent
    [[nodiscard]] bool isValid() const
    {
        return !path.isEmpty() && path.size() <= 4096 && QDir::isAbsolutePath(path) &&
               !path.contains(QChar::Null);
    }
};

// What reading the file found. The name and tags are the user's own text: sent as the cloud
// song's details, never logged.
struct CloudFileFacts {
    QString md5;                   // lowercase hex
    qint64 size = 0;               // bytes
    QString fileName;              // base name with extension
    QString extension;             // lowercase, without the dot
    QString title, artist, album;  // tags; empty when the file has none
    std::optional<qint64> bitrate; // bits per second
};

struct CloudUploadProgress {
    CloudUploadStep step = CloudUploadStep::Reading;
    qint64 done = 0;  // bytes transferred (Transferring)
    qint64 total = 0; // the file's size once read
};

enum class CloudUploadOutcome { NotSent, Uploaded, Failed, Unknown };
CIRRUS_LOG_ENUM(CloudUploadOutcome, NotSent, Uploaded, Failed, Unknown)
enum class CloudUploadError {
    None,
    InvalidFile, // not readable, empty, or no usable extension
    TooLarge,    // over CloudUploadFile::MaximumBytes
    AuthenticationRequired,
    StaleContext,
    Busy,
    Canceled,
    Network,
    Timeout,
    RateLimited,
    InvalidResponse,
    ProviderRejected
};
CIRRUS_LOG_ENUM(CloudUploadError, None, InvalidFile, TooLarge, AuthenticationRequired, StaleContext,
                Busy, Canceled, Network, Timeout, RateLimited, InvalidResponse, ProviderRejected)

struct CloudUploadResult {
    CloudUploadOutcome outcome = CloudUploadOutcome::NotSent;
    CloudUploadError error = CloudUploadError::None;
    CloudUploadStep step = CloudUploadStep::Reading; // the last step reached
    std::optional<int> providerCode;
    std::optional<CloudSongId> song; // the new cloud song (Uploaded)
    bool transferred = false;        // the file's bytes went to NOS (else it was already there)
    bool alreadyThere = false;       // the account's cloud had the file (publish said 201)
};
} // namespace cirrus::domain
Q_DECLARE_METATYPE(cirrus::domain::CloudUploadProgress)
Q_DECLARE_METATYPE(cirrus::domain::CloudUploadResult)
