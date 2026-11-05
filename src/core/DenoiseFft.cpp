// The denoiser's FFT: pocketfft (already in the repository for the ReHarmoniSer), kept in its own translation unit so that Denoise.h stays light
// for everything that includes it. The header is included exactly as the ReHarmoniSer's Dsp.cpp includes it (no settings of its own): two
// different sets of settings would be two different bodies for the same inline templates. Every call here is on one thread (no pool is made).
// 3.37.0: also dsp::FastFFT (Dsp.h), the spectral pictures' and the ReTouch fixes' FFT: its plan (the twiddle factors) is made once, not at every call.
#include "../reharmoniser/third_party/pocketfft_hdronly.h"
#include "Denoise.h"

namespace td::denoise::detail
{
void realForward (const double* in, int n, std::complex<double>* out)
{
    pocketfft::shape_t shape { (size_t) n };
    pocketfft::stride_t sIn { (std::ptrdiff_t) sizeof (double) }, sOut { (std::ptrdiff_t) sizeof (std::complex<double>) };
    pocketfft::r2c (shape, sIn, sOut, 0, true, in, out, 1.0);
}

void realInverse (const std::complex<double>* in, int n, double* out)
{
    pocketfft::shape_t shape { (size_t) n };
    pocketfft::stride_t sIn { (std::ptrdiff_t) sizeof (std::complex<double>) }, sOut { (std::ptrdiff_t) sizeof (double) };
    pocketfft::c2r (shape, sIn, sOut, 0, false, in, out, 1.0 / (double) n);
}
} // namespace td::denoise::detail

namespace td::dsp
{
struct FastFFT::Plan
{
    explicit Plan (size_t n) : p (n) {}
    pocketfft::detail::pocketfft_c<double> p;
};
FastFFT::FastFFT (int sizePow2) : n (sizePow2), plan (std::make_shared<const Plan> ((size_t) sizePow2)) {}
void FastFFT::forward (std::vector<std::complex<double>>& a) const
{
    plan->p.exec (reinterpret_cast<pocketfft::detail::cmplx<double>*> (a.data()), 1.0, true);
}
void FastFFT::inverse (std::vector<std::complex<double>>& a) const
{
    plan->p.exec (reinterpret_cast<pocketfft::detail::cmplx<double>*> (a.data()), 1.0 / (double) n, false);
}
} // namespace td::dsp
