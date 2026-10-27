#pragma once

#include <vector>

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
Mesh banner();
Mesh ruin();
Mesh temple();
Mesh sageTower();
}  // namespace meshes

}  // namespace illyria::game
