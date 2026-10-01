// Protocol adaptation based on api-enhanced util/crypto.js (MIT).
// See third_party/api-enhanced/NOTICE.md and LICENSE for attribution.
#include "xeapi_crypto.h"

#include "openssl_primitives.h"

#include <QJsonDocument>

#include <openssl/crypto.h>

namespace cirrus::netease
{
namespace
{

using detail::aes;
using detail::cleanse;
using detail::hmacSha256;
using detail::Owned;

const QByteArray StaticKey =
    QByteArray::fromHex("ab1d5a430f6bb04a3f01e81ddd72bd916d5ce591248ac128714806d7f8fb1b84");
constexpr char SignKey[] =
    "mUHCwVNWJbunMqAHf5MImuirT6plvs6VSFW62MGHstFQxhBGdEoIhLItH3djc4+FB/OKty3+lL2rGeoFBpVe5g==";
constexpr char Os[] = "android";

bool printableAscii(QByteArrayView value, qsizetype limit)
{
    if (value.isEmpty() || value.size() > limit) {
        return false;
    }
    for (const char c : value) {
        if (c < 0x21 || c > 0x7e) {
            return false;
        }
    }
    return true;
}

const EVP_CIPHER* ecbFor(qsizetype keySize)
{
    switch (keySize) {
    case 16:
        return EVP_aes_128_ecb();
    case 24:
        return EVP_aes_192_ecb();
    case 32:
        return EVP_aes_256_ecb();
    default:
        return nullptr;
    }
}

std::optional<QByteArray> draw(const RandomBytes& random, qsizetype size)
{
    auto bytes = random ? random(size) : std::nullopt;
    return bytes && bytes->size() == size ? bytes : std::nullopt;
}

QByteArray midTransform(const QByteArray& ciphertext, const QByteArray& mask)
{
    QByteArray xored(ciphertext.size(), Qt::Uninitialized);
    for (qsizetype i = 0; i < ciphertext.size(); ++i) {
        xored[i] = static_cast<char>(ciphertext[i] ^ mask[i & 0x0f]);
    }
    const auto b64 = xored.toBase64();
    const auto rot = (static_cast<unsigned char>(mask[0]) & 0x0f) % b64.size();
    return mask + b64.sliced(rot) + b64.first(rot);
}

std::optional<QByteArray> derive(EVP_PKEY* own, EVP_PKEY* peer)
{
    Owned<EVP_PKEY_CTX, EVP_PKEY_CTX_free> context(EVP_PKEY_CTX_new(own, nullptr),
                                                   EVP_PKEY_CTX_free);
    size_t size = 32;
    QByteArray shared(32, '\0');
    if (!context || EVP_PKEY_derive_init(context.get()) != 1 ||
        EVP_PKEY_derive_set_peer(context.get(), peer) != 1 ||
        EVP_PKEY_derive(context.get(), reinterpret_cast<unsigned char*>(shared.data()), &size) !=
            1 ||
        size != 32 || shared == QByteArray(32, '\0')) {
        cleanse(shared);
        return std::nullopt; // includes small-order / all-zero shared secrets
    }
    return shared;
}

std::optional<QByteArray> gcmEncrypt(const QByteArray& key, const QByteArray& iv,
                                     const QByteArray& plaintext)
{
    Owned<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    QByteArray output(plaintext.size() + 16, Qt::Uninitialized);
    auto* bytes = reinterpret_cast<unsigned char*>(output.data());
    int written = 0;
    int tail = 0;
    if (!context ||
        EVP_EncryptInit_ex(context.get(), EVP_aes_128_gcm(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1 ||
        EVP_EncryptUpdate(context.get(), bytes, &written,
                          reinterpret_cast<const unsigned char*>(plaintext.constData()),
                          static_cast<int>(plaintext.size())) != 1 ||
        EVP_EncryptFinal_ex(context.get(), bytes + written, &tail) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, 16, bytes + written + tail) != 1) {
        return std::nullopt;
    }
    output.resize(written + tail + 16);
    return output;
}

// S = ephemeralPublic | iv | AES-128-GCM(HKDF-like(shared), plaintext) | tag
std::optional<QByteArray> encryptS(const QByteArray& dynamicKey, const XeapiPublicKey& key,
                                   QByteArray ephemeralPrivate, const QByteArray& iv)
{
    const auto* raw = reinterpret_cast<const unsigned char*>(ephemeralPrivate.constData());
    Owned<EVP_PKEY, EVP_PKEY_free> own(
        EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, raw, 32), EVP_PKEY_free);
    cleanse(ephemeralPrivate);
    Owned<EVP_PKEY, EVP_PKEY_free> peer(
        EVP_PKEY_new_raw_public_key(
            EVP_PKEY_X25519, nullptr,
            reinterpret_cast<const unsigned char*>(key.publicKey.constData()), 32),
        EVP_PKEY_free);
    QByteArray ephemeralPublic(32, '\0');
    size_t publicSize = 32;
    if (!own || !peer ||
        EVP_PKEY_get_raw_public_key(own.get(),
                                    reinterpret_cast<unsigned char*>(ephemeralPublic.data()),
                                    &publicSize) != 1 ||
        publicSize != 32) {
        return std::nullopt;
    }
    auto shared = derive(own.get(), peer.get());
    if (!shared) {
        return std::nullopt;
    }
    auto prk = hmacSha256(QByteArray(32, '\0'), *shared);
    cleanse(*shared);
    auto okm = prk ? hmacSha256(*prk, ephemeralPublic + '\x01') : std::nullopt;
    if (prk) {
        cleanse(*prk);
    }
    if (!okm) {
        return std::nullopt;
    }
    auto aesKey = okm->first(16);
    cleanse(*okm);
    QByteArray plaintext = dynamicKey.toBase64() + '|' + Os + '|' + key.sk;
    const auto encrypted = gcmEncrypt(aesKey, iv, plaintext);
    cleanse(aesKey);
    cleanse(plaintext);
    if (!encrypted) {
        return std::nullopt;
    }
    return ephemeralPublic + iv + *encrypted;
}

} // namespace

bool XeapiPublicKey::isValid() const
{
    if (!printableAscii(version.toUtf8(), 64) || publicKey.size() != 32 || sk.isEmpty() ||
        sk.size() > 1024 || !sk.isValidUtf8()) {
        return false;
    }
    for (const unsigned char c : sk) {
        if (c < 0x20 || c == 0x7f) {
            return false;
        }
    }
    return true;
}

bool XeapiSessionKey::isValid() const
{
    return printableAscii(id, 256) && printableAscii(key, 32) && ecbFor(key.size());
}

EncodedPayload encodeXeapi(const QList<FormField>& body, const XeapiPublicKey& key,
                           const std::optional<XeapiSessionKey>& session, const RandomBytes& random)
{
    if (!key.isValid() || (session && !session->isValid())) {
        return {CryptoError::InvalidInput, {}};
    }
    QList<FormField> fields;
    for (const auto& field : body) {
        if (field.name != QLatin1String("e_r")) {
            fields.append(field);
        }
    }
    const auto form = encodeForm(fields);
    if (form.size() > MaxProtocolPlaintext / 2) {
        return {CryptoError::InvalidInput, {}};
    }
    // Byte-identical to JSON.stringify of the closed subset; base64 needs no escaping.
    const QByteArray plaintext =
        "{\"body\":\"" + form.toBase64() + "\",\"queryString\":\"e_r=true\"}";

    auto dynamicKey = session ? std::optional<QByteArray>(session->key) : draw(random, 16);
    const auto mask = dynamicKey ? draw(random, 16) : std::nullopt;
    auto ephemeral = mask ? draw(random, 32) : std::nullopt;
    const auto iv = ephemeral ? draw(random, 12) : std::nullopt;
    if (!iv) {
        if (dynamicKey) {
            cleanse(*dynamicKey);
        }
        if (ephemeral) {
            cleanse(*ephemeral);
        }
        return {CryptoError::RandomFailure, {}};
    }
    const auto inner = aes(plaintext, StaticKey, EVP_aes_256_ecb(), true);
    const auto b =
        inner ? aes(midTransform(*inner, *mask), *dynamicKey, ecbFor(dynamicKey->size()), true)
              : std::nullopt;
    const auto s = b ? encryptS(*dynamicKey, key, std::move(*ephemeral), *iv) : std::nullopt;
    cleanse(*dynamicKey);
    const QByteArray routing =
        key.version.toLatin1() + '|' + (session ? session->id : QByteArray());
    const auto r = s ? aes(routing, StaticKey, EVP_aes_256_ecb(), true) : std::nullopt;
    if (!r) {
        return {CryptoError::EncryptionFailure, {}};
    }
    return {CryptoError::None,
            {{QStringLiteral("B"), QString::fromLatin1(b->toBase64())},
             {QStringLiteral("S"), QString::fromLatin1(s->toBase64())},
             {QStringLiteral("R"), QString::fromLatin1(r->toBase64())}}};
}

QByteArray xeapiSignature(QByteArrayView timestamp, QByteArrayView nonce)
{
    // The key is the Base64 TEXT itself, as in the reference; it is not decoded.
    const auto digest = hmacSha256(QByteArrayView(SignKey), QByteArray(timestamp) + nonce);
    return digest ? digest->toBase64() : QByteArray();
}

bool verifyXeapiSignature(QByteArrayView timestamp, QByteArrayView nonce, QByteArrayView signature)
{
    const auto expected = xeapiSignature(timestamp, nonce);
    return !expected.isEmpty() && signature.size() == expected.size() &&
           CRYPTO_memcmp(expected.constData(), signature.data(),
                         static_cast<size_t>(expected.size())) == 0;
}

QString anonymousRegistrationUsername(QStringView deviceId)
{
    constexpr char XorKey[] = "3go8&$8*3*3h0k(2)2";
    constexpr qsizetype XorKeySize = sizeof(XorKey) - 1;
    const auto id = deviceId.toUtf8(); // any non-ASCII byte fails printableAscii
    if (!printableAscii(id, 128)) {
        return {};
    }
    QByteArray xored(id.size(), Qt::Uninitialized);
    for (qsizetype i = 0; i < id.size(); ++i) {
        xored[i] = static_cast<char>(id[i] ^ XorKey[i % XorKeySize]); // stays 7-bit ASCII
    }
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int size = 0;
    if (EVP_Digest(xored.constData(), static_cast<size_t>(xored.size()), digest, &size, EVP_md5(),
                   nullptr) != 1) {
        return {};
    }
    const auto encoded = QByteArray(reinterpret_cast<const char*>(digest), size).toBase64();
    return QString::fromLatin1((id + ' ' + encoded).toBase64());
}

std::optional<XeapiPublicKey> decryptXeapiPublicKey(QByteArrayView encryptedData)
{
    if (encryptedData.isEmpty() || encryptedData.size() > 16 * 1024) {
        return std::nullopt;
    }
    const auto decoded = QByteArray::fromBase64Encoding(QByteArray(encryptedData),
                                                        QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() % 16 != 0) {
        return std::nullopt;
    }
    auto plaintext = aes(decoded.decoded, StaticKey, EVP_aes_256_ecb(), false);
    if (!plaintext) {
        return std::nullopt;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(*plaintext, &error);
    cleanse(*plaintext);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const auto object = document.object();
    const auto version = object.value(QStringLiteral("version"));
    const auto publicKey = object.value(QStringLiteral("publicKey"));
    const auto sk = object.value(QStringLiteral("sk"));
    if (!publicKey.isString() || !sk.isString()) {
        return std::nullopt;
    }
    XeapiPublicKey key;
    if (version.isString()) {
        key.version = version.toString();
    } else if (version.isDouble() && version.toDouble() >= 0 &&
               version.toDouble() <= 9007199254740991.0 &&
               version.toDouble() == static_cast<double>(version.toInteger())) {
        key.version = QString::number(version.toInteger());
    }
    const auto raw = QByteArray::fromBase64Encoding(publicKey.toString().toLatin1(),
                                                    QByteArray::AbortOnBase64DecodingErrors);
    key.publicKey = raw ? raw.decoded : QByteArray();
    key.sk = sk.toString().toUtf8();
    if (!key.isValid()) {
        cleanse(key.sk);
        return std::nullopt;
    }
    return key;
}

} // namespace cirrus::netease
