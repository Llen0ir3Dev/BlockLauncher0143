#include <nexus/worldgen/noise.hpp>

#include <cmath>

namespace nexus::worldgen {

namespace {
constexpr double kPi = 3.14159265358979323846;

double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
double lerp(double a, double b, double t) { return a + (b - a) * t; }

double lattice_value(uint64_t seed, int x, int y, int z) {
    uint64_t h = seed;
    h ^= static_cast<uint64_t>(static_cast<int64_t>(x)) * 0x9e3779b97f4a7c15ull;
    h ^= static_cast<uint64_t>(static_cast<int64_t>(y)) * 0xc2b2ae3d27d4eb4full;
    h ^= static_cast<uint64_t>(static_cast<int64_t>(z)) * 0x165667b19e3779f9ull;
    h ^= h >> 30;
    h *= 0xbf58476d1ce4e5b9ull;
    h ^= h >> 27;
    h *= 0x94d049bb133111ebull;
    h ^= h >> 31;
    return static_cast<double>(h >> 11) / static_cast<double>(1ull << 53);
}
} // namespace

uint64_t Rng::next_u64() {
    uint64_t z = (state_ += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

uint32_t Rng::next_u32() { return static_cast<uint32_t>(next_u64() >> 32); }

int Rng::next_int(int bound) {
    if (bound <= 0) return 0;
    return static_cast<int>(next_u64() % static_cast<uint64_t>(bound));
}

int Rng::range(int min_inclusive, int max_inclusive) {
    if (max_inclusive <= min_inclusive) return min_inclusive;
    return min_inclusive + next_int(max_inclusive - min_inclusive + 1);
}

double Rng::next_double() {
    return static_cast<double>(next_u64() >> 11) / static_cast<double>(1ull << 53);
}

float Rng::next_float() { return static_cast<float>(next_double()); }
bool Rng::chance(double probability) { return next_double() < probability; }

uint64_t hash_combine(uint64_t seed, uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

uint64_t hash_position(int x, int z, uint64_t seed) {
    return hash_combine(hash_combine(seed, static_cast<uint64_t>(static_cast<int64_t>(x))),
                        static_cast<uint64_t>(static_cast<int64_t>(z)));
}

uint64_t hash_position(int x, int y, int z, uint64_t seed) {
    return hash_combine(hash_position(x, z, seed), static_cast<uint64_t>(static_cast<int64_t>(y)));
}

ValueNoise2D::ValueNoise2D(uint64_t seed) : seed_(seed) {}

double ValueNoise2D::lattice(int x, int y) const { return lattice_value(seed_, x, y, 0); }

double ValueNoise2D::sample(double x, double y) const {
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const double tx = fade(x - fx);
    const double ty = fade(y - fy);
    const double v00 = lattice(ix, iy);
    const double v10 = lattice(ix + 1, iy);
    const double v01 = lattice(ix, iy + 1);
    const double v11 = lattice(ix + 1, iy + 1);
    return lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);
}

ValueNoise3D::ValueNoise3D(uint64_t seed) : seed_(seed) {}

double ValueNoise3D::lattice(int x, int y, int z) const {
    return lattice_value(seed_, x, y, z);
}

double ValueNoise3D::sample(double x, double y, double z) const {
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const double fz = std::floor(z);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const int iz = static_cast<int>(fz);
    const double tx = fade(x - fx);
    const double ty = fade(y - fy);
    const double tz = fade(z - fz);
    const double v000 = lattice(ix, iy, iz);
    const double v100 = lattice(ix + 1, iy, iz);
    const double v010 = lattice(ix, iy + 1, iz);
    const double v110 = lattice(ix + 1, iy + 1, iz);
    const double v001 = lattice(ix, iy, iz + 1);
    const double v101 = lattice(ix + 1, iy, iz + 1);
    const double v011 = lattice(ix, iy + 1, iz + 1);
    const double v111 = lattice(ix + 1, iy + 1, iz + 1);
    const double x00 = lerp(v000, v100, tx);
    const double x10 = lerp(v010, v110, tx);
    const double x01 = lerp(v001, v101, tx);
    const double x11 = lerp(v011, v111, tx);
    return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

double fbm2(const ValueNoise2D& noise, double x, double y, int octaves,
            double persistence, double lacunarity) {
    double sum = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double normalizer = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += noise.sample(x * frequency + i * 17.13, y * frequency - i * 9.71) * amplitude;
        normalizer += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return normalizer > 0.0 ? sum / normalizer : 0.0;
}

double fbm3(const ValueNoise3D& noise, double x, double y, double z, int octaves,
            double persistence, double lacunarity) {
    double sum = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double normalizer = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += noise.sample(x * frequency + i * 5.31, y * frequency - i * 3.17,
                            z * frequency + i * 11.73) * amplitude;
        normalizer += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return normalizer > 0.0 ? sum / normalizer : 0.0;
}

double ridged2(const ValueNoise2D& noise, double x, double y, int octaves) {
    double sum = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double normalizer = 0.0;
    for (int i = 0; i < octaves; ++i) {
        const double value = 1.0 - std::fabs(noise.sample(x * frequency, y * frequency) * 2.0 - 1.0);
        sum += value * value * amplitude;
        normalizer += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return normalizer > 0.0 ? sum / normalizer : 0.0;
}

} // namespace nexus::worldgen
