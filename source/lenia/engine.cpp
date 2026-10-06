#include "engine.hpp"
#include "toroidal_math.hpp"
#include "texture_loader.hpp"
#include "ui.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "glm/glm.hpp"
#include "glm/vec2.hpp"
#include <cmath>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace {
    constexpr const char* shader_controls_path = "../resources/shader_controls.cfg";
}

Lenia::Engine::Engine() noexcept : Engine(1024, 1024, 10) {};

Lenia::Engine::Engine(const u32 w, const u32 h, const u8 scale) noexcept :
    Engine(w, h, w, h, scale, 0.f) {}

Lenia::Engine::Engine(const u32 w, const u32 h, const u8 scale, const f32 dtOverride) noexcept :
    Engine(w, h, w, h, scale, dtOverride) {}

Lenia::Engine::Engine(const u32 winW, const u32 winH, const u32 simW, const u32 simH, const u8 scale, const f32 dtOverride, const bool streaming) noexcept :
    m_windowWidth(winW),
    m_windowHeight(winH),
    m_simWidth(simW),
    m_simHeight(simH),
    m_scale(scale),
    m_streaming(streaming),
    m_colorBuffer(),
    m_dtOverride(dtOverride > 0.f ? std::optional<f32>{dtOverride} : std::nullopt) {
    initGL();

    loadAnimalInfo();
    m_animalIdx = 26;
    m_currentAnimal = std::make_unique<Animal>(m_animals[m_animalIdx], scale);

    m_currentAnimal->computePadded(m_simWidth);
    m_simulation = std::make_unique<Simulation>(m_simWidth, m_simHeight, m_scale, 2);
    auto cells = m_currentAnimal->getCells(); 


    m_simulation->placeCells(cells, m_currentAnimal->m_info.m_w, m_currentAnimal->m_info.m_h, 0, 0);

    m_colorBuffer = std::make_unique<Buffer<ColorPalette>, BufferBinding, std::vector<ColorPalette>>(BufferBinding::COLOR, {Magma});
    applyColorPalette(Magma);
    loadShaderControls();
    m_loadedShaderControls = m_shaderControls;
    m_infoPanelState = std::make_unique<UI::InfoPanelState>(UI::InfoPanelState{{}, {}, m_shaderControls});
    m_simulation->loadFFT();

    m_numGroupsX = (m_simulation->m_w + 31) / 32;
    m_numGroupsY = (m_simulation->m_h + 31) / 32;
    syncStatsEditable();
}

Lenia::Engine::~Engine() noexcept {
    m_simulation.reset();
    m_currentAnimal.reset();
    m_colorBuffer.reset();
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteProgram(m_shaderProgram);
    glDeleteProgram(m_computeProgram);
    glDeleteBuffers(1, &m_VBO);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

const std::vector<Lenia::AnimalInfo>& Lenia::Engine::getAnimalInfo() const noexcept {
    return m_animals;
}

void Lenia::Engine::initGL() noexcept {
    i32 width, height, channels;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->AddFontFromFileTTF("../resources/consolas.ttf", 50);

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        exit(-1);
    }
    if (m_streaming) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    }
    m_window = glfwCreateWindow(m_windowWidth, m_windowHeight, "", NULL, NULL);
    if (!m_window) {
        std::cerr << "Failed to create OpenGL window" << std::endl;
        glfwTerminate();
        exit(-1);
    }

    u8* const pixels = stbi_load("../resources/lenia.png", &width, &height, &channels, 4);
    GLFWimage icon{width, height, pixels};

    glfwSetWindowIcon(m_window, 1, &icon); 
    stbi_image_free(pixels); 

    glfwMakeContextCurrent(m_window);
    glfwSetWindowTitle(m_window, "Lenia");
    gladLoadGL((GLADloadfunc)glfwGetProcAddress);
    ImGui_ImplGlfw_InitForOpenGL(m_window, true); 
    ImGui_ImplOpenGL3_Init();

    std::string compute_shader_code = Lenia::loadShaderFile("../shaders/lenia.comp");
    std::string frag_shader_code = Lenia::loadShaderFile("../shaders/lenia.frag");
    std::string vertex_shader_code = Lenia::loadShaderFile("../shaders/lenia.vert");

    constexpr float vertices[] = {
        -1.0f, -1.0f, 0.0f,
        1.0f, -1.0f, 0.0f,
        1.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f
    };

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glfwSwapInterval(0);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(f32), (void*)0);
    glEnableVertexAttribArray(0);

    GLuint compute_shader = createShader(GL_COMPUTE_SHADER, compute_shader_code.c_str());
	GLuint fragment_shader = createShader(GL_FRAGMENT_SHADER, frag_shader_code.c_str());
	GLuint vertex_shader = createShader(GL_VERTEX_SHADER, vertex_shader_code.c_str());

    m_shaderProgram = glCreateProgram();
    m_computeProgram = glCreateProgram();

    glAttachShader(m_computeProgram, compute_shader);
	glLinkProgram(m_computeProgram);
    glAttachShader(m_shaderProgram, fragment_shader);
    glAttachShader(m_shaderProgram, vertex_shader);
    glLinkProgram(m_shaderProgram);
    checkProgramLinking(m_shaderProgram);
    checkProgramLinking(m_computeProgram);
    glDeleteShader(compute_shader);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
}

