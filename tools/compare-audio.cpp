// Compare native audio with a Genesis Plus GX capture of the same replay.
// Both runs are anchored at the same replay gate; for each window after it,
// report the lag that best aligns the waveforms and the correlation there.
// This measures sound timing (constant, small lag) and similarity. The two
// YM2612/PSG implementations and output filters differ, so equality is not
// expected. Build: tools/compare-audio.sh
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <complex>
#include <cstring>
#include <vector>

namespace {
std::vector<float> mono(const std::vector<int16_t> &stereo) {
    std::vector<float> out(stereo.size() / 2);
    for (size_t i = 0; i < out.size(); i++) out[i] = (stereo[i * 2] + stereo[i * 2 + 1]) * (0.5f / 32768.f);
    return out;
}
std::vector<int16_t> read_all(const char *path, size_t skip) {
    FILE *f = std::fopen(path, "rb");
    if (!f) { std::perror(path); std::exit(1); }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, long(skip), SEEK_SET);
    std::vector<int16_t> data((size - skip) / 2);
    if (std::fread(data.data(), 2, data.size(), f) != data.size()) { std::perror(path); std::exit(1); }
    std::fclose(f);
    return data;
}
// Offset of the "data" chunk payload in a RIFF/WAVE file.
size_t wav_data_offset(const char *path) {
    FILE *f = std::fopen(path, "rb");
    if (!f) { std::perror(path); std::exit(1); }
    char header[12];
    if (std::fread(header, 1, 12, f) != 12 || std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) {
        std::fprintf(stderr, "%s: not a WAVE file\n", path); std::exit(1);
    }
    size_t at = 12;
    for (;;) {
        char chunk[8];
        if (std::fread(chunk, 1, 8, f) != 8) { std::fprintf(stderr, "%s: no data chunk\n", path); std::exit(1); }
        uint32_t length = uint8_t(chunk[4]) | uint8_t(chunk[5]) << 8 | uint8_t(chunk[6]) << 16 | uint32_t(uint8_t(chunk[7])) << 24;
        at += 8;
        if (!std::memcmp(chunk, "data", 4)) { std::fclose(f); return at; }
        at += length + (length & 1);
        std::fseek(f, long(at), SEEK_SET);
    }
}
// Log energy in 32 log-spaced bands (60 Hz - 16 kHz), 2048-point Hann FFT,
// 10 ms hop. Phase-independent: two YM2612 cores playing the same notes differ
// in operator phase and output filtering, so waveforms do not correlate.
std::vector<std::array<float, 32>> spectrogram(const std::vector<float> &x, double rate, size_t hop) {
    constexpr size_t N = 2048;
    std::vector<std::array<float, 32>> out;
    std::vector<std::complex<double>> buf(N);
    unsigned band_of[N / 2];
    for (size_t k = 0; k < N / 2; k++) {
        const double hz = k * rate / N;
        const double b = hz < 60 ? -1 : std::log(hz / 60) / std::log(16000.0 / 60) * 32;
        band_of[k] = b < 0 || b >= 32 ? 32 : unsigned(b);
    }
    for (size_t at = 0; at + N <= x.size(); at += hop) {
        for (size_t i = 0; i < N; i++) buf[i] = x[at + i] * (0.5 - 0.5 * std::cos(2 * M_PI * i / (N - 1)));
        for (size_t i = 1, j = 0; i < N; i++) {   // iterative radix-2 FFT
            size_t bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(buf[i], buf[j]);
        }
        for (size_t len = 2; len <= N; len <<= 1) {
            const std::complex<double> w = std::polar(1.0, -2 * M_PI / len);
            for (size_t i = 0; i < N; i += len) {
                std::complex<double> v = 1;
                for (size_t k = 0; k < len / 2; k++, v *= w) {
                    const auto a = buf[i + k], b = buf[i + k + len / 2] * v;
                    buf[i + k] = a + b; buf[i + k + len / 2] = a - b;
                }
            }
        }
        std::array<double, 33> e{};
        for (size_t k = 0; k < N / 2; k++) e[band_of[k]] += std::norm(buf[k]);
        std::array<float, 32> row;
        for (unsigned b = 0; b < 32; b++) row[b] = float(std::log10(e[b] + 1e-6));
        out.push_back(row);
    }
    return out;
}
double correlate_bands(const std::vector<std::array<float, 32>> &a, const std::vector<std::array<float, 32>> &b,
                       size_t at_a, size_t at_b, size_t n) {
    double sa = 0, sb = 0, count = n * 32;
    for (size_t i = 0; i < n; i++) for (unsigned k = 0; k < 32; k++) { sa += a[at_a + i][k]; sb += b[at_b + i][k]; }
    sa /= count; sb /= count;
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < n; i++) for (unsigned k = 0; k < 32; k++) {
        const double x = a[at_a + i][k] - sa, y = b[at_b + i][k] - sb;
        ab += x * y; aa += x * x; bb += y * y;
    }
    return aa > 0 && bb > 0 ? ab / std::sqrt(aa * bb) : 0;
}
// Onset strength: positive change of log band energy between hops. Static
// spectral shape cancels, so correlation reflects when notes and effects start.
std::vector<std::array<float, 32>> flux(const std::vector<std::array<float, 32>> &s) {
    std::vector<std::array<float, 32>> out(s.size());
    for (size_t i = 1; i < s.size(); i++)
        for (unsigned k = 0; k < 32; k++) out[i][k] = std::max(0.f, s[i][k] - s[i - 1][k]);
    return out;
}
// Normalized correlation of a[i] with b[i + lag] over the window.
double correlate(const float *a, const float *b, size_t n, long lag, size_t step = 1) {
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < n; i += step) {
        const double x = a[i], y = b[long(i) + lag];
        ab += x * y; aa += x * x; bb += y * y;
    }
    return aa > 0 && bb > 0 ? ab / std::sqrt(aa * bb) : 0;
}
}

