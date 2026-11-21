#include "spectrum.h"
#include <cmath>
#include <numbers>

namespace cirrus::media::automix
{

Spectrum::Spectrum(int size) : m_size(size)
{
    const int half = size / 2;
    m_window.resize(size_t(size));
    double sum = 0;
    for (int i = 0; i < size; ++i) {
        m_window[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / size));
        sum += m_window[size_t(i)];
    }
    m_scale = float(2 / sum); // a sine's peak bin: amplitude · Σw / 2
    m_work.resize(size_t(half));
    m_twiddles.resize(size_t(half / 2));
    for (int k = 0; k < half / 2; ++k)
        m_twiddles[size_t(k)] = std::polar(1.0f, float(-2 * std::numbers::pi * k / half));
    m_split.resize(size_t(half));
    for (int k = 0; k < half; ++k)
        m_split[size_t(k)] = std::polar(1.0f, float(-2 * std::numbers::pi * k / size));
    m_reversed.resize(size_t(half));
    int bits = 0;
    while ((1 << bits) < half)
        ++bits;
    for (int i = 0; i < half; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            r |= ((i >> b) & 1) << (bits - 1 - b);
        m_reversed[size_t(i)] = r;
    }
}

void Spectrum::transform()
{
    const int n = int(m_work.size());
    for (int i = 0; i < n; ++i)
        if (const int r = m_reversed[size_t(i)]; r > i)
            std::swap(m_work[size_t(i)], m_work[size_t(r)]);
    for (int length = 2; length <= n; length *= 2) {
        const int step = n / length;
        for (int start = 0; start < n; start += length) {
            for (int k = 0; k < length / 2; ++k) {
                // By hand: std::complex's product also handles infinities, slowly.
                const auto w = m_twiddles[size_t(k * step)];
                auto& even = m_work[size_t(start + k)];
                auto& odd = m_work[size_t(start + k + length / 2)];
                const std::complex<float> t{w.real() * odd.real() - w.imag() * odd.imag(),
                                            w.real() * odd.imag() + w.imag() * odd.real()};
                odd = even - t;
                even += t;
            }
        }
    }
}

void Spectrum::power(const float* frame, float* power)
{
    const int half = m_size / 2;
    for (int k = 0; k < half; ++k)
        m_work[size_t(k)] = {frame[2 * k] * m_window[size_t(2 * k)],
                             frame[2 * k + 1] * m_window[size_t(2 * k + 1)]};
    transform();
    // The even and odd samples' spectra out of the half-size one, then combined.
    for (int k = 0; k <= half; ++k) {
        const auto z = m_work[size_t(k % half)];
        const auto mirror = std::conj(m_work[size_t((half - k) % half)]);
        const std::complex<float> even{0.5f * (z.real() + mirror.real()),
                                       0.5f * (z.imag() + mirror.imag())};
        // (z - mirror) / 2i
        const std::complex<float> odd{0.5f * (z.imag() - mirror.imag()),
                                      -0.5f * (z.real() - mirror.real())};
        const auto twiddle = k < half ? m_split[size_t(k)] : std::complex<float>(-1, 0);
        const float re = even.real() + twiddle.real() * odd.real() - twiddle.imag() * odd.imag();
        const float im = even.imag() + twiddle.real() * odd.imag() + twiddle.imag() * odd.real();
        power[k] = (re * re + im * im) * m_scale * m_scale;
    }
}

} // namespace cirrus::media::automix
