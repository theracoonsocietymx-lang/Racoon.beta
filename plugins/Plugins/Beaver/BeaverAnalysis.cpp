#include "BeaverAnalysis.h"

namespace racoon::beaver
{
using namespace juce;

// ===================================================================== saving
MemoryBlock FrameSet::save() const
{
    MemoryBlock raw;
    {
        MemoryOutputStream os (raw, false);
        os.writeInt (0x42565246);           // "BVRF"
        os.writeFloat (frameSeconds); os.writeFloat (refHz); os.writeInt (frames);
        os.writeString (name);
        os.writeBool (! noise.empty()); os.writeBool (! f0.empty());
        os.write (level.data(), level.size());
        if (! noise.empty()) os.write (noise.data(), noise.size());
        for (float f : f0) os.writeFloat (f);
    }
    MemoryBlock out;
    {
        MemoryOutputStream mo (out, false);
        GZIPCompressorOutputStream gz (mo, 9);
        gz.write (raw.getData(), raw.getSize());
        gz.flush();
    }
    return out;
}

std::shared_ptr<FrameSet> FrameSet::load (const MemoryBlock& block)
{
    MemoryInputStream mi (block, false);
    GZIPDecompressorInputStream gz (mi);
    MemoryBlock raw;
    gz.readIntoMemoryBlock (raw);
    MemoryInputStream is (raw, false);
    if (is.readInt() != 0x42565246) return {};
    auto f = std::make_shared<FrameSet>();
    f->frameSeconds = is.readFloat(); f->refHz = is.readFloat(); f->frames = is.readInt();
    f->name = is.readString();
    const bool hasNoise = is.readBool(), hasF0 = is.readBool();
    if (f->frames <= 0 || f->frames > 200000) return {};
    f->level.resize ((size_t) f->frames * numPartials);
    if (is.read (f->level.data(), (int) f->level.size()) != (int) f->level.size()) return {};
    if (hasNoise)
    {
        f->noise.resize ((size_t) f->frames * numNoiseBands);
        if (is.read (f->noise.data(), (int) f->noise.size()) != (int) f->noise.size()) return {};
    }
    if (hasF0) { f->f0.resize ((size_t) f->frames); for (auto& v : f->f0) v = is.readFloat(); }
    return f;
}

float bandEdge (int b) { return 50.0f * std::pow (320.0f, (float) jlimit (0, numNoiseBands, b) / (float) numNoiseBands); }

// ===================================================================== pitch (YIN, with the difference function from FFTs)
std::vector<float> trackPitch (const float* x, int n, double sr, int hop)
{
    const int W = 1024, tauMax = (int) (sr / 30.0), tauMin = (int) (sr / 1000.0);
    const int order = 12, N = 1 << order;          // 4096 >= W + W + tauMax
    juce::dsp::FFT fft (order);
    std::vector<float> A ((size_t) N * 2), B ((size_t) N * 2), d ((size_t) tauMax + 2), cm ((size_t) tauMax + 2);
    std::vector<double> cum ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i) cum[(size_t) i + 1] = cum[(size_t) i] + (double) x[i] * x[i];
    std::vector<float> out, aper;
    for (int t = 0; t + W + tauMax < n; t += hop)
    {
        std::fill (A.begin(), A.end(), 0.0f); std::fill (B.begin(), B.end(), 0.0f);
        for (int j = 0; j < W; ++j) A[(size_t) j] = x[t + j];
        for (int j = 0; j < W + tauMax; ++j) B[(size_t) j] = x[t + j];
        fft.performRealOnlyForwardTransform (A.data());
        fft.performRealOnlyForwardTransform (B.data());
        // cross-correlation r(tau) = sum a_j b_(j+tau): conj(A) * B
        for (int k = 0; k <= N / 2; ++k)
        {
            const float ar = A[(size_t) 2 * k], ai = A[(size_t) 2 * k + 1], br = B[(size_t) 2 * k], bi = B[(size_t) 2 * k + 1];
            B[(size_t) 2 * k] = ar * br + ai * bi;
            B[(size_t) 2 * k + 1] = ar * bi - ai * br;
        }
        fft.performRealOnlyInverseTransform (B.data());
        const double e1 = cum[(size_t) t + W] - cum[(size_t) t];
        if (e1 < 1.0e-6) { out.push_back (0.0f); aper.push_back (1.0f); continue; }
        double run = 0.0;
        d[0] = 0.0f; cm[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const double e2 = cum[(size_t) (t + tau + W)] - cum[(size_t) (t + tau)];
            d[(size_t) tau] = (float) jmax (0.0, e1 + e2 - 2.0 * B[(size_t) tau]);
            run += d[(size_t) tau];
            cm[(size_t) tau] = run > 0.0 ? (float) (d[(size_t) tau] * tau / run) : 1.0f;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (cm[(size_t) tau] < 0.12f)
            {
                while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                best = tau; break;
            }
        if (best < 0)
        {
            best = tauMin;
            for (int tau = tauMin; tau < tauMax; ++tau) if (cm[(size_t) tau] < cm[(size_t) best]) best = tau;
        }
        double tauF = best;
        if (best > tauMin && best < tauMax - 1)
        {
            const double ym = cm[(size_t) best - 1], y0 = cm[(size_t) best], yp = cm[(size_t) best + 1];
            const double den = ym - 2.0 * y0 + yp;
            if (std::abs (den) > 1.0e-12) tauF += 0.5 * (ym - yp) / den;
        }
        out.push_back ((float) (sr / tauF));
        aper.push_back (cm[(size_t) best]);
    }
    // each raw frame looked at the samples from t to t + W + tau: its centre sits W / 2 + tau / 2 after t. Move every
    // value to the frame whose centre it describes (frame i is centred on i * hop, like the spectrum frames)
    {
        std::vector<float> o2 (out.size(), 0.0f), a2 (out.size(), 1.0f);
        for (size_t i = 0; i < out.size(); ++i)
        {
            const float guess = out[i] > 0.0f ? out[i] : 110.0f;
            const int lag = (int) std::round ((W * 0.5 + 0.5 * sr / guess) / hop);
            const int j = (int) i + lag;
            if (j < (int) out.size()) { o2[(size_t) j] = out[i]; a2[(size_t) j] = aper[i]; }
        }
        // the first frames have no window of their own: they take the first measured one
        const int firstLag = (int) std::round ((W * 0.5 + 0.5 * sr / (out.empty() || out[0] <= 0.0f ? 110.0f : out[0])) / hop);
        for (int j = 0; j < (int) out.size() && j < firstLag; ++j) { o2[(size_t) j] = out.empty() ? 0.0f : out[0]; a2[(size_t) j] = aper.empty() ? 1.0f : aper[0]; }
        out.swap (o2); aper.swap (a2);
    }
    // median of 5, then unvoiced where the aperiodicity is high
    std::vector<float> med (out.size());
    for (size_t i = 0; i < out.size(); ++i)
    {
        float w[5]; int c = 0;
        for (int k = -2; k <= 2; ++k) { const int j = (int) i + k; if (j >= 0 && j < (int) out.size()) w[c++] = out[(size_t) j]; }
        std::sort (w, w + c);
        med[i] = aper[i] > 0.35f ? 0.0f : w[c / 2];
    }
    return med;
}

// ===================================================================== audio to frames
std::shared_ptr<FrameSet> analyse (const AudioBuffer<float>& mono, const AnalysisOptions& opt, const String& name)
{
    const double sr = 48000.0;
    const int hop = 240;
    const int n = mono.getNumSamples();
    const float* x = mono.getReadPointer (0);
    auto fs = std::make_shared<FrameSet>();
    fs->name = name;
    fs->frameSeconds = (float) (hop / sr);

    std::vector<float> f0 = trackPitch (x, n, sr, hop);
    // the reference: the median voiced pitch, on the nearest semitone
    float ref = 0.0f;
    {
        std::vector<float> v;
        for (float f : f0) if (f > 0.0f) v.push_back (f);
        if (! v.empty()) { std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end()); ref = v[v.size() / 2]; }
    }
    if (opt.asSpectrum || ref <= 0.0f) ref = 440.0f * std::pow (2.0f, ((opt.asSpectrum ? opt.refNote : 45) - 69) / 12.0f);
    else ref = 440.0f * std::pow (2.0f, std::round (12.0f * std::log2 (ref / 440.0f)) / 12.0f);
    fs->refHz = ref;

