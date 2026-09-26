#include "form_encoding.h"

namespace cirrus::netease
{
namespace
{

QByteArray scalarUtf8(QStringView value)
{
    // URLSearchParams first converts to a Unicode scalar value string. Replace
    // lone UTF-16 surrogates explicitly rather than relying on Qt's fallback.
    QString normalized;
    normalized.reserve(value.size());
    for (qsizetype index = 0; index < value.size(); ++index) {
        const QChar character = value.at(index);
        if (character.isHighSurrogate()) {
            if (index + 1 < value.size() && value.at(index + 1).isLowSurrogate()) {
                normalized.append(character);
                normalized.append(value.at(++index));
            } else {
                normalized.append(QChar::ReplacementCharacter);
            }
        } else if (character.isLowSurrogate()) {
            normalized.append(QChar::ReplacementCharacter);
        } else {
            normalized.append(character);
        }
    }
    return normalized.toUtf8();
}

bool isFormSafe(unsigned char byte)
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || byte == '*' || byte == '-' || byte == '.' || byte == '_';
}

} // namespace

QByteArray encodeFormComponent(QStringView value)
{
    constexpr char hex[] = "0123456789ABCDEF";
    const QByteArray utf8 = scalarUtf8(value);
    QByteArray encoded;
    for (char character : utf8) {
        const auto byte = static_cast<unsigned char>(character);
        if (isFormSafe(byte)) {
            encoded.append(character);
        } else if (byte == ' ') {
            encoded.append('+');
        } else {
            encoded.append('%');
            encoded.append(hex[byte >> 4]);
            encoded.append(hex[byte & 0x0F]);
        }
    }
    return encoded;
}

QByteArray encodeForm(const QList<FormField>& fields)
{
    QByteArray body;
    bool first = true;
    for (const auto& field : fields) {
        if (!first) {
            body.append('&');
        }
        first = false;
        body.append(encodeFormComponent(field.name));
        body.append('=');
        body.append(encodeFormComponent(field.value));
    }
    return body;
}

} // namespace cirrus::netease
