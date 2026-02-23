// Viewpoints (MIT License) - See LICENSE file
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

bool DataManager::loadFile(const std::string& path, ProgressCallback progress, size_t maxRows) {
    std::string ext;
    auto dot = path.find_last_of('.');
    if (dot != std::string::npos) {
        ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    if (ext == "parquet" || ext == "pq") {
        return loadParquetFile(path, progress, maxRows);
    }
    return loadAsciiFile(path, progress, maxRows);
}

bool DataManager::loadAsciiFile(const std::string& path, ProgressCallback progress, size_t maxRows) {
    m_filePath = path;
    m_error.clear();
    m_data = DataSet{};

    std::ifstream file(path);
    if (!file.is_open()) {
        m_error = "Cannot open file: " + path;
        return false;
    }

    // Get file size for progress reporting
    file.seekg(0, std::ios::end);
    size_t totalBytes = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    size_t bytesRead = 0;

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
    // Estimate row count from file size for better reserve
    size_t estRows = (totalBytes > 0 && m_data.numCols > 0) ?
        totalBytes / (m_data.numCols * 8) : 10000;
    m_data.data.reserve(m_data.numCols * estRows);

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
        bytesRead += line.size() + 1;  // +1 for newline

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (isCommentLine(line))
            continue;

        parseLine(line);
        linesRead++;

        // Check row limit
        if (maxRows > 0 && m_data.numRows >= maxRows) {
            fprintf(stderr, "Row limit reached: %zu rows\n", maxRows);
            break;
        }

        // Report progress every 10000 lines
        if (progress && linesRead % 10000 == 0) {
            if (!progress(bytesRead, totalBytes)) {
                m_error = "Loading cancelled";
                return false;
            }
        }
    }

    if (m_data.numRows == 0) {
        m_error = "No valid data rows found";
        return false;
    }

    fprintf(stderr, "Loaded %s: %zu rows x %zu columns\n",
            path.c_str(), m_data.numRows, m_data.numCols);

    // Remove constant columns (all values identical)
    {
        std::vector<bool> keep(m_data.numCols, true);
        int removed = 0;
        for (size_t col = 0; col < m_data.numCols; col++) {
            float first = m_data.data[col];
            bool constant = true;
            for (size_t row = 1; row < m_data.numRows; row++) {
                if (m_data.data[row * m_data.numCols + col] != first) {
                    constant = false;
                    break;
                }
            }
            if (constant) {
                keep[col] = false;
                removed++;
                fprintf(stderr, "  Removing constant column '%s' (value=%.6g)\n",
                        m_data.columnLabels[col].c_str(), first);
            }
        }

        if (removed > 0) {
            size_t newCols = m_data.numCols - removed;
            std::vector<std::string> newLabels;
            std::vector<float> newData;
            newData.reserve(m_data.numRows * newCols);

            for (size_t col = 0; col < m_data.numCols; col++) {
                if (keep[col])
                    newLabels.push_back(m_data.columnLabels[col]);
            }

            for (size_t row = 0; row < m_data.numRows; row++) {
                for (size_t col = 0; col < m_data.numCols; col++) {
                    if (keep[col])
                        newData.push_back(m_data.data[row * m_data.numCols + col]);
                }
            }

            m_data.columnLabels = std::move(newLabels);
            m_data.data = std::move(newData);
            m_data.numCols = newCols;
            fprintf(stderr, "  Removed %d constant columns, %zu columns remaining\n",
                    removed, m_data.numCols);
        }
    }

    m_data.data.shrink_to_fit();

    return true;
}

size_t DataManager::removeSelectedRows(const std::vector<int>& selection) {
    if (selection.size() != m_data.numRows)
        return 0;

    size_t removed = 0;
    std::vector<float> newData;
    newData.reserve(m_data.numRows * m_data.numCols);

    for (size_t row = 0; row < m_data.numRows; row++) {
        if (selection[row] > 0) {
            removed++;
            continue;
        }
        for (size_t col = 0; col < m_data.numCols; col++)
            newData.push_back(m_data.data[row * m_data.numCols + col]);
    }

    m_data.data = std::move(newData);
    m_data.numRows -= removed;
    m_data.data.shrink_to_fit();

    fprintf(stderr, "Removed %zu selected rows, %zu rows remaining\n",
            removed, m_data.numRows);
    return removed;
}

