#include "xeapi_key_vault.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace cirrus::bootstrap
{

using netease::XeapiPublicKey;
using secrets::SecretError;

XeapiKeyVault::XeapiKeyVault(netease::XeapiKeyRing& ring, secrets::SecretStore& store,
                             QObject* parent)
    : QObject(parent), m_ring(&ring), m_store(store)
{
    Q_ASSERT(ring.thread() == thread());
    connect(&ring, &netease::XeapiKeyRing::changed, this, &XeapiKeyVault::ringChanged);
}

QByteArray XeapiKeyVault::serialize(const XeapiPublicKey& key)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("format"), 1},
                                     {QStringLiteral("version"), key.version},
                                     {QStringLiteral("publicKey"),
                                      QString::fromLatin1(key.publicKey.toBase64())},
                                     {QStringLiteral("sk"), QString::fromUtf8(key.sk)}})
        .toJson(QJsonDocument::Compact);
}

std::optional<XeapiPublicKey> XeapiKeyVault::deserialize(const QByteArray& data)
{
    const auto document = QJsonDocument::fromJson(data);
    const auto object = document.object();
    const auto version = object.value(QStringLiteral("version"));
    const auto publicKey = object.value(QStringLiteral("publicKey"));
    const auto sk = object.value(QStringLiteral("sk"));
    if (!document.isObject() || object.value(QStringLiteral("format")).toInt() != 1 ||
        !version.isString() || !publicKey.isString() || !sk.isString()) {
        return std::nullopt;
    }
    const auto raw = QByteArray::fromBase64Encoding(publicKey.toString().toLatin1(),
                                                    QByteArray::AbortOnBase64DecodingErrors);
    XeapiPublicKey key{version.toString(), raw ? raw.decoded : QByteArray(),
                       sk.toString().toUtf8()};
    return key.isValid() ? std::optional<XeapiPublicKey>(std::move(key)) : std::nullopt;
}

void XeapiKeyVault::restore()
{
    m_store.read(QString::fromLatin1(EntryKey), this, [this](secrets::SecretResult result) {
        auto error = result.error;
        if (result.ok()) {
            const auto key = deserialize(result.data);
            result.data.fill('\0');
            if (!key) {
                error = SecretError::Failed; // damaged record: never loaded half-valid
            } else if (m_ring && !m_ring->current()) {
                m_restoring = true; // restoring is not a change worth writing back
                const bool replaced = m_ring->replace(*key);
                m_restoring = false;
                error = replaced ? SecretError::None : SecretError::Failed;
            }
            // A ring that already holds a key got it more recently: keep it.
        }
        emit restored(error);
    });
}

void XeapiKeyVault::ringChanged()
{
    if (m_restoring) {
        return;
    }
    m_dirty = true;
    flush();
}

void XeapiKeyVault::flush()
{
    if (m_busy || !m_dirty || !m_ring) {
        return;
    }
    m_busy = true;
    m_dirty = false;
    const auto onDone = [this](SecretError error) {
        m_busy = false;
        // NotFound on delete already means "absent"; other errors are reported.
        if (error != SecretError::None && error != SecretError::NotFound) {
            emit persistFailed(error);
        }
        flush(); // coalesced later changes: store only the latest state
    };
    const auto key = QString::fromLatin1(EntryKey);
    if (const auto current = m_ring->current()) {
        auto data = serialize(*current);
        m_store.write(key, data, this, onDone);
        data.fill('\0');
    } else {
        m_store.remove(key, this, onDone);
    }
}

} // namespace cirrus::bootstrap
