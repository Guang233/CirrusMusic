#pragma once
#include "domain/music/song_wiki_text_types.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
namespace cirrus::test
{
inline domain::Song wikiTextSong()
{
    domain::Song s;
    s.id = {"9007199254740993"};
    s.title = " synthetic 雲 & <text> ";
    domain::AlbumReference a;
    a.id = domain::AlbumId{"9007199254740995"};
    a.title = " synthetic album ";
    s.album = a;
    return s;
}
inline domain::SongWikiTextQuery wikiTextQuery(domain::SessionToken context)
{
    return domain::SongWikiTextQuery::fromSong(context, wikiTextSong()).value();
}
inline QJsonObject wikiTextObject()
{
    const QJsonObject meta{{"text", " synthetic & <text> "},
                           {"id", "not-an-action"},
                           {"jumpUrl", "orpheus://synthetic"},
                           {"imgUrl", "https://synthetic.invalid/image"}};
    const QJsonObject row{
        {"title", " synthetic label "}, {"pattern", 2}, {"wikiSubMetaVos", QJsonArray{meta, meta}}};
    const QJsonObject section{
        {"blockCode", "wikiSubBlockSongInfoVo"},
        {"blockInfo", QJsonObject{{"blockName", " synthetic section "},
                                  {"desc", " synthetic description "},
                                  {"wikiSubElementVos", QJsonArray{row, row}}}}};
    const QJsonObject base{
        {"blockCode", "wikiSubBlockBaseInfoVo"},
        {"blockInfo", QJsonObject{{"blockName", " synthetic base "},
                                  {"wikiSubElementVos",
                                   QJsonArray{QJsonObject{{"title", " synthetic date "},
                                                          {"content", " synthetic content "}}}}}}};
    const QJsonObject primary{
        {"positionCode", "songDetailNewSongWiki"},
        {"bizCode", "songDetailNewSongWiki"},
        {"rnData",
         QJsonObject{
             {"songName", wikiTextSong().title},
             {"albumId", wikiTextSong().album->id->value},
             {"encodeId", "opaque-do-not-decode"},
             {"blocks", QJsonArray{section,
                                   QJsonObject{{"blockCode", "wikiSubBlockMVVo"},
                                               {"blockInfo", QJsonObject{{"mustNotVisit", true}}}},
                                   base}}}}};
    return QJsonObject{
        {"code", 200},
        {"data",
         QJsonObject{
             {"blocks",
              QJsonArray{QJsonObject{{"positionCode", "songWikiPodcast"}, {"rnData", 42}}, primary,
                         QJsonObject{{"positionCode", "syntheticFutureBlock"},
                                     {"rnData", QJsonValue::Null}}}}}}};
}
inline QByteArray wikiTextBody()
{
    return QJsonDocument(wikiTextObject()).toJson(QJsonDocument::Compact);
}
} // namespace cirrus::test
