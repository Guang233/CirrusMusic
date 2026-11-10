#pragma once
// Response shapes measured 2026-11-10 (docs/api/user-profile-reads.md); values synthetic.
namespace cirrus::test
{
inline const char* detailJson = R"json({"code":200,"level":10,"peopleCanSeeMyPlayRecord":false,
  "profile":{"userId":37179178,"nickname":"N","avatarUrl":"https://p1.music.126.net/a.jpg",
  "signature":"","follows":171,"followeds":7540967,"playlistCount":2,"followed":false,
  "artistId":1007170}})json";
inline const char* fansJson = R"json({"code":200,"more":true,"size":99,"followeds":[
  {"userId":17768923204,"nickname":"A","avatarUrl":"https://p1.music.126.net/b.jpg",
   "signature":null,"followed":false,"time":1785239795550},
  {"userId":1856635330,"nickname":"B","avatarUrl":null,"signature":"s","followed":true,
   "time":1780910723689}]})json";
inline const char* followsJson = R"json({"code":200,"more":false,"touchCount":28,"follow":[
  {"userId":7864598263,"nickname":"C","avatarUrl":"https://p1.music.126.net/c.jpg",
   "signature":"","followed":false,"time":0}]})json";
inline const char* playlistsJson = R"json({"code":200,"more":false,"playlist":[
  {"id":321479230,"name":"P","coverImgUrl":"https://p1.music.126.net/d.jpg","trackCount":9,
   "subscribed":null,"specialType":5,"creator":{"userId":37179178}},
  {"id":321479231,"name":"Q","trackCount":3,"subscribed":true,"specialType":0,
   "creator":{"userId":5}}]})json";
inline const char* rankingJson = R"json({"code":200,"weekData":[{"playCount":0,"score":100,
  "song":{"id":2144694164,"name":"S","ar":[{"id":1,"name":"R"}],"al":{"id":2,"name":"L"},
  "dt":162653}}]})json";
inline const char* hiddenListJson = R"json({"code":400,"message":"x"})json";
inline const char* hiddenRankingJson = R"json({"code":-2,"msg":"x"})json";
inline const char* missingJson = R"json({"code":404})json";
} // namespace cirrus::test
