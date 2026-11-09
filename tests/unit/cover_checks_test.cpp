// The cover checks of CoverFetcher (docs/tasks/resource-cache-backend.md): what the
// image hosts send is kept as it is once it passes them; a cache hit only gets the
// header check.
#include "infrastructure/cache/cover_fetcher.h"

#include <QBuffer>
#include <QImage>
#include <QTest>

using cirrus::cache::CoverFetcher;

namespace
{
QByteArray encoded(QSize size, const char* format)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor(40, 90, 160));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format);
    return bytes;
}
} // namespace

class CoverChecksTest : public QObject
{
    Q_OBJECT
  private slots:
    void acceptsCovers()
    {
        for (const char* format : {"JPEG", "PNG"}) {
            const auto bytes = encoded({512, 512}, format);
            QCOMPARE(CoverFetcher::rejection(bytes), nullptr);
            QVERIFY(CoverFetcher::plausible(bytes));
        }
        // Not square, at the most.
        QCOMPARE(CoverFetcher::rejection(encoded({2048, 1024}, "JPEG")), nullptr);
    }
    void rejects()
    {
        QCOMPARE(QByteArray(CoverFetcher::rejection({})), QByteArray("bytes"));
        QCOMPARE(QByteArray(CoverFetcher::rejection("not an image at all")), QByteArray("format"));
        const auto wide = encoded({2049, 16}, "PNG");
        QCOMPARE(QByteArray(CoverFetcher::rejection(wide)), QByteArray("dimensions"));
        QVERIFY(!CoverFetcher::plausible(wide));
        QVERIFY(!CoverFetcher::plausible("not an image at all"));
        // A format outside the three, even a valid image.
        QCOMPARE(QByteArray(CoverFetcher::rejection(encoded({64, 64}, "BMP"))),
                 QByteArray("format"));
    }
    void rejectsBrokenData()
    {
        // A PNG header with its data cut away: the header passes, decoding does not.
        auto bytes = encoded({256, 256}, "PNG");
        bytes.truncate(64);
        QVERIFY(CoverFetcher::plausible(bytes));
        QCOMPARE(QByteArray(CoverFetcher::rejection(bytes)), QByteArray("decode"));
    }
};

QTEST_GUILESS_MAIN(CoverChecksTest)
#include "cover_checks_test.moc"
