#pragma once

// A squad's name set on a card: whole, or over two lines, or with its small words dropped, and only
// then cut (the cut never ends on "of", "the" or a stray space). The page's card (game/web/index.html,
// cardNameLines) does the same steps; keep the two alike.

#include <string>
#include <vector>

namespace illyria::game {

inline bool isCardConnector(const std::string& w) { return w == "of" || w == "the" || w == "and" || w == "at" || w == "in" || w == "on"; }

namespace detail {

inline std::vector<std::string> cardWords(const std::string& s) {
    std::vector<std::string> words;
    std::string w;
    for (char c : s) {
        if (c == ' ') {
            if (!w.empty()) words.push_back(w);
            w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty()) words.push_back(w);
    return words;
}

inline std::string joinCardWords(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& w : v) s += (s.empty() ? "" : " ") + w;
    return s;
}

// Fills lines greedily to maxW; a line never ends on a small word (it opens the next line instead).
// False if it takes more than maxLines or a word alone is too wide.
template <class Width>
bool packCardWords(const std::vector<std::string>& words, Width width, float maxW, size_t maxLines, std::vector<std::string>& out) {
    out.clear();
    std::vector<std::string> line;
    for (const std::string& w : words) {
        std::vector<std::string> tried = line;
        tried.push_back(w);
        if (line.empty() || width(joinCardWords(tried)) <= maxW) {
            line = tried;
            continue;
        }
        std::vector<std::string> carry;
        while (line.size() > 1 && isCardConnector(line.back())) {
            carry.insert(carry.begin(), line.back());
            line.pop_back();
        }
        out.push_back(joinCardWords(line));
        line = carry;
        line.push_back(w);
    }
    if (!line.empty()) out.push_back(joinCardWords(line));
    if (out.size() > maxLines) return false;
    for (const auto& l : out)
        if (width(l) > maxW) return false;
    return true;
}

}  // namespace detail

// `out` gets the lines (at most maxLines); true when the whole name is there (small words dropped at most),
// false when it had to be cut.
template <class Width>
bool fitCardName(const std::string& name, Width width, float maxW, size_t maxLines, std::vector<std::string>& out) {
    const std::vector<std::string> words = detail::cardWords(name);
    if (words.empty()) {
        out.assign(1, "");
        return true;
    }
    if (detail::packCardWords(words, width, maxW, maxLines, out)) return true;
    for (int pass = 0; pass < 2; ++pass) {  // "the" first, then every small word (never the first word)
        std::vector<std::string> kept;
        for (size_t i = 0; i < words.size(); ++i)
            if (i == 0 || !(pass == 0 ? words[i] == "the" : isCardConnector(words[i]))) kept.push_back(words[i]);
        if (kept.size() != words.size() && detail::packCardWords(kept, width, maxW, maxLines, out)) return true;
    }
    // Cut: pack whole lines as far as they go, then shorten what is left until it and a full stop fit.
    out.clear();
    std::string line;
    size_t i = 0;
    while (i < words.size() && out.size() + 1 < maxLines) {
        const std::string tried = line.empty() ? words[i] : line + " " + words[i];
        if (!line.empty() && width(tried) > maxW) {
            while (line.size() > 1 && width(line) > maxW) line.pop_back();  // (one word wider than the card)
            out.push_back(line);
            line.clear();
            continue;
        }
        line = tried;
        ++i;
    }
    std::string last = line;
    for (; i < words.size(); ++i) last += (last.empty() ? "" : " ") + words[i];
    while (last.size() > 1 && width(last + ".") > maxW) last.pop_back();
    for (;;) {  // no trailing space, full stop or small word
        while (!last.empty() && (last.back() == ' ' || last.back() == '.')) last.pop_back();
        const size_t sp = last.rfind(' ');
        if (sp != std::string::npos && isCardConnector(last.substr(sp + 1))) last.erase(sp);
        else break;
    }
    out.push_back(last + ".");
    return false;
}

}  // namespace illyria::game
