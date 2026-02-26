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
        case NormMode::MaxAbs:     return "Max |val|";
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

// Helper: map values from [inMin, inMax] to [-0.9, 0.9]
static void mapToDisplay(std::vector<float>& values, float inMin, float inMax) {
    float range = inMax - inMin;
    if (range == 0.0f) range = 1.0f;
    for (auto& v : values)
        v = ((v - inMin) / range) * 1.8f - 0.9f;
}

// Helper: clamp values to [lo, hi]
static void clampValues(std::vector<float>& values, float lo, float hi) {
    for (auto& v : values)
        v = std::max(lo, std::min(hi, v));
}

// Helper: percentile (linear interpolation)
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
            float mn = *std::min_element(values.begin(), values.end());
            float mx = *std::max_element(values.begin(), values.end());
            float range = mx - mn;
            if (range == 0.0f) range = 1.0f;
            for (auto& v : values)
                v = (v - mn) / range;
            mapToDisplay(values, 0.0f, 1.0f);
            break;
        }

        case NormMode::ZeroMax: {
            float mx = *std::max_element(values.begin(), values.end());
            if (mx == 0.0f) mx = 1.0f;
            for (auto& v : values)
                v = v / mx;
            mapToDisplay(values, 0.0f, 1.0f);
            break;
        }

        case NormMode::MaxAbs: {
            float maxAbs = 0.0f;
            for (auto v : values)
                maxAbs = std::max(maxAbs, std::abs(v));
            if (maxAbs == 0.0f) maxAbs = 1.0f;
            for (auto& v : values)
                v = v / maxAbs;
            mapToDisplay(values, -1.0f, 1.0f);
            break;
        }

        case NormMode::Trim1e2:
        case NormMode::Trim1e3: {
            float pLo = (mode == NormMode::Trim1e2) ? 0.01f : 0.001f;
            float pHi = 1.0f - pLo;
            auto sorted = values;
            std::sort(sorted.begin(), sorted.end());
            float lo = percentile(sorted, pLo);
            float hi = percentile(sorted, pHi);
            clampValues(values, lo, hi);
            mapToDisplay(values, lo, hi);
            break;
        }

        case NormMode::ThreeSigma: {
            double sum = 0.0, sum2 = 0.0;
            for (auto v : values) { sum += v; sum2 += (double)v * v; }
            float mean = static_cast<float>(sum / values.size());
            float var = static_cast<float>(sum2 / values.size() - (double)mean * mean);
            float sigma = std::sqrt(std::max(var, 0.0f));
            if (sigma == 0.0f) sigma = 1.0f;
            float lo = mean - 3.0f * sigma;
            float hi = mean + 3.0f * sigma;
            clampValues(values, lo, hi);
            mapToDisplay(values, lo, hi);
            break;
        }

        case NormMode::Log10: {
            // Shift so minimum is 1.0, then log10
            float mn = *std::min_element(values.begin(), values.end());
            float shift = (mn <= 0.0f) ? (1.0f - mn) : 0.0f;
            for (auto& v : values)
                v = std::log10(v + shift);
            float logMin = *std::min_element(values.begin(), values.end());
            float logMax = *std::max_element(values.begin(), values.end());
            mapToDisplay(values, logMin, logMax);
            break;
        }

        case NormMode::Arctan: {
            // Center on median, scale by IQR
            auto sorted = values;
            std::sort(sorted.begin(), sorted.end());
            float median = percentile(sorted, 0.5f);
            float q1 = percentile(sorted, 0.25f);
            float q3 = percentile(sorted, 0.75f);
            float iqr = q3 - q1;
            if (iqr == 0.0f) iqr = 1.0f;
            for (auto& v : values)
                v = std::atan((v - median) / iqr) * (2.0f / 3.14159265f);
            mapToDisplay(values, -1.0f, 1.0f);
            break;
        }

        case NormMode::Rank: {
            // Rank with average rank for ties
            size_t n = values.size();
            std::vector<size_t> idx(n);
            std::iota(idx.begin(), idx.end(), 0);
            std::sort(idx.begin(), idx.end(),
                      [&](size_t a, size_t b) { return values[a] < values[b]; });

            std::vector<float> ranks(n);
            size_t i = 0;
            while (i < n) {
                size_t j = i;
                while (j < n && values[idx[j]] == values[idx[i]]) j++;
                float avgRank = static_cast<float>(i + j - 1) / 2.0f;
                for (size_t k = i; k < j; k++)
                    ranks[idx[k]] = avgRank;
                i = j;
            }
            // Map ranks to [0, 1]
            float maxRank = static_cast<float>(n - 1);
            if (maxRank == 0.0f) maxRank = 1.0f;
            for (auto& r : ranks)
                r = r / maxRank;
            values = ranks;
            mapToDisplay(values, 0.0f, 1.0f);
            break;
        }

        case NormMode::Gaussianize: {
            // Rank then inverse normal CDF (via erfinv)
            size_t n = values.size();
            std::vector<size_t> idx(n);
            std::iota(idx.begin(), idx.end(), 0);
            std::sort(idx.begin(), idx.end(),
                      [&](size_t a, size_t b) { return values[a] < values[b]; });

            std::vector<float> result(n);
            for (size_t i = 0; i < n; i++) {
                // Map rank to (0, 1) avoiding exact 0 and 1
                float p = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
                // Inverse normal CDF: sqrt(2) * erfinv(2p - 1)
                result[idx[i]] = 1.4142135f * erfinv(2.0f * p - 1.0f);
            }
            // Clamp and map
            float mn = *std::min_element(result.begin(), result.end());
            float mx = *std::max_element(result.begin(), result.end());
            values = result;
            mapToDisplay(values, mn, mx);
            break;
        }

        default:
            break;
    }

    return values;
}
