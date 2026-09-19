// Viewpoints (MIT License) - See LICENSE file
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstddef>
#include <cstdint>

// Load safety limits. maxRows 0 still means unlimited (CLI `-n 0`).
constexpr size_t kDefaultMaxRows = 20'000'000;
constexpr size_t kMaxColumns = 4096;
constexpr size_t kMaxAsciiLineBytes = 1'000'000;
constexpr size_t kMaxStdinSnapshotLines = 20'000'000;
constexpr size_t kMaxStdinSnapshotBytes = 512ull * 1024 * 1024;
constexpr size_t kMaxPngBlobBytes = 256 * 1024;
constexpr uint32_t kMaxPngDecodeDim = 1024;

// True if bytes look like a PNG whose IHDR width/height are in (0, maxDim]
// and whose size is at most maxBytes. Used before wxImage decode.
inline bool pngIhdrWithinLimits(const uint8_t* p, size_t len,
                                size_t maxBytes = kMaxPngBlobBytes,
                                uint32_t maxDim = kMaxPngDecodeDim) {
    if (!p || len < 24 || len > maxBytes) return false;
    if (p[0] != 0x89 || p[1] != 'P' || p[2] != 'N' || p[3] != 'G') return false;
    if (p[12] != 'I' || p[13] != 'H' || p[14] != 'D' || p[15] != 'R') return false;
    uint32_t w = (uint32_t(p[16]) << 24) | (uint32_t(p[17]) << 16) |
                 (uint32_t(p[18]) << 8) | uint32_t(p[19]);
    uint32_t h = (uint32_t(p[20]) << 24) | (uint32_t(p[21]) << 16) |
                 (uint32_t(p[22]) << 8) | uint32_t(p[23]);
    return w > 0 && h > 0 && w <= maxDim && h <= maxDim;
}

struct ColumnMeta {
    bool isCategorical = false;
    std::vector<std::string> categories; // sorted alphabetically; index = float value stored in data
};

struct DataSet {
    std::vector<std::string> columnLabels;
    std::vector<float> data;  // row-major: data[row * numCols + col]
    std::vector<ColumnMeta> columnMeta; // parallel to columnLabels, one per column
    size_t numRows = 0;
    size_t numCols = 0;

    float value(size_t row, size_t col) const {
        return data[row * numCols + col];
    }

    // Get a column's min and max values
    void columnRange(size_t col, float& minVal, float& maxVal) const;

    // Name of a parquet binary PNG column, if present. Bytes are not stored
    // here — DataManager::readPointImage() loads one row on demand.
    std::string pointImageColumnName;
};

class DataManager {
public:
    // Load an ASCII data file. Returns true on success.
    // Progress callback receives (bytesRead, totalBytes), returns false to cancel.
    using ProgressCallback = std::function<bool(size_t bytesRead, size_t totalBytes)>;
    bool loadFile(const std::string& path, ProgressCallback progress = nullptr, size_t maxRows = 0);
    bool loadAsciiFile(const std::string& path, ProgressCallback progress = nullptr, size_t maxRows = 0);
    bool loadParquetFile(const std::string& path, ProgressCallback progress = nullptr, size_t maxRows = 0);

    const DataSet& dataset() const { return m_data; }
    const std::string& errorMessage() const { return m_error; }
    const std::string& filePath() const { return m_filePath; }

    bool hasPointImages() const { return !m_pngFileRows.empty(); }
    // Load the PNG for one dataset row from the source parquet (cached).
    bool readPointImage(size_t row, std::vector<uint8_t>& out) const;

    // Remove rows where selection[row] > 0. Returns number of rows removed.
    size_t removeSelectedRows(const std::vector<int>& selection);

    // Replace dataset from pre-parsed text lines (header + data rows).
    // The first element of `lines` is the column header; the rest are data rows.
    // Returns true on success.
    bool replaceFromLines(const std::vector<std::string>& lines);

    // Save data to CSV. If selection is provided, only saves rows where selection[row] > 0.
    bool saveAsCsv(const std::string& path, const std::vector<int>& selection = {}) const;

    // Save data to Parquet. If selection is provided, only saves rows where selection[row] > 0.
    bool saveAsParquet(const std::string& path, const std::vector<int>& selection = {}) const;

private:
    void appendRowIndexColumn();
    bool isCommentLine(const std::string& line) const;
    std::vector<std::string> splitTokens(const std::string& line, char delimiter) const;
    void clearPointImageSource();

    DataSet m_data;
    std::string m_filePath;
    std::string m_error;
    char m_delimiter = ' ';  // whitespace by default

    // Lazy PNG: parquet column index and per-dataset-row file row.
    int m_pngParquetCol = -1;
    std::vector<int64_t> m_pngFileRows;
    mutable int64_t m_pngCacheFileRow = -1;
    mutable std::vector<uint8_t> m_pngCache;
};
