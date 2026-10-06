#pragma once
#include <QByteArray>
namespace cirrus::test
{
inline QByteArray dailyPlaylistsFixture()
{
    // Pure synthetic metadata, no real catalogue/account/creator/URL or file access.
    return R"({"code":200,"recommend":[{"id":"9007199254740993","name":"  <b>synthetic daily</b>  ","copywriter":"synthetic reason","picUrl":"https://metadata.invalid/cover","playcount":12.5,"playCount":999,"trackCount":0,"updateTime":0},{"id":2,"name":"","copywriter":null,"picUrl":"file:///synthetic"},{"id":"9007199254740993","name":"synthetic duplicate","copywriter":"","playcount":0}]})";
}
} // namespace cirrus::test
