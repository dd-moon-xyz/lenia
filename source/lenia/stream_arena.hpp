#pragma once

#include "engine.hpp"
#include "stream_interaction.hpp"
#include "stream_motion.hpp"

namespace Lenia {
    struct StreamOrganism {
        u32 size, type, scale;
        f32 x, y, velocity;
    };

    class StreamArena {
    public:
        StreamArena(u32 size, u32 fps, f32 dt, f32 spaceSpeedMultiplier, const std::vector<StreamOrganism>& organisms);
        ~StreamArena();
        std::vector<u8> frame();
        std::vector<f32> masses() const;
        void redirect(bool outward, bool boosted);
        void replace(u32 index, const StreamOrganism& organism, f32 angle);
    private:
        struct Slot {
            std::unique_ptr<Animal> animal;
            std::unique_ptr<Simulation> simulation;
            glm::vec2 position, velocity;
            f32 heading = 1.57079632679f;
            StreamVisualMotion visual;
            f32 radius = 1.f;
            f32 displayScale = 1.f;
            f32 speedMultiplier = 1.f;
            f32 spawnAngle = 0.f;
            f32 steeringOffset = 0.f, steeringTime = 0.f;
            bool spawning = false;
            u32 frames = 0;
            u32 palette = 0;
            InteractionField field;
        };
        Engine m_engine;
        StreamInteraction m_interaction;
        std::vector<Slot> m_slots;
        std::vector<std::unique_ptr<Buffer<ColorPalette>>> m_palettes;
        u32 m_size, m_fps;
        f32 m_dt, m_spaceSpeedMultiplier;
        bool m_outward = false;
        bool m_boosted = false;
        GLuint m_program, m_boundsProgram, m_boundsBuffer;
        void initialize(Slot& slot, const StreamOrganism& organism);
        void normalizeVelocities();
        void advance(Slot& slot);
    };

    int streamArena();
}
