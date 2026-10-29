#pragma once
// Just enough JSON for the tests to read the game's manifests.
#include <cstdlib>
#include <string>
#include <vector>

namespace testjson {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    double number = 0;
    std::string str;
    std::vector<Json> items;
    // An object's fields (two lists: a pair could not hold the incomplete Json).
    std::vector<std::string> keys;
    std::vector<Json> values;
    const Json& operator[](const std::string& key) const {
        static const Json null;
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key) return values[i];
        return null;
    }
};

struct Parser {
    const char* p;
    bool ok = true;
    void ws() {
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
    }
    std::string string() {
        std::string s;
        if (*p != '"') { ok = false; return s; }
        ++p;
        while (*p && *p != '"') {
            if (*p == '\\') {
                ++p;
                switch (*p) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'u': s += '?'; p += 4; break;
                    default: s += *p;
                }
                ++p;
            } else {
                s += *p++;
            }
        }
        if (*p == '"') ++p;
        else ok = false;
        return s;
    }
    Json value() {
        ws();
        Json j;
        if (*p == '{') {
            j.type = Json::Type::Object;
            ++p;
            ws();
            if (*p == '}') { ++p; return j; }
            while (ok) {
                ws();
                std::string key = string();
                ws();
                if (*p++ != ':') ok = false;
                j.keys.push_back(key);
                j.values.push_back(value());
                ws();
                if (*p == ',') { ++p; continue; }
                if (*p++ != '}') ok = false;
                break;
            }
        } else if (*p == '[') {
            j.type = Json::Type::Array;
            ++p;
            ws();
            if (*p == ']') { ++p; return j; }
            while (ok) {
                j.items.push_back(value());
                ws();
                if (*p == ',') { ++p; continue; }
                if (*p++ != ']') ok = false;
                break;
            }
        } else if (*p == '"') {
            j.type = Json::Type::String;
            j.str = string();
        } else if (*p == 't' || *p == 'f') {
            j.type = Json::Type::Bool;
            j.number = *p == 't';
            p += *p == 't' ? 4 : 5;
        } else if (*p == 'n') {
            p += 4;
        } else {
            j.type = Json::Type::Number;
            char* end = nullptr;
            j.number = std::strtod(p, &end);
            if (end == p) ok = false;
            p = end;
        }
        return j;
    }
};

}  // namespace testjson
