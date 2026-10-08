#include "stream_motion.hpp"
#include "stream_pattern.hpp"
#include <iostream>

int main() {
    // Internal pulsing must not be interpreted as a changing forward direction.
    Lenia::StreamVisualMotion visual;
    visual.update({128.f, 128.f}, {128.f, 128.f}, 1.f, 256.f, 1.f / 60.f);
    float minimumSource = 256.f, maximumSource = 0.f;
    float minimumFit = 2.f, maximumFit = 0.f;
    for (int step = 0; step < 600; ++step) {
        const float offset = step % 2 == 0 ? 0.25f : -0.25f;
        visual.update({128.f + offset, 128.f}, {128.f + offset, 128.f}, 1.f + offset, 256.f, 1.f / 60.f);
        if (std::abs(visual.heading - 1.57079632679f) > 0.001f) return 10;
        if (step > 60) {
            minimumSource = std::min(minimumSource, visual.source.x);
            maximumSource = std::max(maximumSource, visual.source.x);
            minimumFit = std::min(minimumFit, visual.fit);
            maximumFit = std::max(maximumFit, visual.fit);
        }
    }
    if (maximumSource - minimumSource > 0.04f || maximumFit - minimumFit > 0.02f) return 11;
    // True movement across the toroidal seam is continuous, with bounded rotation.
    visual = {};
    visual.update({255.f, 128.f}, {255.f, 128.f}, 1.f, 256.f, 1.f / 60.f);
    for (int step = 1; step < 600; ++step) {
        const float center = std::fmod(255.f + step * 0.1f, 256.f);
        const float previousHeading = visual.heading, previousSource = visual.source.x;
        visual.update({center, 128.f}, {center, 128.f}, 1.f, 256.f, 1.f / 60.f);
        if (std::abs(visual.heading - previousHeading) > 0.8f / 60.f + 0.00001f ||
            std::abs(std::remainder(visual.source.x - previousSource, 256.f)) > 0.101f) return 12;
    }
    if (std::abs(visual.heading) > 0.001f || std::abs(std::remainder(visual.source.x - 58.9f, 256.f)) > 0.8f) return 13;
    for (int step = 1; step <= 360; ++step) {
        const float center = 58.9f - step * 0.1f;
        const float previousHeading = visual.heading;
        visual.update({center, 128.f}, {center, 128.f}, 1.f, 256.f, 1.f / 60.f);
        if (std::abs(visual.heading - previousHeading) > 0.8f / 60.f + 0.00001f) return 14;
    }
    if (std::abs(std::remainder(visual.heading - 3.14159265359f, 6.28318530718f)) > 0.001f) return 15;

    // Random boundary entries retain speed, point inward, and reach the visible circle.
    float minimumOffset = 1.f, maximumOffset = -1.f;
    for (int sample = 0; sample < 256; ++sample) {
        const float angle = sample * 6.28318530718f / 256;
        const glm::vec2 normal{std::cos(angle), std::sin(angle)};
        const auto velocity = Lenia::randomStreamVelocity(25.f, angle + 3.14159265359f, 0.78539816340f);
        const float inward = -(normal.x * velocity.x + normal.y * velocity.y);
        const float tangent = normal.x * velocity.y - normal.y * velocity.x;
        if (std::abs(std::hypot(velocity.x, velocity.y) - 25.f) > 0.001f || inward < 17.f ||
            (504.f + 48.f) * std::abs(tangent) / 25.f >= 504.f * 0.9f) return 8;
        minimumOffset = std::min(minimumOffset, tangent / 25.f);
        maximumOffset = std::max(maximumOffset, tangent / 25.f);
    }
    if (minimumOffset >= -0.25f || maximumOffset <= 0.25f) return 9;

    std::array<unsigned int, 16384> rings{};
    for (int y = 4; y < 124; y += 8) {
        for (int x = 4; x < 124; x += 8) {
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const int distance = dx * dx + dy * dy;
                    if (distance >= 2 && distance <= 5) rings[(y + dy) * 128 + x + dx] = 1;
                }
            }
        }
    }
    if (!Lenia::scatteredRingPattern(rings)) return 6;
    std::array<unsigned int, 16384> body{};
    for (int index = 0; index < 16384; ++index) body[index] = 1;
    if (Lenia::scatteredRingPattern(body) || Lenia::scatteredRingPattern({})) return 7;
    glm::vec2 position{85.f, 50.f}, velocity{100.f, 0.f};
    Lenia::advanceStreamMotion(position, velocity, 0.2f, 50.f, 45.f);
    if (std::abs(position.x - 85.f) > 0.001f || velocity.x >= 0.f) return 1;
    for (int step = 0; step < 10000; ++step) {
        Lenia::advanceStreamMotion(position, velocity, 1.f / 60.f, 50.f, 45.f);
        if (std::hypot(position.x - 50.f, position.y - 50.f) > 45.001f ||
            std::abs(std::hypot(velocity.x, velocity.y) - 100.f) > 0.01f) return 2;
    }
    // A birth begins outside the visible circle and travels inward.
    constexpr float center = 512.f, limit = 504.f, spriteRadius = 80.f;
    position = {center + limit + spriteRadius, center};
    velocity = {-215.f, 0.f};
    Lenia::advanceStreamMotion(position, velocity, 1.f / 60.f, center, limit + spriteRadius);
    if (position.x - center - spriteRadius <= limit * 0.9f || velocity.x >= 0.f) return 3;
    // Reflection starts only once the entire organism is beyond the visible circle.
    position = {center + limit + spriteRadius - 1.f, center};
    velocity = {215.f, 0.f};
    Lenia::advanceStreamMotion(position, velocity, 1.f / 60.f, center, limit + spriteRadius);
    if (velocity.x >= 0.f || position.x - center - spriteRadius <= limit * 0.9f) return 4;
    // Angled trajectories preserve velocity and stay inside the turning boundary.
    position = {center, center};
    velocity = {123.f, -170.f};
    const float speed = std::hypot(velocity.x, velocity.y);
    for (int step = 0; step < 10000; ++step) {
        Lenia::advanceStreamMotion(position, velocity, 1.f / 60.f, center, limit + spriteRadius);
        if (std::hypot(position.x - center, position.y - center) > limit + spriteRadius + 0.01f ||
            std::abs(std::hypot(velocity.x, velocity.y) - speed) > 0.1f) return 5;
    }
    velocity = {25.f, 0.f};
    Lenia::steerStreamVelocity(velocity, 3.14159265359f, 0.1f);
    if (std::abs(std::atan2(velocity.y, velocity.x)) > 0.08001f ||
        std::abs(std::hypot(velocity.x, velocity.y) - 25.f) > 0.001f) return 8;
    for (int step = 0; step < 300; ++step) Lenia::steerStreamVelocity(velocity, 1.f, 1.f / 60.f);
    if (std::abs(std::atan2(velocity.y, velocity.x) - 1.f) > 0.001f) return 9;
    std::cout << "Native reflection and hidden boundary motion passed\n";
    return 0;
}
