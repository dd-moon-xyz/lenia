#include "stream_arena.hpp"
#include "stream_motion.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
    GLuint program(const char* vertex, const char* fragment, GLenum type) {
        GLuint result = glCreateProgram();
        const auto code = Lenia::loadShaderFile(fragment);
        GLuint shader = Lenia::createShader(type, code.c_str());
        glAttachShader(result, shader);
        GLuint vertexShader = 0;
        if (vertex) {
            const auto vertexCode = Lenia::loadShaderFile(vertex);
            vertexShader = Lenia::createShader(GL_VERTEX_SHADER, vertexCode.c_str());
            glAttachShader(result, vertexShader);
        }
        glLinkProgram(result);
        Lenia::checkProgramLinking(result);
        glDeleteShader(shader);
        if (vertexShader) glDeleteShader(vertexShader);
        return result;
    }

    bool readOrganism(Lenia::StreamOrganism& organism) {
        return static_cast<bool>(std::cin >> organism.size >> organism.type >> organism.scale
            >> organism.x >> organism.y >> organism.velocity);
    }
}

Lenia::StreamArena::StreamArena(u32 size, u32 fps, f32 dt, f32 spaceSpeedMultiplier, const std::vector<StreamOrganism>& organisms) :
    m_engine(size, size, 512, 512, 1, dt, true),
    m_size(size), m_fps(fps), m_dt(dt), m_spaceSpeedMultiplier(spaceSpeedMultiplier) {
    m_engine.m_simulation.reset();
    m_engine.m_currentAnimal.reset();
    m_program = program("../shaders/lenia.vert", "../shaders/arena.frag", GL_FRAGMENT_SHADER);
    m_boundsProgram = program(nullptr, "../shaders/arena_bounds.comp", GL_COMPUTE_SHADER);
    glGenBuffers(1, &m_boundsBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_boundsBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 4 * sizeof(i32), nullptr, GL_DYNAMIC_COPY);
    for (const auto& palette : {Magma, Ocean, Aurora, Ember, Ice}) {
        m_palettes.push_back(std::make_unique<Buffer<ColorPalette>>(BufferBinding::COLOR,
            std::vector<ColorPalette>{palette}));
    }
    m_slots.resize(organisms.size());
    for (std::size_t index = 0; index < organisms.size(); ++index) {
        initialize(m_slots[index], organisms[index]);
        auto& slot = m_slots[index];
        const f32 center = m_size * 0.5f;
        slot.spawning = std::hypot(slot.position.x - center, slot.position.y - center) >= center - 9.f;
        slot.spawnAngle = std::atan2(slot.position.y - center, slot.position.x - center);
        slot.velocity = slot.spawning
            ? randomStreamVelocity(organisms[index].velocity, slot.spawnAngle + 3.14159265359f, 0.78539816340f)
            : randomStreamVelocity(organisms[index].velocity);
    }
    normalizeVelocities();
}

Lenia::StreamArena::~StreamArena() {
    glDeleteProgram(m_program);
    glDeleteProgram(m_boundsProgram);
    glDeleteBuffers(1, &m_boundsBuffer);
}

void Lenia::StreamArena::initialize(Slot& slot, const StreamOrganism& organism) {
    if (organism.type >= m_engine.m_animals.size() || organism.size < 256 || organism.size > m_size ||
        organism.scale < 1 || organism.scale > 10 || !std::isfinite(organism.velocity) ||
        organism.velocity <= 0.f || organism.velocity > 25.f ||
        !std::isfinite(organism.x) || !std::isfinite(organism.y) ||
        organism.x < 0.f || organism.x >= m_size || organism.y < 0.f || organism.y >= m_size) {
        throw std::invalid_argument("Invalid arena organism");
    }
    const auto& info = m_engine.m_animals[organism.type];
    if (2 * info.m_r * organism.scale + 1 > organism.size ||
        std::max(info.m_w, info.m_h) * organism.scale > m_size / 3) {
        throw std::invalid_argument("Organism does not fit arena");
    }
    slot.simulation.reset();
    slot.animal = std::make_unique<Animal>(info, static_cast<u8>(organism.scale));
    slot.animal->computePadded(organism.size);
    slot.simulation = std::make_unique<Simulation>(organism.size, organism.size, organism.scale, 2);
    slot.simulation->clearCells();
    slot.simulation->placeCells(slot.animal->getCells(), info.m_w, info.m_h, organism.size / 2, organism.size / 2);
    slot.simulation->loadFFT();
    slot.position = {organism.x, organism.y};
    slot.frames = 0;
    static std::mt19937 generator(std::random_device{}());
    slot.palette = std::uniform_int_distribution<u32>(0, static_cast<u32>(m_palettes.size() - 1))(generator);
    slot.displayScale = std::uniform_real_distribution<f32>(0.5f, 2.f)(generator);
    slot.speedMultiplier = std::uniform_real_distribution<f32>(0.5f, 2.f)(generator);
    slot.heading = 1.57079632679f;
    slot.visual = {};
    slot.spawning = false;
    slot.field = {};
    m_interaction.reset(static_cast<u32>(&slot - m_slots.data()), organism.size);
}

