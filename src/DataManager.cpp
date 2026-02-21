#include "DataManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>

void DataSet::columnRange(size_t col, float& minVal, float& maxVal) const {
    minVal = std::numeric_limits<float>::max();
    maxVal = std::numeric_limits<float>::lowest();
    for (size_t r = 0; r < numRows; r++) {
        float v = data[r * numCols + col];
        if (std::isfinite(v)) {
            minVal = std::min(minVal, v);
            maxVal = std::max(maxVal, v);
        }
    }
}

bool DataManager::isCommentLine(const std::string& line) const {
    if (line.empty()) return true;
    char first = line[0];
    return first == '#' || first == '!' || first == '%';
}

std::vector<std::string> DataManager::splitTokens(const std::string& line, char delimiter) const {
    std::vector<std::string> tokens;

    if (delimiter == ' ') {
        // Whitespace-delimited: split on any whitespace
        std::istringstream ss(line);
        std::string token;
        while (ss >> token) {
            tokens.push_back(token);
        }
    } else {
        // Character-delimited (comma, tab, etc.)
        std::istringstream ss(line);
        std::string token;
        while (std::getline(ss, token, delimiter)) {
            // Trim whitespace
            auto start = token.find_first_not_of(" \t");
            auto end = token.find_last_not_of(" \t");
            if (start != std::string::npos) {
                tokens.push_back(token.substr(start, end - start + 1));
            } else {
                tokens.emplace_back();  // empty field
            }
        }
    }
    return tokens;
}

bool DataManager::loadAsciiFile(const std::string& path) {
    m_filePath = path;
    m_error.clear();
    m_data = DataSet{};

    std::ifstream file(path);
    if (!file.is_open()) {
        m_error = "Cannot open file: " + path;
        return false;
    }

    // Detect delimiter from file extension
    if (path.size() >= 4) {
        std::string ext = path.substr(path.find_last_of('.') + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == "csv") {
            m_delimiter = ',';
        } else if (ext == "tsv") {
            m_delimiter = '\t';
        } else {
            m_delimiter = ' ';
        }
    }

    std::string line;
    std::string lastCommentLine;

    // Phase 1: Skip header comment block, find column labels
    while (std::getline(file, line)) {
        // Strip trailing \r (Windows line endings)
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (isCommentLine(line)) {
            if (!line.empty())
                lastCommentLine = line;
            continue;
        }

        // First non-comment line: could be labels or data
        break;
    }

    if (file.eof() && line.empty()) {
        m_error = "File is empty or contains only comments";
        return false;
    }

    // Determine if the first non-comment line is labels or data
    auto tokens = splitTokens(line, m_delimiter);
    if (tokens.empty()) {
        m_error = "No data found in file";
        return false;
    }

    // Try to parse first token as a number. If it fails, this line has labels.
    bool firstLineIsLabels = false;
    {
        std::istringstream test(tokens[0]);
        double val;
        if (!(test >> val)) {
            firstLineIsLabels = true;
        }
    }

    std::string firstDataLine;

    if (firstLineIsLabels) {
        m_data.columnLabels = tokens;
        m_data.numCols = tokens.size();
        // Next line will be data
    } else {
        // Check if the last comment line has labels (commented labels)
        if (!lastCommentLine.empty()) {
            std::string labelLine = lastCommentLine.substr(1); // strip comment char
            auto labelTokens = splitTokens(labelLine, m_delimiter);
            if (labelTokens.size() == tokens.size()) {
                m_data.columnLabels = labelTokens;
            }
        }

        // Generate default labels if needed
        if (m_data.columnLabels.empty()) {
            m_data.numCols = tokens.size();
            m_data.columnLabels.resize(m_data.numCols);
            for (size_t i = 0; i < m_data.numCols; i++) {
                m_data.columnLabels[i] = "Column_" + std::to_string(i + 1);
            }
        }

        m_data.numCols = m_data.columnLabels.size();
        firstDataLine = line;  // This line is data, not labels
    }

    // Phase 2: Read data rows
    // Reserve space (estimate based on file size)
    m_data.data.reserve(m_data.numCols * 10000);

    auto parseLine = [&](const std::string& dataLine) -> bool {
        auto dataTokens = splitTokens(dataLine, m_delimiter);
        if (dataTokens.size() < m_data.numCols) {
            return false;  // skip short lines
        }

        for (size_t col = 0; col < m_data.numCols; col++) {
            float val = 0.0f;
            const std::string& tok = dataTokens[col];

            if (tok.empty()) {
                val = 0.0f;
            } else if (tok == "NaN" || tok == "nan" || tok == "NAN" ||
                       tok == "NA" || tok == "na" || tok == "inf" || tok == "-inf") {
                val = 0.0f;
            } else {
                std::istringstream ss(tok);
                double dval;
                if (ss >> dval) {
                    val = static_cast<float>(dval);
                } else {
                    val = 0.0f;  // unparseable → bad value proxy
                }
            }
            m_data.data.push_back(val);
        }
        m_data.numRows++;
        return true;
    };

    // Parse the first data line if we already read it
    if (!firstDataLine.empty()) {
        parseLine(firstDataLine);
    }

    // Read remaining lines
    size_t linesRead = 0;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (isCommentLine(line))
            continue;

        parseLine(line);
        linesRead++;

        if (linesRead % 10000 == 0) {
            fprintf(stderr, "  Read %zu rows...\n", m_data.numRows);
        }
    }

    if (m_data.numRows == 0) {
        m_error = "No valid data rows found";
        return false;
    }

    // Shrink to fit
    m_data.data.shrink_to_fit();

    fprintf(stderr, "Loaded %s: %zu rows x %zu columns\n",
            path.c_str(), m_data.numRows, m_data.numCols);

    return true;
}
