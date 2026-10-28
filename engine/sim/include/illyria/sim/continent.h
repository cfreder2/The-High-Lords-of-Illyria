#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "illyria/sim/region.h"
#include "illyria/sim/vec3.h"

namespace illyria::sim {

// The continent of Illyria: an original map with the strategic structure of
// the classic Warlords map (docs/MAP.md). A western sea, a great river that
// forks and divides the land into realms, mountain chains whose passes are
// chokepoints, an eastern range along the whole edge, swamp, forest, lakes,
// and roads that cross the rivers at a few fords and bridges.
//
// This class holds the hand-placed design and evaluates the *macro* terrain:
// deterministic fixed-point functions of position, cheap enough to sample the
// whole continent. Fine 4 m detail is added per chunk on top (ContinentChunk).
class Continent {
public:
    static constexpr int64_t kWidth = 12800;   // metres, west to east
    static constexpr int64_t kHeight = 16000;  // metres, south to north

    struct River {
        std::string name;
        std::vector<Vec3> points;   // source to mouth; z = water surface (filled in)
        Fixed halfWidth;            // typical; widths has the value at each point
        Fixed depth;
        std::vector<Fixed> widths = {};  // half width at each point (filled in)
        // Bounding boxes of runs of kSpan segments, to skip far ones quickly.
        static constexpr size_t kSpan = 16;
        struct Box {
            Fixed minX, minY, maxX, maxY;
        };
        std::vector<Box> boxes = {};
    };
    struct Ridge {
        std::string name;
        Polyline line;  // halfWidth = footprint
        Fixed peak;     // metres above the land
    };
    struct Crossing {
        // Fords are waded; stone bridges carry roads over rivers and creeks;
        // footbridges are narrow timber spans, a file of infantry wide.
        enum class Kind : uint8_t { Ford, Bridge, Footbridge };
        Kind kind;
        std::string name;
        Vec3 pos;
        // Bridges: the deck runs from pos - dir * halfSpan to pos + dir * halfSpan,
        // halfWidth either side, its top at height deck.
        Vec3 dir = {};
        Fixed halfSpan = {}, halfWidth = {}, deck = {};
        bool bridged() const { return kind != Kind::Ford; }
    };
    struct Area {  // swamps, forests and lakes: soft-edged ellipses
        std::string name;
        Vec3 centre;
        Fixed rx, ry;
    };
    // The eight warring sides (docs/MAP.md), each starting from its capital.
    static constexpr int kFactionCount = 8;
    static constexpr uint8_t kNeutral = 255;
    struct Faction {
        std::string name;
        std::string capital;
    };
    // A walled city on a levelled pad; capitals are castles. Every city produces
    // armies and income for its owner (the campaign layer).
    struct City {
        std::string name;
        Vec3 pos;      // z = the pad's height
        Fixed radius;  // the walls
        uint8_t owner = kNeutral;
        bool capital = false;
    };
    // Places heroes explore: ruins hold a guardian and a reward, temples bless,
    // sages tell what they know (first come, first served).
    struct Site {
        enum class Kind : uint8_t { Ruin, Temple, Sage };
        Kind kind;
        std::string name;
        Vec3 pos;  // z = ground height
    };
    struct Label {  // realm and region names for the map
        std::string name;
        Vec3 pos;
        uint8_t kind;  // 0 realm, 1 mountains, 2 water, 3 forest/swamp, 4 crossing
    };

    // Where the land's shape comes from: the relief baked offline by
    // tools/terrainbake (hydraulic erosion and drainage, stored as data, so
    // identical everywhere), or the procedural design it was baked from.
    enum class Relief : uint8_t { Baked, Design };
    explicit Continent(uint64_t seed, Relief relief = Relief::Baked);
    bool baked() const { return baked_; }

    // Macro terrain: no fine detail, but rivers, coast and mountains are exact.
    Fixed heightAt(Fixed x, Fixed y) const;
    Ground groundAt(Fixed x, Fixed y) const;
    // Water surface at a point covered by river, lake or sea (else -inf sentinel: returns false).
    bool waterSurface(Fixed x, Fixed y, Fixed& surface) const;

