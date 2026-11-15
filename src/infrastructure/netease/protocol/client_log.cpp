#include "client_log.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QUrl>
#include <QUuid>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <zstd.h>

#include <memory>

namespace cirrus::netease
{
namespace
{
// The service's NCBL key-wrap modulus (public, fixed), e = 65537.
constexpr char kModulus[] = "fd90bd466ff9bc8a3fec2fbcf263b90d5c564879fa5d7aab89b31c1d5cb4139d";
constexpr quint16 kMetaBlock = 0x4343;
constexpr int kHeader = 70;
constexpr qsizetype kFrame = 0x8000;

void le16(QByteArray& out, quint16 v)
{
    out.append(char(v & 0xff));
    out.append(char(v >> 8));
}
void le32(QByteArray& out, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        out.append(char((v >> (8 * i)) & 0xff));
}

// The clients compress their logs with zstd (Android: ZStdCompressor only).
std::optional<QByteArray> compress(const QByteArray& data)
{
    QByteArray out(qsizetype(ZSTD_compressBound(size_t(data.size()))), Qt::Uninitialized);
    const auto written =
        ZSTD_compress(out.data(), size_t(out.size()), data.constData(), size_t(data.size()), 3);
    if (ZSTD_isError(written))
        return std::nullopt;
    out.truncate(qsizetype(written));
    return out;
}

// keyA^e mod N, 32 bytes big-endian.
std::optional<QByteArray> wrapKey(const QByteArray& key)
{
    using Bn = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
    std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> ctx(BN_CTX_new(), BN_CTX_free);
    BIGNUM* raw = nullptr;
    if (!ctx || !BN_hex2bn(&raw, kModulus))
        return std::nullopt;
    Bn n(raw, BN_free);
    Bn e(BN_new(), BN_free), m(BN_new(), BN_free), r(BN_new(), BN_free);
    if (!e || !m || !r || !BN_set_word(e.get(), 65537) ||
        !BN_bin2bn(reinterpret_cast<const unsigned char*>(key.constData()), int(key.size()),
                   m.get()) ||
        !BN_mod_exp(r.get(), m.get(), e.get(), n.get(), ctx.get()))
        return std::nullopt;
    QByteArray out(32, Qt::Uninitialized);
    if (BN_bn2binpad(r.get(), reinterpret_cast<unsigned char*>(out.data()), 32) != 32)
        return std::nullopt;
    return out;
}

} // namespace

std::optional<QByteArray> chacha20(const QByteArray& key, quint32 counter, const QByteArray& nonce,
                                   const QByteArray& data)
{
    if (key.size() != 32 || nonce.size() != 12)
        return std::nullopt;
    QByteArray iv; // OpenSSL: 32-bit little-endian counter, then the 96-bit nonce
    le32(iv, counter);
    iv.append(nonce);
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),
                                                                        EVP_CIPHER_CTX_free);
    QByteArray out(data.size(), Qt::Uninitialized);
    int length = 0;
    if (!ctx ||
        EVP_EncryptInit_ex(ctx.get(), EVP_chacha20(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1 ||
        (!data.isEmpty() &&
         EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &length,
                           reinterpret_cast<const unsigned char*>(data.constData()),
                           int(data.size())) != 1) ||
        length != data.size())
        return std::nullopt;
    return out;
}

QByteArray androidDeviceId(const QString& installationId)
{
    const auto digest = QCryptographicHash::hash(
        "cirrus-android-device\n" + installationId.toUtf8(), QCryptographicHash::Sha256);
    const QByteArray raw =
        "null\t02:00:00:00:00:00\t" + digest.left(8).toHex() + '\t' + digest.mid(8, 8).toHex();
    return QUrl::toPercentEncoding(QString::fromLatin1(raw.toBase64()));
}

QByteArray clientLogRecord(qint64 epochSeconds, const QByteArray& action, const QJsonObject& data)
{
    return QByteArray::number(epochSeconds) + '\x01' + action + '\x01' +
           QJsonDocument(data).toJson(QJsonDocument::Compact) + '\n';
}

