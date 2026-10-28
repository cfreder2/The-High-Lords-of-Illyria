#include <cmath>

#include "figures.h"
#include "test.h"

using namespace illyria;
using namespace illyria::game;

// Every type has a figure for every frame: finite, standing on the ground
// while alive, the side's colour on it, and its pictures inside a sheet.
TEST(every_figure_poses_stands_and_wears_its_colours) {
    for (int t = 0; t < static_cast<int>(sim::UnitType::Count); ++t) {
        const auto type = static_cast<sim::UnitType>(t);
        const figures::Plan plan = figures::plan(type);
        bool finite = true, marked = false, grounded = true;
        for (int f = 0; f < figures::kFramesPerView; ++f) {
            const std::vector<float>& m = plan.frames[f];
            CHECK(m.size() >= 30u * figures::kVertexFloats && m.size() % (3 * figures::kVertexFloats) == 0);
            float lowest = 1e9f;
            for (size_t i = 0; i < m.size(); i += figures::kVertexFloats) {
                for (int k = 0; k < figures::kVertexFloats; ++k) finite = finite && std::isfinite(m[i + k]);
                marked = marked || m[i + 9] > 0.5f;
                lowest = std::min(lowest, m[i + 2]);
            }
            // Idle and walking figures touch the ground (feet, hooves or claws).
            if (f < figures::kFirstFrame[2]) grounded = grounded && std::fabs(lowest) < 0.12f;
            for (int v = 0; v < figures::kViews; ++v) {
                const figures::Extent& e = plan.extents[v][f];
                CHECK(e.u1 > e.u0 && e.v1 > e.v0);
                CHECK(e.u1 - e.u0 < 40 && e.v1 - e.v0 < 40);
            }
        }
        if (!finite || !marked || !grounded) std::printf("  %s: finite %d, team colour %d, on the ground %d\n", sim::unitTypeName(type), finite, marked, grounded);
        CHECK(finite);
        CHECK(marked);
        CHECK(grounded);
    }
}

TEST(sheet_views_turn_with_the_figure) {
    // View 0 looks at the figure's face, view 2 at its right side, view 4 at its back.
    CHECK(figures::view(0).toward.x > 0.8f);
    CHECK(figures::view(2).toward.y < -0.8f);
    CHECK(figures::view(4).toward.x < -0.8f);
    // Seen from its right side, a figure facing +x faces the picture's right.
    CHECK(figures::view(2).right.x > 0.99f);
    for (int v = 0; v < figures::kViews; ++v) CHECK_NEAR(figures::view(v).up.dot(figures::view(v).right), 0.0, 1e-5);
}
