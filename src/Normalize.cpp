// Viewpoints (MIT License) - See LICENSE file
#include "Normalize.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>

const char* NormModeName(NormMode mode) {
    switch (mode) {
        case NormMode::MinMax:     return "Min-Max";
        case NormMode::ZeroMax:    return "+ only";
        case NormMode::MaxAbs:    return "Max |val|";
        case NormMode::Trim1e2:    return "Trim 1%";
        case NormMode::Trim1e3:    return "Trim 0.1%";
        case NormMode::ThreeSigma: return "3 Sigma";
        case NormMode::Log10:      return "Log10";
        case NormMode::Arctan:     return "Arctan";
        case NormMode::Rank:       return "Rank";
        case NormMode::Gaussianize:return "Gaussian";
        default:                   return "Unknown";
    }
}

std::vector<std::string> AllNormModeNames() {
    std::vector<std::string> names;
    for (int i = 0; i < static_cast<int>(NormMode::COUNT); i++)
        names.push_back(NormModeName(static_cast<NormMode>(i)));
    return names;
}

// Helper: extract column values from strided data
static std::vector<float> extractColumn(const float* data, size_t numRows, size_t stride) {
    std::vector<float> col(numRows);
    for (size_t i = 0; i < numRows; i++)
        col[i] = data[i * stride];
    return col;
}

// Helper: collect only finite values (filtering NaN/Inf)
static std::vector<float> finiteValues(const std::vector<float>& values) {
    std::vector<float> out;
    out.reserve(values.size());
    for (float v : values)
        if (std::isfinite(v)) out.push_back(v);
    return out;
}

