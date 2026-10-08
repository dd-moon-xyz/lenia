#include "stream_hexapod.hpp"
#include <cuda_runtime.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
    constexpr int RES = 128;
    constexpr float PI = 3.14159265359f, LENGTH = 97.625f, REACH = LENGTH * 3.f - 3.f;
    struct Foot { float2 position, start, target; float progress, duration, threshold, ready, reachBias, lateralBias; unsigned int random; };
    struct Walker {
        float2 position, previous, roots[6], points[6][4];
        Foot feet[6];
        float heading, zoom, time, trajectory, health, stress;
        int palette;
    };
    void checked(cudaError_t result) {
        if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
    }
    __host__ __device__ float2 add(float2 a, float2 b) { return make_float2(a.x + b.x, a.y + b.y); }
    __host__ __device__ float2 sub(float2 a, float2 b) { return make_float2(a.x - b.x, a.y - b.y); }
    __host__ __device__ float2 mul(float2 a, float b) { return make_float2(a.x * b, a.y * b); }
    __device__ float norm(float2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
    __host__ __device__ float2 rotate(float2 a, float angle) {
        return make_float2(cosf(angle) * a.x - sinf(angle) * a.y, sinf(angle) * a.x + cosf(angle) * a.y);
    }
    __device__ float clip(float v, float lo, float hi) { return fminf(hi, fmaxf(lo, v)); }
    __device__ float2 root(const Walker& w, int arm) {
        int level = arm % 3;
        float side = arm < 3 ? -1.f : 1.f;
        return add(w.position, rotate(make_float2(side * 27.712813f * (level == 0 ? 1.f : .7f), -16.f + level * 16.f), w.heading));
    }
    __device__ float2 rest(int arm) {
        const float angles[] = {.8f, 1.1f, 1.45f};
        float a = angles[arm % 3];
        return make_float2((arm < 3 ? -1.f : 1.f) * 127.5f * cosf(a), 127.5f * sinf(a));
    }
    __device__ float randomUnit(Foot& foot) {
        unsigned int value = foot.random;
        value ^= value << 13; value ^= value >> 17; value ^= value << 5;
        foot.random = value;
        return (value & 0xffffffu) / 16777216.f;
    }
    __device__ void randomStroke(Foot& foot, float time) {
        foot.duration = .18f + .32f * randomUnit(foot);
        foot.threshold = 150.f + 100.f * randomUnit(foot);
        foot.ready = time + foot.duration + .05f + .35f * randomUnit(foot);
        foot.reachBias = 24.f * randomUnit(foot);
        foot.lateralBias = (randomUnit(foot) - .5f) * 32.f;
    }
    __global__ void advance(Walker* walkers, int count, int size, float dt, bool preview) {
        int index = blockIdx.x * blockDim.x + threadIdx.x;
        if (index >= count) return;
        Walker& w = walkers[index];
        w.previous = w.position;
        int steps = max(1, static_cast<int>(ceilf(dt * 240.f)));
        float interval = dt / steps;
        for (int step = 0; step < steps; ++step) {
            w.time += interval;
            float2 center = make_float2(size * .5f / w.zoom, size * .5f / w.zoom);
            float2 radial = sub(w.position, center);
            float desired = w.trajectory;
            float boundary = size * .5f - 8.f + 700.f * w.zoom * .707107f;
            float2 travel = make_float2(cosf(desired), sinf(desired));
            float distance = norm(radial);
            float projection = travel.x * radial.x + travel.y * radial.y;
            if (!preview && distance * w.zoom >= boundary && projection > 0.f) {
                float2 reflected = sub(travel, mul(radial, 2.f * projection / (distance * distance)));
                w.trajectory = atan2f(reflected.y, reflected.x);
                desired = w.trajectory;
            }
            if (preview) desired = PI * .5f - .45f + .35f * sinf(w.time * .4f);
            float turn = remainderf(desired - PI * .5f - w.heading, 2.f * PI);
            float2 direction = rotate(make_float2(0.f, 1.f), w.heading);
            int contacts = 0, moving = 0;
            for (int arm = 0; arm < 6; ++arm) { contacts += w.feet[arm].progress >= 1.f; moving += w.feet[arm].progress < 1.f; }
            float alignment = fmaxf(0.f, cosf(turn));
            float speed = 48.f * alignment * contacts / (2.f + contacts);
            float2 position = add(w.position, mul(direction, speed * interval));
            float heading = w.heading + clip(turn, -interval, interval);
            bool supported = true;
            for (int arm = 0; arm < 6; ++arm) {
                if (w.feet[arm].progress < 1.f) continue;
                float2 r = add(position, rotate(rotate(sub(root(w, arm), w.position), -w.heading), heading));
                supported = supported && norm(sub(w.feet[arm].position, r)) <= REACH;
            }
            if (supported) { w.position = position; w.heading = heading; }
            direction = rotate(make_float2(0.f, 1.f), w.heading);
            float reach = REACH - 8.f - fminf(80.f, fabsf(turn) * 80.f);
            int candidate = -1;
            float error = -1.f;
            float2 target = make_float2(0.f, 0.f);
            for (int arm = 0; arm < 6; ++arm) {
                Foot& foot = w.feet[arm];
                float2 r = root(w, arm);
                if (foot.progress < 1.f) {
                    foot.progress = fminf(1.f, foot.progress + interval / foot.duration);
                    float t = foot.progress, blend = t * t * (3.f - 2.f * t);
                    float2 travel = sub(foot.target, foot.start);
                    float2 point = add(add(foot.start, mul(travel, blend)), mul(make_float2(-travel.y, travel.x), .08f * sinf(PI * t)));
                    float2 offset = sub(point, r);
                    foot.position = add(r, mul(offset, fminf(1.f, REACH / fmaxf(norm(offset), 1e-9f))));
                }
                if (foot.progress >= 1.f) {
                    float lateral = rest(arm).x * .25f + foot.lateralBias;
                    float armReach = reach - foot.reachBias;
                    float2 ideal = add(add(r, rotate(make_float2(lateral, 0.f), w.heading)), mul(direction, sqrtf(armReach * armReach - lateral * lateral)));
                    float distance = w.time >= foot.ready ? norm(sub(foot.position, ideal)) / foot.threshold : -1.f;
                    if (distance > error) { candidate = arm; error = distance; target = ideal; }
                }
            }
            if (candidate >= 0 && moving < 2 && error > 1.f) {
                Foot& foot = w.feet[candidate];
                foot.start = foot.position; foot.target = target; foot.progress = 0.f; randomStroke(foot, w.time);
            }
        }
        for (int arm = 0; arm < 6; ++arm) {
            float2 r = root(w, arm), offset = sub(w.feet[arm].position, r);
            float distance = norm(offset), lo = 0.f, hi = 2.f * PI / 3.f;
            for (int iteration = 0; iteration < 24; ++iteration) {
                float bend = (lo + hi) * .5f;
                float reach = LENGTH * sinf(3.f * bend * .5f) / sinf(bend * .5f);
                if (reach > distance) lo = bend; else hi = bend;
            }
            float bend = (lo + hi) * .5f, angle = atan2f(offset.y, offset.x), side = arm < 3 ? -1.f : 1.f;
            w.roots[arm] = rotate(sub(r, w.position), -w.heading);
            w.points[arm][0] = sub(r, w.position);
            for (int part = 0; part < 3; ++part) {
                float a = angle + side * bend * (part - 1);
                w.points[arm][part + 1] = add(w.points[arm][part], make_float2(LENGTH * cosf(a), LENGTH * sinf(a)));
            }
        }
    }
    __global__ void redirect(Walker* walkers, int count, int size, bool outward) {
        int index = blockIdx.x * blockDim.x + threadIdx.x;
        if (index >= count) return;
        Walker& w = walkers[index];
        float2 radial = sub(mul(w.position,w.zoom),make_float2(size*.5f,size*.5f));
        w.trajectory = atan2f(radial.y,radial.x) + (outward ? 0.f : PI);
    }
    __device__ float segmentDistance(float2 p, float2 a, float2 b) {
        float2 v = sub(b, a), d = sub(p, a);
        float t = clip((d.x * v.x + d.y * v.y) / fmaxf(1e-6f, v.x * v.x + v.y * v.y), 0.f, 1.f);
        return norm(sub(d, mul(v, t)));
    }
    __device__ float hexDistance(float2 p, float radius) {
        float distance = -1e6f;
        for (int edge = 0; edge < 6; ++edge) {
            float angle = edge * PI / 3.f;
            distance = fmaxf(distance, p.x * cosf(angle) + p.y * sinf(angle) - radius * .8660254f);
        }
        return distance;
    }
    __device__ float tissue(float distance, float softness) { return 1.f / (1.f + expf(distance / softness)); }
    __global__ void body(const Walker* walkers, float* density, int count) {
        int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y, index = blockIdx.z;
        if (x >= RES || y >= RES || index >= count) return;
        const Walker& w = walkers[index];
        float2 p = make_float2((x + .5f - RES * .5f) * 700.f / RES, (y + .5f - RES * .5f) * 700.f / RES);
        float state = 0.f;
        for (int arm = 0; arm < 6; ++arm) {
            for (int part = 0; part < 3; ++part) {
                float2 a = w.points[arm][part], b = w.points[arm][part + 1];
                float2 v = sub(b, a), d = sub(p, a);
                float t = clip((d.x * v.x + d.y * v.y) / (LENGTH * LENGTH), 0.f, 1.f);
                float wave = sinf(w.time * 4.f - (part + t) * 2.6f + arm * .8f);
                float distance = segmentDistance(p, a, b);
                float outer = tissue(distance - 8.5f, 2.f) * (.49f + .02f * wave);
                float inner = tissue(distance - 3.f, 2.f) * (.137f + .098f * wave);
                state = fmaxf(state, outer + inner);
            }
        }
        float2 local = rotate(p, -w.heading);
        float head = tissue(hexDistance(add(local, make_float2(0.f, 32.f)), 32.f), 2.f);
        float nucleus = tissue(hexDistance(add(local, make_float2(-1.5f * sinf(w.time * 2.2f), 32.f)), 56.f / 3.f), 2.f);
        state = fmaxf(state, head * .51f + nucleus * (.463f + .039f * sinf(w.time * 2.2f)));
        float left = local.y <= 16.f ? 27.712813f + (19.399f - 27.712813f) * clip((local.y + 16.f) / 32.f, 0.f, 1.f)
                                     : 19.399f * clip((28.f - local.y) / 12.f, 0.f, 1.f);
        float torsoDistance = fmaxf(fabsf(local.x) - left, fmaxf(-16.f - local.y, local.y - 28.f));
        float torso = tissue(torsoDistance, 2.f);
        state = fmaxf(state, torso * (.51f + (.137f + .098f * sinf(w.time * 2.2f - local.y * .065f))));
        density[index * RES * RES + y * RES + x] = clip(state, 0.f, 1.f);
    }
    __device__ float sample(const float* field, float x, float y) {
        int ix = static_cast<int>(floorf(x)), iy = static_cast<int>(floorf(y));
        if (ix < 0 || iy < 0 || ix >= RES - 1 || iy >= RES - 1) return 0.f;
        float fx = x - ix, fy = y - iy;
        return (field[iy * RES + ix] * (1.f - fx) + field[iy * RES + ix + 1] * fx) * (1.f - fy)
             + (field[(iy + 1) * RES + ix] * (1.f - fx) + field[(iy + 1) * RES + ix + 1] * fx) * fy;
    }
    __device__ float hash(float2 p) {
        p.x = p.x * 233.34f - floorf(p.x * 233.34f); p.y = p.y * 851.74f - floorf(p.y * 851.74f);
        float offset = p.x * (p.x + 23.45f) + p.y * (p.y + 23.45f);
        float v = (p.x + offset) * (p.y + offset); return v - floorf(v);
    }
    __device__ float noise(float2 p) {
        float2 base = make_float2(floorf(p.x), floorf(p.y));
        float x = p.x - base.x, y = p.y - base.y;
        x = x * x * (3.f - 2.f * x); y = y * y * (3.f - 2.f * y);
        return (hash(base) * (1.f - x) + hash(add(base, make_float2(1.f, 0.f))) * x) * (1.f - y)
             + (hash(add(base, make_float2(0.f, 1.f))) * (1.f - x) + hash(add(base, make_float2(1.f, 1.f))) * x) * y;
    }
    __global__ void fluid(const Walker* walkers, const float* density, const float* before, float* after, int count, float dt, float displacement) {
        int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y, index = blockIdx.z;
        if (x >= RES || y >= RES || index >= count) return;
        const Walker& w = walkers[index];
        const float* src = before + index * RES * RES;
        const float* state = density + index * RES * RES;
        float sx = (sample(state, x + 1.f, y) - sample(state, x - 1.f, y)) * .5f;
        float sy = (sample(state, x, y + 1.f) - sample(state, x, y - 1.f)) * .5f;
        float gx = (sample(src, x + 1.f, y) - sample(src, x - 1.f, y)) * .5f;
        float gy = (sample(src, x, y + 1.f) - sample(src, x, y - 1.f)) * .5f;
        float2 p = make_float2(x * .075f + w.time * .3f, y * .075f - w.time * .22f);
        float cx = noise(add(p, make_float2(0.f, .5f))) - noise(add(p, make_float2(0.f, -.5f)));
        float cy = noise(add(p, make_float2(-.5f, 0.f))) - noise(add(p, make_float2(.5f, 0.f)));
        float2 velocity = make_float2(-sy * 14.f - gy * 20.f + cx * 9.f, sx * 14.f + gx * 20.f + cy * 9.f);
        float2 shift = mul(sub(w.position, w.previous), RES / 700.f * displacement);
        float px = x + shift.x - velocity.x * dt * 3.f, py = y + shift.y - velocity.y * dt * 3.f;
        float advected = sample(src, px, py);
        float average = .25f * (sample(src, px - 1, py) + sample(src, px + 1, py) + sample(src, px, py - 1) + sample(src, px, py + 1));
        float injected = fmaxf(0.f, state[y * RES + x] - .235f) * dt * 1.8f * w.health;
        after[index * RES * RES + y * RES + x] = clip((advected * .95f + average * .05f) * expf(-dt * .65f) + injected, 0.f, .43f);
    }
    __device__ float3 color(float value, int palette) {
        if (palette == 0) {
            const float levels[] = {0.f,.1f,.23f,.36f,.48f,.58f,.68f,.78f,.87f,.94f,1.f};
            const float3 reference[] = {{0,0,0},{8.f/255,5.f/255,19.f/255},{26.f/255,18.f/255,67.f/255},
                {42.f/255,28.f/255,134.f/255},{38.f/255,51.f/255,217.f/255},{32.f/255,141.f/255,249.f/255},
                {55.f/255,236.f/255,225.f/255},{150.f/255,1,204.f/255},{1,247.f/255,186.f/255},
                {1,196.f/255,171.f/255},{1,159.f/255,202.f/255}};
            int i = 0;
            while (i < 9 && value > levels[i + 1]) ++i;
            float f = clip((value - levels[i]) / (levels[i + 1] - levels[i]),0.f,1.f);
            float3 a = reference[i], b = reference[i + 1];
            return make_float3(a.x + (b.x-a.x)*f,a.y + (b.y-a.y)*f,a.z + (b.z-a.z)*f);
        }
        const float3 stops[5][6] = {
            {{0,0,0},{.1f,.07f,.26f},{.15f,.2f,.85f},{.13f,.55f,.98f},{.59f,1,.8f},{1,.62f,.79f}},
            {{0,0,0},{.02f,.05f,.25f},{0,.25f,.65f},{0,.65f,.8f},{.3f,.9f,.85f},{.9f,1,1}},
            {{0,0,0},{.15f,.02f,.3f},{.45f,.05f,.65f},{.1f,.55f,.75f},{.2f,.95f,.55f},{.85f,1,.7f}},
            {{0,0,0},{.25f,0,.05f},{.65f,.05f,.02f},{1,.3f,0},{1,.75f,.1f},{1,.95f,.75f}},
            {{0,0,0},{.05f,.05f,.25f},{.2f,.25f,.55f},{.4f,.6f,.85f},{.7f,.9f,1},{1,1,1}}
        };
        float t = clip(value, 0.f, 1.f) * 5.f;
        int i = min(4, static_cast<int>(t)); float f = t - i;
        float3 a = stops[palette][i], b = stops[palette][i + 1];
        return make_float3(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f);
    }
    __global__ void compose(const Walker* walkers, const float* density, const float* wake, unsigned char* output, int count, int size, bool preview) {
        int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
        if (x >= size || y >= size) return;
        float3 rgb = make_float3(0,0,0);
        float2 point = make_float2(x + .5f, y + .5f), center = make_float2(size * .5f, size * .5f);
        if (preview || norm(sub(point, center)) < (size * .5f - 8.f) * .9f) {
            for (int i = 0; i < count; ++i) {
                const Walker& w = walkers[i];
                float2 local = sub(point, preview ? center : mul(w.position, w.zoom));
                float extent = 350.f * w.zoom;
                if (fabsf(local.x) > extent || fabsf(local.y) > extent) continue;
                float px = local.x / (700.f * w.zoom) * RES + RES * .5f - .5f;
                float py = local.y / (700.f * w.zoom) * RES + RES * .5f - .5f;
                float value = fmaxf(sample(density + i * RES * RES, px, py), sample(wake + i * RES * RES, px, py));
                if (value < .015f) continue;
                float3 c = color(value, w.palette);
                rgb.x += c.x * w.health; rgb.y += c.y * w.health; rgb.z += c.z * w.health;
            }
        }
        int index = ((size - 1 - y) * size + x) * 3;
        output[index] = static_cast<unsigned char>(255.f * fminf(1.f, rgb.x));
        output[index + 1] = static_cast<unsigned char>(255.f * fminf(1.f, rgb.y));
        output[index + 2] = static_cast<unsigned char>(255.f * fminf(1.f, rgb.z));
    }
    __global__ void contact(const Walker* walkers, const float* density, float2* totals, int count) {
        int index = blockIdx.x;
        if (index >= count) return;
        __shared__ float overlaps[256], masses[256];
        const Walker& own = walkers[index];
        float overlap = 0.f, mass = 0.f;
        for (int pixel = threadIdx.x; pixel < RES * RES; pixel += blockDim.x) {
            float value = density[index * RES * RES + pixel];
            if (value <= .02f || own.health <= 0.f) continue;
            mass += value;
            float2 offset = make_float2((pixel % RES + .5f - RES * .5f) * 700.f / RES,
                                       (pixel / RES + .5f - RES * .5f) * 700.f / RES);
            float2 world = mul(add(own.position, offset), own.zoom);
            float incoming = 0.f;
            for (int peer = 0; peer < count; ++peer) {
                if (peer == index || walkers[peer].health <= 0.f) continue;
                const Walker& other = walkers[peer];
                float2 delta = sub(mul(world, 1.f / other.zoom), other.position);
                if (fabsf(delta.x) >= 350.f || fabsf(delta.y) >= 350.f) continue;
                incoming += sample(density + peer * RES * RES, delta.x * RES / 700.f + RES * .5f - .5f,
                                   delta.y * RES / 700.f + RES * .5f - .5f) * other.health;
            }
            overlap += value * fminf(1.f, incoming);
        }
        overlaps[threadIdx.x] = overlap; masses[threadIdx.x] = mass;
        __syncthreads();
        for (int offset = 128; offset > 0; offset /= 2) {
            if (threadIdx.x < offset) { overlaps[threadIdx.x] += overlaps[threadIdx.x + offset]; masses[threadIdx.x] += masses[threadIdx.x + offset]; }
            __syncthreads();
        }
        if (threadIdx.x == 0) totals[index] = make_float2(overlaps[0], masses[0]);
    }
    __global__ void damage(Walker* walkers, const float2* totals, float* masses, int count, float dt) {
        int index = blockIdx.x * blockDim.x + threadIdx.x;
        if (index >= count) return;
        Walker& w = walkers[index];
        float exposure = clip(totals[index].x / fmaxf(totals[index].y,.001f),0.f,1.f);
        w.stress = fminf(1.f,w.stress + dt * exposure * 3.f);
        w.health = fmaxf(0.f,w.health - dt * (.08f * w.stress + .30f * exposure));
        masses[index] = totals[index].y * w.health;
    }
    Walker initialize(int size, int scale, float x, float y, bool preview, bool spawning,
                      float spawnAngle, std::mt19937& generator) {
        Walker w{};
        w.zoom = std::min(size / 3.f, std::uniform_real_distribution<float>(96.f,224.f)(generator) * scale) / 700.f;
        w.position = make_float2(x / w.zoom, y / w.zoom);
        w.previous = w.position;
        w.heading = std::uniform_real_distribution<float>(-PI,PI)(generator);
        w.palette = std::uniform_int_distribution<int>(0,4)(generator);
        if (preview) { w.zoom = size / 700.f; w.position = make_float2(x / w.zoom, y / w.zoom); w.previous = w.position; w.heading = -.45f; w.palette = 0; }
        w.health = 1.f;
        if (spawning) {
            float radius = (size * .5f - 8.f) * .9f + 700.f * w.zoom * .707107f + 8.f;
            w.position = make_float2((size * .5f + radius * cosf(spawnAngle)) / w.zoom,
                                    (size * .5f + radius * sinf(spawnAngle)) / w.zoom);
            w.previous = w.position;
            w.heading = spawnAngle + PI * .5f + std::uniform_real_distribution<float>(-.785f,.785f)(generator);
        }
        w.trajectory = w.heading + PI * .5f;
        for (int arm = 0; arm < 6; ++arm) {
            int level = arm % 3; float side = arm < 3 ? -1.f : 1.f;
            const float angles[] = {.8f,1.1f,1.45f}; float angle = angles[level];
            float2 point = make_float2(side * (27.712813f * (level == 0 ? 1.f : .7f) + 127.5f * cosf(angle)), -16.f + level * 16.f + 127.5f * sinf(angle) + (1 - level) * 8.f);
            w.feet[arm].position = add(w.position, rotate(point, w.heading));
            w.feet[arm].progress = 1.f;
            w.feet[arm].random = generator() | 1u;
            w.feet[arm].duration = std::uniform_real_distribution<float>(.18f,.5f)(generator);
            w.feet[arm].threshold = std::uniform_real_distribution<float>(150.f,250.f)(generator);
            w.feet[arm].ready = std::uniform_real_distribution<float>(0.f,.8f)(generator);
            w.feet[arm].reachBias = std::uniform_real_distribution<float>(0.f,24.f)(generator);
            w.feet[arm].lateralBias = std::uniform_real_distribution<float>(-16.f,16.f)(generator);
        }
        return w;
    }
    class Arena {
    public:
        Walker* walkers = nullptr;
        float *density = nullptr, *wake = nullptr, *next = nullptr;
        unsigned char* output = nullptr;
        float2* contacts = nullptr;
        float* masses = nullptr;
        Arena(int count, int size) {
            checked(cudaMalloc(&walkers, count * sizeof(Walker)));
            checked(cudaMalloc(&contacts, count * sizeof(float2)));
            checked(cudaMalloc(&masses, count * sizeof(float)));
            checked(cudaMalloc(&density, count * RES * RES * sizeof(float)));
            checked(cudaMalloc(&wake, count * RES * RES * sizeof(float)));
            checked(cudaMalloc(&next, count * RES * RES * sizeof(float)));
            checked(cudaMalloc(&output, size * size * 3));
            checked(cudaMemset(wake, 0, count * RES * RES * sizeof(float)));
        }
        ~Arena() { cudaFree(walkers); cudaFree(density); cudaFree(wake); cudaFree(next); cudaFree(output); cudaFree(contacts); cudaFree(masses); }
    };
}

