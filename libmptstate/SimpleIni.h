#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

class SimpleIni {

public:
    void load(const std::string& filename) {
        std::ifstream file(filename);
        std::string line, section;

        while (std::getline(file, line)) {
            trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;

            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                trim(section);
            } else {
                auto pos = line.find('=');
                if (pos == std::string::npos) continue;

                std::string key = line.substr(0, pos);
                std::string val = line.substr(pos + 1);
                trim(key);
                trim(val);
                data[section + "." + key] = val;
            }
        }
    }

    std::string get(const std::string& section, const std::string& key, const std::string& def = "") const {
        auto it = data.find(section + "." + key);
        return it != data.end() ? it->second : def;
    }

    int getInt(const std::string& section, const std::string& key, int def = 0) const {
        auto val = get(section, key);
        return val.empty() ? def : std::stoi(val);
    }

    bool getBool(const std::string& section, const std::string& key, bool def = false) const {
        auto val = get(section, key);
        return val == "true" || val == "1";
    }

    double getDouble(const std::string& section, const std::string& key, double def = 0.0) const {
        auto val = get(section, key);
        try {
            return val.empty() ? def : std::stod(val);
        } catch (...) {
            return def;
        }
    }



private:
    std::unordered_map<std::string, std::string> data;

    static void trim(std::string& s) {
        size_t first = s.find_first_not_of(" \t\r\n");
        size_t last = s.find_last_not_of(" \t\r\n");
        s = (first == std::string::npos) ? "" : s.substr(first, last - first + 1);
    }
};
