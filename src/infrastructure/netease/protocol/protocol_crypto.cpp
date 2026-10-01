// Protocol adaptation based on api-enhanced util/crypto.js (MIT).
// See third_party/api-enhanced/NOTICE.md and LICENSE for attribution.
#include "protocol_crypto.h"

#include "official_route.h"
#include "openssl_primitives.h"

#include <QJsonDocument>

#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include <algorithm>

namespace cirrus::netease
{
namespace
{

using detail::aes;
using detail::Owned;

constexpr char PresetKey[] = "0CoJUm6Qyw8W8jud";
constexpr char EapiKey[] = "e82ckenh8dichen8";
constexpr char Iv[] = "0102030405060708";
constexpr char Alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
constexpr char PublicKey[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDgtQn2JZ34ZC28NWYpAUd98iZ37BUrX/"
    "aKzmFbt7clFSs6sXqHauqKWqdtLkF2KexO40H1YTX8z2lSgBBOAxLsvaklV8k4cBFK9snQXE9/"
    "DDaFt6Rr7iVZMldczhC0JNgTz+SHXT6CBHuX3e9SdB1Ua44oncaTWz7OBGLbCiK45wIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

std::optional<QByteArray> rsaKey(QByteArray secret)
{
    std::reverse(secret.begin(), secret.end());
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(PublicKey, -1), BIO_free);
    if (!bio) {
        return std::nullopt;
    }
    Owned<EVP_PKEY, EVP_PKEY_free> key(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr),
                                       EVP_PKEY_free);
    if (!key) {
        return std::nullopt;
    }
    Owned<EVP_PKEY_CTX, EVP_PKEY_CTX_free> context(EVP_PKEY_CTX_new(key.get(), nullptr),
                                                   EVP_PKEY_CTX_free);
    if (!context || EVP_PKEY_encrypt_init(context.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(context.get(), RSA_NO_PADDING) <= 0) {
        return std::nullopt;
    }
    const int width = EVP_PKEY_get_size(key.get());
    if (width < secret.size()) {
        return std::nullopt;
    }
    QByteArray block(width - secret.size(), '\0');
    block.append(secret);
    QByteArray encrypted(width, Qt::Uninitialized);
    size_t size = static_cast<size_t>(width);
    const int result =
        EVP_PKEY_encrypt(context.get(), reinterpret_cast<unsigned char*>(encrypted.data()), &size,
                         reinterpret_cast<const unsigned char*>(block.constData()),
                         static_cast<size_t>(block.size()));
    OPENSSL_cleanse(block.data(), static_cast<size_t>(block.size()));
    OPENSSL_cleanse(secret.data(), static_cast<size_t>(secret.size()));
    if (result <= 0) {
        return std::nullopt;
    }
    encrypted.resize(static_cast<qsizetype>(size));
    return encrypted.toHex();
}

std::optional<QByteArray> randomSecret(const RandomBytes& random)
{
    if (!random) {
        return std::nullopt;
    }
    QByteArray secret;
    // Rejection sampling avoids modulo bias. Bound attempts even for a broken
    // injected source; production RAND_bytes failure is not a fixed-key fallback.
    for (int attempt = 0; attempt < 64 && secret.size() < 16; ++attempt) {
        const auto bytes = random(32);
        if (!bytes || bytes->size() != 32) {
            return std::nullopt;
        }
        for (char character : *bytes) {
            const auto byte = static_cast<unsigned char>(character);
            if (byte < 248) {
                secret.append(Alphabet[byte % 62]);
                if (secret.size() == 16) {
                    break;
                }
            }
        }
    }
    return secret.size() == 16 ? std::optional<QByteArray>(secret) : std::nullopt;
}

} // namespace

std::optional<QByteArray> secureRandomBytes(qsizetype size)
{
    if (size <= 0 || size > MaxProtocolPlaintext) {
        return std::nullopt;
    }
    QByteArray bytes(size, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), static_cast<int>(size)) != 1) {
        return std::nullopt;
    }
    return bytes;
}

EncodedPayload encodeWeapi(const QJsonObject& payload, const RandomBytes& random)
{
    const auto json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    if (json.size() > MaxProtocolPlaintext) {
        return {CryptoError::InvalidInput, {}};
    }
    auto secret = randomSecret(random);
    if (!secret) {
        return {CryptoError::RandomFailure, {}};
    }
    const auto first = aes(json, PresetKey, EVP_aes_128_cbc(), true, Iv);
    const auto second =
        first ? aes(first->toBase64(), *secret, EVP_aes_128_cbc(), true, Iv) : std::nullopt;
    const auto wrapped = second ? rsaKey(*secret) : std::nullopt;
    OPENSSL_cleanse(secret->data(), static_cast<size_t>(secret->size()));
    if (!second || !wrapped) {
        return {CryptoError::EncryptionFailure, {}};
    }
    return {CryptoError::None,
            {{QStringLiteral("params"), QString::fromLatin1(second->toBase64())},
             {QStringLiteral("encSecKey"), QString::fromLatin1(*wrapped)}}};
}

EncodedPayload encodeEapi(QStringView logicalPath, const QJsonObject& payload)
{
    if (logicalPath.size() > 2048 || !officialRequestUrl(Protocol::Eapi, logicalPath)) {
        return {CryptoError::InvalidInput, {}};
    }
    const auto json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    if (json.size() > MaxProtocolPlaintext) {
        return {CryptoError::InvalidInput, {}};
    }
    const auto path = logicalPath.toUtf8();
    const QByteArray message = "nobody" + path + "use" + json + "md5forencrypt";
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digestSize = 0;
    if (EVP_Digest(message.constData(), static_cast<size_t>(message.size()), digest, &digestSize,
                   EVP_md5(), nullptr) != 1) {
        return {CryptoError::EncryptionFailure, {}};
    }
    const auto hex = QByteArray(reinterpret_cast<const char*>(digest), digestSize).toHex();
    const auto encrypted = aes(path + "-36cd479b6b5-" + json + "-36cd479b6b5-" + hex, EapiKey,
                               EVP_aes_128_ecb(), true);
    if (!encrypted) {
        return {CryptoError::EncryptionFailure, {}};
    }
    return {CryptoError::None,
            {{QStringLiteral("params"), QString::fromLatin1(encrypted->toHex().toUpper())}}};
}

DecryptedPayload decryptEapi(const QByteArray& ciphertext)
{
    if (ciphertext.isEmpty() || ciphertext.size() % 16 != 0 ||
        ciphertext.size() > MaxProtocolPlaintext + 16) {
        return {CryptoError::InvalidInput, {}};
    }
    const auto plaintext = aes(ciphertext, EapiKey, EVP_aes_128_ecb(), false);
    if (!plaintext || plaintext->size() > MaxProtocolPlaintext) {
        return {CryptoError::DecryptionFailure, {}};
    }
    return {CryptoError::None, *plaintext};
}

} // namespace cirrus::netease
