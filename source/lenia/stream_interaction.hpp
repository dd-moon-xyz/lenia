#pragma once

#include "core.hpp"
#include <array>

namespace Lenia {
    constexpr u32 STREAM_MAX_ORGANISMS = 100;

    struct InteractionField {
        c64* cells = nullptr;
        u32 size = 0;
        f32 sourceX = 0, sourceY = 0, x = 0, y = 0;
        f32 angle = 0, fit = 1, width = 0, height = 0, radius = 0, mass = 0;
        bool active = false;
    };

    class StreamInteraction {
    public:
        ~StreamInteraction();
        void reset(u32 index, u32 size);
        void apply(const std::vector<InteractionField>& fields, f32 dt);
        f32 health(u32 index) const { return m_health[index]; }
    private:
        std::array<c64*, STREAM_MAX_ORGANISMS> m_snapshots{};
        std::array<f32, STREAM_MAX_ORGANISMS> m_health{}, m_stress{};
        f32* m_exposure = nullptr;
        void* m_scene = nullptr;
    };
}
