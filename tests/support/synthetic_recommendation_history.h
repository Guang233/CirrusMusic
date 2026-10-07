#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace cirrus::test
{
inline QByteArray recommendationDatesFixture()
{
    const QJsonArray dates{QStringLiteral("2020-06-21"), QStringLiteral("2024-02-29"),
                           QStringLiteral("2020-06-21")};
    return QJsonDocument(QJsonObject{{"code", 200},
                                     {"data", QJsonObject{{"dates", dates},
                                                          {"purchaseUrl", "ignored"},
                                                          {"status", 0}}}})
        .toJson(QJsonDocument::Compact);
}
inline QByteArray recommendationHistoryFixture()
{
    return R"({"code":200,"data":{"songs":[{"id":"9007199254740993","name":"  <b>synthetic history</b>  ","ar":[],"al":{},"dt":0},{"id":2,"name":"synthetic second"},{"id":"9007199254740993","name":"synthetic repeated"}]}})";
}
} // namespace cirrus::test
