#include "ruby.h"

#include <QHash>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace cirrus::ui
{
namespace
{

// --- Characters ---------------------------------------------------------------------

bool isHiragana(char32_t c) { return c >= 0x3041 && c <= 0x3096; }
bool isKatakana(char32_t c) { return (c >= 0x30a1 && c <= 0x30fa) || c == 0x30fc; }
bool isKana(char32_t c) { return isHiragana(c) || isKatakana(c); }
bool isKanji(char32_t c)
{
    return (c >= 0x4e00 && c <= 0x9fff) || (c >= 0x3400 && c <= 0x4dbf) ||
           (c >= 0x20000 && c <= 0x2ffff) || c == 0x3005 /* 々 */ || c == 0x3006 /* 〆 */ ||
           c == 0x30f6 /* ヶ */ || c == 0x30f5 /* ヵ */;
}
bool isLatin(char32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           (c >= 0xff10 && c <= 0xff19) || (c >= 0xff21 && c <= 0xff3a) ||
           (c >= 0xff41 && c <= 0xff5a);
}
bool isOpenBracket(char32_t c) { return c == '(' || c == 0xff08; }
bool isCloseBracket(char32_t c) { return c == ')' || c == 0xff09; }

// Katakana to hiragana (ー stays).
char32_t toHiragana(char32_t c) { return c >= 0x30a1 && c <= 0x30f6 ? c - 0x60 : c; }

bool isSmallYa(char32_t h) { return h == U'ゃ' || h == U'ゅ' || h == U'ょ'; }
bool isSmallVowel(char32_t h)
{
    return h == U'ぁ' || h == U'ぃ' || h == U'ぅ' || h == U'ぇ' || h == U'ぉ' || h == U'ゎ';
}
bool isSmall(char32_t h) { return isSmallYa(h) || isSmallVowel(h); }
bool isVowel(QChar c) { return QStringLiteral("aiueo").contains(c); }

// --- Readings -----------------------------------------------------------------------

// Hepburn first, then the other ways romanizations write it (particles both ways).
const QHash<char32_t, QStringList>& kanaTable()
{
    static const QHash<char32_t, QStringList> table = [] {
        QHash<char32_t, QStringList> t;
        const auto add = [&t](char32_t kana, const char* readings) {
            t.insert(kana, QString::fromLatin1(readings).split(u'|'));
        };
        add(U'あ', "a"), add(U'い', "i"), add(U'う', "u"), add(U'え', "e"), add(U'お', "o");
        add(U'か', "ka"), add(U'き', "ki"), add(U'く', "ku"), add(U'け', "ke"), add(U'こ', "ko");
        add(U'が', "ga"), add(U'ぎ', "gi"), add(U'ぐ', "gu"), add(U'げ', "ge"), add(U'ご', "go");
        add(U'さ', "sa"), add(U'し', "shi|si"), add(U'す', "su"), add(U'せ', "se");
        add(U'そ', "so"), add(U'ざ', "za"), add(U'じ', "ji|zi"), add(U'ず', "zu");
        add(U'ぜ', "ze"), add(U'ぞ', "zo"), add(U'た', "ta"), add(U'ち', "chi|ti");
        add(U'つ', "tsu|tu"), add(U'て', "te"), add(U'と', "to"), add(U'だ', "da");
        add(U'ぢ', "ji|di|zi"), add(U'づ', "zu|du"), add(U'で', "de"), add(U'ど', "do");
        add(U'な', "na"), add(U'に', "ni"), add(U'ぬ', "nu"), add(U'ね', "ne"), add(U'の', "no");
        add(U'は', "ha|wa"), add(U'ひ', "hi"), add(U'ふ', "fu|hu"), add(U'へ', "he|e");
        add(U'ほ', "ho"), add(U'ば', "ba"), add(U'び', "bi"), add(U'ぶ', "bu"), add(U'べ', "be");
        add(U'ぼ', "bo"), add(U'ぱ', "pa"), add(U'ぴ', "pi"), add(U'ぷ', "pu"), add(U'ぺ', "pe");
        add(U'ぽ', "po"), add(U'ま', "ma"), add(U'み', "mi"), add(U'む', "mu"), add(U'め', "me");
        add(U'も', "mo"), add(U'や', "ya"), add(U'ゆ', "yu"), add(U'よ', "yo"), add(U'ら', "ra");
        add(U'り', "ri"), add(U'る', "ru"), add(U'れ', "re"), add(U'ろ', "ro"), add(U'わ', "wa");
        add(U'ゐ', "wi|i"), add(U'ゑ', "we|e"), add(U'を', "wo|o"), add(U'ゔ', "vu|bu");
        add(U'ん', "n|nn|m");
        add(U'ぁ', "a"), add(U'ぃ', "i"), add(U'ぅ', "u"), add(U'ぇ', "e"), add(U'ぉ', "o");
        add(U'ゃ', "ya"), add(U'ゅ', "yu"), add(U'ょ', "yo"), add(U'ゎ', "wa");
        return t;
    }();
    return table;
}

QString stripVowel(const QString& reading)
{
    return !reading.isEmpty() && isVowel(reading.back()) ? reading.chopped(1) : reading;
}

void addUnique(QStringList& list, const QString& item)
{
    if (!list.contains(item))
        list << item;
}

// Readings of a base kana with an optional small one after it (きょ, ティ).
QStringList syllableReadings(char32_t base, char32_t small)
{
    const QStringList baseReadings = kanaTable().value(base);
    if (!small)
        return baseReadings;
    const QString vowel = kanaTable().value(small).constFirst().right(1);
    QStringList readings;
    if (isSmallYa(small)) {
        if (base == U'し') {
            readings << "sh" + vowel << "sy" + vowel;
        } else if (base == U'じ') {
            readings << "j" + vowel << "jy" + vowel << "zy" + vowel;
        } else if (base == U'ち') {
            readings << "ch" + vowel << "ty" + vowel << "cy" + vowel;
        } else if (base == U'ぢ') {
            readings << "j" + vowel << "dy" + vowel;
        } else {
            for (const QString& reading : baseReadings)
                addUnique(readings, stripVowel(reading) + "y" + vowel);
        }
        return readings;
    }
    // Small vowels make foreign sounds: ティ ti/thi, ファ fa, ウィ wi.
    if (base == U'て' || base == U'で')
        readings << stripVowel(baseReadings.constFirst()) + vowel
                 << stripVowel(baseReadings.constFirst()) + "h" + vowel;
    if (base == U'う')
        readings << "w" + vowel << "u" + vowel;
    for (const QString& reading : baseReadings)
        addUnique(readings, stripVowel(reading) + vowel);
    for (const QString& reading : baseReadings)
        addUnique(readings, reading + vowel);
    return readings;
}

// Doubles the first consonant for a preceding っ (kitte, matcha / maccha).
QStringList geminated(const QStringList& readings)
{
    QStringList result;
    for (const QString& reading : readings) {
        if (reading.isEmpty() || isVowel(reading.front()) || reading.front() == u'n')
            continue;
        if (reading.startsWith(u"ch"))
            addUnique(result, "t" + reading);
        addUnique(result, reading.front() + reading);
    }
    return result;
}

// --- Units --------------------------------------------------------------------------

enum class Kind { Kana, Kanji, Latin };

// A unit the romanization is matched against.
struct MatchUnit {
    Kind kind;
    QStringList readings; // Kana: possible readings; Latin: the exact text
    int kanji = 0;        // Kanji: number of characters
    int display = 0;      // index of the display unit it belongs to
};

struct DisplayUnit {
    QString text;
    int from = 0;
    int to = 0;
    bool annotated = true; // false: no ruby (punctuation, spaces, English)
};

struct Tokens {
    QList<MatchUnit> match;
    QList<DisplayUnit> display;
};

// Code points of `text` with their UTF-16 offsets.
struct CodePoint {
    char32_t c;
    int at;
};
std::vector<CodePoint> codePoints(const QString& text)
{
    std::vector<CodePoint> points;
    for (int i = 0; i < text.size();) {
        char32_t c = text.at(i).unicode();
        int length = 1;
        if (text.at(i).isHighSurrogate() && i + 1 < text.size() &&
            text.at(i + 1).isLowSurrogate()) {
            c = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            length = 2;
        }
        points.push_back({c, i});
        i += length;
    }
    return points;
}

// The last vowel a kana unit's first reading ends with, for long vowels after it.
QChar lastVowel(const QList<MatchUnit>& units)
{
    if (units.isEmpty() || units.back().kind != Kind::Kana || units.back().readings.isEmpty())
        return {};
    const QString& reading = units.back().readings.constFirst();
    return !reading.isEmpty() && isVowel(reading.back()) ? reading.back() : QChar();
}

// Splits kana from position `p` into match units (each with its own display unit
// unless `display` >= 0) and returns the position after them.
size_t addKana(const std::vector<CodePoint>& points, size_t p, const QString& line, Tokens& tokens,
               int display)
{
    const auto at = [&](size_t i) { return i < points.size() ? toHiragana(points[i].c) : 0; };
    const auto end = [&](size_t i) { return i < points.size() ? points[i].at : int(line.size()); };
    const size_t start = p;
    const char32_t c = at(p);
    QStringList readings;
    const QChar previous = lastVowel(tokens.match);

    if (c == U'っ') {
        const char32_t next = at(p + 1);
        if (isKana(next) && next != U'っ' && next != U'ー' && !isSmall(next)) {
            const char32_t small = isSmall(at(p + 2)) ? at(p + 2) : 0;
            readings = geminated(syllableReadings(next, small));
            p += small ? 3 : 2;
        } else {
            readings = {QString(), QStringLiteral("t"), QStringLiteral("tsu")};
            p += 1;
        }
    } else if (c == U'ー') {
        if (!previous.isNull()) {
            readings = {previous, QString()};
            if (previous == u'o')
                readings << QStringLiteral("u");
            else if (previous == u'e')
                readings << QStringLiteral("i");
        } else {
            readings = {QString(), "a", "i", "u", "e", "o"};
        }
        p += 1;
    } else {
        const char32_t small = isSmall(at(p + 1)) && !isSmall(c) ? at(p + 1) : 0;
        readings = syllableReadings(c, small);
        p += small ? 2 : 1;
        // Long vowels: とう tou/to, とお too/tou/to, せい sei/see/se.
        if (!small && !previous.isNull()) {
            if (c == U'う' && (previous == u'o' || previous == u'u'))
                addUnique(readings, QString());
            if (c == U'お' && previous == u'o')
                addUnique(readings, "u"), addUnique(readings, QString());
            if (c == U'い' && previous == u'e')
                addUnique(readings, "e"), addUnique(readings, QString());
            if (c == U'え' && previous == u'e')
                addUnique(readings, QString());
            if (c == U'あ' && previous == u'a')
                addUnique(readings, QString());
        }
    }
    if (readings.isEmpty())
        readings = {QString()};

    if (display < 0) {
        display = int(tokens.display.size());
        tokens.display.push_back({line.mid(points[start].at, end(p) - points[start].at),
                                  points[start].at, end(p), true});
    }
    tokens.match.push_back({Kind::Kana, readings, 0, display});
    return p;
}

Tokens tokenize(const QString& line)
{
    Tokens tokens;
    const std::vector<CodePoint> points = codePoints(line);
    const auto end = [&](size_t i) { return i < points.size() ? points[i].at : int(line.size()); };
    size_t p = 0;
    while (p < points.size()) {
        const char32_t c = points[p].c;
        if (isKanji(c)) {
            size_t q = p;
            while (q < points.size() && isKanji(points[q].c))
                ++q;
            const int from = points[p].at;
            const QString kanji = line.mid(from, end(q) - from);
            // A reading in brackets right after it: 運命(さだめ).
            if (q < points.size() && isOpenBracket(points[q].c)) {
                size_t r = q + 1;
                while (r < points.size() && isKana(points[r].c))
                    ++r;
                if (r > q + 1 && r < points.size() && isCloseBracket(points[r].c)) {
                    const int display = int(tokens.display.size());
                    tokens.display.push_back({kanji, from, end(r + 1), true});
                    for (size_t k = q + 1; k < r;)
                        k = addKana(points, k, line, tokens, display);
                    p = r + 1;
                    continue;
                }
            }
            tokens.display.push_back({kanji, from, end(q), true});
            tokens.match.push_back({Kind::Kanji, {}, int(q - p), int(tokens.display.size()) - 1});
            p = q;
        } else if (isKana(c)) {
            p = addKana(points, p, line, tokens, -1);
        } else if (isLatin(c)) {
            size_t q = p;
            while (q < points.size() && isLatin(points[q].c))
                ++q;
            const int from = points[p].at;
            const QString text = line.mid(from, end(q) - from);
            tokens.display.push_back({text, from, end(q), false});
            tokens.match.push_back({Kind::Latin,
                                    {text.normalized(QString::NormalizationForm_KC).toLower()},
                                    0,
                                    int(tokens.display.size()) - 1});
            p = q;
        } else {
            // Punctuation and spaces: shown, not matched; consecutive ones together.
            const int from = points[p].at;
            if (!tokens.display.isEmpty() && !tokens.display.back().annotated &&
                tokens.display.back().to == from &&
                (tokens.match.isEmpty() ||
                 tokens.match.back().display != int(tokens.display.size()) - 1)) {
                tokens.display.back().to = end(p + 1);
                tokens.display.back().text =
                    line.mid(tokens.display.back().from, end(p + 1) - tokens.display.back().from);
            } else {
                tokens.display.push_back(
                    {line.mid(from, end(p + 1) - from), from, end(p + 1), false});
            }
            ++p;
        }
    }
    return tokens;
}

// --- Romanization -------------------------------------------------------------------

// Lowercase letters and digits only; vowels with a macron or circumflex doubled
// (ō → ou, ā → aa). `map` gives each character's index in the original.
struct Normalized {
    QString text;
    QList<int> map;
};
Normalized normalizeRomaji(const QString& romanization)
{
    Normalized result;
    for (int i = 0; i < romanization.size(); ++i) {
        const QString decomposed =
            QString(romanization.at(i)).normalized(QString::NormalizationForm_KD);
        if (decomposed.isEmpty())
            continue;
        const QChar base = decomposed.at(0).toLower();
        if (!((base >= u'a' && base <= u'z') || (base >= u'0' && base <= u'9')))
            continue;
        result.text += base;
        result.map << i;
        const bool longMark = decomposed.size() > 1 && (decomposed.at(1).unicode() == 0x0304 ||
                                                        decomposed.at(1).unicode() == 0x0302);
        if (longMark && isVowel(base)) {
            result.text += base == u'o' ? QChar(u'u') : base;
            result.map << i;
        }
    }
    return result;
}

} // namespace

bool containsKana(const QString& text)
{
    for (const CodePoint& point : codePoints(text)) {
        if (isKana(point.c) && point.c != 0x30fc)
            return true;
    }
    return false;
}

std::optional<QList<RubyUnit>> alignRuby(const QString& line, const QString& romanization)
{
    const Tokens tokens = tokenize(line);
    const Normalized romaji = normalizeRomaji(romanization);
    const int n = int(tokens.match.size());
    const int m = int(romaji.text.size());
    if (n == 0 || m == 0)
        return std::nullopt;

    // cost[i][j]: the best way to give the first i units the first j letters. A kanji
    // run of k characters may take 1..8k letters and costs its distance from about 3.5k
    // (two morae each); a kana's later readings cost a little more than its first, and
    // reading nothing (a long vowel left out) much more.
    constexpr double kInf = std::numeric_limits<double>::infinity();
    std::vector<std::vector<double>> cost(n + 1, std::vector<double>(m + 1, kInf));
    std::vector<std::vector<int>> from(n + 1, std::vector<int>(m + 1, -1));
    cost[0][0] = 0;
    for (int i = 0; i < n; ++i) {
        const MatchUnit& unit = tokens.match[i];
        for (int j = 0; j <= m; ++j) {
            if (cost[i][j] == kInf)
                continue;
            const auto relax = [&](int length, double extra) {
                if (j + length > m)
                    return;
                const double total = cost[i][j] + extra;
                if (total < cost[i + 1][j + length]) {
                    cost[i + 1][j + length] = total;
                    from[i + 1][j + length] = j;
                }
            };
            if (unit.kind == Kind::Kanji) {
                for (int length = 1; length <= 8 * unit.kanji && j + length <= m; ++length)
                    relax(length, std::abs(length - 3.5 * unit.kanji) * 0.1);
            } else {
                for (int r = 0; r < unit.readings.size(); ++r) {
                    const QString& reading = unit.readings[r];
                    // A dropped long vowel only when nothing else fits.
                    if (QStringView(romaji.text).mid(j).startsWith(reading))
                        relax(int(reading.size()), reading.isEmpty() ? 0.3 : r * 0.01);
                }
            }
        }
    }
    if (cost[n][m] == kInf)
        return std::nullopt;

    // Letters [start, end) of the romaji per match unit, then per display unit.
    std::vector<int> starts(n + 1);
    starts[n] = m;
    for (int i = n, j = m; i > 0; --i) {
        j = from[i][j];
        starts[i - 1] = j;
    }
    QList<RubyUnit> result;
    int used = 0; // original characters already given to a unit
    for (int d = 0; d < tokens.display.size(); ++d) {
        const DisplayUnit& unit = tokens.display[d];
        RubyUnit ruby{unit.text, QString(), unit.from, unit.to};
        int first = -1, last = -1;
        for (int i = 0; i < n; ++i) {
            if (tokens.match[i].display == d) {
                if (first < 0)
                    first = i;
                last = i;
            }
        }
        if (first >= 0 && starts[last + 1] > starts[first]) {
            const int begin = std::max(romaji.map[starts[first]], used);
            const int end = romaji.map[starts[last + 1] - 1] + 1;
            if (unit.annotated && end > begin)
                ruby.ruby = romanization.mid(begin, end - begin).trimmed();
            used = std::max(used, end);
        }
        result << ruby;
    }
    return result;
}

std::optional<QList<RubyUnit>> alignRubyByTime(const QString& line, const QList<TimedText>& words,
                                               const QList<TimedText>& romaji)
{
    if (words.isEmpty() || romaji.isEmpty())
        return std::nullopt;
    // Where each word is in the line.
    QList<int> offsets;
    int cursor = 0;
    for (const TimedText& word : words) {
        const int at = line.indexOf(word.text, cursor);
        if (at < 0)
            return std::nullopt;
        offsets << at;
        cursor = at + int(word.text.size());
    }
    // Words joined by a syllable sung over several of them form one group.
    QList<int> group(words.size());
    for (int i = 0; i < group.size(); ++i)
        group[i] = i;
    QList<QList<int>> sungOver; // per syllable, the words it overlaps
    for (const TimedText& syllable : romaji) {
        QList<int> over;
        for (int i = 0; i < words.size(); ++i) {
            const qint64 overlap = std::min(syllable.endMs, words[i].endMs) -
                                   std::max(syllable.startMs, words[i].startMs);
            if (overlap > 0)
                over << i;
        }
        for (int i : over)
            group[i] = group[over.constFirst()];
        sungOver << over;
    }
    QList<RubyUnit> result;
    int position = 0;
    for (int i = 0; i < words.size();) {
        int j = i;
        while (j + 1 < words.size() && group[j + 1] == group[i])
            ++j;
        if (offsets[i] > position)
            result << RubyUnit{line.mid(position, offsets[i] - position), QString(), position,
                               offsets[i]};
        const int end = offsets[j] + int(words[j].text.size());
        QString ruby;
        for (int s = 0; s < romaji.size(); ++s) {
            if (!sungOver[s].isEmpty() && sungOver[s].constFirst() >= i &&
                sungOver[s].constFirst() <= j)
                ruby += romaji[s].text.trimmed();
        }
        const QString text = line.mid(offsets[i], end - offsets[i]);
        bool needsRuby = false;
        for (const CodePoint& point : codePoints(text))
            needsRuby = needsRuby || isKana(point.c) || isKanji(point.c);
        if (needsRuby && ruby.isEmpty())
            return std::nullopt;
        result << RubyUnit{text, ruby, offsets[i], end};
        position = end;
        i = j + 1;
    }
    if (position < line.size())
        result << RubyUnit{line.mid(position), QString(), position, int(line.size())};
    return result;
}

} // namespace cirrus::ui
