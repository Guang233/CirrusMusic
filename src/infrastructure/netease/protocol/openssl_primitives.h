#pragma once

// Internal to cirrus_netease_protocol; not a general crypto API.
#include <QByteArray>

#include <openssl/evp.h>

#include <memory>
#include <optional>

namespace cirrus::netease::detail
{

template <typename T, void (*Free)(T*)> using Owned = std::unique_ptr<T, decltype(Free)>;

// PKCS#7 block cipher (ECB/CBC). No partial output on failure.
[[nodiscard]] std::optional<QByteArray> aes(const QByteArray& input, const QByteArray& key,
                                            const EVP_CIPHER* cipher, bool encrypt,
                                            const char* iv = nullptr);
[[nodiscard]] std::optional<QByteArray> hmacSha256(QByteArrayView key, QByteArrayView data);
void cleanse(QByteArray& bytes);

} // namespace cirrus::netease::detail