bool DataManager::saveAsCsv(const std::string& path, const std::vector<int>& selection) const {
    std::ofstream file(path);
    if (!file.is_open())
        return false;

    bool filterSelected = !selection.empty() && selection.size() == m_data.numRows;

    // Header
    for (size_t col = 0; col < m_data.numCols; col++) {
        if (col > 0) file << ',';
        file << m_data.columnLabels[col];
    }
    file << '\n';

    // Data rows
    size_t written = 0;
    for (size_t row = 0; row < m_data.numRows; row++) {
        if (filterSelected && selection[row] == 0)
            continue;
        for (size_t col = 0; col < m_data.numCols; col++) {
            if (col > 0) file << ',';
            file << m_data.data[row * m_data.numCols + col];
        }
        file << '\n';
        written++;
    }

    fprintf(stderr, "Saved %zu rows to %s\n", written, path.c_str());
    return true;
}

#ifdef HAS_PARQUET
// Parquet loading via Apache Arrow
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>
#include <parquet/file_reader.h>
#endif

bool DataManager::loadParquetFile(const std::string& path, ProgressCallback progress, size_t maxRows) {
#ifndef HAS_PARQUET
    m_error = "Parquet support not available (install apache-arrow and rebuild)";
    return false;
#else
    m_filePath = path;
    m_error.clear();
    m_data = DataSet{};

    // Open file
    auto result = arrow::io::ReadableFile::Open(path);
    if (!result.ok()) {
        m_error = "Cannot open parquet file: " + result.status().ToString();
        return false;
    }
    auto infile = *result;

    // Create Parquet reader
    std::unique_ptr<parquet::arrow::FileReader> reader;
    auto st = parquet::arrow::FileReader::Make(arrow::default_memory_pool(), parquet::ParquetFileReader::Open(infile), &reader);
    if (!st.ok()) {
        m_error = "Cannot read parquet file: " + st.ToString();
        return false;
    }

    // Read entire table
    std::shared_ptr<arrow::Table> table;
    st = reader->ReadTable(&table);
    if (!st.ok()) {
        m_error = "Failed to read parquet table: " + st.ToString();
        return false;
    }

    int numCols = table->num_columns();
    int64_t numRows = table->num_rows();

    // Apply row limit
    if (maxRows > 0 && numRows > static_cast<int64_t>(maxRows)) {
        table = table->Slice(0, maxRows);
        numRows = maxRows;
        fprintf(stderr, "Parquet: limited to %lld rows (of %lld) x %d columns\n",
                numRows, table->num_rows(), numCols);
    } else {
        fprintf(stderr, "Parquet: %lld rows x %d columns\n", numRows, numCols);
    }

    // Identify numeric columns (skip string/binary/etc)
    std::vector<int> numericCols;
    for (int c = 0; c < numCols; c++) {
        auto type = table->column(c)->type();
        if (type->id() == arrow::Type::DOUBLE || type->id() == arrow::Type::FLOAT ||
            type->id() == arrow::Type::INT8 || type->id() == arrow::Type::INT16 ||
            type->id() == arrow::Type::INT32 || type->id() == arrow::Type::INT64 ||
            type->id() == arrow::Type::UINT8 || type->id() == arrow::Type::UINT16 ||
            type->id() == arrow::Type::UINT32 || type->id() == arrow::Type::UINT64) {
            numericCols.push_back(c);
        }
    }

    if (numericCols.empty()) {
        m_error = "No numeric columns found in parquet file";
        return false;
    }

    m_data.numCols = numericCols.size();
    m_data.numRows = static_cast<size_t>(numRows);

    // Column labels
    for (int c : numericCols) {
        m_data.columnLabels.push_back(table->field(c)->name());
    }

    // Extract data row-major
    fprintf(stderr, "  Allocating %zu x %zu = %zu floats (%.1f MB)\n",
            m_data.numRows, m_data.numCols, m_data.numRows * m_data.numCols,
            m_data.numRows * m_data.numCols * 4.0 / 1048576.0);  // 1048576 = 2^20 = bytes per MB
    fflush(stderr);
    try {
        m_data.data.resize(m_data.numRows * m_data.numCols, 0.0f);
    } catch (const std::exception& e) {
        m_error = std::string("Memory allocation failed: ") + e.what();
        return false;
    }

    // Extract each numeric column using raw value access
    for (size_t ci = 0; ci < numericCols.size(); ci++) {
        int colIdx = numericCols[ci];
        auto chunked = table->column(colIdx);
        auto typeId = chunked->type()->id();
        fprintf(stderr, "  Col %zu/%zu: %s (%s)\n", ci, numericCols.size(),
                table->field(colIdx)->name().c_str(),
                chunked->type()->ToString().c_str());
        fflush(stderr);

        // Use Arrow's built-in GetScalar for safe extraction
        size_t rowOffset = 0;
        for (int chunk = 0; chunk < chunked->num_chunks(); chunk++) {
            auto arr = chunked->chunk(chunk);
            int64_t len = arr->length();

            // Get raw data pointer for fast access
            const uint8_t* rawData = arr->data()->buffers[1]->data();
            int nullCount = arr->null_count();

            for (int64_t r = 0; r < len; r++) {
                size_t idx = (rowOffset + r) * m_data.numCols + ci;
                if (idx >= m_data.data.size()) break;

                if (nullCount > 0 && arr->IsNull(r)) {
                    m_data.data[idx] = 0.0f;
                    continue;
                }

                switch (typeId) {
                    case arrow::Type::DOUBLE:
                        m_data.data[idx] = static_cast<float>(reinterpret_cast<const double*>(rawData)[r]);
                        break;
                    case arrow::Type::FLOAT:
                        m_data.data[idx] = reinterpret_cast<const float*>(rawData)[r];
                        break;
                    case arrow::Type::INT64:
                        m_data.data[idx] = static_cast<float>(reinterpret_cast<const int64_t*>(rawData)[r]);
                        break;
                    case arrow::Type::INT32:
                        m_data.data[idx] = static_cast<float>(reinterpret_cast<const int32_t*>(rawData)[r]);
                        break;
                    case arrow::Type::INT16:
                        m_data.data[idx] = static_cast<float>(reinterpret_cast<const int16_t*>(rawData)[r]);
                        break;
                    case arrow::Type::INT8:
                        m_data.data[idx] = static_cast<float>(reinterpret_cast<const int8_t*>(rawData)[r]);
                        break;
                    case arrow::Type::UINT64:
                        m_data.data[idx] = static_cast<float>(reinterpret_cast<const uint64_t*>(rawData)[r]);
                        break;
                    case arrow::Type::UINT32:
                        m_data.data[idx] = static_cast<float>(reinterpret_cast<const uint32_t*>(rawData)[r]);
                        break;
                    default:
                        m_data.data[idx] = 0.0f;
                        break;
                }
            }
            rowOffset += len;
            fprintf(stderr, "    chunk %d: %lld rows (offset %zu)\n", chunk, len, rowOffset);
            fflush(stderr);
        }

        if (progress && ci % 3 == 0) {
            if (!progress(ci + 1, numericCols.size())) {
                m_error = "Loading cancelled";
                return false;
            }
        }
    }

    fprintf(stderr, "Loaded parquet %s: %zu rows x %zu numeric columns\n",
            path.c_str(), m_data.numRows, m_data.numCols);

    // Remove constant columns (same logic as ASCII loader)
    {
        std::vector<bool> keep(m_data.numCols, true);
        int removed = 0;
        for (size_t col = 0; col < m_data.numCols; col++) {
            float first = m_data.data[col];
            bool constant = true;
            for (size_t row = 1; row < m_data.numRows; row++) {
                if (m_data.data[row * m_data.numCols + col] != first) {
                    constant = false;
                    break;
                }
            }
            if (constant) {
                keep[col] = false;
                removed++;
                fprintf(stderr, "  Removing constant column '%s' (value=%.6g)\n",
                        m_data.columnLabels[col].c_str(), first);
            }
        }
        if (removed > 0) {
            size_t newCols = m_data.numCols - removed;
            std::vector<std::string> newLabels;
            std::vector<float> newData;
            newData.reserve(m_data.numRows * newCols);
            for (size_t col = 0; col < m_data.numCols; col++)
                if (keep[col]) newLabels.push_back(m_data.columnLabels[col]);
            for (size_t row = 0; row < m_data.numRows; row++)
                for (size_t col = 0; col < m_data.numCols; col++)
                    if (keep[col]) newData.push_back(m_data.data[row * m_data.numCols + col]);
            m_data.columnLabels = std::move(newLabels);
            m_data.data = std::move(newData);
            m_data.numCols = newCols;
            fprintf(stderr, "  Removed %d constant columns, %zu remaining\n", removed, m_data.numCols);
        }
    }

    m_data.data.shrink_to_fit();
    return true;
#endif // HAS_PARQUET
}
