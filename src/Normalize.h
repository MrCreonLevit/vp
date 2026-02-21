#pragma once

#include <vector>
#include <string>

enum class NormMode {
    None = 0,       // Raw data, min-max mapped to fill view
    MinMax,         // Normalize to [0, 1]
    ZeroMax,        // Normalize to [0, max]
    MaxAbs,         // Normalize by max absolute value to [-1, 1]
    Trim1e2,        // Trim to 1st-99th percentile, then min-max
    Trim1e3,        // Trim to 0.1st-99.9th percentile, then min-max
    ThreeSigma,     // Mean +/- 3 standard deviations
    Log10,          // Log base 10 (shifted so min > 0)
    Arctan,         // Arctangent (sigmoid-like, maps (-inf,inf) to (-1,1))
    Rank,           // Rank ordering (ties get average rank)
    Gaussianize,    // Rank then inverse normal CDF
    COUNT
};

// Returns display name for each mode
const char* NormModeName(NormMode mode);

// Returns a list of all mode names (for populating a dropdown)
std::vector<std::string> AllNormModeNames();

// Normalize a column of data values.
// Input: raw values for N data rows.
// Output: normalized values mapped to [-0.9, 0.9] range for display.
std::vector<float> NormalizeColumn(const float* rawData, size_t numRows,
                                   size_t stride, NormMode mode);
