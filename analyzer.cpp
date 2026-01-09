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
    
    // Trim whitespace and quotes
    size_t start = 0;
    size_t end = dt.size();
    while (start < end && (dt[start] <= 32 || dt[start] == '"')) ++start;
    while (end > start && (dt[end-1] <= 32 || dt[end-1] == '"')) --end;
    if (end - start < 13) return false;
    
    // Find space or 'T'
    size_t spacePos = string::npos;
    for (size_t i = start; i < end; ++i) {
        if (dt[i] == ' ' || dt[i] == 'T') {
            spacePos = i;
            break;
        }
    }
    if (spacePos == string::npos || spacePos + 3 > end) return false;
    
    char h1 = dt[spacePos + 1];
    char h2 = dt[spacePos + 2];
    if (h1 < '0' || h1 > '9' || h2 < '0' || h2 > '9') return false;
    
    hour = (h1 - '0') * 10 + (h2 - '0');
    return hour >= 0 && hour <= 23;
}

static bool parseLine(const string& line, string& zone, int& hour) {
    // Parse 6-field CSV: TripID,PickupZoneID,DropoffZoneID,PickupDateTime,DistanceKm,FareAmount
    // We need field[1] (PickupZoneID) and field[3] (PickupDateTime)
    
    bool inQuote = false;
    int fieldNum = 0;
    size_t fieldStart = 0;
    string pickupZone, pickupTime;
    
    for (size_t i = 0; i <= line.size(); ++i) {
        if (i < line.size() && line[i] == '"') {
            inQuote = !inQuote;
        }
        
        if (!inQuote && (i == line.size() || line[i] == ',')) {
            if (fieldNum == 1) {
                // PickupZoneID
                pickupZone = line.substr(fieldStart, i - fieldStart);
            } else if (fieldNum == 3) {
                // PickupDateTime
                pickupTime = line.substr(fieldStart, i - fieldStart);
            }
            
            fieldNum++;
            fieldStart = i + 1;
            
            if (fieldNum > 3) break; // We have what we need
        }
    }
    
    if (pickupZone.empty()) return false;
    
    // Trim zone
    size_t zstart = 0;
    size_t zend = pickupZone.size();
    while (zstart < zend && (pickupZone[zstart] <= 32 || pickupZone[zstart] == '"')) ++zstart;
    while (zend > zstart && (pickupZone[zend-1] <= 32 || pickupZone[zend-1] == '"')) --zend;
    
    if (zstart >= zend) return false;
    zone = pickupZone.substr(zstart, zend - zstart);
    
    return extractHour(pickupTime, hour);
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
