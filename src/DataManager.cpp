// Viewpoints (MIT License) - See LICENSE file
#include "DataManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
#include <set>
#include <map>
#include <unordered_map>
#include <chrono>

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
    using Clock = std::chrono::steady_clock;
    auto tStart = Clock::now();
    auto elapsed = [](Clock::time_point since) {
        return std::chrono::duration<double>(Clock::now() - since).count();
    };

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

    // Treat as header only if ALL tokens fail numeric parse.
    // If at least one token parses as a number, the line is data.
    bool firstLineIsLabels = true;
    for (const auto& tok : tokens) {
        std::istringstream test(tok);
        double val;
        if (test >> val) {
            firstLineIsLabels = false;
            break;
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

    // Track candidate categorical (string) columns
    std::vector<bool> candidateCategorical(m_data.numCols, false);
    std::vector<std::vector<std::string>> rawStrings(m_data.numCols);
    bool firstDataRow = true;

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
                if (candidateCategorical[col])
                    rawStrings[col].push_back(tok);
            } else if (candidateCategorical[col]) {
                // Already known categorical — store raw string, placeholder float
                rawStrings[col].push_back(tok);
                val = 0.0f;
            } else {
                // Try numeric parse
                bool isSpecial = (tok == "NaN" || tok == "nan" || tok == "NAN" ||
                                  tok == "NA" || tok == "na" || tok == "inf" || tok == "-inf");
                if (isSpecial) {
                    val = 0.0f;
                } else {
                    std::istringstream ss(tok);
                    double dval;
                    if (ss >> dval) {
                        val = static_cast<float>(dval);
                    } else {
                        // Failed numeric parse
                        if (firstDataRow) {
                            // Mark as candidate categorical
                            candidateCategorical[col] = true;
                            rawStrings[col].push_back(tok);
                            val = 0.0f;
                        } else {
                            val = 0.0f;  // unparseable in non-first row of numeric col
                        }
                    }
                }
            }
            m_data.data.push_back(val);
        }
        m_data.numRows++;
        firstDataRow = false;
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
    fprintf(stderr, "TIMING:   read+parse        %.3f s\n", elapsed(tStart));

    // Encode confirmed categorical columns
    auto tCat = Clock::now();
    m_data.columnMeta.resize(m_data.numCols);
    for (size_t col = 0; col < m_data.numCols; col++) {
        if (candidateCategorical[col] && !rawStrings[col].empty()) {
            // Collect unique strings with O(1) lookup
            std::unordered_map<std::string, int> tempMap;
            tempMap.reserve(rawStrings[col].size());
            std::vector<std::string> uniqueStrings;
            for (const auto& s : rawStrings[col]) {
                if (tempMap.emplace(s, 0).second)
                    uniqueStrings.push_back(s);
            }
            std::sort(uniqueStrings.begin(), uniqueStrings.end());
            // Assign sorted indices
            std::unordered_map<std::string, int> indexMap;
            indexMap.reserve(uniqueStrings.size() * 2);
            for (int i = 0; i < (int)uniqueStrings.size(); i++)
                indexMap[uniqueStrings[i]] = i;

            // Overwrite placeholder floats with category indices
            for (size_t row = 0; row < m_data.numRows; row++)
                m_data.data[row * m_data.numCols + col] = static_cast<float>(indexMap[rawStrings[col][row]]);

            m_data.columnMeta[col].isCategorical = true;
            m_data.columnMeta[col].categories = std::move(uniqueStrings);
            fprintf(stderr, "  Categorical column '%s': %zu categories\n",
                    m_data.columnLabels[col].c_str(), m_data.columnMeta[col].categories.size());
        }
        // Free raw string memory
        rawStrings[col].clear();
        rawStrings[col].shrink_to_fit();
    }

    fprintf(stderr, "TIMING:   categorical encode %.3f s\n", elapsed(tCat));

    // Remove constant columns (all values identical)
    auto tConst = Clock::now();
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
            std::vector<ColumnMeta> newMeta;
            std::vector<float> newData;
            newData.reserve(m_data.numRows * newCols);

            for (size_t col = 0; col < m_data.numCols; col++) {
                if (keep[col]) {
                    newLabels.push_back(m_data.columnLabels[col]);
                    newMeta.push_back(m_data.columnMeta[col]);
                }
            }

            for (size_t row = 0; row < m_data.numRows; row++) {
                for (size_t col = 0; col < m_data.numCols; col++) {
                    if (keep[col])
                        newData.push_back(m_data.data[row * m_data.numCols + col]);
                }
            }

            m_data.columnLabels = std::move(newLabels);
            m_data.columnMeta = std::move(newMeta);
            m_data.data = std::move(newData);
            m_data.numCols = newCols;
            fprintf(stderr, "  Removed %d constant columns, %zu columns remaining\n",
                    removed, m_data.numCols);
        }
    }

    m_data.data.shrink_to_fit();
    fprintf(stderr, "TIMING:   const col removal  %.3f s\n", elapsed(tConst));
    fprintf(stderr, "TIMING:   ASCII loader total %.3f s\n", elapsed(tStart));

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
            float val = m_data.data[row * m_data.numCols + col];
            if (col < m_data.columnMeta.size() && m_data.columnMeta[col].isCategorical) {
                const auto& cats = m_data.columnMeta[col].categories;
                int idx = std::max(0, std::min(static_cast<int>(val), static_cast<int>(cats.size()) - 1));
                file << cats[idx];
            } else {
                file << val;
            }
        }
        file << '\n';
        written++;
    }

    fprintf(stderr, "Saved %zu rows to %s\n", written, path.c_str());
    return true;
}

