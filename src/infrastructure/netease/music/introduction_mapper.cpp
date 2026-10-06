#include "introduction_mapper.h"
#include "metadata_mapping.h"

namespace cirrus::netease
{
namespace
{
using namespace domain;
using namespace mapping;
constexpr qsizetype TextBudget = 256 * 1024;
bool requiredText(const QJsonValue& value, QString& output, qsizetype maximum = 4096)
{
    std::optional<QString> text;
    if (!metadataText(value, text, maximum) || !text)
        return false;
    output = *text;
    return true;
}
bool echoMatches(const QJsonObject& object, const QString& key, const QString& id)
{
    const auto value = object.value(key);
    return value.isUndefined() || value.isNull() || providerId(value) == id;
}
std::optional<SongWikiKind> kind(const QString& type)
{
    if (type == QStringLiteral("songTag"))
        return SongWikiKind::Tags;
    if (type == QStringLiteral("songBizTag"))
        return SongWikiKind::BusinessTags;
    if (type == QStringLiteral("language"))
        return SongWikiKind::Language;
    if (type == QStringLiteral("bpm"))
        return SongWikiKind::Bpm;
    return std::nullopt;
}
QJsonValue title(const QJsonObject& object)
{
    return object.value(QStringLiteral("uiElement"))
        .toObject()
        .value(QStringLiteral("mainTitle"))
        .toObject()
        .value(QStringLiteral("title"));
}
} // namespace
application::MusicResult mapIntroductionResponse(const application::MusicQuery& query,
                                                 const QJsonObject& object)
{
    using namespace application;
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (!validMusicQuery(query) || object.value(QStringLiteral("code")) != QJsonValue(200))
        return invalid;
    if (const auto* q = std::get_if<ArtistIntroductionQuery>(&query)) {
        const auto rows = object.value(QStringLiteral("introduction"));
        ArtistIntroduction result;
        result.artist = q->artist;
        if (!rows.isArray() || rows.toArray().size() > 64 ||
            !echoMatches(object, QStringLiteral("id"), q->artist.value) ||
            !metadataText(object.value(QStringLiteral("briefDesc")), result.briefDescription,
                          65536))
            return invalid;
        qsizetype total = result.briefDescription ? result.briefDescription->size() : 0;
        for (const auto row : rows.toArray()) {
            if (!row.isObject())
                return invalid;
            const auto section = row.toObject();
            IntroductionSection value;
            if (!metadataText(section.value(QStringLiteral("ti")), value.title) ||
                !requiredText(section.value(QStringLiteral("txt")), value.text, 65536))
                return invalid;
            total += value.text.size() + (value.title ? value.title->size() : 0);
            if (total > TextBudget)
                return invalid;
            result.sections.append(std::move(value));
        }
        return result;
    }
    const auto* q = std::get_if<SongWikiQuery>(&query);
    if (!q || !object.value(QStringLiteral("data")).isObject())
        return invalid;
    const auto data = object.value(QStringLiteral("data")).toObject();
    const auto blocks = data.value(QStringLiteral("blocks"));
    if (!blocks.isArray() || blocks.toArray().size() > 32 ||
        !echoMatches(data, QStringLiteral("songId"), q->song.value))
        return invalid;
    SongWikiSummary result;
    result.song = q->song;
    qsizetype total = 0;
    for (const auto blockValue : blocks.toArray()) {
        if (!blockValue.isObject())
            return invalid;
        const auto block = blockValue.toObject();
        QString code;
        if (!requiredText(block.value(QStringLiteral("code")), code))
            return invalid;
        if (code != QStringLiteral("SONG_PLAY_ABOUT_SONG_BASIC")) {
            result.unsupportedContent = true;
            continue; // no similar-song lookup, images, links, sheets or business actions
        }
        const auto creatives = block.value(QStringLiteral("creatives"));
        if (!creatives.isArray() || creatives.toArray().size() > 100)
            return invalid;
        for (const auto creativeValue : creatives.toArray()) {
            if (!creativeValue.isObject())
                return invalid;
            const auto creative = creativeValue.toObject();
            QString type;
            if (!requiredText(creative.value(QStringLiteral("creativeType")), type))
                return invalid;
            const auto category = kind(type);
            if (!category) {
                result.unsupportedContent = true;
                continue;
            }
            if (result.cards.size() >= 100)
                return invalid;
            SongWikiCard card;
            card.kind = *category;
            if (!requiredText(title(creative), card.title))
                return invalid;
            const bool tags =
                *category == SongWikiKind::Tags || *category == SongWikiKind::BusinessTags;
            const auto rows = tags ? creative.value(QStringLiteral("resources"))
                                   : creative.value(QStringLiteral("uiElement"))
                                         .toObject()
                                         .value(QStringLiteral("textLinks"));
            if (!rows.isArray() || rows.toArray().size() > (tags ? 100 : 16))
                return invalid;
            total += card.title.size();
            for (const auto row : rows.toArray()) {
                if (!row.isObject())
                    return invalid;
                QString value;
                if (!requiredText(tags ? title(row.toObject())
                                       : row.toObject().value(QStringLiteral("text")),
                                  value))
                    return invalid;
                total += value.size();
                card.values.append(std::move(value));
            }
            if (total > TextBudget)
                return invalid;
            result.cards.append(std::move(card));
        }
    }
    return result;
}
} // namespace cirrus::netease