void Lenia::Engine::loadAnimalInfo() noexcept {
    std::ifstream file("../resources/animals_dim.csv");
    if (!file.is_open()) {
        std::cerr << "file resources/animals.csv couldn't be opened" << std::endl;
        exit(-1);
    }
    std::string line;
    while (std::getline(file, line)) {
        std::vector<std::string> tokens;
        std::stringstream ss(line);
        std::string token;
        while (std::getline(ss, token, ','))
            tokens.push_back(token);
        const size_t R = (size_t)std::stoul(tokens[5]);
        const f32 dt = 1.f / std::stof(tokens[6]);
        std::stringstream beta_stream(tokens[7]);
        std::vector<f32> vBeta;
        while (std::getline(beta_stream, token, ';'))
            vBeta.push_back(std::stof(token));
        const f32 mu = std::stof(tokens[8]);
        const f32 sigma = std::stof(tokens[9]);
        const KernelCore kn = static_cast<KernelCore>(std::stoi(tokens[10]) - 1);
        const GrowthFunction gn = static_cast<GrowthFunction>(std::stoi(tokens[11]) - 1);
        const size_t w = std::stoul(tokens[12]);
        const size_t h = std::stoul(tokens[13]);
        const Taxonomy tax = {tokens[4], tokens[0], tokens[1], tokens[2], tokens[3]};
        const std::vector<f32> beta = vBeta;
        auto animal = AnimalInfo{tax, R, w, h, m_scale, dt, 1.f / (f32)R * (f32)R, mu, sigma, beta, kn, gn, tokens[14]};
        m_animals.push_back(animal);
    }
}

void Lenia::Engine::dumpAnimals() {
    std::ofstream outfile("../resources/animals_dim.csv");
    for (const auto& animal : m_animals) {
        std::stringstream beta;
        for (size_t i = 0; i < animal.m_beta.size() - 1; i++) {
            beta << animal.m_beta[i] << ";";
        }
        beta << *(animal.m_beta.end() - 1);
        Animal temp = Animal(animal, 1);
        outfile 
            << animal.m_taxonomy._class << ","
            << animal.m_taxonomy.order << ","
            << animal.m_taxonomy.family << ","
            << animal.m_taxonomy.subfamily << ","
            << animal.m_taxonomy.species << ","
            << animal.m_r << ","
            << 1 / animal.m_dt << ","
            << beta.str() << ","
            << animal.m_mu << ","
            << animal.m_sigma << ","
            << static_cast<char>(static_cast<u8>(animal.m_kn) + '0' + 1) << ","
            << static_cast<char>(static_cast<u8>(animal.m_gn) + '0' + 1) << ","
            << temp.m_w << ","
            << temp.m_h << ","
            << animal.m_rle << ","
            << "\n";
    }
}

void Lenia::Engine::dumpAnimalsEdited() {
    const auto& animal = m_animals[m_animalIdx];

    std::vector<std::string> existingLines;
    {
        std::ifstream infile("../resources/edited_animals.csv");
        std::string line;
        while (std::getline(infile, line)) {
            existingLines.push_back(line);
        }
    }

    std::stringstream beta;
    for (size_t i = 0; i < animal.m_beta.size() - 1; i++) {
        beta << animal.m_beta[i] << ";";
    }
    beta << *(animal.m_beta.end() - 1);
    Animal temp = Animal(animal, 1);
    std::stringstream row;
    row << animal.m_taxonomy._class << ","
        << animal.m_taxonomy.order << ","
        << animal.m_taxonomy.family << ","
        << animal.m_taxonomy.subfamily << ","
        << animal.m_taxonomy.species << ","
        << animal.m_r << ","
        << 1 / animal.m_dt << ","
        << beta.str() << ","
        << animal.m_mu << ","
        << animal.m_sigma << ","
        << static_cast<char>(static_cast<u8>(animal.m_kn) + '0' + 1) << ","
        << static_cast<char>(static_cast<u8>(animal.m_gn) + '0' + 1) << ","
        << temp.m_w << ","
        << temp.m_h << ","
        << animal.m_rle << ",";

    const std::string species = animal.m_taxonomy.species;
    bool replaced = false;
    for (auto& line : existingLines) {
        std::stringstream ss(line);
        std::string tok;
        for (int i = 0; i < 5 && std::getline(ss, tok, ','); i++);
        if (tok == species) {
            line = row.str();
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        existingLines.push_back(row.str());
    }

    std::ofstream outfile("../resources/edited_animals.csv", std::ios::trunc);
    for (const auto& line : existingLines) {
        outfile << line << "\n";
    }
}

void Lenia::Engine::syncStatsEditable() noexcept {
    m_infoPanelState->stats.scale = m_scale;
    m_infoPanelState->stats.r = static_cast<i32>(m_currentAnimal->m_info.m_r);
    m_infoPanelState->stats.mu = m_currentAnimal->m_info.m_mu;
    m_infoPanelState->stats.sigma = m_currentAnimal->m_info.m_sigma;
    m_infoPanelState->stats.dt = getCurrentDt();
}

void Lenia::Engine::handleKeyboardInputs() noexcept {
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    f32 scroll;
    if (ImGui::IsKeyPressed(ImGuiKey_I)) {
        m_showInfo = !m_showInfo;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_P)) {
        m_paused = !m_paused;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !m_controlMode)  {
        m_animalIdx = (m_animalIdx + 1) % m_animals.size();
        m_currentAnimal = std::make_unique<Animal>(m_animals[m_animalIdx], m_scale);
        syncStatsEditable();
        reset();
    } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && !m_controlMode)  {
        m_animalIdx = m_animalIdx == 0 ? m_animals.size() - 1 : m_animalIdx - 1;
        m_currentAnimal = std::make_unique<Animal>(m_animals[m_animalIdx], m_scale);
        syncStatsEditable();
        reset();
    }
    if (ImGui::IsKeyDown(ImGuiKey_RightArrow) && m_controlMode && !m_mouseControlMode) {
        move(true, 0.1f);
    } else if (ImGui::IsKeyDown(ImGuiKey_LeftArrow) && m_controlMode && !m_mouseControlMode)  {
        move(false, 0.1f);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  {                
        m_scale = std::max(m_scale - 1, 1);
        m_simulation->m_scale = m_scale;
        m_currentAnimal->resize(m_scale);
        reset();
    } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        m_scale = std::min(m_scale + 1, 30);
        m_currentAnimal->resize(m_scale);
        m_simulation->m_scale = m_scale;
        reset();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        count = 0;
        reset();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_B)) {
        m_showBoundingBoxes = !m_showBoundingBoxes;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_G)) {
        m_showGrid = !m_showGrid;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_M)) {
        if (m_controlMode)
            m_mouseControlMode = !m_mouseControlMode;
        else
            m_showCenterOfMass = !m_showCenterOfMass;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_C)) {
        m_controlMode = !m_controlMode;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F)) {
        m_focusMode = !m_focusMode;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_D)) {
        m_drawMode = DrawMode::CIRCLE;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
        m_drawMode = DrawMode::STENCIL;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_S)) {
        UI::searchAnimal(m_animals);
    }
    if ((scroll = ImGui::GetIO().MouseWheel) != 0.f && m_drawMode != DrawMode::NONE) {
        f32 scroll_amount = scroll > 0.f ? 1.1f : 0.9f;
        m_drawRadius = m_drawRadius > 5.f ? (m_drawRadius * scroll_amount) : 5.01f;
    }
}

