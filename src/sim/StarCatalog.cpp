#include "StarCatalog.hpp"

#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>
#include <iostream>

namespace {

// 앞뒤 공백/개행/캐리지리턴을 잘라낸다. isspace가 '\r'도 공백으로 보므로
// CRLF로 들어온 필드도 이 한 번으로 정리된다.
std::string trim(const std::string &s) {
    size_t start = 0, end = s.size();
    while (start < end && std::isspace((unsigned char)s[start])) start++;
    while (end > start && std::isspace((unsigned char)s[end - 1])) end--;
    return s.substr(start, end - start);
}

// 쉼표로 나누고 각 필드를 trim한다. 따옴표로 감싼 필드는 이 CSV들에 없으므로 다루지 않는다.
std::vector<std::string> splitCsv(const std::string &line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(trim(field));
    }
    return fields;
}

} // namespace

glm::vec3 StarCatalog::raDecToDir(double raDeg, double decDeg) {
    double ra  = glm::radians(raDeg);
    double dec = glm::radians(decDeg);
    // 초기 규약: 적경을 Y축 둘레 경도, 적위를 위도로 본다. Task 3에서 배경과 맞춰 확정한다.
    double x = std::cos(dec) * std::cos(ra);
    double y = std::sin(dec);
    double z = std::cos(dec) * std::sin(ra);
    return glm::normalize(glm::vec3((float)x, (float)y, (float)z));
}

bool StarCatalog::load(const std::string &dir) {
    stars_.clear();
    constellations_.clear();
    idToIndex_.clear();

    // --- stars.csv: id,raDeg,decDeg ---
    {
        std::ifstream f(dir + "/stars.csv");
        if (!f.is_open()) return false;
        std::string line;
        bool header = true;
        while (std::getline(f, line)) {
            if (header) { header = false; continue; }
            if (trim(line).empty()) continue;
            auto fields = splitCsv(line);
            if (fields.size() < 3) continue;
            int id = std::stoi(fields[0]);
            double ra = std::stod(fields[1]);
            double dec = std::stod(fields[2]);
            idToIndex_[id] = (int)stars_.size();
            stars_.push_back(CatalogStar{ id, raDecToDir(ra, dec) });
        }
    }

    // --- names.csv: constellation,latin ---
    std::unordered_map<std::string, std::string> nameMap;
    {
        std::ifstream f(dir + "/names.csv");
        if (!f.is_open()) return false;
        std::string line;
        bool header = true;
        while (std::getline(f, line)) {
            if (header) { header = false; continue; }
            if (trim(line).empty()) continue;
            auto fields = splitCsv(line);
            if (fields.size() < 2) continue;
            nameMap[fields[0]] = fields[1];
        }
    }

    // --- lines.csv: constellation,idA,idB ---
    {
        std::ifstream f(dir + "/lines.csv");
        if (!f.is_open()) return false;
        std::string line;
        bool header = true;
        std::unordered_map<std::string, int> abbrToIndex; // 약자 -> constellations_ 인덱스
        while (std::getline(f, line)) {
            if (header) { header = false; continue; }
            if (trim(line).empty()) continue;
            auto fields = splitCsv(line);
            if (fields.size() < 3) continue;
            const std::string &abbr = fields[0];
            int idA = std::stoi(fields[1]);
            int idB = std::stoi(fields[2]);

            auto itA = idToIndex_.find(idA);
            auto itB = idToIndex_.find(idB);
            if (itA == idToIndex_.end() || itB == idToIndex_.end()) continue; // 모르는 id는 건너뛰고 계속

            int gA = itA->second;
            int gB = itB->second;

            int cIdx;
            auto itAbbr = abbrToIndex.find(abbr);
            if (itAbbr == abbrToIndex.end()) {
                cIdx = (int)constellations_.size();
                abbrToIndex[abbr] = cIdx;
                constellations_.push_back(Constellation{});
                constellations_.back().abbr = abbr;
            } else {
                cIdx = itAbbr->second;
            }

            Constellation &c = constellations_[cIdx];
            c.edges.push_back(glm::ivec2(gA, gB));
            c.adjacency[gA].push_back(gB);
            c.adjacency[gB].push_back(gA);
        }
    }

    // latin 이름과 centroidDir 마무리
    for (auto &c : constellations_) {
        auto itName = nameMap.find(c.abbr);
        c.latin = (itName != nameMap.end()) ? itName->second : c.abbr;

        glm::vec3 sum(0.0f);
        int count = 0;
        for (const auto &kv : c.adjacency) {
            sum += stars_[kv.first].dir;
            ++count;
        }
        c.centroidDir = (count > 0) ? glm::normalize(sum) : glm::vec3(0.0f, 0.0f, 1.0f);
    }

    std::cout << "[catalog] stars=" << stars_.size()
              << " constellations=" << constellations_.size() << "\n";
    return true;
}

int StarCatalog::pickNearest(const glm::vec3 &rayDir, float maxAngleRad, int &outStarIndex) const {
    outStarIndex = -1;
    float bestDot = std::cos(maxAngleRad); // 이보다 큰 내적 = 이 각도보다 가까움
    int bestConstellation = -1;
    for (size_t c = 0; c < constellations_.size(); ++c) {
        for (const auto &e : constellations_[c].edges) {
            for (int idx : {e.x, e.y}) {
                float d = glm::dot(rayDir, stars_[idx].dir);
                if (d > bestDot) { bestDot = d; outStarIndex = idx; bestConstellation = (int)c; }
            }
        }
    }
    return bestConstellation;
}
