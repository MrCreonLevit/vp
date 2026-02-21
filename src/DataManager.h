// Viewpoints (MIT License) - See LICENSE file
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstddef>

struct DataSet {
    std::vector<std::string> columnLabels;
    std::vector<float> data;  // row-major: data[row * numCols + col]
    size_t numRows = 0;
    size_t numCols = 0;

    float value(size_t row, size_t col) const {
        return data[row * numCols + col];
    }

    // Get a column's min and max values
    void columnRange(size_t col, float& minVal, float& maxVal) const;
};

class DataManager {
public:
    // Load an ASCII data file. Returns true on success.
    // Progress callback receives (bytesRead, totalBytes), returns false to cancel.
    using ProgressCallback = std::function<bool(size_t bytesRead, size_t totalBytes)>;
    bool loadAsciiFile(const std::string& path, ProgressCallback progress = nullptr);

    const DataSet& dataset() const { return m_data; }
    const std::string& errorMessage() const { return m_error; }
    const std::string& filePath() const { return m_filePath; }

    // Remove rows where selection[row] > 0. Returns number of rows removed.
    size_t removeSelectedRows(const std::vector<int>& selection);

    // Save data to CSV. If selection is provided, only saves rows where selection[row] > 0.
    bool saveAsCsv(const std::string& path, const std::vector<int>& selection = {}) const;

private:
    bool isCommentLine(const std::string& line) const;
    std::vector<std::string> splitTokens(const std::string& line, char delimiter) const;

    DataSet m_data;
    std::string m_filePath;
    std::string m_error;
    char m_delimiter = ' ';  // whitespace by default
};
