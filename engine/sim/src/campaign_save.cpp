// Saved games: the campaign's whole state, written as compact bytes (varints,
// length-prefixed strings) in base64 so a browser can keep it as text. What
// the map decides (terrain, rivers, hexes' shape, outposts) is rebuilt by the
// constructor from the same continent and seed; everything that changes as
// the war goes on is written here. The save ends with the campaign's
// checksum: a restored campaign must hash the same, or the save is refused.
#include <memory>
#include <string>
#include <vector>

#include "illyria/sim/campaign.h"
#include "illyria/sim/hash.h"

namespace illyria::sim {

namespace {

constexpr const char* kMagic = "ILC1.";  // bump the digit when the layout changes
constexpr uint32_t kFormat = 1;

class Writer {
public:
    void u(uint64_t v) {
        while (v >= 0x80) {
            bytes_.push_back(static_cast<uint8_t>(v | 0x80));
            v >>= 7;
        }
        bytes_.push_back(static_cast<uint8_t>(v));
    }
    void i(int64_t v) { u((static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63)); }  // zigzag
    void b(bool v) { u(v ? 1 : 0); }
    void str(const std::string& s) {
        u(s.size());
        bytes_.insert(bytes_.end(), s.begin(), s.end());
    }
    const std::vector<uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
    uint64_t u() {
        uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (at_ >= bytes_.size()) {
                ok_ = false;
                return 0;
            }
            const uint8_t c = bytes_[at_++];
            v |= static_cast<uint64_t>(c & 0x7F) << shift;
            if (!(c & 0x80)) return v;
        }
        ok_ = false;
        return 0;
    }
    int64_t i() {
        const uint64_t z = u();
        return static_cast<int64_t>(z >> 1) ^ -static_cast<int64_t>(z & 1);
    }
    int32_t i32() { return static_cast<int32_t>(i()); }
    uint32_t u32() { return static_cast<uint32_t>(u()); }
    uint8_t u8() { return static_cast<uint8_t>(u()); }
    bool b() { return u() != 0; }
    std::string str() {
        const uint64_t n = u();
        if (n > bytes_.size() - at_) {
            ok_ = false;
            return {};
        }
        std::string s(bytes_.begin() + static_cast<std::ptrdiff_t>(at_), bytes_.begin() + static_cast<std::ptrdiff_t>(at_ + n));
        at_ += n;
        return s;
    }
    // A count of things to follow, refused when the save could not hold that many.
    size_t count() {
        const uint64_t n = u();
        if (n > bytes_.size() - at_) {
            ok_ = false;
            return 0;
        }
        return static_cast<size_t>(n);
    }
    // A unit type, refusing the save when it names none.
    UnitType type(bool orNone = false) {
        const uint64_t t = u();
        if (t > static_cast<uint64_t>(UnitType::Count) || (t == static_cast<uint64_t>(UnitType::Count) && !orNone)) ok_ = false;
        return ok_ ? static_cast<UnitType>(t) : UnitType::LightInfantry;
    }
    bool ok() const { return ok_; }
    bool done() const { return at_ == bytes_.size(); }

private:
    std::vector<uint8_t> bytes_;
    size_t at_ = 0;
    bool ok_ = true;
};

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string toBase64(const std::vector<uint8_t>& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (size_t k = 0; k < in.size(); k += 3) {
        const uint32_t n = (static_cast<uint32_t>(in[k]) << 16) | (k + 1 < in.size() ? static_cast<uint32_t>(in[k + 1]) << 8 : 0u) |
                           (k + 2 < in.size() ? in[k + 2] : 0u);
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += k + 1 < in.size() ? kB64[(n >> 6) & 63] : '=';
        out += k + 2 < in.size() ? kB64[n & 63] : '=';
    }
    return out;
}

bool fromBase64(const std::string& in, size_t from, std::vector<uint8_t>& out) {
    uint32_t acc = 0;
    int bits = 0;
    for (size_t k = from; k < in.size(); ++k) {
        const char c = in[k];
        if (c == '=') break;
        int v = -1;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(acc >> bits));
        }
    }
    return true;
}