void Lenia::Engine::move(const bool right, const f32 value) {
    const f32 direction_length_squared =
        m_simulation->m_direction.x * m_simulation->m_direction.x +
        m_simulation->m_direction.y * m_simulation->m_direction.y;
    if (direction_length_squared <= 1e-6f) {
        return;
    }

    glm::vec2 circle;
    const f32 simulation_radius = static_cast<f32>(m_currentAnimal->m_info.m_r * m_currentAnimal->m_scale);
    const f32 steering_distance = std::max(simulation_radius * 0.75f, 16.f);
    const f32 steering_radius = std::max(simulation_radius * 0.18f, 6.f);
    const f32 attenuation = std::max(0.97f, 1.f - value * 0.1f);
    glm::vec2 start = glm::vec2(m_simulation->m_centerOfMass[0], m_simulation->m_centerOfMass[1]);
    if (right)
        circle = glm::vec2(start.x - m_simulation->m_direction.y * steering_distance, start.y + m_simulation->m_direction.x * steering_distance);
    else 
        circle = glm::vec2(start.x + m_simulation->m_direction.y * steering_distance, start.y - m_simulation->m_direction.x * steering_distance);
    circle.x = Lenia::wrapCoordinate(circle.x, static_cast<f32>(m_simWidth));
    circle.y = Lenia::wrapCoordinate(circle.y, static_cast<f32>(m_simHeight));
    if (m_showInfo) {
        ImDrawList *draw_list = ImGui::GetBackgroundDrawList();
        draw_list->AddCircle(worldToScreen(circle), steering_radius, IM_COL32(255, 0, 0, 255), 64);
    }
    m_simulation->placeCellsCircle((u16)circle.x, (u16)circle.y, static_cast<u16>(steering_radius), attenuation);
}

void Lenia::Engine::moveToMouseToroidal() noexcept {
    const ImVec2 mouse = ImGui::GetMousePos();
    const glm::vec2 worldMouse = screenToWorld(glm::vec2{mouse.x, mouse.y});
    const f32 move_scalar = m_simulation->getMoveScalar(worldMouse);
    const f32 move_intensity = move_scalar * move_scalar * 3.f;
    move(move_scalar < 0, move_intensity);
}