// Helper: NaN-safe min and max over a vector
static void finiteMinMax(const std::vector<float>& values, float& mn, float& mx) {
    mn = std::numeric_limits<float>::max();
    mx = std::numeric_limits<float>::lowest();
    for (float v : values) {
        if (!std::isfinite(v)) continue;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    if (mn > mx) { mn = 0.0f; mx = 1.0f; }  // all NaN fallback
}

// Helper: map finite values from [inMin, inMax] to [-0.9, 0.9]; NaN passes through
static void mapToDisplay(std::vector<float>& values, float inMin, float inMax) {
    float range = inMax - inMin;
    if (range == 0.0f) range = 1.0f;
    for (auto& v : values) {
        if (!std::isfinite(v)) continue;
        v = ((v - inMin) / range) * 1.8f - 0.9f;
    }
}

// Helper: clamp finite values to [lo, hi]; NaN passes through
static void clampValues(std::vector<float>& values, float lo, float hi) {
    for (auto& v : values) {
        if (!std::isfinite(v)) continue;
        v = std::max(lo, std::min(hi, v));
    }
}

// Helper: percentile (linear interpolation) on pre-sorted finite values
static float percentile(std::vector<float>& sorted, float p) {
    if (sorted.empty()) return 0.0f;
    float idx = p * (sorted.size() - 1);
    size_t lo = static_cast<size_t>(idx);
    size_t hi = std::min(lo + 1, sorted.size() - 1);
    float frac = idx - lo;
    return sorted[lo] * (1.0f - frac) + sorted[hi] * frac;
}

// Inverse error function approximation (for Gaussianize)
static float erfinv(float x) {
    float tt1, tt2, lnx, sgn;
    sgn = (x < 0) ? -1.0f : 1.0f;
    x = std::abs(x);
    if (x >= 1.0f) return sgn * 6.0f;  // clamp
    lnx = std::log(1.0f - x * x);
    tt1 = 2.0f / (3.14159265f * 0.147f) + 0.5f * lnx;
    tt2 = lnx / 0.147f;
    return sgn * std::sqrt(-tt1 + std::sqrt(tt1 * tt1 - tt2));
}

std::vector<float> NormalizeColumn(const float* rawData, size_t numRows,
                                   size_t stride, NormMode mode) {
    if (numRows == 0) return {};

    auto values = extractColumn(rawData, numRows, stride);

    switch (mode) {

        case NormMode::MinMax: {
            float mn, mx;
            finiteMinMax(values, mn, mx);
            float range = mx - mn;
            if (range == 0.0f) range = 1.0f;
            for (auto& v : values) {
                if (!std::isfinite(v)) continue;
                v = (v - mn) / range;
            }
            mapToDisplay(values, 0.0f, 1.0f);
            break;
        }

        case NormMode::ZeroMax: {
            float mn, mx;
            finiteMinMax(values, mn, mx);
            if (mx == 0.0f) mx = 1.0f;
            for (auto& v : values) {
                if (!std::isfinite(v)) continue;
                v = v / mx;
            }
            mapToDisplay(values, 0.0f, 1.0f);
            break;
        }

        case NormMode::MaxAbs: {
            float maxAbs = 0.0f;
            for (auto v : values)
                if (std::isfinite(v))
                    maxAbs = std::max(maxAbs, std::abs(v));
            if (maxAbs == 0.0f) maxAbs = 1.0f;
            for (auto& v : values) {
                if (!std::isfinite(v)) continue;
                v = v / maxAbs;
            }
            mapToDisplay(values, -1.0f, 1.0f);
            break;
        }

        case NormMode::Trim1e2:
        case NormMode::Trim1e3: {
            float pLo = (mode == NormMode::Trim1e2) ? 0.01f : 0.001f;
            float pHi = 1.0f - pLo;
            auto sorted = finiteValues(values);
            std::sort(sorted.begin(), sorted.end());
            float lo = percentile(sorted, pLo);
            float hi = percentile(sorted, pHi);
            clampValues(values, lo, hi);
            mapToDisplay(values, lo, hi);
            break;
        }

        case NormMode::ThreeSigma: {
            double sum = 0.0, sum2 = 0.0;
            size_t count = 0;
            for (auto v : values) {
                if (!std::isfinite(v)) continue;
                sum += v;
                sum2 += (double)v * v;
                count++;
            }
            if (count == 0) break;
            float mean = static_cast<float>(sum / count);
            float var = static_cast<float>(sum2 / count - (double)mean * mean);
            float sigma = std::sqrt(std::max(var, 0.0f));
            if (sigma == 0.0f) sigma = 1.0f;
            float lo = mean - 3.0f * sigma;
            float hi = mean + 3.0f * sigma;
            clampValues(values, lo, hi);
            mapToDisplay(values, lo, hi);
            break;
        }

        case NormMode::Log10: {
            float mn, mx;
            finiteMinMax(values, mn, mx);
            float shift = (mn <= 0.0f) ? (1.0f - mn) : 0.0f;
            for (auto& v : values) {
                if (!std::isfinite(v)) continue;
                v = std::log10(v + shift);
            }
            float logMin, logMax;
            finiteMinMax(values, logMin, logMax);
            mapToDisplay(values, logMin, logMax);
            break;
        }

        case NormMode::Arctan: {
            auto sorted = finiteValues(values);
            std::sort(sorted.begin(), sorted.end());
            float median = percentile(sorted, 0.5f);
            float q1 = percentile(sorted, 0.25f);
            float q3 = percentile(sorted, 0.75f);
            float iqr = q3 - q1;
            if (iqr == 0.0f) iqr = 1.0f;
            for (auto& v : values) {
                if (!std::isfinite(v)) continue;
                v = std::atan((v - median) / iqr) * (2.0f / 3.14159265f);
            }
            mapToDisplay(values, -1.0f, 1.0f);
            break;
        }

        case NormMode::Rank: {
            // Rank only finite values; NaN keeps NaN
            size_t n = values.size();
            // Collect indices of finite values
            std::vector<size_t> finiteIdx;
            finiteIdx.reserve(n);
            for (size_t i = 0; i < n; i++)
                if (std::isfinite(values[i])) finiteIdx.push_back(i);

            size_t nf = finiteIdx.size();
            if (nf == 0) break;

            std::sort(finiteIdx.begin(), finiteIdx.end(),
                      [&](size_t a, size_t b) { return values[a] < values[b]; });

            std::vector<float> ranks(n, std::numeric_limits<float>::quiet_NaN());
            size_t i = 0;
            while (i < nf) {
                size_t j = i;
                while (j < nf && values[finiteIdx[j]] == values[finiteIdx[i]]) j++;
                float avgRank = static_cast<float>(i + j - 1) / 2.0f;
                for (size_t k = i; k < j; k++)
                    ranks[finiteIdx[k]] = avgRank;
                i = j;
            }
            float maxRank = static_cast<float>(nf - 1);
            if (maxRank == 0.0f) maxRank = 1.0f;
            for (size_t i = 0; i < n; i++)
                if (std::isfinite(ranks[i]))
                    ranks[i] = ranks[i] / maxRank;
            values = ranks;
            mapToDisplay(values, 0.0f, 1.0f);
            break;
        }

        case NormMode::Gaussianize: {
            size_t n = values.size();
            std::vector<size_t> finiteIdx;
            finiteIdx.reserve(n);
            for (size_t i = 0; i < n; i++)
                if (std::isfinite(values[i])) finiteIdx.push_back(i);

            size_t nf = finiteIdx.size();
            if (nf == 0) break;

            std::sort(finiteIdx.begin(), finiteIdx.end(),
                      [&](size_t a, size_t b) { return values[a] < values[b]; });

            std::vector<float> result(n, std::numeric_limits<float>::quiet_NaN());
            for (size_t i = 0; i < nf; i++) {
                float p = (static_cast<float>(i) + 0.5f) / static_cast<float>(nf);
                result[finiteIdx[i]] = 1.4142135f * erfinv(2.0f * p - 1.0f);
            }
            float mn, mx;
            finiteMinMax(result, mn, mx);
            values = result;
            mapToDisplay(values, mn, mx);
            break;
        }

        default:
            break;
    }

    return values;
}