void writeArmy(Writer& w, const Army& a) {
    w.u(a.id);
    w.u(static_cast<uint64_t>(a.type));
    w.str(a.name);
    w.i(a.soldiers);
    w.i(a.fullStrength);
    w.i(a.kills);
    w.i(a.rank);
    w.i(a.blessings);
    w.i(a.hits);
    w.u(a.bornDay);
    w.b(a.ally);
    w.u(a.temples);
    w.u(a.artifacts.size());
    for (uint8_t k : a.artifacts) w.u(k);
}

Army readArmy(Reader& r) {
    Army a;
    a.id = r.u32();
    a.type = r.type();
    a.name = r.str();
    a.soldiers = r.i32();
    a.fullStrength = r.i32();
    a.kills = r.i32();
    a.rank = r.i32();
    a.blessings = r.i32();
    a.hits = r.i32();
    a.bornDay = r.u32();
    a.ally = r.b();
    a.temples = r.u8();
    a.artifacts.resize(r.count());
    for (uint8_t& k : a.artifacts) k = r.u8();
    return a;
}

uint64_t sealOf(const std::vector<uint8_t>& bytes) {
    StateHasher h;
    for (uint8_t b : bytes) h.addU64(b);
    return h.value();
}

// What the map is: a save is for this continent only.
uint64_t mapPrint(const Campaign& c) {
    StateHasher h;
    h.addI64(c.width());
    h.addI64(c.height());
    h.addU64(c.cities().size());
    h.addU64(c.sites().size());
    h.addU64(c.hexes().size());
    h.addU64(c.sides().size());
    for (int32_t y = 0; y < c.height(); y += 7)
        for (int32_t x = 0; x < c.width(); x += 7) h.addU64(static_cast<uint64_t>(c.terrainAt(x, y)) | (c.navigable(x, y) ? 0x100u : 0u));
    for (const Campaign::CityState& s : c.cities()) {
        h.addI64(s.cx);
        h.addI64(s.cy);
    }
    return h.value();
}

}  // namespace