std::optional<QByteArray> sealNcbl(const QByteArray& meta, const QByteArray& body,
                                   const RandomBytes& random)
{
    if (meta.size() > 0xffff || body.size() > MaxProtocolPlaintext)
        return std::nullopt;
    auto keyA = random(32);
    auto uuid = random(16);
    const auto seqBytes = random(2);
    if (!keyA || keyA->size() != 32 || !uuid || uuid->size() != 16 || !seqBytes ||
        seqBytes->size() != 2)
        return std::nullopt;
    if (quint8((*keyA)[0]) >= 0xa3) // keeps the key below the modulus
        (*keyA)[0] = char(0xa2);
    (*uuid)[6] = char((quint8((*uuid)[6]) & 0x0f) | 0x40);
    (*uuid)[8] = char((quint8((*uuid)[8]) & 0x3f) | 0x80);
    const auto keyB = wrapKey(*keyA);
    if (!keyB)
        return std::nullopt;
    const auto nonce = uuid->left(12);
    const quint32 counter =
        (quint32(quint8((*uuid)[12])) | quint32(quint8((*uuid)[13])) << 8 |
         quint32(quint8((*uuid)[14])) << 16 | quint32(quint8((*uuid)[15])) << 24) >>
        2;
    const quint32 baseSeq = quint32(quint8((*seqBytes)[0])) | quint32(quint8((*seqBytes)[1])) << 8;

    const auto metaCipher = chacha20(*keyB, counter, nonce, meta);
    const auto compressed = compress(body);
    if (!metaCipher || !compressed)
        return std::nullopt;
    QByteArray metaBlock;
    le16(metaBlock, kMetaBlock);
    le16(metaBlock, quint16(metaCipher->size()));
    metaBlock.append(*metaCipher);

    QByteArray frames;
    quint32 seq = baseSeq;
    for (qsizetype off = 0; off < compressed->size(); off += kFrame) {
        const auto cipher = chacha20(*keyA, counter, nonce, compressed->mid(off, kFrame));
        if (!cipher)
            return std::nullopt;
        le16(frames, quint16(cipher->size()));
        le32(frames, seq++);
        frames.append(*cipher);
    }
    QByteArray header("NCBL");
    le32(header, 3);
    le16(header, quint16(kHeader + metaBlock.size()));
    header.append(*uuid);
    header.append(*keyB);
    le32(header, baseSeq);
    le32(header, seq - 1);
    le32(header, quint32(frames.size()));
    Q_ASSERT(header.size() == kHeader);
    return header + metaBlock + frames;
}

ClientLogUpload clientLogUpload(const QByteArray& meta, const QList<ClientLogFile>& files,
                                const RandomBytes& random)
{
    const auto boundary = random(16);
    if (files.isEmpty() || !boundary || boundary->size() != 16)
        return {CryptoError::EncryptionFailure};
    ClientLogUpload upload;
    // A UUID boundary, as the Android client's OkHttp sends.
    const auto mark = QUuid::fromRfc4122(*boundary).toByteArray(QUuid::WithoutBraces);
    upload.contentType = "multipart/form-data; boundary=" + mark;
    for (const auto& file : files) {
        const auto sealed = sealNcbl(meta, file.records, random);
        if (!sealed || file.name.isEmpty() || file.name.contains('"') || file.name.contains('\r') ||
            file.name.contains('\n'))
            return {CryptoError::EncryptionFailure};
        upload.fileNames.append(file.name);
        upload.body += "--" + mark +
                       "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" +
                       file.name + "\"\r\nContent-Type: multipart/form-data\r\nContent-Length: " +
                       QByteArray::number(sealed->size()) + "\r\n\r\n" + *sealed + "\r\n";
    }
    upload.body += "--" + mark + "--\r\n";
    return upload;
}

} // namespace cirrus::netease