    int N = 2048;
    while (N < (int) (8.0 * sr / ref) && N < 8192) N <<= 1;
    int order = 0; while ((1 << order) < N * 2) ++order;
    const int M = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) N), buf ((size_t) M * 2), mag ((size_t) M / 2 + 1);
    double wsum = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const double t = MathConstants<double>::twoPi * i / (N - 1);
        win[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t));
        wsum += win[(size_t) i];
    }
    const double binHz = sr / M;
    const int frames = jmax (1, (n - 1) / hop + 1);
    fs->frames = frames;
    fs->level.assign ((size_t) frames * numPartials, 0);
    fs->noise.assign ((size_t) frames * numNoiseBands, 0);
    if (! opt.asSpectrum) fs->f0.assign ((size_t) frames, 0.0f);
    float held = ref;
    std::vector<uint8_t> taken ((size_t) M / 2 + 1);
    for (int fr = 0; fr < frames; ++fr)
    {
        const int centre = fr * hop;
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int s = centre - N / 2 + i;
            buf[(size_t) i] = (s >= 0 && s < n ? x[s] : 0.0f) * win[(size_t) i];
        }
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 0; k <= M / 2; ++k) mag[(size_t) k] = buf[(size_t) k] * (float) (2.0 / wsum);   // amplitude of a sine
        // the pitch of this frame (the analysis hop and the pitch hop are the same)
        const int pi = jmin ((int) f0.size() - 1, fr);
        float f = ref;
        if (! opt.asSpectrum)
        {
            const float tracked = pi >= 0 ? f0[(size_t) pi] : 0.0f;
            if (tracked > 0.0f) held = tracked;
            f = held;
            fs->f0[(size_t) fr] = tracked;
        }
        std::fill (taken.begin(), taken.end(), 0);
        const double search = opt.asSpectrum ? 0.5 : 0.4;
        for (int k = 1; k <= numPartials; ++k)
        {
            const double target = k * f;
            float db = -96.0f;
            if (target < 0.45 * sr)
            {
                const int lo = jmax (1, (int) std::floor ((target - search * f) / binHz)), hi = jmin (M / 2 - 1, (int) std::ceil ((target + search * f) / binHz));
                int best = lo;
                for (int b = lo; b <= hi; ++b) if (mag[(size_t) b] > mag[(size_t) best]) best = b;
                const double ym = 20.0 * std::log10 (mag[(size_t) best - 1] + 1.0e-9), y0 = 20.0 * std::log10 (mag[(size_t) best] + 1.0e-9),
                             yp = 20.0 * std::log10 (mag[(size_t) best + 1] + 1.0e-9);
                const double den = ym - 2.0 * y0 + yp;
                const double off = std::abs (den) > 1.0e-9 ? jlimit (-0.5, 0.5, 0.5 * (ym - yp) / den) : 0.0;
                db = (float) (y0 - 0.25 * (ym - yp) * off);
                if (opt.asSpectrum) { for (int b = jmax (0, (int) ((target - 0.1 * f) / binHz)); b <= jmin (M / 2, (int) ((target + 0.1 * f) / binHz)); ++b) taken[(size_t) b] = 1; }
                else
                {
                    // the window's main lobe (plus a margin) belongs to the harmonic, not to the noise
                    const int lobe = 4 * M / N + 3;
                    for (int b = jmax (0, best - lobe); b <= jmin (M / 2, best + lobe); ++b) taken[(size_t) b] = 1;
                }
            }
            fs->level[(size_t) fr * numPartials + (size_t) (k - 1)] = FrameSet::q (db);
        }
        // what the harmonics left: power density per band
        for (int b = 0; b < numNoiseBands; ++b)
        {
            const int lo = jmax (1, (int) (bandEdge (b) / binHz)), hi = jmin (M / 2, jmax (lo, (int) (bandEdge (b + 1) / binHz)));
            double p = 0.0; int c = 0;
            for (int k = lo; k <= hi; ++k) { ++c; if (! taken[(size_t) k]) p += (double) mag[(size_t) k] * mag[(size_t) k]; }
            const double density = p / jmax (1, c) / binHz;
            fs->noise[(size_t) fr * numNoiseBands + (size_t) b] = FrameSet::q ((float) (10.0 * std::log10 (density + 1.0e-14)));
        }
    }
    // every material at the same height: the loudest partial of the whole material at -3 dB (the noise moves with it)
    {
        int top = 0;
        for (auto v : fs->level) top = jmax (top, (int) v);
        const int target = FrameSet::q (-3.0f);
        const int shift = top > 0 ? target - top : 0;
        if (shift != 0)
        {
            for (auto& v : fs->level) if (v > 0) v = (uint8_t) jlimit (1, 255, (int) v + shift);
            for (auto& v : fs->noise) if (v > 0) v = (uint8_t) jlimit (1, 255, (int) v + shift);
        }
    }
    return fs;
}

