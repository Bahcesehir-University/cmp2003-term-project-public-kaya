#include "analyzer.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

using namespace std;

TripAnalyzer::ZoneStats::ZoneStats() : total(0) {
    memset(byHour, 0, sizeof(byHour));
}

static inline bool isWhitespace(unsigned char c) { 
    return c <= 32; 
}

static inline void cleanBounds(const char*& start, const char*& end) {
    while (start < end && isWhitespace(*start)) ++start;
    while (end > start && isWhitespace(end[-1])) --end;
    if (end > start + 1 && *start == '"' && end[-1] == '"') {
        ++start; --end;
        while (start < end && isWhitespace(*start)) ++start;
        while (end > start && isWhitespace(end[-1])) --end;
    }
}

static inline void skipBOM(const char*& start, const char*& end) {
    if (end - start >= 3 &&
        (unsigned char)start[0] == 0xEF &&
        (unsigned char)start[1] == 0xBB &&
        (unsigned char)start[2] == 0xBF) start += 3;
}

static inline bool parseThreeFields(const char* lineStart, const char* lineEnd,
                                    const char*& field0Start, const char*& field0End,
                                    const char*& field1Start, const char*& field1End,
                                    const char*& field2Start, const char*& field2End) {
    // Fast path: no quotes
    const void* hasQuote = memchr(lineStart, '"', lineEnd - lineStart);
    
    if (!hasQuote) {
        const char* comma1 = (const char*)memchr(lineStart, ',', lineEnd - lineStart);
        if (!comma1) return false;
        const char* comma2 = (const char*)memchr(comma1 + 1, ',', lineEnd - (comma1 + 1));
        if (!comma2) return false;
        
        field0Start = lineStart; field0End = comma1;
        field1Start = comma1 + 1; field1End = comma2;
        field2Start = comma2 + 1; field2End = lineEnd;
        return true;
    }
    
    // Slow path: quote-aware
    bool inQuote = false;
    int fieldIndex = 0;
    const char* fieldStart = lineStart;
    
    for (const char* p = lineStart; p <= lineEnd; ++p) {
        if (p < lineEnd && *p == '"') inQuote = !inQuote;
        
        if (!inQuote && (p == lineEnd || *p == ',')) {
            if (fieldIndex == 0) { field0Start = fieldStart; field0End = p; }
            else if (fieldIndex == 1) { field1Start = fieldStart; field1End = p; }
            else if (fieldIndex == 2) { field2Start = fieldStart; field2End = p; return true; }
            
            ++fieldIndex;
            fieldStart = p + 1;
        }
    }
    return false;
}

static inline bool extractHourValue(const char* timeStart, const char* timeEnd, int& hourOut) {
    const char* start = timeStart;
    const char* end = timeEnd;
    cleanBounds(start, end);
    
    if (end - start < 13) return false;
    
    char h1 = start[11];
    char h2 = start[12];
    
    if ((unsigned)(h1 - '0') > 9u || (unsigned)(h2 - '0') > 9u) return false;
    
    int h = (h1 - '0') * 10 + (h2 - '0');
    if ((unsigned)h > 23u) return false;
    
    hourOut = h;
    return true;
}

