#include "StemsCore.h"
#include "model.hpp"
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

namespace racoon::stems
{
namespace
{
struct Cancelled {};

// one loaded model shared by every separation of the session: loading takes a second and 300 MB
std::mutex modelLock;
std::unique_ptr<demucscpp::demucs_model> cached;
std::string cachedFile;

const demucscpp::demucs_model* getModel (const std::string& file, std::string& err)
{
    std::lock_guard<std::mutex> l (modelLock);
    if (cached != nullptr && cachedFile == file) return cached.get();
    auto m = std::make_unique<demucscpp::demucs_model>();
    if (! demucscpp::load_demucs_model (file, m.get())) { err = "No se pudo leer el modelo de separacion"; return nullptr; }
    if (! m->is_4sources) { err = "El modelo no es el de 4 stems (htdemucs 4s)"; return nullptr; }
    cached = std::move (m);
    cachedFile = file;
    return cached.get();
}
}

bool looksLikeModel (const std::string& file)
{
    std::FILE* f = std::fopen (file.c_str(), "rb");
    if (f == nullptr) return false;
    uint32_t magic = 0;
    const bool read = std::fread (&magic, sizeof (magic), 1, f) == 1;
    std::fseek (f, 0, SEEK_END);
    const long size = std::ftell (f);
    std::fclose (f);
    return read && magic == 0x646d6334 && size > 40L * 1024 * 1024;
}

Result separate (const std::string& file, const std::vector<float>& left, const std::vector<float>& right,
                 int threads, std::function<void (float)> progress, const std::atomic<bool>* cancel)
{
    Result res;
    const int n = (int) std::min (left.size(), right.size());
    if (n < 1024) { res.error = "El audio es demasiado corto"; return res; }
    const auto* model = getModel (file, res.error);
    if (model == nullptr) return res;

    // pieces of the song, each with 0.75 s of overlap on both sides (as demucs.cpp's threaded driver)
    const int overlap = (int) std::floor (sampleRate * 0.75f);
    threads = std::max (1, std::min (threads, n / (sampleRate * 8) + 1));   // no piece shorter than about 8 s
    const int segLen = (int) std::ceil ((double) n / threads);
    std::vector<Eigen::MatrixXf> segs ((size_t) threads);
    for (int t = 0; t < threads; ++t)
    {
        const int start = t * segLen, end = std::min (n, start + segLen);
        Eigen::MatrixXf s = Eigen::MatrixXf::Zero (2, end - start + 2 * overlap);
        for (int i = 0; i < s.cols(); ++i)
        {
            const int src = start - overlap + i;
            if (src < 0 || src >= n) continue;
            s (0, i) = left[(size_t) src];
            s (1, i) = right[(size_t) src];
        }
        segs[(size_t) t] = std::move (s);
    }
    std::vector<Eigen::Tensor3dXf> outs ((size_t) threads);
    std::vector<float> prog ((size_t) threads, 0.0f);
    std::mutex progLock;
    std::vector<std::string> errors ((size_t) threads);
    auto work = [&] (int t)
    {
        auto cb = [&, t] (float p, const std::string&)
        {
            if (cancel != nullptr && cancel->load()) throw Cancelled();
            std::lock_guard<std::mutex> l (progLock);
            prog[(size_t) t] = std::max (prog[(size_t) t], std::min (1.0f, p));
            float sum = 0.0f;
            for (float x : prog) sum += x;
            if (progress) progress (sum / (float) threads);
        };
        try { outs[(size_t) t] = demucscpp::demucs_inference (*model, segs[(size_t) t], cb); }
        catch (const Cancelled&) { errors[(size_t) t] = "cancel"; }
        catch (const std::exception& e) { errors[(size_t) t] = e.what(); }
        catch (...) { errors[(size_t) t] = "error"; }
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back (work, t);
    work (0);
    for (auto& th : pool) th.join();
    for (auto& e : errors)
        if (! e.empty()) { res.error = e == "cancel" ? "Cancelado" : "La separacion fallo: " + e; return res; }

    // put the pieces back; the overlaps cross-fade linearly into each other
    for (int s = 0; s < numStems; ++s)
        for (int c = 0; c < 2; ++c) res.out[s][c].assign ((size_t) n, 0.0f);
    std::vector<float> weight ((size_t) n, 0.0f);
    for (int t = 0; t < threads; ++t)
    {
        const auto& o = outs[(size_t) t];
        const int start = t * segLen;
        const int cols = (int) o.dimension (2);
        for (int i = 0; i < cols; ++i)
        {
            const int dst = start - overlap + i;
            if (dst < 0 || dst >= n) continue;
            // weight: ramps over the overlap at each end of the piece (1 in the middle)
            float w = 1.0f;
            if (i < 2 * overlap && t > 0) w = (float) i / (float) (2 * overlap);
            if (cols - 1 - i < 2 * overlap && t < threads - 1) w = std::min (w, (float) (cols - 1 - i) / (float) (2 * overlap));
            w = std::max (w, 1.0e-4f);
            for (int s = 0; s < numStems; ++s)
                for (int c = 0; c < 2; ++c) res.out[s][c][(size_t) dst] += w * o (s, c, i);
            weight[(size_t) dst] += w;
        }
    }
    for (int s = 0; s < numStems; ++s)
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) res.out[s][c][(size_t) i] /= std::max (1.0e-4f, weight[(size_t) i]);
    if (progress) progress (1.0f);
    res.ok = true;
    return res;
}
}
