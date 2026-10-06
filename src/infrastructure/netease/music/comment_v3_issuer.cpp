#include "comment_v3_issuer.h"
#include "infrastructure/netease/protocol/comment_v3_policy.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QThread>
#include <QTimer>
#include <utility>
namespace cirrus::netease
{
QByteArray commentTokenFrom(QByteArray body)
{
    if (body.size() > 16 * 1024 || !body.isValidUtf8())
        return {};
    body = body.trimmed();
    if (body.endsWith(';'))
        body.chop(1);
    if (!body.startsWith("null(") || !body.endsWith(')'))
        return {};
    const auto doc = QJsonDocument::fromJson(body.mid(5, body.size() - 6));
    if (!doc.isArray())
        return {};
    const auto a = doc.array();
    if (a.size() != 3 || a[0] != QJsonValue(200) || !a[1].isDouble() || a[1].toDouble() < 0 ||
        a[1].toDouble() > 9007199254740991.0 || a[1] != QJsonValue(a[1].toInteger(-1)) ||
        !a[2].isString())
        return {};
    const auto text = a[2].toString();
    const auto token = text.toUtf8();
    return validCommentV3Token(token) ? token : QByteArray{};
}
struct CommentV3Issuer::Pending {
    QPointer<QNetworkReply> reply;
    QByteArray body;
    bool canceled = false;
};
CommentV3Issuer::CommentV3Issuer(std::unique_ptr<QNetworkAccessManager> manager, QObject* parent)
    : QObject(parent), m_manager(nullptr)
{
    if (!manager)
        manager = std::make_unique<QNetworkAccessManager>();
    Q_ASSERT(!manager->parent() && manager->thread() == thread());
    manager->setParent(this);
    m_manager = manager.release();
}
CommentV3Issuer::~CommentV3Issuer()
{
    for (const auto& p : std::as_const(m_pending))
        if (p->reply) {
            p->reply->disconnect(this);
            p->reply->abort();
        }
}
quint64 CommentV3Issuer::acquire()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pending.size() >= 8)
        return 0;
    const auto id = ++m_nextId;
    m_pending.insert(id, std::make_shared<Pending>());
    QTimer::singleShot(0, this, [this, id] { start(id); });
    return id;
}
void CommentV3Issuer::start(quint64 id)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    if (p->canceled) {
        finish(id, CommentTokenError::Canceled);
        return;
    }
    QNetworkRequest wire(
        QUrl(QStringLiteral("https://ac.dun.163yun.com/v3/b?pn=YD00000558929251")));
    wire.setRawHeader("Accept-Encoding", "identity");
    wire.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                      QNetworkRequest::ManualRedirectPolicy);
    wire.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    wire.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    wire.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    wire.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    wire.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    wire.setTransferTimeout(10000);
    p->reply = m_manager->get(wire);
    p->reply->setReadBufferSize(16 * 1024);
    connect(p->reply, &QNetworkReply::readyRead, this, [this, id] { consume(id); });
    connect(p->reply, &QNetworkReply::finished, this, [this, id] {
        consume(id);
        if (m_pending.contains(id))
            finish(id);
    });
    auto* timer = new QTimer(p->reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id] { finish(id, CommentTokenError::Timeout); });
    timer->start(15000);
    if (p->reply->isFinished()) {
        consume(id);
        if (m_pending.contains(id))
            finish(id);
    }
}
void CommentV3Issuer::consume(quint64 id)
{
    const auto p = m_pending.value(id);
    if (!p || !p->reply)
        return;
    p->body += p->reply->read(16 * 1024 + 1 - p->body.size());
    if (p->body.size() > 16 * 1024)
        finish(id, CommentTokenError::InvalidResponse);
}
void CommentV3Issuer::cancel(quint64 id)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    p->canceled = true;
    QTimer::singleShot(0, this, [this, id] { finish(id, CommentTokenError::Canceled); });
}
void CommentV3Issuer::finish(quint64 id, CommentTokenError error)
{
    const auto p = m_pending.take(id);
    if (!p)
        return;
    CommentTokenResult r;
    r.error = p->canceled ? CommentTokenError::Canceled : error;
    if (p->reply && r.error == CommentTokenError::None) {
        qsizetype headers = 0;
        for (const auto& pair : p->reply->rawHeaderPairs())
            headers += pair.first.size() + pair.second.size();
        if (headers > 64 * 1024 ||
            (!p->reply->rawHeader("Content-Encoding").isEmpty() &&
             p->reply->rawHeader("Content-Encoding").toLower() != "identity"))
            r.error = CommentTokenError::InvalidResponse;
        else if (p->reply->error() != QNetworkReply::NoError ||
                 p->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            r.error = CommentTokenError::Network;
        else {
            r.token = commentTokenFrom(p->body);
            if (r.token.isEmpty())
                r.error = CommentTokenError::InvalidResponse;
        }
    }
    p->body.fill('\0');
    if (p->reply) {
        p->reply->disconnect(this);
        p->reply->abort();
        p->reply->deleteLater();
    }
    emit completed(id, std::move(r));
}
} // namespace cirrus::netease