int main(int argc, char **argv) {
    if (argc != 9) {
        std::fprintf(stderr, "usage: %s genesis.wav genesis_rate genesis_samples genesis_frames genesis_gate "
                             "native.s16 native_gate frames\n", argv[0]);
        return 2;
    }
    const auto genesis = mono(read_all(argv[1], wav_data_offset(argv[1])));
    const double grate = std::atof(argv[2]), gper = std::atof(argv[3]) / std::atof(argv[4]);
    const long ggate = std::atol(argv[5]);
    const auto native_raw = mono(read_all(argv[6], 0));
    const long ngate = std::atol(argv[7]), frames = std::atol(argv[8]);
    // The port's output: the YM2612 rate (master clock / 7 / 144), 1008 clocks a sample.
    const double nrate = 53693175.0 / 7 / 144, nper = 896040.0 / 1008;

    // Resample native to the Genesis capture rate (linear interpolation).
    std::vector<float> native(size_t(native_raw.size() * grate / nrate));
    for (size_t i = 0; i < native.size(); i++) {
        const double t = i * nrate / grate;
        const size_t k = size_t(t);
        const double f = t - k;
        native[i] = float(native_raw[k] * (1 - f) + (k + 1 < native_raw.size() ? native_raw[k + 1] : 0) * f);
    }
    // Genesis trace frame 1 is its first frame; native frame 0 is its first.
    const double gstart = (ggate - 1) * gper, nstart = ngate * nper * grate / nrate;
    const size_t window = size_t(grate / 2);
    const long reach = long(grate * 0.1);   // search +-100 ms
    const size_t span = size_t(frames * gper);
    std::vector<double> lags, scores;
    unsigned silent = 0;
    std::printf("window_s lag_ms correlation\n");
    for (size_t at = size_t(reach); at + window + reach < span; at += window) {
        const size_t g = size_t(gstart) + at, n = size_t(nstart) + at;
        if (g + window >= genesis.size() || n + window + reach >= native.size()) break;
        double energy = 0;
        for (size_t i = 0; i < window; i++) energy += genesis[g + i] * genesis[g + i];
        if (energy / window < 1e-5) { silent++; continue; }
        long best = 0; double score = -2;
        for (long lag = -reach; lag <= reach; lag += 4) {
            const double c = correlate(&genesis[g], &native[n], window, lag, 4);
            if (c > score) { score = c; best = lag; }
        }
        const long coarse = best;
        for (long lag = coarse - 4; lag <= coarse + 4; lag++) {
            const double c = correlate(&genesis[g], &native[n], window, lag);
            if (lag == coarse - 4 || c > score) { score = c; best = lag; }
        }
        lags.push_back(best * 1000.0 / grate);
        scores.push_back(score);
        std::printf("%8.2f %7.2f %11.4f\n", at / grate, lags.back(), score);
    }
    if (lags.empty()) { std::fprintf(stderr, "no audible windows\n"); return 1; }
    // Spectral comparison: 1 s windows of 10 ms hops, lag search +-100 ms.
    const size_t hop = size_t(grate / 100);
    const auto gs = spectrogram(std::vector<float>(genesis.begin() + size_t(gstart), genesis.begin() + std::min(genesis.size(), size_t(gstart) + span)), grate, hop);
    const auto ns = spectrogram(std::vector<float>(native.begin() + size_t(nstart), native.begin() + std::min(native.size(), size_t(nstart) + span)), grate, hop);
    const auto gf = flux(gs), nf = flux(ns);
    // Onset correlation per 1 s window; control: the same native audio 5 s later.
    std::vector<double> olags, oscores, control;
    for (size_t at = 10; at + 110 < std::min(gf.size(), nf.size()); at += 100) {
        long best = 0; double score = -2;
        for (long lag = -10; lag <= 10; lag++) {
            const double c = correlate_bands(gf, nf, at, size_t(long(at) + lag), 100);
            if (c > score) { score = c; best = lag; }
        }
        olags.push_back(best * 10.0); oscores.push_back(score);
        const size_t shifted = at + 500 < nf.size() - 110 ? at + 500 : at - 500;
        double worst = -2;
        for (long lag = -10; lag <= 10; lag++) worst = std::max(worst, correlate_bands(gf, nf, at, size_t(long(shifted) + lag), 100));
        control.push_back(worst);
    }
    std::vector<double> slags, sscores;
    for (size_t at = 10; at + 110 < std::min(gs.size(), ns.size()); at += 100) {
        long best = 0; double score = -2;
        for (long lag = -10; lag <= 10; lag++) {
            const double c = correlate_bands(gs, ns, at, size_t(long(at) + lag), 100);
            if (c > score) { score = c; best = lag; }
        }
        slags.push_back(best * 10.0); sscores.push_back(score);
    }
    auto sorted = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v; };
    const auto l = sorted(lags), c = sorted(scores);
    auto pct = [](const std::vector<double> &v, double p) { return v[size_t(p * (v.size() - 1))]; };
    const auto sl = sorted(slags), sc = sorted(sscores);
    std::printf("SPECTRAL windows=%zu lag_ms p5=%.0f median=%.0f p95=%.0f band_correlation p5=%.3f median=%.3f min=%.3f\n",
                sl.size(), pct(sl, .05), pct(sl, .5), pct(sl, .95), pct(sc, .05), pct(sc, .5), sc.front());
    const auto ol = sorted(olags), oc = sorted(oscores), cc = sorted(control);
    std::printf("ONSETS windows=%zu lag_ms p5=%.0f median=%.0f p95=%.0f correlation p5=%.3f median=%.3f control(5 s misaligned) median=%.3f p95=%.3f\n",
                ol.size(), pct(ol, .05), pct(ol, .5), pct(ol, .95), pct(oc, .05), pct(oc, .5), pct(cc, .5), pct(cc, .95));
    // Mean level difference per band (native minus original, dB) over the span.
    std::printf("BAND_LEVEL_DB");
    for (unsigned k = 0; k < 32; k++) {
        double a = 0, b = 0; size_t n = std::min(gs.size(), ns.size());
        for (size_t i = 0; i < n; i++) { a += gs[i][k]; b += ns[i][k]; }
        std::printf(" %.0fHz:%+.1f", 60 * std::pow(16000.0 / 60, (k + 0.5) / 32), 10 * (b - a) / n);
    }
    double ga = 0, na = 0;
    for (size_t i = 0; i < size_t(span) && size_t(gstart) + i < genesis.size() && size_t(nstart) + i < native.size(); i++) {
        ga += genesis[size_t(gstart) + i] * genesis[size_t(gstart) + i]; na += native[size_t(nstart) + i] * native[size_t(nstart) + i];
    }
    std::printf("\nRMS_DB original=%.1f native=%.1f\n", 10 * std::log10(ga / span), 10 * std::log10(na / span));
    std::printf("SUMMARY windows=%zu silent=%u lag_ms p5=%.2f median=%.2f p95=%.2f correlation p5=%.3f median=%.3f\n",
                l.size(), silent, pct(l, .05), pct(l, .5), pct(l, .95), pct(c, .05), pct(c, .5));
}
