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
        Fixed halfWidth;
        Fixed depth;
    };
    struct Ridge {
        std::string name;
        Polyline line;  // halfWidth = footprint
        Fixed peak;     // metres above the land
    };
    struct Crossing {
        enum class Kind : uint8_t { Ford, Bridge };
        Kind kind;
        std::string name;
        Vec3 pos;
    };
    struct Area {  // swamps, forests and lakes: soft-edged ellipses
        std::string name;
        Vec3 centre;
        Fixed rx, ry;
    };
    struct Label {  // realm and region names for the map
        std::string name;
        Vec3 pos;
        uint8_t kind;  // 0 realm, 1 mountains, 2 water, 3 forest/swamp, 4 crossing
    };

    explicit Continent(uint64_t seed);

    // Macro terrain: no fine detail, but rivers, coast and mountains are exact.
    Fixed heightAt(Fixed x, Fixed y) const;
    Ground groundAt(Fixed x, Fixed y) const;
    // Water surface at a point covered by river, lake or sea (else -inf sentinel: returns false).
    bool waterSurface(Fixed x, Fixed y, Fixed& surface) const;

    const std::vector<River>& rivers() const { return rivers_; }
    const std::vector<Ridge>& ridges() const { return ridges_; }
    const std::vector<Crossing>& crossings() const { return crossings_; }
    const std::vector<Polyline>& roads() const { return roads_; }
    const std::vector<Area>& swamps() const { return swamps_; }
    const std::vector<Area>& forests() const { return forests_; }
    const std::vector<Area>& lakes() const { return lakes_; }
    const std::vector<Label>& labels() const { return labels_; }
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
    };
    Sample sample(Fixed x, Fixed y) const;
    // Coast and mountains only (height, land, mountain filled in).
    Sample landform(Fixed x, Fixed y) const;
    // Applies rivers, lakes and roads to a landform sample.
    void carve(Sample& s, Fixed x, Fixed y) const;
    bool waterSurfaceOf(const Sample& s, Fixed& surface) const;
    bool onBridge(Fixed x, Fixed y, Fixed roadDist) const;
    // Ground type for dry land.
    Ground landGround(Fixed x, Fixed y, Fixed height, Fixed mountain, Fixed roadDist) const;

private:
    void computeRiverSurfaces();
    Fixed landDistance(Fixed x, Fixed y) const;
    Fixed baseHeight(Fixed x, Fixed y, Fixed& mountain) const;  // before rivers

    uint64_t seed_;
    std::vector<River> rivers_;
    std::vector<Ridge> ridges_;
    std::vector<Vec3> passes_;  // z = radius
    std::vector<Crossing> crossings_;
    std::vector<Polyline> roads_;
    std::vector<Area> swamps_, forests_, lakes_;
    std::vector<Fixed> lakeSurfaces_;
    std::vector<Label> labels_;
    struct SeaShape {
        Vec3 a, b;
        Fixed radius;
        bool land;  // islands are land shapes inside the sea
    };
    std::vector<SeaShape> sea_;
};

}  // namespace illyria::sim
