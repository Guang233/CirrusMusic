// The update check's pure parts (ui/presentation/update_release.h): versions, GitHub's
// latest-release answer, the release page rule and the notes as plain text.
#include "update_release.h"

#include <QtTest>

using namespace cirrus::ui::updates;
using namespace Qt::StringLiterals;

class UpdateReleaseTest : public QObject
{
    Q_OBJECT

  private slots:
    void parsesVersions()
    {
        QCOMPARE(parseVersion(u"v0.1.2"), (std::array<int, 3>{0, 1, 2}));
        QCOMPARE(parseVersion(u"1.10.0"), (std::array<int, 3>{1, 10, 0}));
        QVERIFY(!parseVersion(u"v0.2.0-beta.1"));
        QVERIFY(!parseVersion(u"0.2"));
        QVERIFY(!parseVersion(u"0.2.0.1"));
        QVERIFY(!parseVersion(u"v0.+1.0"));
        QVERIFY(!parseVersion(u""));
    }

    void comparesVersions()
    {
        QVERIFY(isNewer(u"0.1.2", u"0.1.1"));
        QVERIFY(isNewer(u"v0.2.0", u"0.1.9"));
        QVERIFY(isNewer(u"0.10.0", u"0.9.0")); // numbers, not text
        QVERIFY(!isNewer(u"0.1.1", u"0.1.1"));
        QVERIFY(!isNewer(u"0.1.0", u"0.1.1"));
        QVERIFY(!isNewer(u"0.2.0-rc1", u"0.1.1"));
        QVERIFY(!isNewer(u"0.2.0", u"dev"));
    }

    void acceptsOnlyTheProjectsReleasePages()
    {
        QVERIFY(
            isReleasePage(QUrl(u"https://github.com/Guang233/CirrusMusic/releases/tag/v0.1.2"_s)));
        QVERIFY(
            !isReleasePage(QUrl(u"http://github.com/Guang233/CirrusMusic/releases/tag/v0.1.2"_s)));
        QVERIFY(
            !isReleasePage(QUrl(u"https://github.com/someone/CirrusMusic/releases/tag/v0.1.2"_s)));
        QVERIFY(!isReleasePage(
            QUrl(u"https://github.com.evil.example/Guang233/CirrusMusic/releases/x"_s)));
        QVERIFY(!isReleasePage(QUrl(u"https://user@github.com/Guang233/CirrusMusic/releases/x"_s)));
        QVERIFY(!isReleasePage(QUrl(u"https://github.com/Guang233/CirrusMusic/archive/x"_s)));
    }

    void parsesTheLatestRelease()
    {
        // Plain literals: moc does not see the class around multi-line raw strings.
        const QByteArray json =
            "{\"tag_name\": \"v0.1.2\", \"name\": \"Cirrus 0.1.2\", \"draft\": false,"
            " \"prerelease\": false,"
            " \"html_url\": \"https://github.com/Guang233/CirrusMusic/releases/tag/v0.1.2\","
            " \"published_at\": \"2026-11-20T08:00:00Z\","
            " \"body\": \"### 修复\\r\\n\\r\\n- 一处 **问题**。\\r\\n\"}";
        const auto release = parseRelease(json);
        QVERIFY(release);
        QCOMPARE(release->version, u"0.1.2"_s);
        QCOMPARE(release->title, u"Cirrus 0.1.2"_s);
        QCOMPARE(release->notes, u"修复\n\n• 一处 问题。"_s);
        QCOMPARE(release->published, QDateTime(QDate(2026, 11, 20), QTime(8, 0), QTimeZone::UTC));
    }

    void rejectsWhatIsNotARelease()
    {
        QVERIFY(!parseRelease("not json"));
        const char* page =
            "\"html_url\": \"https://github.com/Guang233/CirrusMusic/releases/tag/v0.2.0\"";
        QVERIFY(!parseRelease(QByteArray("{\"tag_name\": \"v0.2.0\", \"prerelease\": true, ") +
                              page + '}'));
        QVERIFY(
            !parseRelease(QByteArray("{\"tag_name\": \"v0.2.0\", \"draft\": true, ") + page + '}'));
        QVERIFY(!parseRelease(QByteArray("{\"tag_name\": \"nightly\", ") + page + '}'));
        QVERIFY(
            !parseRelease("{\"tag_name\": \"v0.2.0\", \"html_url\": \"https://example.com/x\"}"));
    }

    void writesTheNotesAsPlainText()
    {
        const QString markdown =
            u"## [0.2.0]\n\n### 界面\n\n- 新的 [播放页](https://x.example/a)。\n"
            "* `代码` 和 __强调__\n  - 子项<br>\n\n\n\n![图](https://x.example/i.png)\n"_s;
        QCOMPARE(notesToPlainText(markdown),
                 u"[0.2.0]\n\n界面\n\n• 新的 播放页。\n• 代码 和 强调\n  • 子项\n\n图"_s);
    }
};

QTEST_GUILESS_MAIN(UpdateReleaseTest)
#include "update_release_test.moc"