int Lenia::streamHexapod(bool preview) {
    int size, fps, count;
    float dt, boost;
    if (!(std::cin >> size >> fps >> dt >> boost >> count) || size < 256 || size > 1024 || fps < 1 || fps > 60 || count < 1 || count > 100 || !std::isfinite(dt) || dt <= 0 || dt > .2f || !std::isfinite(boost) || boost < 1) return 1;
    std::vector<Walker> walkers(count);
    std::mt19937 generator(std::random_device{}());
    for (auto& w : walkers) {
        int fieldSize, type, scale; float x, y, velocity;
        if (!(std::cin >> fieldSize >> type >> scale >> x >> y >> velocity) || type != 528 || scale < 1 || scale > 10 || !std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= size || y >= size) return 1;
        w = initialize(size,scale,x,y,preview,false,0.f,generator);
    }
    try {
        Arena arena(count,size);
        checked(cudaMemcpy(arena.walkers,walkers.data(),count*sizeof(Walker),cudaMemcpyHostToDevice));
        std::vector<unsigned char> pixels(size*size*3);
        std::vector<float> masses(count,100.f);
        bool boosted = false;
        std::cout << "READY\n" << std::flush;
        char command;
        while (std::cin >> command) {
            if (command == 'o' || command == 'i' || command == 'b') {
                boosted = command != 'i';
                redirect<<<(count+63)/64,64>>>(arena.walkers,count,size,command == 'o');
                checked(cudaGetLastError());
                std::cout << "READY\n" << std::flush; continue;
            }
            if (command == 'r') {
                int index, fieldSize, type, scale; float x,y,velocity,angle;
                if (!(std::cin >> index >> fieldSize >> type >> scale >> x >> y >> velocity >> angle) ||
                    index < 0 || index >= count || type != 528 || scale < 1 || scale > 10 || !std::isfinite(angle)) return 1;
                Walker replacement = initialize(size,scale,x,y,preview,true,angle,generator);
                checked(cudaMemcpy(arena.walkers + index,&replacement,sizeof(Walker),cudaMemcpyHostToDevice));
                checked(cudaMemset(arena.wake + index * RES * RES,0,RES * RES * sizeof(float)));
                checked(cudaMemset(arena.next + index * RES * RES,0,RES * RES * sizeof(float)));
                std::cout << "READY\n" << std::flush; continue;
            }
            if (command != 'n') return 1;
            float step = dt * (boosted ? boost : 1.f);
            advance<<<(count+63)/64,64>>>(arena.walkers,count,size,step,preview);
            dim3 block(16,16), grid((RES+15)/16,(RES+15)/16,count);
            body<<<grid,block>>>(arena.walkers,arena.density,count);
            contact<<<count,256>>>(arena.walkers,arena.density,arena.contacts,count);
            damage<<<(count+63)/64,64>>>(arena.walkers,arena.contacts,arena.masses,count,1.f/fps);
            int substeps = std::max(1,static_cast<int>(std::ceil(step*30.f)));
            for (int i=0;i<substeps;++i) {
                fluid<<<grid,block>>>(arena.walkers,arena.density,arena.wake,arena.next,count,step/substeps,1.f/substeps);
                std::swap(arena.wake,arena.next);
            }
            compose<<<dim3((size+15)/16,(size+15)/16),block>>>(arena.walkers,arena.density,arena.wake,arena.output,count,size,preview);
            checked(cudaGetLastError());
            checked(cudaMemcpy(pixels.data(),arena.output,pixels.size(),cudaMemcpyDeviceToHost));
            checked(cudaMemcpy(masses.data(),arena.masses,count*sizeof(float),cudaMemcpyDeviceToHost));
            std::cout.write(reinterpret_cast<const char*>(masses.data()),masses.size()*sizeof(float));
            std::cout.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
            std::cout.flush();
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
