#pragma once

#include <functional>
#include <vector>

#include "illyria/game/gmath.h"

namespace illyria::game {

// Low-poly meshes built in code: interleaved position (3), normal (3) and
// colour (3) per vertex, unindexed triangles. Each is modelled in a unit
// space that instance transforms scale to size:
//   trees, bushes, rocks: base at the origin, height 1, roughly radius 1
//   house: footprint [-1, 1] x [-1, 1], walls 1 high, roof above
//   capsule: along x from -0.5 to 0.5, radius 0.5; sphere: radius 0.5
//   wall: [-1, 1] x [-1, 1], 1 high, with merlons above; tower: radius 1, walls
//   1 high under a cone roof; banner: a pole 1 high with a white flag (tinted)
//   ruin, temple, sage tower: whole buildings about 1 across
struct Mesh {
    std::vector<float> data;
    int vertexCount() const { return static_cast<int>(data.size() / 9); }
};

namespace meshes {
// Several shapes per kind, so no grove is a row of clones (same unit space as
// the single versions below).
std::vector<Mesh> pineVariants();   // spruce, tall fir, young bushy pine, dead snag
std::vector<Mesh> oakVariants();    // oak, broad oak, elm, autumn oak, birch, autumn birch, dead oak
std::vector<Mesh> bushVariants();   // round, berry, dark thorn, reeds (variant 3)
std::vector<Mesh> rockVariants();   // blob, slab, shard, cluster
Mesh stump();                       // a stump with its fallen top: base at origin, ~1 across
Mesh pine();
Mesh oak();
Mesh bush();
Mesh rock();
Mesh house();
Mesh pillar();
Mesh capsule();
Mesh sphere();
Mesh wall();
Mesh tower();
// Castle pieces (stone; the mesh shader lays masonry over them):
//   curtain: a wall section [-1, 1] x [-1, 1], walk at z = 1 (its parapet
//     is drawn as blocks, where the sim puts the merlons)
//   block: a stone block [-1, 1] x [-1, 1] x [0, 1]
//   drum: a round tower radius 1, 1 high, crenellated, no roof
//   keep: a square keep [-1, 1]^2, 1 high, battlements and corner turrets
//   ramp: a solid wedge whose top rises along x from z = 0 at x = -1 to
//     z = 1 at x = 1, [-1, 1] across; its sides run on a little below z = 0
Mesh curtain();
Mesh block();
Mesh drum();
Mesh keep();
Mesh ramp();
//   gate: two timber doors banded with iron across x in [-1, 1] (+y outward),
//     z from 0 to 1; broken: battered in, hanging and strewn in the passage
Mesh gate(bool broken);
Mesh banner();
// A flagpole: radius 1 (scale it), z from 0 to 1, a gilt finial on top; and
// its cloth, a swallow-tailed banner flying along +x from the pole, hanging
// from z = 0 to -0.64 (scale it by its length).
Mesh flagPole();
Mesh flagCloth();
Mesh ruin();
Mesh temple();
Mesh sageTower();
// A fallen trunk along x from -1 to 1, radius 1 (scale: half length, girth, girth).
Mesh log();
// A campaign boat: a timber hull along x from -1 (stern) to 1 (bow), beam
// about 0.5, the waterline at z = 0, a mast to z = 1.5. The sail is its own
// mesh in white, to tint with the side's colour; same unit space.
Mesh ship();
Mesh sail();
// A timber pier running along x from 0 (the shore) to 1, [-1, 1] across, its
// deck top at z = 0 on piles reaching down to z = -1.
Mesh pier();
// A clump of tree crowns for the distant continent, about 1 across.
Mesh copse();
std::vector<Mesh> copseVariants();  // broadleaf, pines, autumn
// Ground cover, in metres (not scaled to unit size). All normals point up so
// they light like the ground they grow from; colour (2, 2, 2) marks petals,
// which the shader colours per plant.
Mesh grassTuft();
Mesh flowers();
Mesh pebbles();
Mesh twigs();
Mesh fern();
// A whole bridge in world coordinates (drawn with an identity instance): a
// stone road bridge on arches and piers, or a timber footbridge on piles.
// The deck runs from centre - dir * halfSpan to centre + dir * halfSpan, its
// top at deck; ground(x, y) gives the bed and banks below.
void bridge(Mesh& out, V3 centre, V3 dir, float halfSpan, float halfWidth, float deck, bool foot,
            const std::function<float(float, float)>& ground);
}  // namespace meshes

}  // namespace illyria::game
