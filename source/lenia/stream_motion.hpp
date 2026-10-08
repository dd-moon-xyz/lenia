#pragma once

#include "glm/vec2.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace Lenia {
    struct StreamVisualMotion {
        glm::vec2 source{}, previousCenter{}, displacement{};
        float heading = 1.57079632679f, targetHeading = 1.57079632679f;
        float fit = 1.f, elapsed = 0.f, travel = 0.f;
        bool initialized = false;

        void update(glm::vec2 center, glm::vec2 targetSource, float targetFit, float extent, float dt) {
            if (!initialized) {
                source = targetSource;
                previousCenter = center;
                fit = targetFit;
                initialized = true;
                return;
            }
            const glm::vec2 movement(std::remainder(center.x - previousCenter.x, extent),
                std::remainder(center.y - previousCenter.y, extent));
            displacement += movement;
            travel += std::hypot(movement.x, movement.y);
            previousCenter = center;
            elapsed += dt;
            if (elapsed >= 0.5f) {
                if (std::hypot(displacement.x, displacement.y) > std::max(0.1f, travel * 0.25f))
                    targetHeading = std::atan2(displacement.y, displacement.x);
                displacement = {};
                elapsed = 0.f;
                travel = 0.f;
            }
            heading += std::clamp(std::remainder(targetHeading - heading, 6.28318530718f), -0.8f * dt, 0.8f * dt);
            const float centerBlend = 1.f - std::exp(-dt / 0.12f);
            source += centerBlend * glm::vec2(std::remainder(targetSource.x - source.x, extent),
                std::remainder(targetSource.y - source.y, extent));
            source = {std::fmod(source.x + extent, extent), std::fmod(source.y + extent, extent)};
            fit += (1.f - std::exp(-dt / 0.3f)) * (targetFit - fit);
        }
    };

    inline glm::vec2 randomStreamVelocity(float speed, float heading = 0.f, float spread = 3.14159265359f) {
        static std::mt19937 generator(std::random_device{}());
        const float angle = heading + std::uniform_real_distribution<float>(-spread, spread)(generator);
        return {speed * std::cos(angle), speed * std::sin(angle)};
    }

    inline void steerStreamVelocity(glm::vec2& velocity, float target, float dt) {
        const float speed = std::hypot(velocity.x, velocity.y);
        const float heading = std::atan2(velocity.y, velocity.x);
        const float difference = std::remainder(target - heading, 6.28318530718f);
        const float angle = heading + std::clamp(difference, -0.8f * dt, 0.8f * dt);
        velocity = {speed * std::cos(angle), speed * std::sin(angle)};
    }

    inline void advanceStreamMotion(glm::vec2& position, glm::vec2& velocity, float dt, float center, float radius) {
        glm::vec2 delta = position - glm::vec2(center);
        const float distance = std::hypot(delta.x, delta.y);
        if (distance > radius) delta *= radius / distance;
        float remaining = dt;
        for (int iteration = 0; iteration < 8 && remaining > 1e-7f; ++iteration) {
            const float speedSquared = velocity.x * velocity.x + velocity.y * velocity.y;
            const float dot = delta.x * velocity.x + delta.y * velocity.y;
            const float discriminant = std::max(0.f, dot * dot + speedSquared * (radius * radius - delta.x * delta.x - delta.y * delta.y));
            const float hit = std::max(0.f, (-dot + std::sqrt(discriminant)) / speedSquared);
            const float travel = std::min(remaining, hit);
            delta += velocity * travel;
            remaining -= travel;
            if (remaining <= 1e-7f) break;
            const float length = std::hypot(delta.x, delta.y);
            if (length == 0.f) break;
            const glm::vec2 normal = delta / length;
            const float projection = velocity.x * normal.x + velocity.y * normal.y;
            velocity -= 2.f * projection * normal;
            delta *= 0.999999f;
        }
        position = glm::vec2(center) + delta;
    }
}