#ifdef HAS_PARQUET
// Parquet I/O via Apache Arrow
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <parquet/file_reader.h>
#endif

bool DataManager::saveAsParquet(const std::string& path, const std::vector<int>& selection) const {
#ifndef HAS_PARQUET
    fprintf(stderr, "Parquet support not available\n");
    return false;
#else
    bool filterSelected = !selection.empty() && selection.size() == m_data.numRows;

    // Count output rows
    size_t outRows = 0;
    if (filterSelected) {
        for (size_t r = 0; r < m_data.numRows; r++)
            if (selection[r] > 0) outRows++;
    } else {
        outRows = m_data.numRows;
    }

    // Build one array per column (Float32 for numeric, String for categorical)
    std::vector<std::shared_ptr<arrow::Field>> fields;
    std::vector<std::shared_ptr<arrow::Array>> arrays;

    for (size_t col = 0; col < m_data.numCols; col++) {
        bool isCat = col < m_data.columnMeta.size() && m_data.columnMeta[col].isCategorical;

        if (isCat) {
            fields.push_back(arrow::field(m_data.columnLabels[col], arrow::utf8()));
            arrow::StringBuilder builder;
            auto st = builder.Reserve(static_cast<int64_t>(outRows));
            if (!st.ok()) { fprintf(stderr, "Arrow reserve failed\n"); return false; }

            const auto& cats = m_data.columnMeta[col].categories;
            for (size_t row = 0; row < m_data.numRows; row++) {
                if (filterSelected && selection[row] == 0) continue;
                float val = m_data.data[row * m_data.numCols + col];
                int idx = std::max(0, std::min(static_cast<int>(val), static_cast<int>(cats.size()) - 1));
                st = builder.Append(cats[idx]);
                if (!st.ok()) { fprintf(stderr, "Arrow append failed\n"); return false; }
            }

            std::shared_ptr<arrow::Array> arr;
            st = builder.Finish(&arr);
            if (!st.ok()) { fprintf(stderr, "Arrow build failed\n"); return false; }
            arrays.push_back(arr);
        } else {
            fields.push_back(arrow::field(m_data.columnLabels[col], arrow::float32()));
            arrow::FloatBuilder builder;
            auto st = builder.Reserve(static_cast<int64_t>(outRows));
            if (!st.ok()) { fprintf(stderr, "Arrow reserve failed\n"); return false; }

            for (size_t row = 0; row < m_data.numRows; row++) {
                if (filterSelected && selection[row] == 0) continue;
                builder.UnsafeAppend(m_data.data[row * m_data.numCols + col]);
            }

            std::shared_ptr<arrow::Array> arr;
            st = builder.Finish(&arr);
            if (!st.ok()) { fprintf(stderr, "Arrow build failed\n"); return false; }
            arrays.push_back(arr);
        }
    }

    auto schema = arrow::schema(fields);
    auto table = arrow::Table::Make(schema, arrays, static_cast<int64_t>(outRows));

    auto outResult = arrow::io::FileOutputStream::Open(path);
    if (!outResult.ok()) {
        fprintf(stderr, "Failed to open %s for writing\n", path.c_str());
        return false;
    }

    auto st = parquet::arrow::WriteTable(*table, arrow::default_memory_pool(),
                                          outResult.ValueOrDie(), outRows);
    if (!st.ok()) {
        fprintf(stderr, "Parquet write failed: %s\n", st.ToString().c_str());
        return false;
    }

    fprintf(stderr, "Saved %zu rows to %s\n", outRows, path.c_str());
    return true;
#endif
}