void TripAnalyzer::ingestFile(const string& csvPath) {
    FILE* file = fopen(csvPath.c_str(), "rb");
    if (!file) return;

    zones.clear();
    zones.reserve(200000);
    zones.max_load_factor(0.7f);

    static const size_t BUFFER_SIZE = 1 << 22;
    static char buffer[BUFFER_SIZE];

    string overflow;
    overflow.reserve(4096);

    bool bomProcessed = false;
    bool headerSkipped = false;

    while (true) {
        size_t bytesRead = fread(buffer, 1, BUFFER_SIZE, file);
        if (bytesRead == 0) break;

        const char* current = buffer;
        const char* bufferEnd = buffer + bytesRead;

        while (current < bufferEnd) {
            const char* newline = (const char*)memchr(current, '\n', bufferEnd - current);
            
            if (!newline) {
                overflow.append(current, bufferEnd - current);
                break;
            }

            const char* lineStart;
            const char* lineEnd;
            
            if (!overflow.empty()) {
                overflow.append(current, newline - current);
                lineStart = overflow.data();
                lineEnd = overflow.data() + overflow.size();
            } else {
                lineStart = current;
                lineEnd = newline;
            }

            // Remove \r if present
            if (lineEnd > lineStart && lineEnd[-1] == '\r') --lineEnd;
            
            if (lineEnd > lineStart) {
                const char* start = lineStart;
                const char* end = lineEnd;
                
                while (start < end && isWhitespace(*start)) ++start;
                if (start < end) {
                    if (!bomProcessed) {
                        skipBOM(start, end);
                        bomProcessed = true;
                    }
                    while (start < end && isWhitespace(*start)) ++start;
                    
                    if (start < end) {
                        const char *f0s, *f0e, *f1s, *f1e, *f2s, *f2e;
                        
                        if (parseThreeFields(start, end, f0s, f0e, f1s, f1e, f2s, f2e)) {
                            const char* idStart = f0s;
                            const char* idEnd = f0e;
                            cleanBounds(idStart, idEnd);
                            
                            if (idStart < idEnd) {
                                if (!headerSkipped) {
                                    headerSkipped = true;
                                    if ((idEnd - idStart) == 6 && memcmp(idStart, "TripID", 6) == 0) {
                                        overflow.clear();
                                        current = newline + 1;
                                        continue;
                                    }
                                }
                                
                                const char* zoneStart = f1s;
                                const char* zoneEnd = f1e;
                                cleanBounds(zoneStart, zoneEnd);
                                
                                if (zoneStart < zoneEnd) {
                                    int hour;
                                    if (extractHourValue(f2s, f2e, hour)) {
                                        string zoneName(zoneStart, zoneEnd - zoneStart);
                                        auto it = zones.try_emplace(move(zoneName), ZoneStats()).first;
                                        ++it->second.total;
                                        ++it->second.byHour[hour];
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            overflow.clear();
            current = newline + 1;
        }
    }

    if (!overflow.empty()) {
        // Process final line if needed
    }

    fclose(file);
}

vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    vector<ZoneCount> results;
    results.reserve(k);

    auto compareZones = [](const ZoneCount& a, const ZoneCount& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.zone < b.zone;
    };

    for (const auto& entry : zones) {
        ZoneCount candidate{entry.first, entry.second.total};
        
        if ((int)results.size() < k) {
            results.push_back(candidate);
            int i = results.size() - 1;
            while (i > 0 && compareZones(results[i], results[i - 1])) {
                swap(results[i], results[i - 1]);
                --i;
            }
        } else if (compareZones(candidate, results[k - 1])) {
            results[k - 1] = candidate;
            int i = k - 1;
            while (i > 0 && compareZones(results[i], results[i - 1])) {
                swap(results[i], results[i - 1]);
                --i;
            }
        }
    }

    return results;
}

vector<SlotCount> TripAnalyzer::topBusySlots(int k) const {
    vector<SlotCount> results;
    results.reserve(k);

    auto compareSlots = [](const SlotCount& a, const SlotCount& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.zone != b.zone) return a.zone < b.zone;
        return a.hour < b.hour;
    };

    for (const auto& entry : zones) {
        const string& zoneName = entry.first;
        const ZoneStats& stats = entry.second;

        for (int h = 0; h < 24; ++h) {
            long long count = stats.byHour[h];
            if (count <= 0) continue;

            SlotCount candidate{zoneName, h, count};
            
            if ((int)results.size() < k) {
                results.push_back(candidate);
                int i = results.size() - 1;
                while (i > 0 && compareSlots(results[i], results[i - 1])) {
                    swap(results[i], results[i - 1]);
                    --i;
                }
            } else if (compareSlots(candidate, results[k - 1])) {
                results[k - 1] = candidate;
                int i = k - 1;
                while (i > 0 && compareSlots(results[i], results[i - 1])) {
                    swap(results[i], results[i - 1]); 
                    --i;
                }
            }
        }
    }

    return results;
}
