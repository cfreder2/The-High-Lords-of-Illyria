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
    // Two ninths of the 12.8 x 16 km it was first laid out at (docs/MAP.md).
    static constexpr int64_t kWidth = 2844;   // metres, west to east
    static constexpr int64_t kHeight = 3556;  // metres, south to north
    // Heights on the mountains' own scale, given as they were first laid out:
    // the ranges' peaks, and what is judged by them (bare rock, the campaign's
    // hills and mountains, how high a town may stand). A third of that, as
    // the map is a third of the size it was before One World, so the ranges
    // are as steep as they were and their passes as easy (docs/MAP.md).
    static constexpr int64_t kPeakNum = 1, kPeakDen = 3;
    static constexpr Fixed peakHeight(int64_t metres) { return Fixed::fromRatio(metres * kPeakNum, kPeakDen); }

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
        // The deck is level over the water, from pos - dir * halfSpan to pos + dir * halfSpan, at height `deck`; at each end
        // a ramp (rampA behind, rampB ahead, metres; 0: none) grades down to the ground it stands on, endA and endB the
        // ground's height at their far ends, so a man steps on at the level of the bank. The grade is kRampGrade at most.
        Fixed rampA = {}, rampB = {}, endA = {}, endB = {};
        static constexpr int64_t kRampGradePct = 16;
        bool bridged() const { return kind != Kind::Ford; }
        // The deck's top at `s` metres along dir from pos (the whole deck: ramps and span).
        Fixed deckAtAlong(Fixed s) const {
            const auto clamp01 = [](Fixed v) { return v < Fixed::zero() ? Fixed::zero() : v > Fixed::one() ? Fixed::one() : v; };
            if (rampA > Fixed::zero() && s < -halfSpan) return endA + (deck - endA) * clamp01((s + halfSpan + rampA) / rampA);
            if (rampB > Fixed::zero() && s > halfSpan) return endB + (deck - endB) * clamp01((halfSpan + rampB - s) / rampB);
            return deck;
        }
        // How far the deck, ramps included, reaches either side of pos.
        Fixed reachBehind() const { return halfSpan + rampA; }
        Fixed reachAhead() const { return halfSpan + rampB; }
        // Dry ground at the dir end of the deck, beside the road (4 m on from the ramp's foot, 3 m clear of the deck's edge): where
        // an outpost's flag stands, presentation only.
        Vec3 bridgehead() const {
            const Vec3 across{-dir.y, dir.x, Fixed::zero()};
            return pos + dir * (reachAhead() + Fixed::fromInt(4)) + across * (halfWidth + Fixed::fromInt(3));
        }
        // The middle of the whole deck, along dir from pos, and half its length.
        Fixed spanMiddle() const { return (reachAhead() - reachBehind()) / 2; }
        Fixed spanHalf() const { return (reachAhead() + reachBehind()) / 2; }
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
    // The plan of a city's defences, in continent coordinates (z = ground).
    // Walls run corner to corner round each ring (outer first; capitals have
    // an inner bailey too), with a gate in one edge. Archers stand on the
    // wall-walk, reached by ramps against the inner faces, and go round the
    // towers on galleries; the courtyard behind the gate is left open for
    // mustering troops and siege engines.
    struct Castle {
        // A gallery round a tower's inner side at the height of the walk
        // (castle_walls.cpp): stone corbelled out from the tower, so men on
        // the walk go round the tower, never into it. It reaches from the
        // tower's face kGalleryWidth out, and round from the walk's middle
        // line on one side (`from`: unit and flat, out of the tower's
        // middle) through the inside of the ring (`mid`) to the other
        // (`to`), in the order of the ring's corners.
        struct Gallery {
            Vec3 pos;      // the tower's middle (z as the ring's corners)
            Fixed radius;  // the tower's
            Vec3 from, mid, to;
            // Points round it `r` from the tower's middle, `from` to `to`, a few degrees apart.
            std::vector<Vec3> arc(Fixed r) const;
        };
        struct Ring {
            std::vector<Vec3> corners;
            Fixed height, thick;  // walls; the walk lies kWalkBelow under the height
            size_t gateEdge = 0;  // edge corners[i] -> corners[i + 1] holding the gate
            Fixed gateWidth;
            // Which corners have a tower (planCastles: where the wall turns
            // sharply, and about one to every kTowerSpacing of wall); the walk
            // goes round a tower on its gallery and past a bare corner. Empty:
            // every corner has one. (The gate's flanking pair is always there.)
            std::vector<uint8_t> towered = {};
            bool towerAt(size_t corner) const { return towered.empty() || towered[corner] != 0; }
            // The wall-walk's height above the ground.
            Fixed walk() const { return height - kWalkBelow; }
            // The middle of the gate-flanking towers, from the gate's middle
            // (their faces flush with the passage).
            Fixed gateTowerAt() const { return gateWidth / 2 + thick * 6 / 5; }
            Vec3 centre() const {
                Vec3 mid{};
                for (const Vec3& q : corners) mid += q;
                return corners.empty() ? mid : mid / Fixed::fromInt(static_cast<int64_t>(corners.size()));
            }
            // The embrasures along edge e (castle_walls.cpp): their middles, in
            // metres from corners[e], kCrenelPitch apart, clear of the towers.
            std::vector<Fixed> crenels(size_t edge) const;
            // The galleries round its towers (castle_walls.cpp), in the order
            // the walk passes them: each corner's (radius 0 at a bare corner: the walk
            // just turns there), then on the gate's edge the pair flanking the gate.
            std::vector<Gallery> galleries() const;
            // The way men go round its walk: along the walk's middle line and
            // round each tower on its gallery, kGalleryWay out from the tower;
            // where two galleries run into each other (towers close together
            // on a short wall), across from one to the other where they meet.
            // z as the corners.
            std::vector<Vec3> way() const;
        };
        // The top of a curtain wall (castle_walls.cpp). Along the walk's outer
        // edge a parapet kParapetThick deep rises kParapetSill (a low breastwork
        // a man stands at, about knee high) and between the embrasures, which
        // are kCrenelWidth wide every kCrenelPitch, merlons rise to kMerlonTop,
        // higher than a man.
        static constexpr Fixed kWalkBelow = Fixed::fromRatio(13, 10);
        static constexpr Fixed kParapetThick = Fixed::fromRatio(3, 5);
        static constexpr Fixed kParapetSill = Fixed::fromRatio(1, 2);
        static constexpr Fixed kMerlonTop = Fixed::fromInt(2);
        static constexpr Fixed kCrenelWidth = Fixed::fromInt(1);
        static constexpr Fixed kCrenelPitch = Fixed::fromInt(4);
        // A gallery is as wide as the walk behind its parapet, with a breast-
        // high parapet kGalleryParapet thick on its open side; men go round
        // it kGalleryWay out from the tower (the walk's middle line, whose
        // lanes keep right of it).
        static constexpr Fixed kGalleryWidth = Fixed::fromRatio(17, 5);
        static constexpr Fixed kGalleryParapet = Fixed::fromRatio(1, 2);
        static constexpr Fixed kGalleryWay = Fixed::fromRatio(13, 10);
        struct Tower {
            Vec3 pos;
            Fixed radius, height;
            bool roofed = false;
        };
        // A ramp of earth faced with stone up a wall's inner face, from the
        // ground (foot) to a level landing kRampLanding long beside the walk
        // (head.z = the walk): gentle enough for men on foot, two abreast.
        // Horses and wheels stay below (docs/GAME_DESIGN.md, castles).
        struct Ramp {
            Vec3 foot, head;
            Fixed halfWidth;
            size_t ring = 0;  // the ring whose walk it climbs to
        };
        // Metres of wall to a corner tower, where the wall turns less than
        // kSharpCorner (the cosine of the turn) there.
        static constexpr int64_t kTowerSpacing = 80;
        static constexpr Fixed kSharpCorner = Fixed::fromRatio(423, 1000);  // cos 65 degrees
        static constexpr Fixed kRampHalfWidth = Fixed::fromRatio(3, 2);
        static constexpr Fixed kRampLanding = Fixed::fromInt(3);
        struct House {
            Vec3 pos;
            Fixed halfX, halfY, height;  // axis-aligned
        };
        std::vector<Ring> rings;
        std::vector<Tower> towers;
        std::vector<Ramp> ramps;
        std::vector<House> houses;
        Vec3 keep, keepAxis;  // the keep: centre (z = ground) and facing
        Fixed keepHalf, keepHeight;
        Vec3 courtyard;  // open ground, radius courtyardRadius
        Fixed courtyardRadius;
        uint8_t shape = 0;  // 0 square, 1 long, 2 polygon, 3 shield
    };
    // A walled city on a levelled pad; capitals are castles. Every city produces
    // armies and income for its owner (the campaign layer).
    struct City {
        std::string name;
        Vec3 pos;      // z = the pad's height
        Fixed radius;  // the walls
        uint8_t owner = kNeutral;
        bool capital = false;
        Castle castle = {};
    };
    // Places heroes explore: ruins hold a guardian and a reward, temples bless,
    // sages tell what they know (first come, first served).
    struct Site {
        enum class Kind : uint8_t { Ruin, Temple, Sage };
        Kind kind;
        std::string name;
        Vec3 pos;  // z = ground height
        // How big its building stands (metres; the renderer's model of it is
        // a unit across) and the quarter turns it is set at, from its place in
        // the list: the battle lays its stones out the same way (Region,
        // siteStones). A ruin is a temple's platform, 2.6 m high at this size
        // with its columns 12 m above it (game/web/models/ruin.bin), on a
        // levelled pad (kRuinPad: its whole footprint; carve).
        static constexpr int kRuinSize = 24, kTempleSize = 44, kSageSize = 26;
        static constexpr int kRuinPad = kRuinSize * 3 / 4, kRuinPadBlend = 24;
        static int size(Kind k) { return k == Kind::Ruin ? kRuinSize : k == Kind::Temple ? kTempleSize : kSageSize; }
        static int turns(size_t index) { return static_cast<int>((index * 2654435761u >> 11) & 3u); }
    };
    struct Label {  // realm and region names for the map
        std::string name;
        Vec3 pos;
        uint8_t kind;  // 0 realm, 1 mountains, 2 water, 3 forest/swamp, 4 crossing
    };
    // Ground shaped for a battle to be fought over, a few hundred metres
    // across, laid on the land once the towns and sites stand
    // (continent_relief.cpp): set pieces made by hand, and gorges found
    // wherever the land has room for one.
    struct Landmark {
        // ChurchHill: a steep hill, a church and a pond on its level top,
        // and a road cut into its side winding up to them. Gorge: a
        // tableland split by a cleft whose floor is the old ground, cliffs
        // either side. Terraces: a hill stepped into level fields.
        enum class Kind : uint8_t { ChurchHill, Gorge, Terraces };
        Kind kind = Kind::Gorge;
        std::string name;  // empty for the gorges found by search
        Vec3 centre;       // z = the land there before it was shaped
        Fixed radius;      // the ground within is the landmark's own
        Fixed rise;        // how far it stands above the land
        // Hills: the level top (its height, and its radius); terraces: the steps.
        Fixed top, summit;
        int32_t steps = 0;
        // Gorges: a tableland halfLength x halfWidth along `axis`, the floor
        // `floor` metres either side of `line`.
        Vec3 axis;
        Fixed halfLength, halfWidth, floor;
        // Gorges: the floor's middle, z = the ground there. Church hill: its road's bench, z = the surface.
        std::vector<Vec3> line;
        std::vector<Obstacle> buildings;  // pos.z = the ground they stand on
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
    // Land in the lakes: Abbey Isle and the Spit that reaches it.
    std::vector<Area> isles() const;
    // Points along each isle's middle line (its longer axis), every 4 m,
    // where its ground stands out of the water: the campaign's grid counts
    // every cell they fall in as land, so a walker crosses the Spit, too
    // narrow for its cells' middles to fall on.
    std::vector<Vec3> isleWalk() const;
    const std::vector<Label>& labels() const { return labels_; }
    static const std::vector<Faction>& factions();
    const std::vector<City>& cities() const { return cities_; }
    const std::vector<Site>& sites() const { return sites_; }
    // The mountain passes: centre (z = how wide they are).
    const std::vector<Vec3>& passes() const { return passes_; }
    const std::vector<Landmark>& landmarks() const { return landmarks_; }
    // The landmark whose ground this is, or null.
    const Landmark* landmarkAt(Fixed x, Fixed y) const;
    // Everything placed on the map: cities, sites and the river surfaces.
    uint64_t checksum() const;
    uint64_t seed() const { return seed_; }
    // The traffic over each point of each road (castle-to-castle journeys: a capital counts four), which makes its width.
    const std::vector<std::vector<int64_t>>& roadTraffic() const { return roadTraffic_; }

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
        // The battle-scale relief in `height` (knolls, ridges, landmarks):
        // height - relief is the land's broad shape, as the design drew it.
        Fixed relief;
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
    void shapeRoads();
    void computeRiverSurfaces();
    void placeCities();
    void placeSites();
    // The campaign's cells (Campaign::kCell) a walker can reach from a town:
    // neither water nor mountain (Campaign::initTerrain), joined to a town's.
    std::vector<uint8_t> walkableCells() const;
    // Whether a footprint of this radius is dry, gentle land clear of rivers; returns the slope.
    bool buildable(Fixed x, Fixed y, Fixed radius, Fixed maxMountain, Sample& centre, Fixed& slope) const;
    Fixed landDistance(Fixed x, Fixed y) const;
    Fixed baseHeight(Fixed x, Fixed y, Fixed& mountain) const;  // the procedural design, before rivers
    // The land before rivers: baked relief (plus fine detail) or the design.
    Fixed reliefHeight(Fixed x, Fixed y, Fixed& mountain) const;
    Fixed massifMask(Fixed x, Fixed y, Fixed& peak) const;
    void loadBakedRelief();
    // Bridges over the creeks the roads from road `firstRoad` on cross (and the shapes of all the bridged crossings from `firstCrossing`).
    void placeBridges(size_t firstRoad = 0, size_t firstCrossing = 0);
    // A bridge's span from its place and direction: dry banks both ends along the axis, the deck's level, the ramps (placeBridges, fitBridgesToRoads).
    void spanCrossing(Crossing& cr);
    void planCastles();
    // The roads to the towns' gates (continent_roads.cpp): each town's path to
    // the road network is found before its castle is planned (which way the
    // gate faces), and laid from the gate once it stands.
    void keepRoadsDry();
    void routeApproaches();
    void layApproaches();
    // The finished roads (continent_roads.cpp): spikes and stubs out, then each road's class from its traffic, the width that
    // follows, and the bends round the boulders on its line. `gateRoad` is the road laid from each town's gate (SIZE_MAX: none).
    void shapeRoads(const std::vector<size_t>& gateRoad);
    void bucketRoads();
    void applyLake(size_t lake, Fixed& z, Sample& s, Fixed x, Fixed y) const;
    // The lakes of the design; the landmarks' ponds follow them in lakes_.
    size_t naturalLakes() const { return naturalLakes_ ? naturalLakes_ : lakes_.size(); }

    // The battle-scale relief (continent_relief.cpp). dress() lays it on
    // once everything else is placed (which it keeps clear of), so towns,
    // sites and bridges stand where the design put them. The set pieces are
    // found room first (planSetPieces, before the towns, which keep off them:
    // they keep their size on a map a third the size it was).
    void planSetPieces();
    // Whether a town of this radius, or a site, at p would stand on a set piece's ground.
    bool onSetPiece(const Vec3& p, Fixed gap) const;
    void dress();
    void addChurchHill(const std::string& name, Vec3 at);
    void addGorgeOnRoad(const std::string& name, Vec3 at, Fixed halfLength);
    void addTerraces(const std::string& name, Vec3 at);
    void findGorges();
    // Room for a landmark of this radius: clear of towns, sites, crossings,
    // lakes and set pieces, on dry ground out of the rivers' valleys (with
    // valleys false, only off their banks: a set piece must go somewhere).
    bool roomForLandmark(Vec3 at, Fixed radius, bool roadsAllowed, bool valleys = true) const;
    void buildClearGrid();
    // Knolls and ridges, scaled by how clear the ground is (metres to add).
    Fixed reliefAt(Fixed x, Fixed y, const Sample& s) const;
    // A landmark's own rise at a point, before its level ground (summit, road) is cut.
    Fixed landmarkRise(const Landmark& lm, Fixed x, Fixed y) const;
    void shapeLandmarks(Fixed& z, Fixed x, Fixed y) const;

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
    // Land in a lake: the isle in the Heartmere and the spit that reaches it.
    struct Isle {
        Area area;
        size_t lake;
        Fixed rise;  // metres above the water at its middle
    };
    std::vector<Isle> isles_;
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> roadBuckets_;  // road and segment near each bucket
    std::vector<Fixed> lakeSurfaces_;
    std::vector<Label> labels_;
    std::vector<City> cities_;
    // Between routeApproaches and layApproaches: each town's path from its middle to the road network, and the order they were found in.
    std::vector<std::vector<Vec3>> approach_;
    std::vector<size_t> approachOrder_;
    std::vector<std::vector<Vec3>> links_;  // and the paths joining the road networks to each other
    // The traffic (journeys between castles' gates) over each point of each road, from which its width came (shapeRoads).
    std::vector<std::vector<int64_t>> roadTraffic_;
    std::vector<Site> sites_;
    // The ruins' levelled ground (x, y, height; carve), laid once the land is
    // made, so nothing placed or dressed before moves.
    std::vector<Vec3> ruinPads_;
    std::vector<Landmark> landmarks_;
    // Where the set pieces go (planSetPieces): x, y, z = the radius of ground they need.
    std::vector<Vec3> setPieces_;
    bool dressed_ = false;
    size_t naturalLakes_ = 0;
    // How freely battle-scale relief may rise (0..255) every kClearCell
    // metres: none at towns, sites, crossings, creeks, pools, lakes and landmarks.
    static constexpr int64_t kClearCell = 16;
    int32_t clearNx_ = 0, clearNy_ = 0;
    std::vector<uint8_t> clear_;
    std::vector<std::vector<uint32_t>> landmarkBuckets_;  // landmarks near each 256 m bucket
    struct SeaShape {
        Vec3 a, b;
        Fixed radius;
        bool land;  // islands are land shapes inside the sea
    };
    std::vector<SeaShape> sea_;
};

}  // namespace illyria::sim