#ifdef HAS_PARQUET
// Type-dispatched column extraction — switch is outside the inner loop
// so the compiler can optimize each instantiation as a tight typed loop.
template<typename T>
static void extractTypedColumn(const T* src, int64_t len, size_t rowOffset,
                               float* data, size_t numCols, size_t ci,
                               const std::shared_ptr<arrow::Array>& arr, int nullCount) {
    if (nullCount == 0) {
        for (int64_t r = 0; r < len; r++)
            data[(rowOffset + r) * numCols + ci] = static_cast<float>(src[r]);
    } else {
        for (int64_t r = 0; r < len; r++) {
            if (arr->IsNull(r))
                data[(rowOffset + r) * numCols + ci] = 0.0f;
            else
                data[(rowOffset + r) * numCols + ci] = static_cast<float>(src[r]);
        }
    }
}
#endif

bool DataManager::loadParquetFile(const std::string& path, ProgressCallback progress, size_t maxRows) {
#ifndef HAS_PARQUET
    m_error = "Parquet support not available (install apache-arrow and rebuild)";
    return false;
#else
    using Clock = std::chrono::steady_clock;
    auto tStart = Clock::now();
    auto elapsed = [](Clock::time_point since) {
        return std::chrono::duration<double>(Clock::now() - since).count();
    };

    m_filePath = path;
    m_error.clear();
    m_data = DataSet{};

    // Open file
    auto tRead = Clock::now();
    auto result = arrow::io::ReadableFile::Open(path);
    if (!result.ok()) {
        m_error = "Cannot open parquet file: " + result.status().ToString();
        return false;
    }
    auto infile = *result;

    // Create Parquet reader
    auto readerResult = parquet::arrow::FileReader::Make(
        arrow::default_memory_pool(), parquet::ParquetFileReader::Open(infile));
    if (!readerResult.ok()) {
        m_error = "Cannot read parquet file: " + readerResult.status().ToString();
        return false;
    }
    auto reader = std::move(*readerResult);

    // Read entire table
    std::shared_ptr<arrow::Table> table;
    auto st = reader->ReadTable(&table);
    if (!st.ok()) {
        m_error = "Failed to read parquet table: " + st.ToString();
        return false;
    }

    fprintf(stderr, "TIMING:   parquet read       %.3f s\n", elapsed(tRead));

    auto tExtract = Clock::now();
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

    // Identify accepted columns (numeric + string types)
    std::vector<int> acceptedCols;
    std::vector<bool> isStringCol;
    for (int c = 0; c < numCols; c++) {
        auto type = table->column(c)->type();
        if (type->id() == arrow::Type::DOUBLE || type->id() == arrow::Type::FLOAT ||
            type->id() == arrow::Type::INT8 || type->id() == arrow::Type::INT16 ||
            type->id() == arrow::Type::INT32 || type->id() == arrow::Type::INT64 ||
            type->id() == arrow::Type::UINT8 || type->id() == arrow::Type::UINT16 ||
            type->id() == arrow::Type::UINT32 || type->id() == arrow::Type::UINT64) {
            acceptedCols.push_back(c);
            isStringCol.push_back(false);
        } else if (type->id() == arrow::Type::STRING || type->id() == arrow::Type::LARGE_STRING) {
            acceptedCols.push_back(c);
            isStringCol.push_back(true);
        }
    }

    if (acceptedCols.empty()) {
        m_error = "No usable columns found in parquet file";
        return false;
    }

    m_data.numCols = acceptedCols.size();
    m_data.numRows = static_cast<size_t>(numRows);
    m_data.columnMeta.resize(m_data.numCols);

    // Column labels
    for (int c : acceptedCols) {
        m_data.columnLabels.push_back(table->field(c)->name());
    }

    // Extract data row-major
    fprintf(stderr, "  Allocating %zu x %zu = %zu floats (%.1f MB)\n",
            m_data.numRows, m_data.numCols, m_data.numRows * m_data.numCols,
            m_data.numRows * m_data.numCols * 4.0 / 1048576.0);
    fflush(stderr);
    try {
        m_data.data.resize(m_data.numRows * m_data.numCols, 0.0f);
    } catch (const std::exception& e) {
        m_error = std::string("Memory allocation failed: ") + e.what();
        return false;
    }

    // --- Extract columns ---
    // Check if we can use the fast all-float row-major path:
    // all numeric columns are single-chunk float with no nulls.
    bool canFastPath = true;
    size_t numStringCols = 0;
    for (size_t ci = 0; ci < acceptedCols.size(); ci++) {
        if (isStringCol[ci]) { numStringCols++; continue; }
        auto chunked = table->column(acceptedCols[ci]);
        if (chunked->num_chunks() != 1 ||
            chunked->type()->id() != arrow::Type::FLOAT ||
            chunked->chunk(0)->null_count() > 0) {
            canFastPath = false;
        }
    }

    if (canFastPath && numStringCols == 0) {
        // Ultra-fast path: all single-chunk float columns, no nulls, no strings.
        // Collect raw float pointers, then do a tiled row-major pass
        // so writes are sequential and source reads stay in L2 cache.
        std::vector<const float*> colPtrs(m_data.numCols);
        for (size_t ci = 0; ci < acceptedCols.size(); ci++) {
            auto arr = table->column(acceptedCols[ci])->chunk(0);
            colPtrs[ci] = reinterpret_cast<const float*>(arr->data()->buffers[1]->data());
        }
        // Process in column tiles to limit concurrent read streams
        constexpr size_t COL_TILE = 8;
        size_t nc = m_data.numCols;
        for (size_t c0 = 0; c0 < nc; c0 += COL_TILE) {
            size_t c1 = std::min(c0 + COL_TILE, nc);
            for (size_t r = 0; r < m_data.numRows; r++) {
                float* dst = &m_data.data[r * nc + c0];
                for (size_t ci = c0; ci < c1; ci++)
                    *dst++ = colPtrs[ci][r];
            }
        }
        fprintf(stderr, "  Fast float path: %zu cols x %zu rows\n",
                m_data.numCols, m_data.numRows);
    } else {
        // General path: per-column extraction with type dispatch
        for (size_t ci = 0; ci < acceptedCols.size(); ci++) {
            int colIdx = acceptedCols[ci];
            auto chunked = table->column(colIdx);
            auto typeId = chunked->type()->id();
            fprintf(stderr, "  Col %zu/%zu: %s (%s)\n", ci, acceptedCols.size(),
                    table->field(colIdx)->name().c_str(),
                    chunked->type()->ToString().c_str());

            if (isStringCol[ci]) {
                // String column: collect all values, then encode as categorical
                std::vector<std::string> allStrings;
                allStrings.reserve(m_data.numRows);
                for (int chunk = 0; chunk < chunked->num_chunks(); chunk++) {
                    auto arr = chunked->chunk(chunk);
                    int64_t len = arr->length();
                    auto strArr = std::static_pointer_cast<arrow::StringArray>(arr);
                    for (int64_t r = 0; r < len; r++) {
                        if (arr->IsNull(r))
                            allStrings.emplace_back();
                        else
                            allStrings.push_back(strArr->GetString(r));
                    }
                }

                // Build category index with O(1) hash lookups instead of O(log n) tree
                std::unordered_map<std::string, int> tempMap;
                tempMap.reserve(allStrings.size());
                std::vector<std::string> uniqueStrings;
                for (const auto& s : allStrings) {
                    if (tempMap.emplace(s, 0).second)
                        uniqueStrings.push_back(s);
                }
                std::sort(uniqueStrings.begin(), uniqueStrings.end());
                std::unordered_map<std::string, int> indexMap;
                indexMap.reserve(uniqueStrings.size() * 2);
                for (int i = 0; i < (int)uniqueStrings.size(); i++)
                    indexMap[uniqueStrings[i]] = i;

                // Write float indices into data
                for (size_t row = 0; row < m_data.numRows; row++)
                    m_data.data[row * m_data.numCols + ci] = static_cast<float>(indexMap[allStrings[row]]);

                m_data.columnMeta[ci].isCategorical = true;
                m_data.columnMeta[ci].categories = std::move(uniqueStrings);
                fprintf(stderr, "    Categorical: %zu categories\n",
                        m_data.columnMeta[ci].categories.size());
            } else {
                // Numeric column: type-dispatched extraction (switch outside inner loop)
                size_t rowOffset = 0;
                for (int chunk = 0; chunk < chunked->num_chunks(); chunk++) {
                    auto arr = chunked->chunk(chunk);
                    int64_t len = arr->length();
                    const uint8_t* rawData = arr->data()->buffers[1]->data();
                    int nullCount = arr->null_count();

                    switch (typeId) {
                        case arrow::Type::FLOAT:
                            extractTypedColumn(reinterpret_cast<const float*>(rawData),
                                len, rowOffset, m_data.data.data(), m_data.numCols, ci, arr, nullCount);
                            break;
                        case arrow::Type::DOUBLE:
                            extractTypedColumn(reinterpret_cast<const double*>(rawData),
                                len, rowOffset, m_data.data.data(), m_data.numCols, ci, arr, nullCount);
                            break;
                        case arrow::Type::INT64:
                            extractTypedColumn(reinterpret_cast<const int64_t*>(rawData),
                                len, rowOffset, m_data.data.data(), m_data.numCols, ci, arr, nullCount);
                            break;
                        case arrow::Type::INT32:
                            extractTypedColumn(reinterpret_cast<const int32_t*>(rawData),
                                len, rowOffset, m_data.data.data(), m_data.numCols, ci, arr, nullCount);
                            break;
                        case arrow::Type::INT16:
                            extractTypedColumn(reinterpret_cast<const int16_t*>(rawData),
                                len, rowOffset, m_data.data.data(), m_data.numCols, ci, arr, nullCount);
                            break;
                        case arrow::Type::INT8:
                            extractTypedColumn(reinterpret_cast<const int8_t*>(rawData),
                                len, rowOffset, m_data.data.data(), m_data.numCols, ci, arr, nullCount);
                            break;
                        case arrow::Type::UINT64:
                            extractTypedColumn(reinterpret_cast<const uint64_t*>(rawData),
                                len, rowOffset, m_data.data.data(), m_data.numCols, ci, arr, nullCount);
                            break;
                        case arrow::Type::UINT32:
                            extractTypedColumn(reinterpret_cast<const uint32_t*>(rawData),
                                len, rowOffset, m_data.data.data(), m_data.numCols, ci, arr, nullCount);
                            break;
                        case arrow::Type::UINT16:
                            extractTypedColumn(reinterpret_cast<const uint16_t*>(rawData),
                                len, rowOffset, m_data.data.data(), m_data.numCols, ci, arr, nullCount);
                            break;
                        case arrow::Type::UINT8:
                            extractTypedColumn(reinterpret_cast<const uint8_t*>(rawData),
                                len, rowOffset, m_data.data.data(), m_data.numCols, ci, arr, nullCount);
                            break;
                        default:
                            for (int64_t r = 0; r < len; r++)
                                m_data.data[(rowOffset + r) * m_data.numCols + ci] = 0.0f;
                            break;
                    }
                    rowOffset += len;
                }
            }

            if (progress && ci % 3 == 0) {
                if (!progress(ci + 1, acceptedCols.size())) {
                    m_error = "Loading cancelled";
                    return false;
                }
            }
        }
    }

    fprintf(stderr, "Loaded parquet %s: %zu rows x %zu columns\n",
            path.c_str(), m_data.numRows, m_data.numCols);
    fprintf(stderr, "TIMING:   col extraction     %.3f s\n", elapsed(tExtract));

    // Remove constant columns (same logic as ASCII loader)
    auto tConst = Clock::now();
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
            std::vector<ColumnMeta> newMeta;
            std::vector<float> newData;
            newData.reserve(m_data.numRows * newCols);
            for (size_t col = 0; col < m_data.numCols; col++) {
                if (keep[col]) {
                    newLabels.push_back(m_data.columnLabels[col]);
                    newMeta.push_back(m_data.columnMeta[col]);
                }
            }
            for (size_t row = 0; row < m_data.numRows; row++)
                for (size_t col = 0; col < m_data.numCols; col++)
                    if (keep[col]) newData.push_back(m_data.data[row * m_data.numCols + col]);
            m_data.columnLabels = std::move(newLabels);
            m_data.columnMeta = std::move(newMeta);
            m_data.data = std::move(newData);
            m_data.numCols = newCols;
            fprintf(stderr, "  Removed %d constant columns, %zu remaining\n", removed, m_data.numCols);
        }
    }

    m_data.data.shrink_to_fit();
    fprintf(stderr, "TIMING:   const col removal  %.3f s\n", elapsed(tConst));
    fprintf(stderr, "TIMING:   parquet total      %.3f s\n", elapsed(tStart));
    return true;
#endif // HAS_PARQUET
}
