#include "gpu_preference.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>

#include <optional>

namespace cirrus::ui::gpu
{

namespace
{

struct Gpu {
    QString slot;   // PCI address, 0000:01:00.0
    QString driver; // kernel driver, nvidia / amdgpu / …
};

// The discrete GPU that is not driving the display, if any. Laptops boot on the
// integrated GPU (boot_vga 1); a desktop whose screen hangs off the discrete GPU
// already renders there.
std::optional<Gpu> offloadGpu()
{
#ifdef Q_OS_LINUX
    // Drivers of discrete GPUs; the integrated ones (i915, xe) are left out so an
    // Intel iGPU is never picked on a machine that boots on the discrete GPU.
    static const QStringList discrete = {QStringLiteral("nvidia"), QStringLiteral("nouveau"),
                                         QStringLiteral("amdgpu"), QStringLiteral("radeon")};
    static const QRegularExpression card(QStringLiteral("^card\\d+$"));

    const QDir drm(QStringLiteral("/sys/class/drm"));
    QList<Gpu> others;
    int gpus = 0;
    for (const QString& name : drm.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        if (!card.match(name).hasMatch())
            continue;
        const QString device = drm.filePath(name + QStringLiteral("/device"));
        // PCI devices only (not simpledrm and the like).
        if (!QFileInfo::exists(device + QStringLiteral("/vendor")))
            continue;
        ++gpus;
        QFile bootVga(device + QStringLiteral("/boot_vga"));
        if (bootVga.open(QIODevice::ReadOnly) && bootVga.readAll().trimmed() == "1")
            continue;
        const QString driver =
            QFileInfo(QFileInfo(device + QStringLiteral("/driver")).symLinkTarget()).fileName();
        if (discrete.contains(driver))
            others.append({QFileInfo(QFileInfo(device).canonicalFilePath()).fileName(), driver});
    }
    if (gpus >= 2 && !others.isEmpty())
        return others.first();
#endif
    return std::nullopt;
}

const std::optional<Gpu>& target()
{
    static const std::optional<Gpu> gpu = offloadGpu();
    return gpu;
}

} // namespace

bool switchable() { return target().has_value(); }

void apply(bool preferDiscrete)
{
    if (!preferDiscrete || !target() || qEnvironmentVariable("CIRRUS_GPU") == u"default")
        return;
    for (const char* name : {"__NV_PRIME_RENDER_OFFLOAD", "__GLX_VENDOR_LIBRARY_NAME",
                             "__VK_LAYER_NV_optimus", "DRI_PRIME"}) {
        if (qEnvironmentVariableIsSet(name))
            return;
    }
    if (target()->driver == u"nvidia") {
        qputenv("__NV_PRIME_RENDER_OFFLOAD", "1");
        qputenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia");
        qputenv("__VK_LAYER_NV_optimus", "NVIDIA_only");
    } else {
        // pci-0000_01_00_0 names the device, rather than "the other one".
        QString slot = target()->slot;
        slot.replace(QLatin1Char(':'), QLatin1Char('_'))
            .replace(QLatin1Char('.'), QLatin1Char('_'));
        qputenv("DRI_PRIME", ("pci-" + slot).toLatin1());
    }
}

} // namespace cirrus::ui::gpu