void Lenia::Engine::moveToMouseLinear() noexcept {
    const ImVec2 mouse = ImGui::GetMousePos();
    const glm::vec2 worldMouse = screenToWorld(glm::vec2{mouse.x, mouse.y});

    const glm::vec2 to_dest_raw = worldMouse - m_simulation->m_centerOfMass;
    const f32 to_dest_len = glm::length(to_dest_raw);

    const f32 fwd_len = glm::length(m_simulation->m_direction);

    const glm::vec2 to_dest = to_dest_raw / to_dest_len;
    const glm::vec2 fwd = m_simulation->m_direction / fwd_len;

    const f32 dot = glm::dot(fwd, to_dest);
    const f32 cross = fwd.x * to_dest.y - fwd.y * to_dest.x;
    const f32 angle = std::atan2(cross, dot);
    const f32 move_scalar = -angle / 3.1415f;
    const f32 move_intensity = move_scalar * move_scalar * 3.f;

    if (m_showInfo) {
        ImDrawList *draw_list = ImGui::GetBackgroundDrawList();
        draw_list->AddLine(
            worldToScreen(m_simulation->m_centerOfMass), 
            worldToScreen(m_simulation->m_centerOfMass + to_dest * 50.f), 
            IM_COL32(255, 0, 0, 255), 
            2.f
        );
        draw_list->AddText(
            worldToScreen(m_simulation->m_centerOfMass + to_dest * 50.f), 
            IM_COL32(255, 0, 0, 255), 
            std::to_string(move_intensity).c_str()
        );
    }

    move(move_scalar < 0, move_intensity);
}

void Lenia::Engine::reset() noexcept {
    m_simulation->clearCells();
    m_simulation->placeCells(
        m_currentAnimal->getCells(), 
        m_currentAnimal->m_info.m_w, 
        m_currentAnimal->m_info.m_h, 
        0, 
        0
    );
    m_simulation->loadFFT();
    m_currentAnimal->computePadded(m_simWidth);
    m_currentAnimal->computeFFTKernel(m_simWidth);
}

[[nodiscard]] bool Lenia::Engine::shouldRun() const noexcept {
    return !glfwWindowShouldClose(m_window);
}

void Lenia::Engine::configureStream(const std::size_t animalIdx, const std::vector<glm::uvec2>& positions, const bool centered) {
    m_animalIdx = animalIdx;
    m_currentAnimal = std::make_unique<Animal>(m_animals.at(animalIdx), m_scale);
    m_currentAnimal->computePadded(m_simWidth);
    m_simulation->clearCells();
    const auto cells = m_currentAnimal->getCells();
    for (const auto& position : positions) {
        m_simulation->placeCells(cells, m_currentAnimal->m_info.m_w, m_currentAnimal->m_info.m_h, position.x, position.y);
    }
    m_simulation->loadFFT();
    m_showGrid = false;
    m_showCenterOfMass = false;
    m_focusMode = centered;
    if (centered) {
        m_infoPanelState->shadersEnabled = false;
    }
}

f32 Lenia::Engine::streamMass() const noexcept {
    return static_cast<f32>(m_simulation->m_mass);
}

glm::vec2 Lenia::Engine::streamDirection() const noexcept {
    return m_simulation->m_direction;
}

std::vector<u8> Lenia::Engine::streamFrame() {
    glfwPollEvents();
    glViewport(0, 0, m_simWidth, m_simHeight);
    m_simulation->update(*m_currentAnimal, getCurrentDt());
    glClear(GL_COLOR_BUFFER_BIT);
    updateGL();
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, ce_indices);
    std::vector<u8> pixels(m_simWidth * m_simHeight * 3);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m_simWidth, m_simHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    count++;
    return pixels;
}

