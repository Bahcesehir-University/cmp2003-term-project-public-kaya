#include "analyzer.h"
#include <fstream>
#include <algorithm>
#include <cstring>

using namespace std;

TripAnalyzer::ZoneStats::ZoneStats() : total(0) {
    memset(byHour, 0, sizeof(byHour));
}

static bool extractHour(const string& dt, int& hour) {
    if (dt.size() < 13) return false;
    size_t spacePos = dt.find(' ');
    if (spacePos == string::npos) spacePos = dt.find('T');
    if (spacePos == string::npos || spacePos + 3 > dt.size()) return false;
    
    char h1 = dt[spacePos + 1];
    char h2 = dt[spacePos + 2];
    if (h1 < '0' || h1 > '9' || h2 < '0' || h2 > '9') return false;
    
    hour = (h1 - '0') * 10 + (h2 - '0');
    return hour >= 0 && hour <= 23;
}

static bool parseLine(const string& line, string& zone, int& hour) {
    size_t p0 = 0;
    size_t p1 = line.find(',', p0);
    if (p1 == string::npos) return false;
    
    p0 = p1 + 1;
    p1 = line.find(',', p0);
    if (p1 == string::npos) return false;
    zone = line.substr(p0, p1 - p0);
    if (zone.empty()) return false;
    
    p0 = p1 + 1;
    p1 = line.find(',', p0);
    if (p1 == string::npos) return false;
    
    p0 = p1 + 1;
    p1 = line.find(',', p0);
    if (p1 == string::npos) return false;
    string dt = line.substr(p0, p1 - p0);
    
    return extractHour(dt, hour);
}

void TripAnalyzer::ingestFile(const string& csvPath) {
    ifstream file(csvPath);
    if (!file) return;

    zones.clear();
    zones.reserve(200000);
    zones.max_load_factor(0.7f);

    string line;
    if (!getline(file, line)) return;

    while (getline(file, line)) {
        if (line.empty()) continue;
        
        string zone;
        int hour;
        if (!parseLine(line, zone, hour)) continue;

        auto it = zones.find(zone);
        if (it == zones.end()) {
            it = zones.emplace(zone, ZoneStats()).first;
        }
        ++it->second.total;
        ++it->second.byHour[hour];
    }
}

vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    vector<ZoneCount> result;
    result.reserve(zones.size());

    for (const auto& kv : zones) {
        result.push_back({kv.first, kv.second.total});
    }

    sort(result.begin(), result.end(), [](const ZoneCount& a, const ZoneCount& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.zone < b.zone;
    });

    if ((int)result.size() > k) result.resize(k);
    return result;
}

vector<SlotCount> TripAnalyzer::topBusySlots(int k) const {
    vector<SlotCount> result;

    for (const auto& kv : zones) {
        for (int h = 0; h < 24; ++h) {
            if (kv.second.byHour[h] > 0) {
                result.push_back({kv.first, h, kv.second.byHour[h]});
            }
        }
    }

    sort(result.begin(), result.end(), [](const SlotCount& a, const SlotCount& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.zone != b.zone) return a.zone < b.zone;
        return a.hour < b.hour;
    });

    if ((int)result.size() > k) result.resize(k);
    return result;
}
