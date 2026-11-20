#include "mix.h"

namespace cirrus::media::automix
{

void mixOverlap(const Plan& plan, const float* out, const float* in, float* dst, qsizetype frames,
                qint64 offset, int rate)
{
    for (qsizetype i = 0; i < frames; ++i) {
        const double seconds = double(offset + i) / rate;
        const float a = plan.gainOut.at(seconds);
        const float b = plan.gainIn.at(seconds);
        dst[i * 2] = softLimit(out[i * 2] * a + in[i * 2] * b);
        dst[i * 2 + 1] = softLimit(out[i * 2 + 1] * a + in[i * 2 + 1] * b);
    }
}

} // namespace cirrus::media::automix