void Lenia::Engine::update() noexcept {
    f64 start = glfwGetTime();
    glfwPollEvents();

    i32 fbW, fbH;
    glfwGetFramebufferSize(m_window, &fbW, &fbH);
    if (fbW > 0 && fbH > 0) {
        m_windowWidth = static_cast<u32>(fbW);
        m_windowHeight = static_cast<u32>(fbH);
        glViewport(0, 0, fbW, fbH);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    handleKeyboardInputs();
    m_simulation->setShowDebugInfo(m_showInfo);
    if (m_drawMode != DrawMode::NONE) {
        handleDrawMode();
    }
    glClear(GL_COLOR_BUFFER_BIT);
    if (!m_paused)  {
        const f32 currentDt = getCurrentDt();
        if (m_showInfo) {
	        auto updatestart = std::chrono::high_resolution_clock::now();
            m_simulation->update(*m_currentAnimal, currentDt);
            m_simulation->m_updateTimeTotal = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - updatestart);
        } else {
            m_simulation->update(*m_currentAnimal, currentDt);
        }
        if (m_controlMode && m_mouseControlMode) {
            moveToMouseLinear();
        }
        if (m_infoPanelState->decayOnExplosion &&
            m_simulation->m_mass > static_cast<f64>(m_infoPanelState->decayMassThreshold)) {
            if (!m_infoPanelState->decayActive) {
                m_infoPanelState->decayActive = true;
                const size_t originalR = m_currentAnimal->m_info.m_r;
                const size_t maxR = m_simWidth / (2 * m_scale) - 1;
                const size_t decayR = std::min(originalR * 3, maxR);
                AnimalInfo replacement{m_currentAnimal->m_info.m_taxonomy,
                    decayR,
                    m_currentAnimal->m_info.m_w, m_currentAnimal->m_info.m_h,
                    m_currentAnimal->m_info.m_startingScale, m_currentAnimal->m_info.m_dt,
                    m_currentAnimal->m_info.m_dx2, m_currentAnimal->m_info.m_mu, m_currentAnimal->m_info.m_sigma,
                    m_currentAnimal->m_info.m_beta, m_currentAnimal->m_info.m_kn,
                    m_currentAnimal->m_info.m_gn, m_currentAnimal->m_info.m_rle};
                m_currentAnimal = std::make_unique<Animal>(replacement, m_scale);
                m_currentAnimal->computePadded(m_simWidth);
                m_currentAnimal->computeFFTKernel(m_simWidth);
            }
        } else if (m_infoPanelState->decayActive) {
            m_infoPanelState->decayActive = false;
        }
    } else {
        UI::pausedText();
    }
    if (m_showInfo) {
        auto &state = *m_infoPanelState;
        state.stats.animalIdx = static_cast<i32>(m_animalIdx + 1);
        state.shaderSaveRequested = false;
        state.shaderResetRequested = false;

        UI::infoPanel(m_updateTime, *m_simulation, *m_currentAnimal.get(), state, m_animals.size(), &m_showInfo);

        if (state.shaderSaveRequested) {
            saveShaderControls();
            m_loadedShaderControls = m_shaderControls;
        }
        if (state.shaderResetRequested) {
            m_shaderControls = m_loadedShaderControls;
        }
        if (state.textureRequest.loadRequested) {
            auto tex = loadTextureFromFile(state.textureRequest.filepath);
            if (!tex.cells.empty()) {
                m_simulation->setPersistentBuffer(tex.cells, tex.width, tex.height);
                state.textureRequest.statusMessage = "Loaded!";
            } else {
                state.textureRequest.statusMessage = "Failed (max 1024x1024)";
            }
        }

        if (state.stats.animalIdx != static_cast<i32>(m_animalIdx + 1)) {
            m_animalIdx = static_cast<std::size_t>(state.stats.animalIdx - 1);
            m_currentAnimal = std::make_unique<Animal>(m_animals[m_animalIdx], m_scale);
            syncStatsEditable();
            reset();
        }
        if (state.stats.scale != m_scale) {
            m_scale = static_cast<u8>(state.stats.scale);
            m_currentAnimal->resize(m_scale);
            m_simulation->m_scale = m_scale;
            reset();
        }
        if (state.stats.dt != getCurrentDt()) {
            m_dtOverride = state.stats.dt;
        }
        if (state.stats.r != static_cast<i32>(m_currentAnimal->m_info.m_r) ||
            state.stats.mu != m_currentAnimal->m_info.m_mu ||
            state.stats.sigma != m_currentAnimal->m_info.m_sigma) {
            AnimalInfo replacement{m_animals[m_animalIdx].m_taxonomy,
                static_cast<size_t>(state.stats.r),
                m_animals[m_animalIdx].m_w, m_animals[m_animalIdx].m_h,
                m_animals[m_animalIdx].m_startingScale, m_animals[m_animalIdx].m_dt,
                m_animals[m_animalIdx].m_dx2, state.stats.mu, state.stats.sigma,
                m_animals[m_animalIdx].m_beta, m_animals[m_animalIdx].m_kn,
                m_animals[m_animalIdx].m_gn, m_animals[m_animalIdx].m_rle};
            std::destroy_at(&m_animals[m_animalIdx]);
            std::construct_at(&m_animals[m_animalIdx], std::move(replacement));
            m_currentAnimal = std::make_unique<Animal>(m_animals[m_animalIdx], m_scale);
            reset();
        }

        if (state.paletteChanged) {
            const ColorPalette* palettes[] = { &Magma, &Greyscale };
            applyColorPalette(*palettes[state.paletteIndex]);
        }

        if (state.stats.saveRequested) {
            dumpAnimalsEdited();
        }

        UI::playerStatsText(*m_currentAnimal.get(), *m_simulation, worldToScreen(m_simulation->m_centerOfMass));
    }
    if (m_controlMode && m_showInfo) {
        UI::directionVector(worldToScreen(m_simulation->m_centerOfMass), m_simulation->m_direction);
    }
    updateGL();
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, ce_indices);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_window);
    m_updateTime = glfwGetTime() - start;
    count++;
}

void Lenia::Engine::updateGL() {
    // glUseProgram(m_computeProgram);
    // glDispatchCompute(m_numGroupsX, m_numGroupsY, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUniform1ui(0, m_simulation->m_w);
    glUniform1ui(1, m_simulation->m_h);
    glUniform1ui(2, m_currentAnimal->m_info.m_r * m_currentAnimal->m_scale);
    glUniform1f(3, getCurrentDt());
    glUniform1f(4, m_currentAnimal->m_info.m_mu);
    glUniform1f(5, m_currentAnimal->m_info.m_sigma);
    glUniform1f(6, m_currentAnimal->m_info.m_dx2);
    glUniform1ui(7, (GLuint)m_currentAnimal->m_info.m_gn);
    glUseProgram(m_shaderProgram);
    glUniform1ui(0, m_simulation->m_w);
    glUniform1ui(1, m_simulation->m_h);
    glUniform1i(2, m_showBoundingBoxes);
    glUniform1i(3, m_showGrid);
    glUniform1i(4, m_showCenterOfMass);
    glUniform1f(5, static_cast<f32>(glfwGetTime()));
    glUniform1ui(6, count);
    applyShaderControls();
    const glm::vec2 cameraOffset = getCameraOffset();
    glUniform2f(55, cameraOffset.x, cameraOffset.y);
    glUniform1ui(56, m_windowWidth);
    glUniform1ui(57, m_windowHeight);
    glUniform1i(58, m_infoPanelState->shadersEnabled ? 1 : 0);
    glUniform1i(59, m_infoPanelState->fluidQuality);
    glUniform1i(60, m_paused ? 1 : 0);
    glBindVertexArray(m_VAO);
}