void Lenia::StreamArena::replace(u32 index, const StreamOrganism& organism, f32 angle) {
    if (index >= m_slots.size() || !std::isfinite(angle)) throw std::invalid_argument("Invalid birth slot or angle");
    auto& slot = m_slots[index];
    initialize(slot, organism);
    slot.velocity = randomStreamVelocity(organism.velocity, angle + 3.14159265359f, 0.78539816340f);
    slot.spawnAngle = angle;
    slot.spawning = true;
    normalizeVelocities();
}

void Lenia::StreamArena::normalizeVelocities() {
    f32 smallest = static_cast<f32>(m_size);
    for (const auto& slot : m_slots) {
        const auto& animal = *slot.animal;
        smallest = std::min(smallest, static_cast<f32>(std::max(animal.m_info.m_w, animal.m_info.m_h) * animal.m_scale));
    }
    for (auto& slot : m_slots) {
        const auto& animal = *slot.animal;
        const f32 size = std::max(animal.m_info.m_w, animal.m_info.m_h) * animal.m_scale;
        const f32 speed = std::hypot(slot.velocity.x, slot.velocity.y);
        slot.velocity *= ((m_boosted ? m_spaceSpeedMultiplier : 1.f) * slot.speedMultiplier * 25.f * smallest / size) / speed;
    }
}

void Lenia::StreamArena::redirect(bool outward, bool boosted) {
    m_outward = outward;
    m_boosted = boosted;
    normalizeVelocities();
    for (auto& slot : m_slots) slot.steeringTime = 0.f;
}

void Lenia::StreamArena::advance(Slot& slot) {
    const f32 center = m_size * 0.5f;
    const f32 radius = center - 8.f + slot.radius;
    if (slot.spawning) {
        slot.position = {center + radius * std::cos(slot.spawnAngle), center + radius * std::sin(slot.spawnAngle)};
        slot.spawning = false;
    }
    slot.steeringTime -= 1.f / m_fps;
    if (slot.steeringTime <= 0.f) {
        const auto direction = randomStreamVelocity(1.f, 0.f, 0.65f);
        slot.steeringOffset = std::atan2(direction.y, direction.x);
        slot.steeringTime = 2.f;
    }
    const glm::vec2 delta = slot.position - glm::vec2(center);
    if (std::hypot(delta.x, delta.y) > 1.f) {
        const f32 target = std::atan2(delta.y, delta.x) + (m_outward ? 0.f : 3.14159265359f) + slot.steeringOffset;
        steerStreamVelocity(slot.velocity, target, 1.f / m_fps);
    }
    advanceStreamMotion(slot.position, slot.velocity, 1.f / m_fps, center, radius);
}

