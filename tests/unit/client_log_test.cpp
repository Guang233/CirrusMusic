#include "infrastructure/netease/protocol/client_log.h"
#include <QFile>
#include <QJsonObject>
#include <QtTest>

using namespace cirrus::netease;

namespace
{
// Deterministic "random" bytes 0, 1, 2… across calls.
RandomBytes counting()
{
    auto next = std::make_shared<int>(0);
    return [next](qsizetype n) -> std::optional<QByteArray> {
        QByteArray out;
        for (qsizetype i = 0; i < n; ++i)
            out.append(char((*next)++ & 0xff));
        return out;
    };
}
quint32 le(const QByteArray& b, int at, int size)
{
    quint32 v = 0;
    for (int i = size - 1; i >= 0; --i)
        v = (v << 8) | quint8(b[at + i]);
    return v;
}
} // namespace

class ClientLogTest : public QObject
{
    Q_OBJECT
  private slots:
    void chacha20Vector()
    {
        // RFC 8439 §2.4.2.
        QByteArray key;
        for (int i = 0; i < 32; ++i)
            key.append(char(i));
        const auto nonce = QByteArray::fromHex("000000000000004a00000000");
        const QByteArray plain = "Ladies and Gentlemen of the class of '99: If I could offer you "
                                 "only one tip for the future, sunscreen would be it.";
        const auto cipher = chacha20(key, 1, nonce, plain);
        QVERIFY(cipher);
        QCOMPARE(cipher->left(16).toHex(), "6e2e359a2568f98041ba0728dd0d6981");
        QCOMPARE(*chacha20(key, 1, nonce, *cipher), plain);
        QVERIFY(!chacha20(key.left(31), 1, nonce, plain));
    }
    void androidDevice()
    {
        const auto id = androidDeviceId("0123456789ABCDEF");
        QCOMPARE(id, androidDeviceId("0123456789ABCDEF")); // stable
        QVERIFY(id != androidDeviceId("0123456789ABCDEE"));
        const auto fields = QByteArray::fromBase64(QByteArray::fromPercentEncoding(id)).split('\t');
        QCOMPARE(fields.size(), 4);
        QCOMPARE(fields[0], "null");
        QCOMPARE(fields[1], "02:00:00:00:00:00");
        QCOMPARE(fields[2].size(), 16);
        QCOMPARE(fields[3].size(), 16);
        QCOMPARE(QByteArray::fromHex(fields[2]).toHex(), fields[2]);
    }
    void recordAndContainer()
    {
        const auto record = clientLogRecord(1794000000, "_pld", QJsonObject{{"id", "1"}});
        QCOMPARE(record, QByteArray("1794000000\x01_pld\x01{\"id\":\"1\"}\n"));
        const QByteArray meta = R"({"os":"pc"})";
        const auto sealed = sealNcbl(meta, record, counting());
        QVERIFY(sealed);
        const auto& b = *sealed;
        QCOMPARE(b.left(4), "NCBL");
        QCOMPARE(le(b, 4, 4), 3u);
        const auto headerLen = le(b, 8, 2);
        QCOMPARE(headerLen, 70u + 4u + quint32(meta.size()));
        QCOMPARE(quint8(b[10 + 6]) & 0xf0, 0x40); // UUID v4
        QCOMPARE(le(b, 70, 2), 0x4343u);
        QCOMPARE(le(b, 72, 2), quint32(meta.size()));
        QCOMPARE(le(b, 66, 4), quint32(b.size()) - headerLen); // the frames' length
        QCOMPARE(le(b, 58, 4), le(b, 62, 4));                  // one frame
        const auto upload = clientLogUpload(
            meta, {{"flush_ua_12345_1_7", record}, {"flush_bi_12345_2_8", record}}, counting());
        QVERIFY(upload.ok());
        QCOMPARE(upload.fileNames, (QList<QByteArray>{"flush_ua_12345_1_7", "flush_bi_12345_2_8"}));
        for (const auto& name : upload.fileNames)
            QVERIFY(upload.body.contains("filename=\"" + name +
                                         "\"\r\nContent-Type: multipart/form-data\r\n"
                                         "Content-Length: "));
        QVERIFY(!clientLogUpload(meta, {{"a\"b", record}}, counting()).ok());
        QVERIFY(upload.contentType.startsWith("multipart/form-data; boundary="));
        // For an independent check (tools: build/research-scripts/weblog/ncbl_check.py).
        if (const auto path = qEnvironmentVariable("CIRRUS_NCBL_SAMPLE"); !path.isEmpty()) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(b);
        }
    }
};
QTEST_GUILESS_MAIN(ClientLogTest)
#include "client_log_test.moc"
