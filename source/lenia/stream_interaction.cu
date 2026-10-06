#include "stream_interaction.hpp"
#include <algorithm>
#include <stdexcept>

namespace {
    struct Scene {
        Lenia::InteractionField fields[Lenia::STREAM_MAX_ORGANISMS];
        bool neighbors[Lenia::STREAM_MAX_ORGANISMS][Lenia::STREAM_MAX_ORGANISMS];
        u32 count;
    };

    void checked(cudaError_t result) {
        if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
    }

    __device__ float sample(const Lenia::InteractionField& field, int x, int y) {
        const int size = field.size;
        x = (x % size + size) % size;
        y = (y % size + size) % size;
        return field.cells[x + y * size].x;
    }

    __global__ void interact(const Scene* scene, u32 target, f32* exposure) {
        __shared__ float sums[256];
        const auto own = scene->fields[target];
        const int width = min(static_cast<int>(own.size), static_cast<int>(ceilf(own.width)) + 2);
        const int height = min(static_cast<int>(own.size), static_cast<int>(ceilf(own.height)) + 2);
        const int index = blockIdx.x * blockDim.x + threadIdx.x;
        float contact = 0;
        if (index < width * height) {
            const int px = static_cast<int>(floorf(own.sourceX)) - width / 2 + index % width;
            const int py = static_cast<int>(floorf(own.sourceY)) - height / 2 + index / width;
            const float state = sample(own, px, py);
            if (state > 0.02f) {
                const float dx = px - own.sourceX, dy = py - own.sourceY;
                const float c = cosf(own.angle), s = sinf(own.angle);
                const float x = own.x + (c * dx - s * dy) * own.fit;
                const float y = own.y + (s * dx + c * dy) * own.fit;
                float incoming = 0;
                for (u32 peer = 0; peer < scene->count; ++peer) {
                    if (!scene->neighbors[target][peer]) continue;
                    const auto other = scene->fields[peer];
                    const float ox = (x - other.x) / other.fit, oy = (y - other.y) / other.fit;
                    const float oc = cosf(other.angle), os = sinf(other.angle);
                    const float sx = oc * ox + os * oy, sy = -os * ox + oc * oy;
                    if (fabsf(sx) > other.width * 0.5f + 1 || fabsf(sy) > other.height * 0.5f + 1) continue;
                    const float fx = sx + other.sourceX, fy = sy + other.sourceY;
                    const int ix = static_cast<int>(floorf(fx)), iy = static_cast<int>(floorf(fy));
                    const float tx = fx - ix, ty = fy - iy;
                    const float density = (1 - ty) * ((1 - tx) * sample(other, ix, iy) + tx * sample(other, ix + 1, iy))
                        + ty * ((1 - tx) * sample(other, ix, iy + 1) + tx * sample(other, ix + 1, iy + 1));
                    incoming += density;
                }
                contact = state * fminf(incoming, 1.f);
            }
        }
        sums[threadIdx.x] = contact;
        __syncthreads();
        for (int offset = 128; offset > 0; offset /= 2) {
            if (threadIdx.x < offset) sums[threadIdx.x] += sums[threadIdx.x + offset];
            __syncthreads();
        }
        if (threadIdx.x == 0) atomicAdd(exposure + target, sums[0]);
    }

    __global__ void fade(Lenia::c64* cells, u32 count, float retention) {
        const u32 index = blockIdx.x * blockDim.x + threadIdx.x;
        if (index < count) cells[index] = {cells[index].x * retention, 0.f};
    }
}

Lenia::StreamInteraction::~StreamInteraction() {
    for (auto snapshot : m_snapshots) cudaFree(snapshot);
    cudaFree(m_exposure);
    cudaFree(m_scene);
}

void Lenia::StreamInteraction::reset(u32 index, u32 size) {
    checked(cudaFree(m_snapshots[index]));
    m_snapshots[index] = nullptr;
    checked(cudaMalloc(&m_snapshots[index], size * size * sizeof(c64)));
    if (!m_exposure) checked(cudaMalloc(&m_exposure, STREAM_MAX_ORGANISMS * sizeof(f32)));
    if (!m_scene) checked(cudaMalloc(&m_scene, sizeof(Scene)));
    m_health[index] = 1.f;
    m_stress[index] = 0.f;
}

void Lenia::StreamInteraction::apply(const std::vector<InteractionField>& fields, f32 dt) {
    Scene scene{};
    scene.count = fields.size();
    std::array<bool, STREAM_MAX_ORGANISMS> interacting{};
    for (u32 index = 0; index < fields.size(); ++index) {
        if (!fields[index].active) continue;
        for (u32 peer = 0; peer < fields.size(); ++peer) {
            if (peer == index || !fields[peer].active) continue;
            if (std::hypot(fields[index].x - fields[peer].x, fields[index].y - fields[peer].y)
                < fields[index].radius + fields[peer].radius) {
                    scene.neighbors[index][peer] = true;
                    interacting[index] = true;
                }
        }
        scene.fields[index] = fields[index];
        scene.fields[index].cells = m_snapshots[index];
        if (interacting[index]) checked(cudaMemcpyAsync(m_snapshots[index], fields[index].cells,
            fields[index].size * fields[index].size * sizeof(c64), cudaMemcpyDeviceToDevice));
    }
    checked(cudaMemcpy(m_scene, &scene, sizeof(scene), cudaMemcpyHostToDevice));
    checked(cudaMemset(m_exposure, 0, STREAM_MAX_ORGANISMS * sizeof(f32)));
    for (u32 index = 0; index < fields.size(); ++index) {
        if (!interacting[index]) continue;
        const auto& field = fields[index];
        const u32 width = std::min(field.size, static_cast<u32>(std::ceil(field.width)) + 2);
        const u32 height = std::min(field.size, static_cast<u32>(std::ceil(field.height)) + 2);
        interact<<<(width * height + 255) / 256, 256>>>(static_cast<const Scene*>(m_scene), index, m_exposure);
        checked(cudaGetLastError());
    }
    std::array<f32, STREAM_MAX_ORGANISMS> exposure{};
    checked(cudaMemcpy(exposure.data(), m_exposure, sizeof(exposure), cudaMemcpyDeviceToHost));
    for (u32 index = 0; index < fields.size(); ++index) {
        const float contact = std::clamp(exposure[index] / std::max(fields[index].mass, 0.001f), 0.f, 1.f);
        const float previousHealth = m_health[index];
        m_stress[index] = std::min(1.f, m_stress[index] + dt * contact * 3.f);
        m_health[index] = std::max(0.f, m_health[index] - dt * (0.04f * m_stress[index] + 0.15f * contact));
        if (fields[index].cells && m_health[index] < previousHealth) {
            const u32 count = fields[index].size * fields[index].size;
            fade<<<(count + 255) / 256, 256>>>(fields[index].cells, count, m_health[index] / previousHealth);
            checked(cudaGetLastError());
        }
    }
}