f32 Lenia::Engine::getCurrentDt() const noexcept {
    return m_dtOverride.value_or(m_currentAnimal->m_info.m_dt);
}

glm::vec2 Lenia::Engine::getCameraOffset() const noexcept {
    if (!m_focusMode || !m_simulation) {
        return glm::vec2{0.f, 0.f};
    }

    const f32 scale = std::min(
        static_cast<f32>(m_simWidth) / static_cast<f32>(m_windowWidth),
        static_cast<f32>(m_simHeight) / static_cast<f32>(m_windowHeight));
    return glm::vec2{
        m_simulation->m_centerOfMass.x - static_cast<f32>(m_windowWidth) * 0.5f * scale,
        m_simulation->m_centerOfMass.y - static_cast<f32>(m_windowHeight) * 0.5f * scale
    };
}

glm::vec2 Lenia::Engine::screenToWorld(const glm::vec2& screenPosition) const noexcept {
    const f32 scale = std::min(
        static_cast<f32>(m_simWidth) / static_cast<f32>(m_windowWidth),
        static_cast<f32>(m_simHeight) / static_cast<f32>(m_windowHeight));
    const glm::vec2 worldPosition = glm::vec2{screenPosition.x * scale, screenPosition.y * scale} + getCameraOffset();
    return glm::vec2{
        Lenia::wrapCoordinate(worldPosition.x, static_cast<f32>(m_simWidth)),
        Lenia::wrapCoordinate(worldPosition.y, static_cast<f32>(m_simHeight))
    };
}

ImVec2 Lenia::Engine::worldToScreen(const glm::vec2& worldPosition) const noexcept {
    const f32 scale = 1.f / std::min(
        static_cast<f32>(m_simWidth) / static_cast<f32>(m_windowWidth),
        static_cast<f32>(m_simHeight) / static_cast<f32>(m_windowHeight));
    if (!m_focusMode || !m_simulation) {
        return ImVec2{worldPosition.x * scale, worldPosition.y * scale};
    }

    const glm::vec2 delta = Lenia::shortestToroidalDelta(
        m_simulation->m_centerOfMass,
        worldPosition,
        static_cast<f32>(m_simWidth),
        static_cast<f32>(m_simHeight));
    return ImVec2{
        static_cast<f32>(m_windowWidth) * 0.5f + delta.x * scale,
        static_cast<f32>(m_windowHeight) * 0.5f + delta.y * scale
    };
}

void Lenia::Engine::applyShaderControls() noexcept {
    glUniform1f(7, m_shaderControls.noiseUvScale0);
    glUniform1f(8, m_shaderControls.noiseUvScale1);
    glUniform1f(9, m_shaderControls.noiseUvScale2);
    glUniform1f(10, m_shaderControls.noiseTimeScale0);
    glUniform1f(11, m_shaderControls.noiseTimeScale1);
    glUniform1f(12, m_shaderControls.noiseTimeScale2);
    glUniform1f(13, m_shaderControls.noisePhase1);
    glUniform1f(14, m_shaderControls.noisePhase2);
    glUniform1f(15, m_shaderControls.noiseWeight0);
    glUniform1f(16, m_shaderControls.noiseWeight1);
    glUniform1f(17, m_shaderControls.noiseWeight2);
    glUniform1f(18, m_shaderControls.stateCurlBase);
    glUniform1f(19, m_shaderControls.stateCurlScale);
    glUniform1f(20, m_shaderControls.fluidCurlScale);
    glUniform1f(21, m_shaderControls.vorticityFluidScale);
    glUniform1f(22, m_shaderControls.vorticityConfinement);
    glUniform1f(23, m_shaderControls.turbulenceBase);
    glUniform1f(24, m_shaderControls.turbulenceDensityScale);
    glUniform1f(25, m_shaderControls.buoyancyBase);
    glUniform1f(26, m_shaderControls.buoyancyStateScale);
    glUniform1f(27, m_shaderControls.buoyancyGradientScale);
    glUniform1f(28, m_shaderControls.advectionScale);
    glUniform1f(29, m_shaderControls.injectionMinState);
    glUniform1f(30, m_shaderControls.injectionMaxState);
    glUniform1f(31, m_shaderControls.injectionBase);
    glUniform1f(32, m_shaderControls.injectionStateScale);
    glUniform1f(33, m_shaderControls.neighborAverageWeight);
    glUniform1f(34, m_shaderControls.laplacianDiffusionScale);
    glUniform1f(35, m_shaderControls.viscosity);
    glUniform1f(36, m_shaderControls.dissipation);
    glUniform1f(37, m_shaderControls.foamShearScale);
    glUniform1f(38, m_shaderControls.foamLaplacianScale);
    glUniform1f(39, m_shaderControls.energyVelocityScale);
    glUniform1f(40, m_shaderControls.energyDensityScale);
    glUniform1f(41, m_shaderControls.stateAdvectionScale);
    glUniform1f(42, m_shaderControls.transportMixScale);
    glUniform1f(43, m_shaderControls.overlayDensityScale);
    glUniform1f(44, m_shaderControls.overlayEnergyScale);
    glUniform1f(45, m_shaderControls.overlayMax);
    glUniform1f(46, m_shaderControls.wispBase);
    glUniform1f(47, m_shaderControls.wispEnergyScale);
    glUniform1f(48, m_shaderControls.gridStateMax);
    glUniform1f(49, m_shaderControls.gridColor);
    glUniform1f(50, m_shaderControls.gridAlpha);
    glUniform3fv(51, 1, m_shaderControls.smokeColorLow.data());
    glUniform3fv(52, 1, m_shaderControls.smokeColorHigh.data());
    glUniform3fv(53, 1, m_shaderControls.wispColor.data());
    glUniform4fv(54, 1, m_shaderControls.centerOfMassColor.data());
}

