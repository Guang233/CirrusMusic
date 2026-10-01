#pragma once

#include "infrastructure/netease/protocol/xeapi_crypto.h"

#include <QObject>

namespace cirrus::netease
{

// Installation-level xeapi key state, replaced only as a whole validated value.
// Thread-affine. changed() is the persistence seam (version/publicKey are public,
// sk is secret and must only reach a secure store).
class XeapiKeyRing final : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    ~XeapiKeyRing() override;

    [[nodiscard]] std::optional<XeapiPublicKey> current() const;
    [[nodiscard]] bool replace(XeapiPublicKey key); // false (unchanged) if invalid
    void clear();

  signals:
    void changed();

  private:
    std::optional<XeapiPublicKey> m_key;
};

} // namespace cirrus::netease