    const std::vector<River>& rivers() const { return rivers_; }
    // Streams found by the drainage bake: they rise where enough water
    // gathers and run down to a river, lake or the sea.
    const std::vector<River>& streams() const { return streams_; }
    const std::vector<Ridge>& ridges() const { return ridges_; }
    const std::vector<Crossing>& crossings() const { return crossings_; }
    const std::vector<Polyline>& roads() const { return roads_; }
    const std::vector<Area>& swamps() const { return swamps_; }
    const std::vector<Area>& forests() const { return forests_; }
    const std::vector<Area>& lakes() const { return lakes_; }
    const std::vector<Label>& labels() const { return labels_; }
    static const std::vector<Faction>& factions();
    const std::vector<City>& cities() const { return cities_; }
    const std::vector<Site>& sites() const { return sites_; }
    // Everything placed on the map: cities, sites and the river surfaces.
    uint64_t checksum() const;
    uint64_t seed() const { return seed_; }

    // Pieces of the height function, exposed for chunk generation.
    struct Sample {
        Fixed height;      // final macro height
        Fixed land;        // signed distance into land (negative: sea)
        Fixed mountain;    // mountain height contribution
        Fixed riverDist;   // distance to the nearest river centreline
        Fixed riverHalfWidth;
        Fixed riverSurface;
        Fixed riverDepth;
        Fixed roadDist;
        Fixed lakeSurface;  // valid if inLake
        bool inLake = false;
        bool atFord = false;
        // Streams and creeks from the drainage bake.
        Fixed streamDist = Fixed::fromInt(1000);
        Fixed streamHalfWidth;
        Fixed streamSurface;
        Fixed basinSurface;  // valid if inBasin: water pooled in a hollow
        bool inBasin = false;
    };
    Sample sample(Fixed x, Fixed y) const;
    // Coast and mountains only (height, land, mountain filled in).
    Sample landform(Fixed x, Fixed y) const;
    // Applies rivers, lakes and roads to a landform sample.
    void carve(Sample& s, Fixed x, Fixed y) const;
    bool waterSurfaceOf(const Sample& s, Fixed& surface) const;
    // True on a bridge deck, with the deck's height.
    bool onBridge(Fixed x, Fixed y, Fixed& deck) const;
    // Ground type for dry land.
    Ground landGround(Fixed x, Fixed y, Fixed height, Fixed mountain, Fixed roadDist) const;

private:
    void meanderRivers();
    void computeRiverSurfaces();
    void placeCities();
    void placeSites();
    // Whether a footprint of this radius is dry, gentle land clear of rivers; returns the slope.
    bool buildable(Fixed x, Fixed y, Fixed radius, Fixed maxMountain, Sample& centre, Fixed& slope) const;
    Fixed landDistance(Fixed x, Fixed y) const;
    Fixed baseHeight(Fixed x, Fixed y, Fixed& mountain) const;  // the procedural design, before rivers
    // The land before rivers: baked relief (plus fine detail) or the design.
    Fixed reliefHeight(Fixed x, Fixed y, Fixed& mountain) const;
    Fixed massifMask(Fixed x, Fixed y, Fixed& peak) const;
    void loadBakedRelief();
    void placeBridges();

    uint64_t seed_;
    std::vector<River> rivers_, streams_;
    bool baked_ = false;
    // The baked relief: decimetres on a regular grid.
    int32_t reliefNx_ = 0, reliefNy_ = 0;
    int64_t reliefCell_ = 32;
    std::vector<int16_t> reliefDelta_, reliefBasin_;
    // Streams by 256 m bucket: (stream, box) pairs to test.
    static constexpr int64_t kStreamBucket = 256;
    int32_t bucketNx_ = 0, bucketNy_ = 0;
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> streamBuckets_;
    std::vector<Ridge> ridges_;
    std::vector<Vec3> passes_;  // z = radius
    std::vector<Crossing> crossings_;
    std::vector<Polyline> roads_;
    std::vector<Area> swamps_, forests_, lakes_;
    std::vector<Fixed> lakeSurfaces_;
    std::vector<Label> labels_;
    std::vector<City> cities_;
    std::vector<Site> sites_;
    struct SeaShape {
        Vec3 a, b;
        Fixed radius;
        bool land;  // islands are land shapes inside the sea
    };
    std::vector<SeaShape> sea_;
};

}  // namespace illyria::sim