void Lenia::Engine::loadShaderControls() noexcept {
    std::ifstream file(shader_controls_path);
    if (!file.is_open()) {
        return;
    }

    ShaderControls loaded = m_shaderControls;
    auto readVec3 = [&](std::array<f32, 3>& value) {
        return static_cast<bool>(file >> value[0] >> value[1] >> value[2]);
    };
    auto readVec4 = [&](std::array<f32, 4>& value) {
        return static_cast<bool>(file >> value[0] >> value[1] >> value[2] >> value[3]);
    };

    if (!(file >> loaded.noiseUvScale0 >> loaded.noiseUvScale1 >> loaded.noiseUvScale2
        >> loaded.noiseTimeScale0 >> loaded.noiseTimeScale1 >> loaded.noiseTimeScale2
        >> loaded.noisePhase1 >> loaded.noisePhase2
        >> loaded.noiseWeight0 >> loaded.noiseWeight1 >> loaded.noiseWeight2
        >> loaded.stateCurlBase >> loaded.stateCurlScale >> loaded.fluidCurlScale
        >> loaded.vorticityFluidScale >> loaded.vorticityConfinement
        >> loaded.turbulenceBase >> loaded.turbulenceDensityScale
        >> loaded.buoyancyBase >> loaded.buoyancyStateScale >> loaded.buoyancyGradientScale
        >> loaded.advectionScale
        >> loaded.injectionMinState >> loaded.injectionMaxState
        >> loaded.injectionBase >> loaded.injectionStateScale
        >> loaded.neighborAverageWeight >> loaded.laplacianDiffusionScale
        >> loaded.viscosity >> loaded.dissipation
        >> loaded.foamShearScale >> loaded.foamLaplacianScale
        >> loaded.energyVelocityScale >> loaded.energyDensityScale
        >> loaded.stateAdvectionScale
        >> loaded.transportMixScale >> loaded.overlayDensityScale
        >> loaded.overlayEnergyScale >> loaded.overlayMax
        >> loaded.wispBase >> loaded.wispEnergyScale
        >> loaded.gridStateMax >> loaded.gridColor >> loaded.gridAlpha)) {
        return;
    }

    if (!readVec3(loaded.smokeColorLow) || !readVec3(loaded.smokeColorHigh) || !readVec3(loaded.wispColor) || !readVec4(loaded.centerOfMassColor)) {
        return;
    }

    m_shaderControls = loaded;
}

void Lenia::Engine::saveShaderControls() const noexcept {
    std::ofstream file(shader_controls_path, std::ios::trunc);
    if (!file.is_open()) {
        return;
    }

    file
        << m_shaderControls.noiseUvScale0 << ' ' << m_shaderControls.noiseUvScale1 << ' ' << m_shaderControls.noiseUvScale2 << '\n'
        << m_shaderControls.noiseTimeScale0 << ' ' << m_shaderControls.noiseTimeScale1 << ' ' << m_shaderControls.noiseTimeScale2 << '\n'
        << m_shaderControls.noisePhase1 << ' ' << m_shaderControls.noisePhase2 << '\n'
        << m_shaderControls.noiseWeight0 << ' ' << m_shaderControls.noiseWeight1 << ' ' << m_shaderControls.noiseWeight2 << '\n'
        << m_shaderControls.stateCurlBase << ' ' << m_shaderControls.stateCurlScale << ' ' << m_shaderControls.fluidCurlScale << '\n'
        << m_shaderControls.vorticityFluidScale << ' ' << m_shaderControls.vorticityConfinement << '\n'
        << m_shaderControls.turbulenceBase << ' ' << m_shaderControls.turbulenceDensityScale << '\n'
        << m_shaderControls.buoyancyBase << ' ' << m_shaderControls.buoyancyStateScale << ' ' << m_shaderControls.buoyancyGradientScale << '\n'
        << m_shaderControls.advectionScale << '\n'
        << m_shaderControls.injectionMinState << ' ' << m_shaderControls.injectionMaxState << ' ' << m_shaderControls.injectionBase << ' ' << m_shaderControls.injectionStateScale << '\n'
        << m_shaderControls.neighborAverageWeight << ' ' << m_shaderControls.laplacianDiffusionScale << ' ' << m_shaderControls.viscosity << ' ' << m_shaderControls.dissipation << '\n'
        << m_shaderControls.foamShearScale << ' ' << m_shaderControls.foamLaplacianScale << ' ' << m_shaderControls.energyVelocityScale << ' ' << m_shaderControls.energyDensityScale << '\n'
        << m_shaderControls.stateAdvectionScale << '\n'
        << m_shaderControls.transportMixScale << ' ' << m_shaderControls.overlayDensityScale << ' ' << m_shaderControls.overlayEnergyScale << ' ' << m_shaderControls.overlayMax << '\n'
        << m_shaderControls.wispBase << ' ' << m_shaderControls.wispEnergyScale << '\n'
        << m_shaderControls.gridStateMax << ' ' << m_shaderControls.gridColor << ' ' << m_shaderControls.gridAlpha << '\n'
        << m_shaderControls.smokeColorLow[0] << ' ' << m_shaderControls.smokeColorLow[1] << ' ' << m_shaderControls.smokeColorLow[2] << '\n'
        << m_shaderControls.smokeColorHigh[0] << ' ' << m_shaderControls.smokeColorHigh[1] << ' ' << m_shaderControls.smokeColorHigh[2] << '\n'
        << m_shaderControls.wispColor[0] << ' ' << m_shaderControls.wispColor[1] << ' ' << m_shaderControls.wispColor[2] << '\n'
        << m_shaderControls.centerOfMassColor[0] << ' ' << m_shaderControls.centerOfMassColor[1] << ' ' << m_shaderControls.centerOfMassColor[2] << ' ' << m_shaderControls.centerOfMassColor[3] << '\n';
}

