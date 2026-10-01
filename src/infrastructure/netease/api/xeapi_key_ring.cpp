#include "xeapi_key_ring.h"

#include <QThread>

namespace cirrus::netease
{
namespace
{
void wipe(std::optional<XeapiPublicKey>& key)
{
    if (key) {
        key->sk.fill('\0');
        key.reset();
    }
}
} // namespace

XeapiKeyRing::~XeapiKeyRing() { wipe(m_key); }

std::optional<XeapiPublicKey> XeapiKeyRing::current() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return m_key;
}

bool XeapiKeyRing::replace(XeapiPublicKey key)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!key.isValid()) {
        return false;
    }
    wipe(m_key);
    m_key = std::move(key);
    emit changed();
    return true;
}

void XeapiKeyRing::clear()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_key) {
        wipe(m_key);
        emit changed();
    }
}

} // namespace cirrus::netease
