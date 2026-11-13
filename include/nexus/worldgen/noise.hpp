#pragma once

#include <cstdint>
#include <vector>

namespace nexus::worldgen {

// Deterministic, platform-independent pseudo random number generator.
// The algorithm is SplitMix64, chosen because it is tiny, fast and produces
// the same sequence on every architecture.
class Rng {
public:
    explicit Rng(uint64_t seed = 0) : state_(seed + 0x9e3779b97f4a7c15ull) {}

    uint64_t next_u64();
    uint32_t next_u32();
    int next_int(int bound);
    int range(int min_inclusive, int max_inclusive);
    double next_double();
    float next_float();
    bool chance(double probability);

private:
    uint64_t state_;
};

uint64_t hash_combine(uint64_t seed, uint64_t value);
uint64_t hash_position(int x, int z, uint64_t seed);
uint64_t hash_position(int x, int y, int z, uint64_t seed);

// Value noise with smooth interpolation.  Two dimensional noise is the common
// case for terrain; three dimensional noise is used by caves.
class ValueNoise2D {
public:
    explicit ValueNoise2D(uint64_t seed = 0);
    double sample(double x, double y) const;

private:
    double lattice(int x, int y) const;
    uint64_t seed_;
};

class ValueNoise3D {
public:
    explicit ValueNoise3D(uint64_t seed = 0);
    double sample(double x, double y, double z) const;

private:
    double lattice(int x, int y, int z) const;
    uint64_t seed_;
};

// Fractal Brownian motion helper.
double fbm2(const ValueNoise2D& noise, double x, double y, int octaves,
            double persistence = 0.5, double lacunarity = 2.0);
double fbm3(const ValueNoise3D& noise, double x, double y, double z, int octaves,
            double persistence = 0.5, double lacunarity = 2.0);

// Ridged noise is useful for caves and mountain ridges.
double ridged2(const ValueNoise2D& noise, double x, double y, int octaves);

} // namespace nexus::worldgen
