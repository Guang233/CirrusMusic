#pragma once
#include <complex>
#include <vector>

namespace cirrus::media::automix
{

// The power spectrum of a windowed frame (docs/tasks/automix.md §4.2): a real FFT of a
// power-of-two size, done as a complex one of half the size. Scaled so that a full-scale
// sine's bin is about 1 (0 dB).
class Spectrum
{
  public:
    explicit Spectrum(int size); // a power of two, at least 16
    [[nodiscard]] int size() const { return m_size; }
    [[nodiscard]] int bins() const { return m_size / 2 + 1; }
    // `frame`: size() samples, unwindowed (the Hann window goes on here). `power`: bins().
    void power(const float* frame, float* power);

  private:
    void transform(); // m_work in place, size m_size / 2

    int m_size;
    std::vector<float> m_window;
    std::vector<std::complex<float>> m_work, m_twiddles, m_split;
    std::vector<int> m_reversed;
    float m_scale = 1;
};

} // namespace cirrus::media::automix
