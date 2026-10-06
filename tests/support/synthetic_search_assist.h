#pragma once
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
namespace cirrus::test
{
inline QByteArray searchAssistFixture(int kind)
{
    if (kind == 0)
        return R"({"code":200,"result":{"order":["artists","songs","albums","playlists","songs","mvs"],"artists":[{"id":"9007199254740993","name":"synthetic artist"}],"songs":[{"id":"9007199254740993","name":"<b>synthetic song</b>"},{"id":"9007199254740993","name":"synthetic duplicate"}],"albums":[{"id":"9007199254740993","name":"synthetic album"}],"playlists":[{"id":"9007199254740993","name":"synthetic playlist"}],"mvs":[{"id":99,"name":"synthetic excluded"}]}})";
    if (kind == 1)
        return R"({"code":200,"result":{"hots":[{"first":"  synthetic 热搜  ","second":1,"third":"ignored"},{"first":"  synthetic 热搜  "}]}})";
    return R"({"code":200,"data":[{"searchWord":"synthetic keyword","score":0,"content":"<b>synthetic text</b>","iconType":2,"iconUrl":"https://metadata.invalid/icon","url":"javascript:synthetic"},{"searchWord":"synthetic keyword"},{"searchWord":"synthetic empty","score":null,"content":"","iconType":null,"iconUrl":"file:///synthetic"}]})";
}
inline QJsonObject searchAssistObject(const QByteArray& body)
{
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(body, &error);
    Q_ASSERT(error.error == QJsonParseError::NoError && doc.isObject());
    return doc.object();
}
} // namespace cirrus::test
