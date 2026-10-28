// Top-down render of the continent plus a JSON overlay of its named features.
#include <chrono>
#include <cstdio>
#include <string>
#include "illyria/sim/continent.h"
using namespace illyria::sim;
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: illyria_continentmap out.ppm out.json\n"); return 2; }
    const int W = 640, H = 800;  // 20 m per pixel
    auto t0 = std::chrono::steady_clock::now();
    // A third argument "design" skips the baked erosion (for trying out the design quickly).
    Continent c(20261027, argc > 3 && std::string(argv[3]) == "design" ? Continent::Relief::Design : Continent::Relief::Baked);
    std::vector<float> h(W * H); std::vector<int> g(W * H);
    for (int py = 0; py < H; ++py) for (int px = 0; px < W; ++px) {
        Fixed x = Fixed::fromInt(px * 20 + 10), y = Fixed::fromInt((H - 1 - py) * 20 + 10);
        h[py * W + px] = (float)c.heightAt(x, y).toDouble();
        g[py * W + px] = (int)c.groundAt(x, y);
    }
    fprintf(stderr, "generated in %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    FILE* f = fopen(argv[1], "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    float maxh = 0;
    for (int i = 0; i < W * H; ++i) if (h[i] > maxh) maxh = h[i];
    for (int py = 0; py < H; ++py) for (int px = 0; px < W; ++px) {
        int i = py * W + px; float z = h[i];
        float zl = px > 0 ? h[i - 1] : z, zu = py > 0 ? h[i - W] : z;
        float shade = 1.0f + ((z - zl) - (z - zu)) * 0.05f; shade = shade < 0.45f ? 0.45f : shade > 1.5f ? 1.5f : shade;
        float c3[3] = {0, 0, 0};
        switch (g[i]) {
            case 0: c3[0]=120;c3[1]=158;c3[2]=72; break;   // plains
            case 1: c3[0]=165;c3[1]=130;c3[2]=85; break;   // road
            case 2: c3[0]=48;c3[1]=96;c3[2]=48; break;     // forest
            case 3: c3[0]=95;c3[1]=110;c3[2]=70; break;    // swamp
            case 4: c3[0]=120;c3[1]=175;c3[2]=200; break;  // shallows
            case 5: c3[0]=45;c3[1]=95;c3[2]=165; break;    // deep
            case 6: c3[0]=135;c3[1]=125;c3[2]=110; break;  // rock
            case 7: c3[0]=120;c3[1]=80;c3[2]=40; break;    // bridge
        }
        if (g[i] != 4 && g[i] != 5 && z > 230) { c3[0]=c3[1]=c3[2]=238; }
        else if (g[i] == 6) { float t = z / 230.0f; c3[0]=110+60*t; c3[1]=100+60*t; c3[2]=90+60*t; }
        else if (g[i] == 0 && z > 120) { c3[0]=150;c3[1]=145;c3[2]=95; }
        bool water = g[i] == 4 || g[i] == 5;
        unsigned char out[3];
        for (int k = 0; k < 3; ++k) { float v = c3[k] * (water ? 1.0f : shade); out[k] = (unsigned char)(v > 255 ? 255 : v); }
        fwrite(out, 1, 3, f);
    }
    fclose(f);
    // Overlays for the Python labeller: rivers, roads, crossings, labels (pixel coords).
    FILE* j = fopen(argv[2], "w");
    auto P = [&](const Vec3& p, double& x, double& y) { x = p.x.toDouble() / 20.0; y = H - 1 - p.y.toDouble() / 20.0; };
    fprintf(j, "{\"rivers\":[");
    for (size_t r = 0; r < c.rivers().size(); ++r) { fprintf(j, "%s{\"w\":%.1f,\"pts\":[", r ? "," : "", c.rivers()[r].halfWidth.toDouble()); for (size_t k = 0; k < c.rivers()[r].points.size(); ++k) { double x, y; P(c.rivers()[r].points[k], x, y); fprintf(j, "%s[%.1f,%.1f]", k ? "," : "", x, y);} fprintf(j, "]}"); }
    fprintf(j, "],\"roads\":[");
    for (size_t r = 0; r < c.roads().size(); ++r) { fprintf(j, "%s[", r ? "," : ""); for (size_t k = 0; k < c.roads()[r].points.size(); ++k) { double x, y; P(c.roads()[r].points[k], x, y); fprintf(j, "%s[%.1f,%.1f]", k ? "," : "", x, y);} fprintf(j, "]"); }
    fprintf(j, "],\"crossings\":[");
    for (size_t r = 0; r < c.crossings().size(); ++r) { double x, y; P(c.crossings()[r].pos, x, y); fprintf(j, "%s{\"k\":%d,\"n\":\"%s\",\"p\":[%.1f,%.1f]}", r ? "," : "", (int)c.crossings()[r].kind, c.crossings()[r].name.c_str(), x, y); }
    fprintf(j, "],\"labels\":[");
    for (size_t r = 0; r < c.labels().size(); ++r) { double x, y; P(c.labels()[r].pos, x, y); fprintf(j, "%s{\"k\":%d,\"n\":\"%s\",\"p\":[%.1f,%.1f]}", r ? "," : "", c.labels()[r].kind, c.labels()[r].name.c_str(), x, y); }
    fprintf(j, "],\"cities\":[");
    for (size_t r = 0; r < c.cities().size(); ++r) { const auto& q = c.cities()[r]; double x, y; P(q.pos, x, y); fprintf(j, "%s{\"o\":%d,\"cap\":%d,\"n\":\"%s\",\"p\":[%.1f,%.1f]}", r ? "," : "", q.owner == Continent::kNeutral ? -1 : q.owner, q.capital ? 1 : 0, q.name.c_str(), x, y); }
    fprintf(j, "],\"sites\":[");
    for (size_t r = 0; r < c.sites().size(); ++r) { const auto& q = c.sites()[r]; double x, y; P(q.pos, x, y); fprintf(j, "%s{\"k\":%d,\"n\":\"%s\",\"p\":[%.1f,%.1f]}", r ? "," : "", (int)q.kind, q.name.c_str(), x, y); }
    fprintf(j, "]}\n"); fclose(j);
    fprintf(stderr, "%zu cities, %zu sites, checksum %016llx\n", c.cities().size(), c.sites().size(), (unsigned long long)c.checksum());
}