void Lenia::Engine::handleDrawMode() noexcept {
    const ImVec2 mouse = ImGui::GetMousePos();
    const glm::vec2 worldMouse = screenToWorld(glm::vec2{mouse.x, mouse.y});
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    switch (m_drawMode) {
        default:
        case DrawMode::CIRCLE: 
            draw_list->AddCircle(mouse, m_drawRadius, IM_COL32(255, 0, 0, 255), 64);
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_simulation->placeCellsCircle((u16)worldMouse.x, (u16)worldMouse.y, static_cast<u16>(m_drawRadius), 1);
            } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                m_simulation->placeCellsCircle((u16)worldMouse.x, (u16)worldMouse.y, static_cast<u16>(m_drawRadius), 0);
            }
            break;
        case DrawMode::STENCIL:
            const size_t half_width = m_currentAnimal->m_info.m_w * m_scale / 2;
            const size_t half_height = m_currentAnimal->m_info.m_h * m_scale / 2;
            const ImVec2 tex_start = ImVec2{mouse.x - half_width, mouse.y - half_height};
            const ImVec2 tex_stop = ImVec2{mouse.x + half_width, mouse.y + half_height};
            draw_list->AddImage((ImTextureID)(intptr_t)m_currentAnimal->m_cellTexture, tex_start, tex_stop); 
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const glm::vec2 worldStart = glm::vec2{
                    Lenia::wrapCoordinate(worldMouse.x - static_cast<f32>(half_width), static_cast<f32>(m_simWidth)),
                    Lenia::wrapCoordinate(worldMouse.y - static_cast<f32>(half_height), static_cast<f32>(m_simHeight))
                };
                m_simulation->placeCells(m_currentAnimal->getCells(), m_currentAnimal->m_info.m_w, m_currentAnimal->m_info.m_h, (u32)worldStart.x, (u32)worldStart.y);
            }
            break;
    }
}

void Lenia::Engine::applyColorPalette(const ColorPalette& colorPalette) noexcept {
    (*m_colorBuffer)[0] = colorPalette;
    m_colorBuffer->storeDataInShader();
}



std::string Lenia::loadShaderFile(const std::string& name) {
    std::ifstream file(name);
    std::string shader_code;
    if (file.is_open()) {
        std::string line;
        while (getline(file, line)) {
            shader_code += line + "\n";
        }
        file.close();
    }
    else {
        std::cerr << "Failed to open shader file: " << name << std::endl;
        exit(-1);
    }
    return shader_code;
}


void Lenia::checkShaderCompilation(GLuint shader) {
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "ERROR: Shader Compilation Failed\n" << infoLog << std::endl;
    }
}

void Lenia::checkProgramLinking(GLuint program) {
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cerr << "ERROR: Program Linking Failed\n%s\n" << infoLog << std::endl;
    }
}

GLuint Lenia::createShader(const GLenum shaderType, const char* shaderCode) {
    GLuint shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &shaderCode, NULL);
    glCompileShader(shader);
    checkShaderCompilation(shader);
    return shader;
}


void Lenia::createTexture(GLuint *texture, const f32* data, const size_t width, const size_t height, const GLint swizzle_mask[4]) noexcept {
    glGenTextures(1, texture);
	glBindTexture(GL_TEXTURE_2D, *texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_FLOAT, data);
	glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle_mask);	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Lenia::createTexture(GLuint *texture, const size_t width, const size_t height) noexcept {
    glGenTextures(1, texture);
	glBindTexture(GL_TEXTURE_2D, *texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Lenia::dumpArrayToFile(const std::vector<f32> &buffer, i32 w, i32 h, const std::string &name) {
	std::ofstream out(name);
	out << w << " " << h << "\n";
	for (const auto& num : buffer)
		out << std::setprecision(2) << num << " ";
	out.close();
}
