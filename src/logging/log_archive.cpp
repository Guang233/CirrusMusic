#include "logger.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <zlib.h>

// A plain zip (PKWARE APPNOTE 6.3) of a few log files: deflated entries, UTF-8 names,
// no zip64 (the log's budget is far below 4 GiB). Used by the diagnostic package
// (docs/tasks/logging.md §7).
namespace cirrus::log
{
namespace
{
void put16(QByteArray& out, quint32 v)
{
    out += char(v & 0xff);
    out += char((v >> 8) & 0xff);
}
void put32(QByteArray& out, quint32 v)
{
    put16(out, v & 0xffff);
    put16(out, v >> 16);
}

std::optional<QByteArray> deflateRaw(const QByteArray& data)
{
    z_stream z{};
    if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return std::nullopt;
    QByteArray out(qsizetype(deflateBound(&z, uLong(data.size()))), Qt::Uninitialized);
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    z.avail_in = uInt(data.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = uInt(out.size());
    const int result = deflate(&z, Z_FINISH);
    const auto written = qsizetype(z.total_out);
    deflateEnd(&z);
    if (result != Z_STREAM_END)
        return std::nullopt;
    out.resize(written);
    return out;
}
} // namespace

bool writeArchive(const QString& target, const QStringList& files)
{
    struct Entry {
        QByteArray name;
        quint32 crc = 0, size = 0, packed = 0, offset = 0;
        quint16 time = 0, date = 0;
    };
    QSaveFile out(target);
    if (files.isEmpty() || !out.open(QIODevice::WriteOnly))
        return false;
    QList<Entry> entries;
    quint32 offset = 0;
    for (const QString& path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const QByteArray data = file.readAll();
        const auto packed = deflateRaw(data);
        if (!packed)
            return false;
        Entry e;
        e.name = QFileInfo(path).fileName().toUtf8();
        e.crc =
            quint32(crc32(0, reinterpret_cast<const Bytef*>(data.constData()), uInt(data.size())));
        e.size = quint32(data.size());
        e.packed = quint32(packed->size());
        e.offset = offset;
        const QDateTime modified = QFileInfo(path).lastModified();
        const QDate d = modified.date();
        const QTime t = modified.time();
        e.time = quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
        e.date = quint16(((qMax(d.year(), 1980) - 1980) << 9) | (d.month() << 5) | d.day());
        QByteArray header;
        put32(header, 0x04034b50);
        put16(header, 20);     // version needed: deflate
        put16(header, 0x0800); // UTF-8 names
        put16(header, 8);      // deflate
        put16(header, e.time);
        put16(header, e.date);
        put32(header, e.crc);
        put32(header, e.packed);
        put32(header, e.size);
        put16(header, quint16(e.name.size()));
        put16(header, 0);
        header += e.name;
        if (out.write(header) != header.size() || out.write(*packed) != packed->size())
            return false;
        offset += quint32(header.size() + packed->size());
        entries.append(e);
    }
    QByteArray directory;
    for (const Entry& e : std::as_const(entries)) {
        put32(directory, 0x02014b50);
        put16(directory, 20); // made by
        put16(directory, 20); // needed
        put16(directory, 0x0800);
        put16(directory, 8);
        put16(directory, e.time);
        put16(directory, e.date);
        put32(directory, e.crc);
        put32(directory, e.packed);
        put32(directory, e.size);
        put16(directory, quint16(e.name.size()));
        put16(directory, 0); // extra
        put16(directory, 0); // comment
        put16(directory, 0); // disk
        put16(directory, 0); // internal attributes
        put32(directory, 0); // external attributes
        put32(directory, e.offset);
        directory += e.name;
    }
    QByteArray end;
    put32(end, 0x06054b50);
    put16(end, 0);
    put16(end, 0);
    put16(end, quint16(entries.size()));
    put16(end, quint16(entries.size()));
    put32(end, quint32(directory.size()));
    put32(end, offset);
    put16(end, 0);
    if (out.write(directory) != directory.size() || out.write(end) != end.size())
        return false;
    return out.commit();
}
} // namespace cirrus::log