std::shared_ptr<FrameSet> analyseFile (const File& file, const AnalysisOptions& opt, String& error)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) { error = "No se pudo abrir el audio."_u; return {}; }
    const int len = (int) jmin ((int64) (r->sampleRate * 30.0), r->lengthInSamples);
    if (len < 4800) { error = "El audio es demasiado corto."_u; return {}; }
    AudioBuffer<float> in ((int) jmax (1u, r->numChannels), len);
    r->read (&in, 0, len, 0, true, true);
    AudioBuffer<float> mono (1, len);
    mono.clear();
    for (int c = 0; c < in.getNumChannels(); ++c) mono.addFrom (0, 0, in, c, 0, len, 1.0f / (float) in.getNumChannels());
    // to 48 kHz
    AudioBuffer<float> m48 = mono;
    if (std::abs (r->sampleRate - 48000.0) > 1.0)
    {
        const double ratio = r->sampleRate / 48000.0;
        const int outLen = (int) (len / ratio);
        m48.setSize (1, outLen);
        LagrangeInterpolator li;
        li.process (ratio, mono.getReadPointer (0), m48.getWritePointer (0), outLen);
    }
    // no DC, peak at -1 dB
    double dc = 0.0;
    for (int i = 0; i < m48.getNumSamples(); ++i) dc += m48.getSample (0, i);
    dc /= jmax (1, m48.getNumSamples());
    for (int i = 0; i < m48.getNumSamples(); ++i) m48.setSample (0, i, m48.getSample (0, i) - (float) dc);
    const float pk = m48.getMagnitude (0, 0, m48.getNumSamples());
    if (pk > 1.0e-6f) m48.applyGain (0.891f / pk);
    return analyse (m48, opt, file.getFileNameWithoutExtension());
}

