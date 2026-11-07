#pragma once

#include <QCryptographicHash>
#include <QMetaType>
#include <QString>

namespace cirrus::domain
{

// Phone (SMS code or password) and email/password sign-in (docs/api/credential-login.md).
// Like a QR authorization, a successful login is NOT an authenticated session: identity
// verification and the exact-token credential handoff still follow.
enum class CredentialMethod { PhoneCode, PhonePassword, EmailPassword };

// Ephemeral: do not log, cache or keep beyond the request. `secret` is the SMS code, or
// the MD5 hex digest of the password (passwordDigest), never the password itself.
struct CredentialLogin {
    CredentialMethod method{};
    QString countryCode; // phone methods: digits without '+', e.g. "86"
    QString account;     // phone digits, or the email address
    QString secret;
};

struct SmsCodeRequest {
    QString countryCode;
    QString phone;
};

[[nodiscard]] inline bool allDigits(const QString& text, qsizetype min, qsizetype max)
{
    if (text.size() < min || text.size() > max)
        return false;
    for (const auto c : text)
        if (c < u'0' || c > u'9')
            return false;
    return true;
}

[[nodiscard]] inline bool validCountryCode(const QString& code)
{
    return allDigits(code, 1, 4) && code.front() != u'0';
}
[[nodiscard]] inline bool validPhoneNumber(const QString& phone) { return allDigits(phone, 4, 15); }
[[nodiscard]] inline bool validSmsCode(const QString& code) { return allDigits(code, 4, 8); }

[[nodiscard]] inline bool validPasswordDigest(const QString& digest)
{
    if (digest.size() != 32)
        return false;
    for (const auto c : digest)
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')))
            return false;
    return true;
}

// A plausible address, not RFC 5322: printable ASCII, one '@' with text on both sides.
[[nodiscard]] inline bool validEmail(const QString& email)
{
    if (email.size() < 3 || email.size() > 254)
        return false;
    qsizetype at = -1;
    for (qsizetype i = 0; i < email.size(); ++i) {
        const auto c = email.at(i).unicode();
        if (c <= 0x20 || c >= 0x7f)
            return false;
        if (c == '@') {
            if (at != -1)
                return false;
            at = i;
        }
    }
    return at > 0 && at < email.size() - 1;
}

[[nodiscard]] inline bool validCredentialLogin(const CredentialLogin& login)
{
    switch (login.method) {
    case CredentialMethod::PhoneCode:
        return validCountryCode(login.countryCode) && validPhoneNumber(login.account) &&
               validSmsCode(login.secret);
    case CredentialMethod::PhonePassword:
        return validCountryCode(login.countryCode) && validPhoneNumber(login.account) &&
               validPasswordDigest(login.secret);
    case CredentialMethod::EmailPassword:
        return login.countryCode.isEmpty() && validEmail(login.account) &&
               validPasswordDigest(login.secret);
    }
    return false;
}

[[nodiscard]] inline bool validSmsCodeRequest(const SmsCodeRequest& request)
{
    return validCountryCode(request.countryCode) && validPhoneNumber(request.phone);
}

// The provider's password form: lowercase MD5 hex of the UTF-8 password. Empty for an
// empty password. Callers should drop the password as soon as they have this.
[[nodiscard]] inline QString passwordDigest(const QString& password)
{
    if (password.isEmpty())
        return {};
    QByteArray utf8 = password.toUtf8();
    const auto digest = QCryptographicHash::hash(utf8, QCryptographicHash::Md5).toHex();
    utf8.fill('\0');
    return QString::fromLatin1(digest);
}

} // namespace cirrus::domain

Q_DECLARE_METATYPE(cirrus::domain::CredentialLogin)
Q_DECLARE_METATYPE(cirrus::domain::SmsCodeRequest)