std::string Campaign::save() const {
    Writer w;
    w.u(kFormat);
    w.u(seed_);
    w.u(mapPrint(*this));
    w.u(tick_);
    w.u(nextId_);
    w.u(rng_.state());
    w.u(rng_.increment());
    w.u(aiRng_.state());
    w.u(aiRng_.increment());
    w.u(sides_.size());
    for (const Side& s : sides_) {
        w.str(s.name);
        w.i(s.gold);
        w.b(s.alive);
        w.b(s.human);
        w.u(static_cast<uint64_t>(s.difficulty));
        w.i(s.deserted);
    }
    w.u(cities_.size());
    for (const CityState& c : cities_) {
        w.u(c.owner);
        w.i(c.defense);
        w.i(c.income);
        w.u(c.options.size());
        for (const Option& o : c.options) {
            w.u(static_cast<uint64_t>(o.type));
            w.i(o.days);
            w.i(o.cost);
            w.i(o.left);
        }
        w.i(c.producing);
        w.i(c.progress);
        w.i(c.cx);
        w.i(c.cy);
        w.b(c.razed);
        w.i(c.vectorTo);
        w.b(c.port);
        w.i(c.hx);
        w.i(c.hy);
        w.i(c.boatDays);
        w.u(c.banner);
        w.i(c.bannerProgress);
    }
    w.u(stacks_.size());
    for (const Stack& s : stacks_) {
        w.u(s.id);
        w.u(s.owner);
        w.i(s.cx);
        w.i(s.cy);
        w.u(s.armies.size());
        for (const Army& a : s.armies) writeArmy(w, a);
        w.u(s.path.size());
        for (const auto& p : s.path) {
            w.i(p.first);
            w.i(p.second);
        }
        w.i(s.progress);
        w.b(s.garrison);
        w.u(s.boat);
    }
    w.u(fights_.size());
    for (const Fight& f : fights_) {
        w.u(f.attacker);
        w.u(f.defender);
        w.i(f.city);
        w.b(f.attackerWon);
        w.b(f.captured);
        w.i(f.attackerLost);
        w.i(f.defenderLost);
        w.u(f.day);
    }
    w.u(armiesRaised_.size());
    for (int32_t n : armiesRaised_) w.i(n);
    w.u(pending_.stack);
    w.i(pending_.cx);
    w.i(pending_.cy);
    w.u(transits_.size());
    for (const Transit& t : transits_) {
        w.u(t.owner);
        w.i(t.from);
        w.i(t.to);
        w.u(t.arriveDay);
        writeArmy(w, t.army);
    }
    w.u(sites_.size());
    for (const SiteState& s : sites_) {
        w.b(s.searched);
        w.i(s.guardian);
        w.u(s.bit);
        w.b(s.gifted);
    }
    w.u(loot_.size());
    for (const Loot& l : loot_) {
        w.i(l.cx);
        w.i(l.cy);
        w.u(l.artifact);
    }
    w.u(deeds_.size());
    for (const Deed& d : deeds_) {
        w.u(d.day);
        w.u(d.side);
        w.str(d.text);
    }
    w.u(offers_.size());
    for (const Offer& o : offers_) {
        w.b(o.open);
        w.i(o.city);
        w.i(o.price);
        w.str(o.name);
        w.u(static_cast<uint64_t>(o.companion));
        w.i(o.companions);
    }
    w.u(artifactFound_.size());
    for (uint8_t f : artifactFound_) w.u(f);
    w.i(heroesHired_);
    w.u(boats_.size());
    for (const Boat& b : boats_) {
        w.u(b.id);
        w.u(b.owner);
        w.str(b.name);
        w.i(b.cx);
        w.i(b.cy);
        w.i(b.port);
        w.u(b.stack);
    }
    w.u(boatsBuilt_.size());
    for (int32_t n : boatsBuilt_) w.i(n);
    w.i(voyages_);
    w.u(hexes_.size());
    for (const Hex& h : hexes_) {
        w.u(h.owner);
        w.i(h.claim);
    }
    w.u(checksum());
    // And the bytes' own hash, for damage to what the checksum does not cover (names, the news).
    std::vector<uint8_t> bytes = w.bytes();
    const uint64_t seal = sealOf(bytes);
    for (int k = 0; k < 8; ++k) bytes.push_back(static_cast<uint8_t>(seal >> (8 * k)));
    return kMagic + toBase64(bytes);
}

