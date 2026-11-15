// The cover palette's black-and-white rule (ui/theme/cover_palette.cpp,
// docs/ui/pages/player.md §3.1): a cover with little real color gets a neutral seed,
// even with a colorful patch or a tint MD3's scoring would pick. And white or gray
// areas don't vote for a hue in the scoring (they are all hue ~209, cyan, in HCT).
#include "cover_palette.h"

#include "cpp/cam/hct.h"

#include <QImage>
#include <QPainter>
#include <QTest>

using cirrus::ui::CoverPalette;
using cirrus::ui::extractCoverPalette;
namespace mcu = material_color_utilities;

namespace
{
// A black and white picture: a diagonal gradient with some blocks.
QImage grayscale(QRgb tint = 0)
{
    QImage image(128, 128, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const int v = (x + y) * 255 / 254;
            image.setPixel(x, y, qRgb(v, v, v));
        }
    QPainter painter(&image);
    painter.fillRect(10, 10, 40, 30, Qt::black);
    painter.fillRect(70, 80, 40, 30, Qt::white);
    if (tint) { // a slight overall cast, as a scan or a print has
        painter.setOpacity(0.08);
        painter.fillRect(image.rect(), QColor::fromRgb(tint));
    }
    return image;
}
double chroma(const QColor& color) { return mcu::Hct(color.rgba() | 0xff000000u).get_chroma(); }
double hue(const QColor& color) { return mcu::Hct(color.rgba() | 0xff000000u).get_hue(); }
} // namespace

class CoverPaletteTest : public QObject
{
    Q_OBJECT
  private slots:
    void blackAndWhite()
    {
        const auto plain = extractCoverPalette(grayscale());
        QVERIFY(plain.valid);
        QVERIFY(plain.monochrome);
        QVERIFY(chroma(plain.seed) < 5);
    }
    void smallColorfulPatch()
    {
        QImage image = grayscale();
        QPainter painter(&image);
        painter.fillRect(100, 0, 20, 20, QColor(30, 200, 60)); // about 2.4%
        painter.end();
        const auto palette = extractCoverPalette(image);
        QVERIFY(palette.monochrome);
        QVERIFY(chroma(palette.seed) < 5);
    }
    void tintedGray()
    {
        const auto palette = extractCoverPalette(grayscale(qRgb(40, 160, 60)));
        QVERIFY(palette.monochrome);
        QVERIFY(chroma(palette.seed) < 5);
    }
    void colorful()
    {
        QImage image(128, 128, QImage::Format_ARGB32);
        image.fill(QColor(30, 80, 200));
        QPainter painter(&image);
        painter.fillRect(0, 64, 128, 64, QColor(230, 120, 30));
        painter.end();
        const auto palette = extractCoverPalette(image);
        QVERIFY(!palette.monochrome);
        QVERIFY(chroma(palette.seed) >= 15);
    }
    // A white cover with a red title and a little pale cyan (2026-11-12): the white
    // made the pale cyan win.
    void whiteBackgroundDoesNotVote()
    {
        QImage image(128, 128, QImage::Format_ARGB32);
        image.fill(QColor(254, 254, 254));
        QPainter painter(&image);
        painter.fillRect(0, 96, 128, 24, QColor(230, 84, 81));  // red, about 19%
        painter.fillRect(0, 0, 32, 12, QColor(160, 205, 207));  // pale cyan, about 2%
        painter.fillRect(96, 0, 32, 12, QColor(247, 229, 200)); // cream, about 2%
        painter.end();
        const auto palette = extractCoverPalette(image);
        QVERIFY(!palette.monochrome);
        const double h = hue(palette.seed);
        QVERIFY2(h < 45 || h > 340, qPrintable(QString::number(h)));
        QVERIFY(chroma(palette.seed) >= 40);
    }
    void blackAndWhiteWithARealColorArea()
    {
        QImage image = grayscale();
        QPainter painter(&image);
        painter.fillRect(0, 0, 64, 64, QColor(200, 40, 40)); // a quarter
        painter.end();
        const auto palette = extractCoverPalette(image);
        QVERIFY(!palette.monochrome);
        QVERIFY(chroma(palette.seed) >= 15);
    }
};

QTEST_GUILESS_MAIN(CoverPaletteTest)
#include "cover_palette_test.moc"
