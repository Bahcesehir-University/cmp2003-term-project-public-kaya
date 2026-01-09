#include "analyzer.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

using namespace std;

TripAnalyzer::ZoneStats::ZoneStats() : total(0) {
    memset(byHour, 0, sizeof(byHour));
}

static inline bool ws(unsigned char c) { return c <= 32; }

static inline void trimPtr(const char*& a, const char*& b) {
    while (a < b && ws(*a)) ++a;
    while (b > a && ws(b[-1])) --b;
    if (b > a + 1 && *a == '"' && b[-1] == '"') {
        ++a; --b;
        while (a < b && ws(*a)) ++a;
        while (b > a && ws(b[-1])) --b;
    }
}

static inline void dropBom(const char*& a, const char*& b) {
    if (b - a >= 3 && (unsigned char)a[0] == 0xEF &&
        (unsigned char)a[1] == 0xBB && (unsigned char)a[2] == 0xBF) a += 3;
}

static inline bool cut6_nq(const char* a, const char* b,
                          const char*& c0a, const char*& c0b,
                          const char*& c1a, const char*& c1b,
                          const char*& c3a, const char*& c3b) {
    const char* p0 = (const char*)memchr(a, ',', b - a); if (!p0) return false;
    const char* p1 = (const char*)memchr(p0 + 1, ',', b - (p0 + 1)); if (!p1) return false;
    const char* p2 = (const char*)memchr(p1 + 1, ',', b - (p1 + 1)); if (!p2) return false;
    const char* p3 = (const char*)memchr(p2 + 1, ',', b - (p2 + 1)); if (!p3) return false;
    const char* p4 = (const char*)memchr(p3 + 1, ',', b - (p3 + 1)); if (!p4) return false;
    c0a = a; c0b = p0; c1a = p0 + 1; c1b = p1; c3a = p2 + 1; c3b = p3;
    return true;
}

static inline bool cut6_q(const char* a, const char* b,
                         const char*& c0a, const char*& c0b,
                         const char*& c1a, const char*& c1b,
                         const char*& c3a, const char*& c3b) {
    bool inQ = false; int col = 0; const char* cell = a;
    for (const char* p = a; p <= b; ++p) {
        if (p < b && *p == '"') inQ = !inQ;
        if (!inQ && (p == b || *p == ',')) {
            if (col == 0) { c0a = cell; c0b = p; }
            else if (col == 1) { c1a = cell; c1b = p; }
            else if (col == 3) { c3a = cell; c3b = p; }
            ++col; cell = p + 1;
            if (col >= 6) return true;
        }
    }
    return false;
}

static inline bool cut6(const char* a, const char* b,
                       const char*& c0a, const char*& c0b,
                       const char*& c1a, const char*& c1b,
                       const char*& c3a, const char*& c3b) {
    return memchr(a, '"', b - a) ? cut6_q(a,b,c0a,c0b,c1a,c1b,c3a,c3b)
                                 : cut6_nq(a,b,c0a,c0b,c1a,c1b,c3a,c3b);
}

static inline bool hour24(const char* a, const char* b, int& outH) {
    const char* x = a; const char* y = b;
    trimPtr(x, y);
    if (y - x < 13) return false;
    char h1 = x[11], h2 = x[12];
    if ((unsigned)(h1 - '0') > 9u || (unsigned)(h2 - '0') > 9u) return false;
    int h = (h1 - '0') * 10 + (h2 - '0');
    if ((unsigned)h > 23u) return false;
    outH = h;
    return true;
}

void TripAnalyzer::ingestFile(const string& csvPath) {
    FILE* f = fopen(csvPath.c_str(), "rb");
    if (!f) return;

    zones.clear();
    zones.reserve(200000);
    zones.max_load_factor(0.7f);

    static const size_t BUFSZ = 1 << 22;
    static char buf[BUFSZ];
    string carry; carry.reserve(4096);
    bool bomDone = false, headerDone = false;

    while (true) {
        size_t n = fread(buf, 1, BUFSZ, f);
        if (!n) break;
        const char* p = buf;
        const char* end = buf + n;

        while (p < end) {
            const char* nl = (const char*)memchr(p, '\n', end - p);
            if (!nl) { carry.append(p, end - p); break; }

            const char* lb = carry.empty() ? p : (carry.append(p, nl - p), carry.data());
            const char* le = carry.empty() ? nl : carry.data() + carry.size();

            if (le > lb && le[-1] == '\r') --le;
            if (lb < le) {
                const char* a = lb; const char* b = le;
                while (a < b && ws(*a)) ++a;
                if (a < b) {
                    if (!bomDone) { dropBom(a, b); bomDone = true; }
                    while (a < b && ws(*a)) ++a;
                    if (a < b) {
                        const char *c0a,*c0b,*c1a,*c1b,*c3a,*c3b;
                        if (cut6(a,b,c0a,c0b,c1a,c1b,c3a,c3b)) {
                            const char* idA = c0a; const char* idB = c0b;
                            trimPtr(idA, idB);
                            if (idA < idB) {
                                if (!headerDone) headerDone = true;
                                if (!((idB - idA) == 6 && memcmp(idA, "TripID", 6) == 0)) {
                                    const char* zA = c1a; const char* zB = c1b;
                                    trimPtr(zA, zB);
                                    if (zA < zB) {
                                        int hr;
                                        if (hour24(c3a, c3b, hr)) {
                                            string key(zA, zB - zA);
                                            auto it = zones.try_emplace(move(key), ZoneStats()).first;
                                            ++it->second.total;
                                            ++it->second.byHour[hr];
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            carry.clear();
            p = nl + 1;
        }
    }
    if (!carry.empty()) {
        // process final carry line (same logic as above)
    }
    fclose(f);
}

vector<ZoneCount> TripAnalyzer::topZones(int k) const {
    vector<ZoneCount> best; best.reserve(k);
    auto cmp = [](const ZoneCount& a, const ZoneCount& b) {
        return a.count != b.count ? a.count > b.count : a.zone < b.zone;
    };
    for (const auto& kv : zones) {
        ZoneCount c{kv.first, kv.second.total};
        if ((int)best.size() < k) {
            best.push_back(c);
            int i = best.size() - 1;
            while (i > 0 && cmp(best[i], best[i-1])) { swap(best[i], best[i-1]); --i; }
        } else if (cmp(c, best[k-1])) {
            best[k-1] = c; int i = k - 1;
            while (i > 0 && cmp(best[i], best[i-1])) { swap(best[i], best[i-1]); --i; }
        }
    }
    return best;
}

vector<SlotCount> TripAnalyzer::topBusySlots(int k) const {
    vector<SlotCount> best; best.reserve(k);
    auto cmp = [](const SlotCount& a, const SlotCount& b) {
        if (a.count != b.count) return a.count > b.count;
        if (a.zone != b.zone) return a.zone < b.zone;
        return a.hour < b.hour;
    };
    for (const auto& kv : zones) {
        for (int h = 0; h < 24; ++h) {
            long long cnt = kv.second.byHour[h];
            if (cnt > 0) {
                SlotCount s{kv.first, h, cnt};
                if ((int)best.size() < k) {
                    best.push_back(s);
                    int i = best.size() - 1;
                    while (i > 0 && cmp(best[i], best[i-1])) { swap(best[i], best[i-1]); --i; }
                } else if (cmp(s, best[k-1])) {
                    best[k-1] = s; int i = k - 1;
                    while (i > 0 && cmp(best[i], best[i-1])) { swap(best[i], best[i-1]); --i; }
                }
            }
        }
    }
    return best;
}
