#pragma once

#include "protocol_crypto.h"

#include <QByteArray>
#include <QJsonObject>

namespace cirrus::netease
{

// The NetEase clients' encrypted log upload (docs/api/explicit-listening-reports.md §4d):
// records `time \x01 action \x01 json`, zstd-compressed and sealed in an NCBL container
// (ChaCha20; its key wrapped with the service's fixed 256-bit RSA key; the client's
// cookies as an encrypted meta block), one multipart file per log stream.
struct ClientLogUpload {
    CryptoError error = CryptoError::None;
    QByteArray body;             // the multipart body
    QByteArray contentType;      // multipart/form-data with its boundary
    QList<QByteArray> fileNames; // the service lists them in `successfiles` once taken
    [[nodiscard]] bool ok() const { return error == CryptoError::None; }
};

// One record of the client's log format, newline-terminated as the Android client writes it.
[[nodiscard]] QByteArray clientLogRecord(qint64 epochSeconds, const QByteArray& action,
                                         const QJsonObject& data);
// The NCBL container for `body` (records) with `meta` (the client's cookies, JSON).
[[nodiscard]] std::optional<QByteArray> sealNcbl(const QByteArray& meta, const QByteArray& body,
                                                 const RandomBytes& random = secureRandomBytes);
// One log file: its name (`flush_<stream>_<pid>_<seq>_<rand>`) and its records.
struct ClientLogFile {
    QByteArray name;
    QByteArray records;
};
// One part per file, all in one request.
[[nodiscard]] ClientLogUpload clientLogUpload(const QByteArray& meta,
                                              const QList<ClientLogFile>& files,
                                              const RandomBytes& random = secureRandomBytes);
// The Android client's device id format (its x4.e: Base64 of "IMEI \t Wi-Fi MAC \t ANDROID_ID \t
// local id", what a current Android reports: "null", the placeholder MAC, two 16-hex ids),
// derived from our installation id so it stays the same; percent-encoded, as its cookie.
// The log service drops play events whose device id is not in this format.
[[nodiscard]] QByteArray androidDeviceId(const QString& installationId);
// ChaCha20 (RFC 8439 block function, 32-bit counter); exposed for tests.
[[nodiscard]] std::optional<QByteArray> chacha20(const QByteArray& key, quint32 counter,
                                                 const QByteArray& nonce, const QByteArray& data);

} // namespace cirrus::netease