std::unique_ptr<Campaign> Campaign::load(const Continent& continent, const std::string& text, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error) *error = why;
        return std::unique_ptr<Campaign>();
    };
    const std::string magic = kMagic;
    std::vector<uint8_t> bytes;
    if (text.compare(0, magic.size(), magic) != 0 || !fromBase64(text, magic.size(), bytes)) return fail("not a saved campaign");
    if (bytes.size() < 8) return fail("damaged (cut short)");
    uint64_t seal = 0;
    for (int k = 0; k < 8; ++k) seal |= static_cast<uint64_t>(bytes[bytes.size() - 8 + static_cast<size_t>(k)]) << (8 * k);
    bytes.resize(bytes.size() - 8);
    if (seal != sealOf(bytes)) return fail("damaged");
    Reader r(std::move(bytes));
    if (r.u() != kFormat) return fail("saved by another version of the game");
    const uint64_t seed = r.u();
    auto c = std::make_unique<Campaign>(continent, seed);
    if (r.u() != mapPrint(*c)) return fail("saved on another map of Illyria (an older version of the game)");
    c->tick_ = r.u32();
    c->nextId_ = r.u32();
    {
        const uint64_t s = r.u(), inc = r.u();
        c->rng_ = Rng::fromState(s, inc);
        const uint64_t as = r.u(), ainc = r.u();
        c->aiRng_ = Rng::fromState(as, ainc);
    }
    if (r.count() != c->sides_.size()) return fail("damaged (sides)");
    for (Side& s : c->sides_) {
        s.name = r.str();
        s.gold = r.i32();
        s.alive = r.b();
        s.human = r.b();
        s.difficulty = r.u() ? Difficulty::Hard : Difficulty::Easy;
        s.deserted = r.i32();
    }
    if (r.count() != c->cities_.size()) return fail("damaged (cities)");
    for (CityState& cs : c->cities_) {
        cs.owner = r.u8();
        cs.defense = r.i32();
        cs.income = r.i32();
        cs.options.resize(r.count());
        for (Option& o : cs.options) {
            o.type = r.type();
            o.days = r.i32();
            o.cost = r.i32();
            o.left = r.i32();
        }
        cs.producing = r.i32();
        cs.progress = r.i32();
        cs.cx = r.i32();
        cs.cy = r.i32();
        cs.razed = r.b();
        cs.vectorTo = r.i32();
        cs.port = r.b();
        cs.hx = r.i32();
        cs.hy = r.i32();
        cs.boatDays = r.i32();
        cs.banner = r.u8();
        cs.bannerProgress = r.i32();
    }
    c->stacks_.resize(r.count());
    for (Stack& s : c->stacks_) {
        s.id = r.u32();
        s.owner = r.u8();
        s.cx = r.i32();
        s.cy = r.i32();
        s.armies.resize(r.count());
        for (Army& a : s.armies) a = readArmy(r);
        s.path.resize(r.count());
        for (auto& p : s.path) {
            p.first = r.i32();
            p.second = r.i32();
        }
        s.progress = r.i32();
        s.garrison = r.b();
        s.boat = r.u32();
    }
    c->fights_.resize(r.count());
    for (Fight& f : c->fights_) {
        f.attacker = r.u8();
        f.defender = r.u8();
        f.city = r.i32();
        f.attackerWon = r.b();
        f.captured = r.b();
        f.attackerLost = r.i32();
        f.defenderLost = r.i32();
        f.day = r.u32();
    }
    c->armiesRaised_.resize(r.count());
    for (int32_t& n : c->armiesRaised_) n = r.i32();
    c->pending_.stack = r.u32();
    c->pending_.cx = r.i32();
    c->pending_.cy = r.i32();
    c->transits_.resize(r.count());
    for (Transit& t : c->transits_) {
        t.owner = r.u8();
        t.from = r.i32();
        t.to = r.i32();
        t.arriveDay = r.u32();
        t.army = readArmy(r);
    }
    if (r.count() != c->sites_.size()) return fail("damaged (sites)");
    for (SiteState& s : c->sites_) {
        s.searched = r.b();
        s.guardian = r.i32();
        s.bit = r.u8();
        s.gifted = r.b();
    }
    c->loot_.resize(r.count());
    for (Loot& l : c->loot_) {
        l.cx = r.i32();
        l.cy = r.i32();
        l.artifact = r.u8();
    }
    c->deeds_.resize(r.count());
    for (Deed& d : c->deeds_) {
        d.day = r.u32();
        d.side = r.u8();
        d.text = r.str();
    }
    if (r.count() != c->offers_.size()) return fail("damaged (offers)");
    for (Offer& o : c->offers_) {
        o.open = r.b();
        o.city = r.i32();
        o.price = r.i32();
        o.name = r.str();
        o.companion = r.type(true);
        o.companions = r.i32();
    }
    if (r.count() != c->artifactFound_.size()) return fail("damaged (artifacts)");
    for (uint8_t& f : c->artifactFound_) f = r.u8();
    c->heroesHired_ = r.i32();
    c->boats_.resize(r.count());
    for (Boat& b : c->boats_) {
        b.id = r.u32();
        b.owner = r.u8();
        b.name = r.str();
        b.cx = r.i32();
        b.cy = r.i32();
        b.port = r.i32();
        b.stack = r.u32();
    }
    c->boatsBuilt_.resize(r.count());
    for (int32_t& n : c->boatsBuilt_) n = r.i32();
    c->voyages_ = r.i32();
    if (r.count() != c->hexes_.size()) return fail("damaged (land)");
    for (Hex& h : c->hexes_) {
        h.owner = r.u8();
        h.claim = r.i32();
    }
    const uint64_t sum = r.u();
    if (!r.ok() || !r.done()) return fail("damaged (cut short)");
    if (sum != c->checksum()) return fail("damaged (its checksum does not match)");
    return c;
}

}  // namespace illyria::sim
