#include "song_wiki_text_mapper.h"
#include "music_mapping.h"
#include <QJsonArray>
#include <QJsonDocument>
namespace cirrus::netease
{
namespace
{
bool text(const QJsonObject& o, QString key, int limit, QString& out, int& aggregate)
{
    const auto v = o.value(key);
    if (!v.isString() || v.toString().size() > limit)
        return false;
    out = v.toString();
    aggregate += out.size();
    return aggregate <= 131072; // UTF-16 units, local budget
}
bool optionalText(const QJsonObject& o, QString key, int limit, std::optional<QString>& out,
                  int& aggregate)
{
    // The service sends null for an empty optional field (desc, content: 2026-11-13).
    if (!o.contains(key) || o.value(key).isNull())
        return true;
    QString s;
    if (!text(o, key, limit, s, aggregate))
        return false;
    out = std::move(s);
    return true;
}
bool code(const QJsonObject& o, QString key)
{
    const auto v = o.value(key);
    return v.isString() && !v.toString().isEmpty() && v.toString().size() <= 80;
}
} // namespace
application::MusicResult mapSongWikiTextResponse(const domain::SongWikiTextQuery& query,
                                                 const QJsonObject& object)
{
    using namespace domain;
    using namespace application;
    const MusicFailure invalid{MusicError::InvalidResponse};
    if (!query.isValid() || object.value("code") != QJsonValue(200) ||
        QJsonDocument(object).toJson(QJsonDocument::Compact).size() > 2 * 1024 * 1024 ||
        !object.value("data").isObject())
        return invalid;
    const auto data = object.value("data").toObject();
    const auto raw = data.value("blocks");
    if (!raw.isArray() || raw.toArray().size() > 32)
        return invalid;
    SongWikiText page{query};
    std::optional<QJsonObject> primary;
    for (const auto v : raw.toArray()) {
        if (!v.isObject() || !code(v.toObject(), "positionCode"))
            return invalid;
        const auto b = v.toObject();
        if (b.value("positionCode") != "songDetailNewSongWiki") {
            ++page.unsupportedBlockCount;
            continue;
        }
        if (primary || b.value("bizCode") != "songDetailNewSongWiki" ||
            !b.value("rnData").isObject())
            return invalid;
        primary = b.value("rnData").toObject();
    }
    if (!primary)
        return invalid; // no fabricated empty basic section / root/data/summary alias
    int aggregate = 0;
    if (!text(*primary, "songName", 4096, page.observedSongName, aggregate) ||
        page.observedSongName != query.sourceTitle())
        return invalid;
    page.observedAlbum = {mapping::providerId(primary->value("albumId"))};
    if (page.observedAlbum.value != query.sourceAlbum().value)
        return invalid;
    const auto groups = primary->value("blocks");
    if (!groups.isArray() || groups.toArray().size() > 32)
        return invalid;
    for (const auto value : groups.toArray()) {
        if (!value.isObject() || !code(value.toObject(), "blockCode"))
            return invalid;
        const auto b = value.toObject();
        const auto kind = b.value("blockCode").toString();
        if (kind != "wikiSubBlockSongInfoVo" && kind != "wikiSubBlockBaseInfoVo") {
            ++page.unsupportedBlockCount;
            continue;
        }
        if (!b.value("blockInfo").isObject())
            return invalid;
        const auto info = b.value("blockInfo").toObject();
        SongWikiTextSection section;
        section.kind = kind == "wikiSubBlockSongInfoVo" ? SongWikiTextSectionKind::SongInfo
                                                        : SongWikiTextSectionKind::BaseInfo;
        if (!text(info, "blockName", 256, section.label, aggregate) ||
            !optionalText(info, "desc", 4096, section.description, aggregate))
            return invalid;
        const auto elements = info.value("wikiSubElementVos");
        if (!elements.isArray() || elements.toArray().size() > 100)
            return invalid;
        for (const auto element : elements.toArray()) {
            if (!element.isObject())
                return invalid;
            const auto row = element.toObject();
            SongWikiTextItem item;
            if (!text(row, "title", 256, item.label, aggregate) ||
                !optionalText(row, "content", 4096, item.content, aggregate))
                return invalid;
            if (row.contains("wikiSubMetaVos") && !row.value("wikiSubMetaVos").isNull()) {
                const auto metas = row.value("wikiSubMetaVos");
                if (!metas.isArray() || metas.toArray().size() > 16)
                    return invalid;
                QStringList values;
                for (const auto meta : metas.toArray()) {
                    if (!meta.isObject())
                        return invalid;
                    QString s;
                    if (!text(meta.toObject(), "text", 4096, s, aggregate))
                        return invalid;
                    values.append(std::move(s)); // no id/URL/image/pattern/role reconstruction
                }
                item.texts = std::move(values);
            }
            // Present ""/[] is kept; a row with neither (missing or null) has nothing to
            // show and is left out rather than failing the whole wiki.
            if (!item.content && !item.texts)
                continue;
            section.items.append(std::move(item));
        }
        page.sections.append(std::move(section)); // preserve original order and duplicates
    }
    return page; // original request + descriptive matches only; identity stays unverified
}
} // namespace cirrus::netease