std::vector<u8> Lenia::StreamArena::frame() {
    std::vector<InteractionField> fields;
    fields.reserve(m_slots.size());
    for (const auto& slot : m_slots) fields.push_back(slot.field);
    m_interaction.apply(fields, 1.f / m_fps);
    glfwPollEvents();
    glViewport(0, 0, m_size, m_size);
    glBindVertexArray(m_engine.m_VAO);
    glUseProgram(m_program);
    glUniform1i(0, m_size);
    glUniform1i(6, true);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, Engine::ce_indices);
    glEnable(GL_SCISSOR_TEST);
    for (auto& slot : m_slots) {
        slot.field.active = false;
        auto& simulation = *slot.simulation;
        const u32 index = static_cast<u32>(&slot - m_slots.data());
        const f32 health = m_interaction.health(index);
        const bool injured = health < 1.f;
        simulation.update(*slot.animal, std::min(m_dt, slot.animal->m_info.m_dt), health, m_interaction.envelope(index));
        simulation.bindField();
        ++slot.frames;
        if (simulation.m_mass <= 0.001) continue;
        const i32 empty[] = {static_cast<i32>(simulation.m_w), static_cast<i32>(simulation.m_h), -1, -1};
        glNamedBufferSubData(m_boundsBuffer, 0, sizeof(empty), empty);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_boundsBuffer);
        glUseProgram(m_boundsProgram);
        glUniform1i(0, simulation.m_w);
        glUniform2f(1, simulation.m_centerOfMass.x, simulation.m_centerOfMass.y);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glDispatchCompute((simulation.m_size + 255) / 256, 1, 1);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        i32 bounds[4];
        glGetNamedBufferSubData(m_boundsBuffer, 0, sizeof(bounds), bounds);
        if (bounds[2] < bounds[0]) continue;
        const f32 bodyWidth = bounds[2] - bounds[0] + 1;
        const f32 bodyHeight = bounds[3] - bounds[1] + 1;
        const glm::vec2 targetSource{
            simulation.m_centerOfMass.x + (bounds[0] + bounds[2]) * 0.5f - simulation.m_w * 0.5f,
            simulation.m_centerOfMass.y + (bounds[1] + bounds[3]) * 0.5f - simulation.m_h * 0.5f};
        if (!injured) {
            const f32 targetFit = slot.displayScale * std::min(1.f,
                std::min(64.f, (m_size * 0.5f - 8.f) * 0.55f) / std::max(bodyWidth, bodyHeight));
            slot.visual.update(simulation.m_centerOfMass, targetSource, targetFit, simulation.m_w, 1.f / m_fps);
            slot.heading = slot.visual.heading;
        }
        const f32 sourceX = injured ? slot.field.sourceX : slot.visual.source.x;
        const f32 sourceY = injured ? slot.field.sourceY : slot.visual.source.y;
        const f32 width = injured ? slot.field.width
            : bodyWidth + 2.f * std::abs(std::remainder(targetSource.x - sourceX, static_cast<f32>(simulation.m_w)));
        const f32 height = injured ? slot.field.height
            : bodyHeight + 2.f * std::abs(std::remainder(targetSource.y - sourceY, static_cast<f32>(simulation.m_h)));
        const f32 fit = injured ? slot.field.fit : slot.visual.fit;
        slot.radius = std::hypot(width, height) * fit * 0.5f + 2.f;
        advance(slot);
        const f32 angle = std::atan2(slot.velocity.y, slot.velocity.x) - slot.heading;
        slot.field = {simulation.deviceField(), static_cast<u32>(simulation.m_w), sourceX, sourceY,
            slot.position.x, slot.position.y, angle, fit, width, height, slot.radius,
            static_cast<f32>(simulation.m_mass), true};
        const i32 left = std::clamp(static_cast<i32>(std::floor(slot.position.x - slot.radius)), 0, static_cast<i32>(m_size));
        const i32 top = std::clamp(static_cast<i32>(std::floor(slot.position.y - slot.radius)), 0, static_cast<i32>(m_size));
        const i32 right = std::clamp(static_cast<i32>(std::ceil(slot.position.x + slot.radius)), 0, static_cast<i32>(m_size));
        const i32 bottom = std::clamp(static_cast<i32>(std::ceil(slot.position.y + slot.radius)), 0, static_cast<i32>(m_size));
        glScissor(left, m_size - bottom, right - left, bottom - top);
        glUseProgram(m_program);
        glUniform1i(0, m_size);
        glUniform1i(1, simulation.m_w);
        glUniform2f(2, sourceX, sourceY);
        glUniform2f(3, slot.position.x, slot.position.y);
        glUniform1f(4, angle);
        glUniform1f(5, fit);
        glUniform1i(6, false);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, static_cast<u8>(BufferBinding::COLOR), m_palettes[slot.palette]->m_ID);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, Engine::ce_indices);
    }
    glDisable(GL_SCISSOR_TEST);
    std::vector<u8> pixels(m_size * m_size * 3);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m_size, m_size, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

std::vector<f32> Lenia::StreamArena::masses() const {
    std::vector<f32> result;
    result.reserve(m_slots.size());
    for (const auto& slot : m_slots) result.push_back(static_cast<f32>(slot.simulation->m_mass));
    return result;
}

int Lenia::streamArena() {
    u32 size, fps, count;
    f32 dt, spaceSpeedMultiplier;
    if (!(std::cin >> size >> fps >> dt >> spaceSpeedMultiplier >> count) || size < 256 || size > 1024 || fps < 1 || fps > 60 ||
        count < 1 || count > 100 || !std::isfinite(dt) || dt <= 0.f || dt > 0.2f ||
        !std::isfinite(spaceSpeedMultiplier) || spaceSpeedMultiplier < 1.f) return 1;
    std::vector<StreamOrganism> organisms(count);
    for (auto& organism : organisms) if (!readOrganism(organism)) return 1;
    int devices = 0;
    if (cudaGetDeviceCount(&devices) != cudaSuccess || devices == 0) {
        std::cerr << "Streaming requires an available CUDA GPU" << std::endl;
        return 1;
    }
    try {
        StreamArena arena(size, fps, dt, spaceSpeedMultiplier, organisms);
        std::cout << "READY\n" << std::flush;
        char command;
        while (std::cin >> command) {
            if (command == 'r') {
                u32 index;
                f32 angle;
                StreamOrganism organism;
                if (!(std::cin >> index) || !readOrganism(organism) || !(std::cin >> angle)) return 1;
                arena.replace(index, organism, angle);
                std::cout << "READY\n" << std::flush;
                continue;
            }
            if (command == 'o' || command == 'i' || command == 'b') {
                arena.redirect(command == 'o', command != 'i');
                std::cout << "READY\n" << std::flush;
                continue;
            }
            if (command != 'n') return 1;
            const auto pixels = arena.frame();
            const auto masses = arena.masses();
            std::cout.write(reinterpret_cast<const char*>(masses.data()), masses.size() * sizeof(f32));
            std::cout.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
            std::cout.flush();
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
