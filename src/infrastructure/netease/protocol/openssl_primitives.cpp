#include "openssl_primitives.h"

#include <openssl/crypto.h>
#include <openssl/hmac.h>

namespace cirrus::netease::detail
{

std::optional<QByteArray> aes(const QByteArray& input, const QByteArray& key,
                              const EVP_CIPHER* cipher, bool encrypt, const char* iv)
{
    Owned<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context || key.size() != EVP_CIPHER_get_key_length(cipher) ||
        EVP_CipherInit_ex(context.get(), cipher, nullptr,
                          reinterpret_cast<const unsigned char*>(key.constData()),
                          reinterpret_cast<const unsigned char*>(iv), encrypt ? 1 : 0) != 1) {
        return std::nullopt;
    }
    QByteArray output(input.size() + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    auto* bytes = reinterpret_cast<unsigned char*>(output.data());
    int written = 0;
    int tail = 0;
    if (EVP_CipherUpdate(context.get(), bytes, &written,
                         reinterpret_cast<const unsigned char*>(input.constData()),
                         static_cast<int>(input.size())) != 1 ||
        EVP_CipherFinal_ex(context.get(), bytes + written, &tail) != 1) {
        cleanse(output);
        return std::nullopt;
    }
    output.resize(written + tail);
    return output;
}

std::optional<QByteArray> hmacSha256(QByteArrayView key, QByteArrayView data)
{
    QByteArray digest(EVP_MAX_MD_SIZE, Qt::Uninitialized);
    unsigned int size = 0;
    if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
              reinterpret_cast<const unsigned char*>(data.data()), static_cast<size_t>(data.size()),
              reinterpret_cast<unsigned char*>(digest.data()), &size)) {
        return std::nullopt;
    }
    digest.resize(size);
    return digest;
}

void cleanse(QByteArray& bytes)
{
    if (!bytes.isEmpty()) {
        OPENSSL_cleanse(bytes.data(), static_cast<size_t>(bytes.size()));
    }
}

} // namespace cirrus::netease::detail