// ===================================================================== picture to frames
std::shared_ptr<FrameSet> fromImage (const Image& src, bool logAxis, float rangeDb, float seconds, const String& name)
{
    if (! src.isValid()) return {};
    const int cols = jmin (2048, src.getWidth()), rows = jmin (numPartials, src.getHeight());
    Image img = (cols != src.getWidth() || rows != src.getHeight()) ? src.rescaled (cols, rows, Graphics::highResamplingQuality) : src;
    auto fs = std::make_shared<FrameSet>();
    fs->name = name;
    fs->frames = cols;
    fs->frameSeconds = jmax (0.001f, seconds) / (float) cols;
    fs->refHz = 55.0f;
    fs->level.assign ((size_t) cols * numPartials, 0);
    const Image::BitmapData bd (img, Image::BitmapData::readOnly);
    auto luma = [&] (int x, int row)   // row 0 = bottom
    {
        const auto c = bd.getPixelColour (x, rows - 1 - jlimit (0, rows - 1, row));
        return 0.2126f * c.getFloatRed() + 0.7152f * c.getFloatGreen() + 0.0722f * c.getFloatBlue();
    };
    for (int x = 0; x < cols; ++x)
        for (int k = 1; k <= numPartials; ++k)
        {
            const float pos = logAxis ? std::log2 ((float) k) / std::log2 ((float) numPartials) * (float) (rows - 1)
                                      : (float) (k - 1) / (float) (numPartials - 1) * (float) (rows - 1);
            const int r0 = (int) pos; const float t = pos - (float) r0;
            const float l = luma (x, r0) * (1.0f - t) + luma (x, jmin (rows - 1, r0 + 1)) * t;
            const float db = -rangeDb * (1.0f - std::pow (jlimit (0.0f, 1.0f, l), 0.8f));
            fs->level[(size_t) x * numPartials + (size_t) (k - 1)] = FrameSet::q (l <= 0.0f ? -96.0f : db);
        }
    return fs;
}
}
