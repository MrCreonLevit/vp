#pragma once

#include <string>
#include <vector>
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
    bool loadAsciiFile(const std::string& path);

    const DataSet& dataset() const { return m_data; }
    const std::string& errorMessage() const { return m_error; }
    const std::string& filePath() const { return m_filePath; }

private:
    bool isCommentLine(const std::string& line) const;
    std::vector<std::string> splitTokens(const std::string& line, char delimiter) const;

    DataSet m_data;
    std::string m_filePath;
    std::string m_error;
    char m_delimiter = ' ';  // whitespace by default
};
