#pragma once

// A small JSON reader for the files the page reads with fetch().json(): the sprite
// indexes and cell tables, the sound manifest, and the HUD text the game itself
// writes (illyria_hud). Numbers are doubles; there is no writer.

#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace illyria::native {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Json> items;                        // an array's
    std::vector<std::pair<std::string, Json>> members;  // an object's, in file order

    const Json* find(const std::string& key) const {
        for (const auto& m : members)
            if (m.first == key) return &m.second;
        return nullptr;
    }
    // Missing or of another kind: the fallback.
    double num(const std::string& key, double fallback = 0) const {
        const Json* j = find(key);
        return j && j->type == Type::Number ? j->number : fallback;
    }
    std::string str(const std::string& key, const std::string& fallback = "") const {
        const Json* j = find(key);
        return j && j->type == Type::String ? j->string : fallback;
    }
    bool flag(const std::string& key, bool fallback = false) const {
        const Json* j = find(key);
        return j && j->type == Type::Bool ? j->boolean : fallback;
    }
    const std::vector<Json>& list(const std::string& key) const {
        static const std::vector<Json> none;
        const Json* j = find(key);
        return j && j->type == Type::Array ? j->items : none;
    }

    // False if the text is not JSON.
    static bool parse(const std::string& text, Json& out) {
        size_t at = 0;
        if (!value(text, at, out, 0)) return false;
        skip(text, at);
        return at == text.size();
    }

private:
    static void skip(const std::string& t, size_t& at) {
        while (at < t.size() && std::isspace(static_cast<unsigned char>(t[at]))) ++at;
    }
    static bool text(const std::string& t, size_t& at, std::string& out) {
        if (at >= t.size() || t[at] != '"') return false;
        ++at;
        out.clear();
        while (at < t.size() && t[at] != '"') {
            char c = t[at++];
            if (c == '\\' && at < t.size()) {
                c = t[at++];
                switch (c) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'u': {
                        // The Basic Multilingual Plane, as UTF-8 (surrogate pairs are not joined).
                        if (at + 4 > t.size()) return false;
                        const unsigned u = static_cast<unsigned>(std::strtoul(t.substr(at, 4).c_str(), nullptr, 16));
                        at += 4;
                        if (u < 0x80) {
                            out += static_cast<char>(u);
                        } else if (u < 0x800) {
                            out += static_cast<char>(0xC0 | (u >> 6));
                            out += static_cast<char>(0x80 | (u & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (u >> 12));
                            out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (u & 0x3F));
                        }
                        continue;
                    }
                    default: break;  // \" \\ \/
                }
            }
            out += c;
        }
        if (at >= t.size()) return false;
        ++at;
        return true;
    }
    static bool value(const std::string& t, size_t& at, Json& out, int depth) {
        if (depth > 64) return false;
        skip(t, at);
        if (at >= t.size()) return false;
        const char c = t[at];
        if (c == '{') {
            out.type = Type::Object;
            ++at;
            skip(t, at);
            if (at < t.size() && t[at] == '}') return ++at, true;
            for (;;) {
                skip(t, at);
                std::string key;
                if (!text(t, at, key)) return false;
                skip(t, at);
                if (at >= t.size() || t[at++] != ':') return false;
                out.members.emplace_back(std::move(key), Json());
                if (!value(t, at, out.members.back().second, depth + 1)) return false;
                skip(t, at);
                if (at >= t.size()) return false;
                if (t[at] == ',') {
                    ++at;
                    continue;
                }
                return t[at++] == '}';
            }
        }
        if (c == '[') {
            out.type = Type::Array;
            ++at;
            skip(t, at);
            if (at < t.size() && t[at] == ']') return ++at, true;
            for (;;) {
                out.items.emplace_back();
                if (!value(t, at, out.items.back(), depth + 1)) return false;
                skip(t, at);
                if (at >= t.size()) return false;
                if (t[at] == ',') {
                    ++at;
                    continue;
                }
                return t[at++] == ']';
            }
        }
        if (c == '"') {
            out.type = Type::String;
            return text(t, at, out.string);
        }
        if (t.compare(at, 4, "true") == 0) return out.type = Type::Bool, out.boolean = true, at += 4, true;
        if (t.compare(at, 5, "false") == 0) return out.type = Type::Bool, out.boolean = false, at += 5, true;
        if (t.compare(at, 4, "null") == 0) return at += 4, true;
        char* end = nullptr;
        out.number = std::strtod(t.c_str() + at, &end);
        if (end == t.c_str() + at) return false;
        out.type = Type::Number;
        at = static_cast<size_t>(end - t.c_str());
        return true;
    }
};

}  // namespace illyria::native
