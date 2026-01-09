#include "analyzer.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

using namespace std;

TripAnalyzer::ZoneStats::ZoneStats() : total(0) {
    memset(byHour, 0, sizeof(byHour));
}

static inline string trim(const string& s) {
    size_t start = 0;
    size_t end = s.size();
    while (start < end && (s[start] <= 32 || s[start] == '"')) ++start;
    while (end > start && (s[end-1] <= 32 || s[end-1] == '"')) --end;
    return s.substr(start, end - start);
}

static bool extractHour(const string& dt, int& hour) {
    string clean = trim(dt);
    if (clean.size() < 13) return false;
    
    // Find space or 'T'
    size_t spacePos = clean.find(' ');
    if (spacePos == string::npos) spacePos = clean.find('T');
    if (spacePos == string::npos || spacePos + 3 > clean.size()) return false;
    
    char h1 = clean[spacePos + 1];
    char h2 = clean[spacePos + 2];
    if (h1 < '0' || h1 > '9' || h2 < '0' || h2 > '9') return false;
    
    hour = (h1 - '0') * 10 + (h2 - '0');
    return hour >= 0 && hour <= 23;
}

void TripAnalyzer::ingestFile(const string& csvPath) {
    ifstream file(csvPath);
    if (!file) return;

    zones.clear();
    zones.reserve(200000);
    zones.max_load_factor(0.7f);

    string line;
    bool headerSkipped = false;
    
    while (getline(file, line)) {
        // Remove trailing \r if present
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        
        if (line.empty()) continue;
        
        // Skip header
        if (!headerSkipped) {
            headerSkipped = true;
            if (line.find("TripID") != string::npos) continue;
        }
        
        // Parse: TripID,PickupZoneID,PickupTime
        istringstream ss(line);
        string tripId, zoneId, pickupTime;
        
        if (!getline(ss, tripId, ',')) continue;
        if (!getline(ss, zoneId, ',')) continue;
        if (!getline(ss, pickupTime, ',')) continue;
        
        tripId = trim(tripId);
        zoneId = trim(zoneId);
        pickupTime = trim(pickupTime);
        
        if (tripId.empty() || zoneId.empty() || pickupTime.empty()) continue;
        
        int hour;
        if (!extractHour(pickupTime, hour)) continue;

        auto it = zones.find(zoneId);
        if (it == zones.end()) {
            it = zones.emplace(zoneId, ZoneStats()).first;
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
