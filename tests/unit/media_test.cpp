#include "infrastructure/media/media_source_policy.h"
#include <QtTest>
using namespace cirrus;

// The CDN address rules every audio download goes through (AudioStore; its redirect and
// header rules are in tst_audio_store).
class MediaTest : public QObject
{
    Q_OBJECT
  private slots:
    void sourcePolicy_data()
    {
        QTest::addColumn<QString>("url");
        for (const auto* url :
             {"file:///etc/passwd", "http://m802.music.126.net/a", "https://localhost/a",
              "https://m802.music.126.net.evil.example/a", "https://evil.example/music.126.net/a",
              "https://user:password@m802.music.126.net/a", "https://m802.music.126.net:8443/a",
              "https://m802.music.126.net/a#fragment", "https://127.0.0.1/a",
              "https://m802.music.126.net/"})
            QTest::newRow(url) << QString::fromLatin1(url);
    }
    void sourcePolicy()
    {
        QFETCH(QString, url);
        QVERIFY(!media::approvedMediaUrl(QUrl(url)));
    }
};
QTEST_GUILESS_MAIN(MediaTest)
#include "media_test.moc"
