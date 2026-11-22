#include "ruby.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace cirrus::ui;

namespace
{
// "text:ruby" per unit, units without ruby as their text alone, joined by "|".
QString shape(const std::optional<QList<RubyUnit>>& units)
{
    if (!units)
        return QStringLiteral("<none>");
    QStringList parts;
    for (const RubyUnit& unit : *units)
        parts << (unit.ruby.isEmpty() ? unit.text : unit.text + u':' + unit.ruby);
    return parts.join(u'|');
}
} // namespace

// Synthetic lines in the shapes NetEase's lyrics and romanizations have
// (docs/ui/pages/player.md §6.3).
class RubyTest : public QObject
{
    Q_OBJECT
  private slots:
    void readings()
    {
        QFETCH(QString, line);
        QFETCH(QString, romaji);
        QFETCH(QString, expected);
        QCOMPARE(shape(alignRuby(line, romaji)), expected);
    }
    void readings_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("romaji");
        QTest::addColumn<QString>("expected");
        // A reading after a kanji run, half- and full-width brackets.
        QTest::newRow("kanji") << "涙が溢(こぼ)れた" << "na mi da ga ko bo re ta"
                               << "涙:na mi da|が:ga|溢:ko bo|れ:re|た:ta";
        QTest::newRow("full-width") << "夢を描（えが）く" << "yu me wo e ga ku"
                                    << "夢:yu me|を:wo|描:e ga|く:ku";
        // The reading covers the kana after the kanji too.
        QTest::newRow("okurigana") << "強く抱き(だき)しめて" << "tsu yo ku da ki shi me te"
                                   << "強:tsu yo|く:ku|抱き:da ki|し:shi|め:me|て:te";
        QTest::newRow("line start") << "抉り(えぐり)抜く" << "e gu ri nu ku"
                                    << "抉り:e gu ri|抜:nu|く:ku";
        // The romanization writes the words before the last kanji twice: as guessed, then
        // in the reading that the generator gives the last kanji run alone.
        QTest::newRow("generator")
            << "記憶の彼方(きおくのかなた)" << "ki o ku no ki o ku no ka na ta"
            << "記憶:ki o ku|の:no|彼方:ki o ku no ka na ta";
        QTest::newRow("gaps") << "六感・錠(ろっかん・じょう)" << "ro ku ka n . ro kka n . jo u"
                              << "六感:ro ku ka n|・|錠:ro kka n . jo u";
        // Not in the romanization: an echo, English, katakana without kanji.
        QTest::newRow("echo") << "君を待つ（君を待つ）" << "ki mi wo ma tsu"
                              << "君:ki mi|を:wo|待:ma|つ:tsu|（|君|を|待|つ|）";
        QTest::newRow("english") << "空を見て (look at the sky)" << "so ra wo mi te"
                                 << "空:so ra|を:wo|見:mi|て:te| |(|look| |at| |the| |sky|)";
        QTest::newRow("katakana echo") << "ベイビー（ベイビー）" << "be i bii"
                                       << "ベ:be|イ:i|ビ:bi|ー:i|（|ベ|イ|ビ|ー|）";
        // Sung: the romanization has it.
        QTest::newRow("sung") << "ずっと（ずっと）" << "zu tto zu tto"
                              << "ず:zu|っと:tto|（|ず:zu|っと:tto|）";
        QTest::newRow("sung reading-like") << "ラ(ラ)" << "ra ra" << "ラ:ra|(|ラ:ra|)";
        // Brackets do not excuse a word missing outside them.
        QTest::newRow("missing") << "君を待つ（ずっと）" << "ki mi ma tsu" << "<none>";
    }
    void keepsTheRange()
    {
        const auto units = alignRuby(QStringLiteral("溢(こぼ)れ"), QStringLiteral("kobore"));
        QVERIFY(units);
        QCOMPARE(units->size(), 2);
        QCOMPARE(units->at(0).from, 0);
        QCOMPARE(units->at(0).to, 5);
        QCOMPARE(units->at(1).from, 5);
    }
    void timedBrackets()
    {
        // An echo in brackets gets no syllable: the line still aligns.
        const QString line = QStringLiteral("夜に(夜に)");
        const QList<TimedText> words{{0, 500, QStringLiteral("夜")},
                                     {500, 1000, QStringLiteral("に")},
                                     {1000, 2000, QStringLiteral("(夜に)")}};
        const QList<TimedText> romaji{{0, 500, QStringLiteral("yo")},
                                      {500, 1000, QStringLiteral("ni")}};
        const auto units = alignRubyByTime(line, words, romaji);
        QCOMPARE(shape(units), "夜:yo|に:ni|(夜に)");
        // Outside brackets a word still needs one.
        QVERIFY(!alignRubyByTime(line, words, {{0, 500, QStringLiteral("yo")}}));
    }
    void sampleSong()
    {
        // The invented sample (ui/samples/lyrics/so-031.json): every romanized line but the last,
        // which leaves out a word on purpose (player.md §6.3).
        QFile file(QStringLiteral(CIRRUS_SAMPLES_DIR "/lyrics/so-031.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        int aligned = 0, total = 0;
        for (const QJsonValue& value : QJsonDocument::fromJson(file.readAll())["lines"].toArray()) {
            const auto line = value.toObject();
            const QString romaji = line["romanization"].toString();
            if (romaji.isEmpty())
                continue;
            ++total;
            aligned += alignRuby(line["text"].toString(), romaji) ? 1 : 0;
        }
        QCOMPARE(total, 10);
        QCOMPARE(aligned, 9);
    }
};

QTEST_MAIN(RubyTest)
#include "ruby_test.moc"
