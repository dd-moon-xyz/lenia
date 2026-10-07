#include "ui.hpp"
#include <algorithm>
#include <cstdio>
#include <random>

namespace {
    void randomizeFluidColors(Lenia::ShaderControls& controls) noexcept {
        static std::mt19937 generator{std::random_device{}()};
        std::uniform_real_distribution<f32> hueDistribution(0.f, 1.f);
        std::uniform_real_distribution<f32> lowSaturationDistribution(0.45f, 0.85f);
        std::uniform_real_distribution<f32> highSaturationDistribution(0.55f, 1.f);
        std::uniform_real_distribution<f32> lowValueDistribution(0.08f, 0.35f);
        std::uniform_real_distribution<f32> highValueDistribution(0.6f, 1.f);
        std::uniform_real_distribution<f32> wispHueOffsetDistribution(0.08f, 0.22f);
        std::uniform_real_distribution<f32> wispSaturationDistribution(0.15f, 0.55f);
        std::uniform_real_distribution<f32> wispValueDistribution(0.7f, 1.f);

        const f32 baseHue = hueDistribution(generator);
        const f32 lowSaturation = lowSaturationDistribution(generator);
        const f32 highSaturation = highSaturationDistribution(generator);
        const f32 lowValue = lowValueDistribution(generator);
        const f32 highValue = std::max(highValueDistribution(generator), lowValue + 0.25f);
        const f32 wispHue = std::fmod(baseHue + wispHueOffsetDistribution(generator), 1.f);

        ImGui::ColorConvertHSVtoRGB(baseHue, lowSaturation, lowValue,
            controls.smokeColorLow[0], controls.smokeColorLow[1], controls.smokeColorLow[2]);
        ImGui::ColorConvertHSVtoRGB(baseHue, highSaturation, std::min(highValue, 1.f),
            controls.smokeColorHigh[0], controls.smokeColorHigh[1], controls.smokeColorHigh[2]);
        ImGui::ColorConvertHSVtoRGB(wispHue, wispSaturationDistribution(generator), wispValueDistribution(generator),
            controls.wispColor[0], controls.wispColor[1], controls.wispColor[2]);
    }

    void shaderControlsSection(Lenia::ShaderControls& controls, bool& saveRequested, bool& resetRequested) noexcept {
        auto slider = [](const char* label, f32& value, f32 minValue, f32 maxValue) {
            ImGui::SliderFloat(label, &value, minValue, maxValue);
        };

        ImGui::SeparatorText("Noise");
        slider("Noise UV Scale 0", controls.noiseUvScale0, 0.001f, 0.4f);
        slider("Noise UV Scale 1", controls.noiseUvScale1, 0.001f, 0.4f);
        slider("Noise UV Scale 2", controls.noiseUvScale2, 0.001f, 0.4f);
        slider("Noise Time Scale 0", controls.noiseTimeScale0, 0.0f, 4.0f);
        slider("Noise Time Scale 1", controls.noiseTimeScale1, 0.0f, 4.0f);
        slider("Noise Time Scale 2", controls.noiseTimeScale2, 0.0f, 4.0f);
        slider("Noise Phase 1", controls.noisePhase1, 0.0f, 128.0f);
        slider("Noise Phase 2", controls.noisePhase2, 0.0f, 128.0f);
        slider("Noise Weight 0", controls.noiseWeight0, 0.0f, 20.0f);
        slider("Noise Weight 1", controls.noiseWeight1, 0.0f, 20.0f);
        slider("Noise Weight 2", controls.noiseWeight2, 0.0f, 20.0f);

        ImGui::SeparatorText("Flow");
        slider("State Curl Base", controls.stateCurlBase, 0.0f, 20.0f);
        slider("State Curl Scale", controls.stateCurlScale, 0.0f, 20.0f);
        slider("Fluid Curl Scale", controls.fluidCurlScale, 0.0f, 20.0f);
        slider("Vorticity Fluid Scale", controls.vorticityFluidScale, 0.0f, 4.0f);
        slider("Vorticity Confinement", controls.vorticityConfinement, 0.0f, 10.0f);
        slider("Turbulence Base", controls.turbulenceBase, 0.0f, 8.0f);
        slider("Turbulence Density Scale", controls.turbulenceDensityScale, 0.0f, 8.0f);
        slider("Buoyancy Base", controls.buoyancyBase, 0.0f, 6.0f);
        slider("Buoyancy State Scale", controls.buoyancyStateScale, 0.0f, 10.0f);
        slider("Buoyancy Gradient Scale", controls.buoyancyGradientScale, 0.0f, 10.0f);
        slider("Advection Scale", controls.advectionScale, 0.0f, 4.0f);

        ImGui::SeparatorText("Density");
        slider("Injection Min State", controls.injectionMinState, 0.0f, 2.0f);
        slider("Injection Max State", controls.injectionMaxState, 0.0f, 2.0f);
        slider("Injection Base", controls.injectionBase, 0.0f, 10.0f);
        slider("Injection State Scale", controls.injectionStateScale, 0.0f, 2.0f);
        slider("Neighbor Average Weight", controls.neighborAverageWeight, 0.0f, 2.0f);
        slider("Laplacian Diffusion Scale", controls.laplacianDiffusionScale, 0.0f, 2.0f);
        slider("Viscosity", controls.viscosity, 0.0f, 2.0f);
        slider("Dissipation", controls.dissipation, 0.80f, 2.0f);

        ImGui::SeparatorText("Detail");
        slider("Foam Shear Scale", controls.foamShearScale, 0.0f, 10.0f);
        slider("Foam Laplacian Scale", controls.foamLaplacianScale, 0.0f, 10.0f);
        slider("Energy Velocity Scale", controls.energyVelocityScale, 0.0f, 2.0f);
        slider("Energy Density Scale", controls.energyDensityScale, 0.0f, 10.0f);
        slider("State Advection Scale", controls.stateAdvectionScale, 0.0f, 2.0f);

        ImGui::SeparatorText("Render");
        slider("Transport Mix Scale", controls.transportMixScale, 0.0f, 2.0f);
        slider("Overlay Density Scale", controls.overlayDensityScale, 0.0f, 4.0f);
        slider("Overlay Energy Scale", controls.overlayEnergyScale, 0.0f, 4.0f);
        slider("Overlay Max", controls.overlayMax, 0.0f, 3.0f);
        slider("Wisp Base", controls.wispBase, 0.0f, 2.0f);
        slider("Wisp Energy Scale", controls.wispEnergyScale, 0.0f, 4.0f);
        slider("Grid State Max", controls.gridStateMax, 0.0f, 2.0f);
        slider("Grid Color", controls.gridColor, 0.0f, 2.0f);
        slider("Grid Alpha", controls.gridAlpha, 0.0f, 2.0f);
        if (ImGui::Button("Randomize Fluid Colors")) {
            randomizeFluidColors(controls);
        }
        ImGui::ColorEdit3("Smoke Color Low", controls.smokeColorLow.data());
        ImGui::ColorEdit3("Smoke Color High", controls.smokeColorHigh.data());
        ImGui::ColorEdit3("Wisp Color", controls.wispColor.data());
        ImGui::ColorEdit4("Center Of Mass Color", controls.centerOfMassColor.data());

        if (ImGui::Button("Save")) {
            saveRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            resetRequested = true;
        }

        controls.injectionMaxState = std::max(controls.injectionMinState, controls.injectionMaxState);
        controls.dissipation = std::max(controls.dissipation, 0.0f);
        controls.overlayMax = std::max(controls.overlayMax, 0.0f);
    }
}

void Lenia::UI::infoPanel(f64 updatetime, const Simulation &sim, const Animal &animal, InfoPanelState &state, const u16 maxAnimals, bool *open) {
    char buffer[512];

    ImGui::SetNextWindowPos(ImVec2(5, 5), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Info", open)) {
        ImGui::End();
        return;
    }
    ImGui::SetWindowFontScale(0.32f);

    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::snprintf(buffer, sizeof(buffer), "time: %.2fms (%.0f fps)",
            updatetime * 1000.f, 1.f / updatetime);
        ImGui::Text("%s", buffer);

        std::snprintf(buffer, sizeof(buffer), "size: [%zu, %zu]", sim.m_w, sim.m_h);
        ImGui::Text("%s", buffer);

        ImGui::PushItemWidth(160);
        ImGui::InputInt("scale", &state.stats.scale);
        ImGui::InputInt("r", &state.stats.r);
        ImGui::InputInt("animal", &state.stats.animalIdx);
        ImGui::InputFloat("mu", &state.stats.mu, 0.001f, 0.01f, "%.4f");
        ImGui::InputFloat("sigma", &state.stats.sigma, 0.001f, 0.01f, "%.4f");
        ImGui::InputFloat("dt", &state.stats.dt, 0.001f, 0.01f, "%.4f");
        ImGui::PopItemWidth();

        state.stats.scale = std::max(state.stats.scale, 1);
        state.stats.r = std::max(state.stats.r, 1);
        state.stats.animalIdx = std::clamp(state.stats.animalIdx, 1, static_cast<i32>(maxAnimals));
        state.stats.saveRequested = ImGui::Button("Save CSV");
    }

    if (ImGui::CollapsingHeader("Species", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("%s", animal.m_info.m_taxonomy.to_string().c_str());

        std::snprintf(buffer, sizeof(buffer), "bounding boxes: %02zu in %4.2f ms (%u threads)\n"
            "area computed: %.2f\n"
            "mass: %4.2f\n"
            "delta: %+08.2f (%+.4f%%)\n"
            "center of mass: [%.1f, %.1f]",
            sim.getNBoundingBoxes(), sim.m_updateTimeBoxes.count() / 1000.f, 1,
            1.f,
            sim.m_mass,
            sim.m_massDelta, sim.m_massDelta / sim.m_mass,
            sim.m_centerOfMass.x, sim.m_centerOfMass.y);
        ImGui::Text("%s", buffer);
    }

    if (ImGui::CollapsingHeader("Kernel")) {
        ImGui::Text("Padded Kernel");
        ImGui::Image((ImTextureID)(intptr_t)animal.m_paddedKernelTexture, ImVec2(256, 256));
        ImGui::Text("FFT Kernel");
        ImGui::Image((ImTextureID)(intptr_t)animal.m_fftKernelTexture, ImVec2(256, 256));
    }

    if (ImGui::CollapsingHeader("Texture Loader")) {
        ImGui::PushItemWidth(280);
        ImGui::InputText("Path", state.textureRequest.filepath, sizeof(state.textureRequest.filepath));
        ImGui::PopItemWidth();
        state.textureRequest.loadRequested = ImGui::Button("Load");
        if (!state.textureRequest.statusMessage.empty()) {
            ImGui::SameLine();
            ImGui::Text("%s", state.textureRequest.statusMessage.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Color Palette", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* paletteNames[] = { "Magma", "Greyscale", "Ocean", "Aurora", "Ember", "Ice" };
        state.paletteChanged = ImGui::Combo("Palette", &state.paletteIndex, paletteNames, IM_ARRAYSIZE(paletteNames));
    }

    if (ImGui::CollapsingHeader("Behaviors", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Decay on explosion", &state.decayOnExplosion);
        ImGui::PushItemWidth(160);
        ImGui::InputFloat("Mass threshold", &state.decayMassThreshold, 100.f, 1000.f, "%.0f");
        ImGui::PopItemWidth();
        state.decayMassThreshold = std::max(state.decayMassThreshold, 1.f);
    }

    ImGui::Checkbox("Enable Shaders", &state.shadersEnabled);
    ImGui::SliderInt("Fluid Quality", &state.fluidQuality, 0, 3);

    if (ImGui::CollapsingHeader("Shader Controls")) {
        shaderControlsSection(state.shaderControls, state.shaderSaveRequested, state.shaderResetRequested);
    }

    ImGui::End();
}

void Lenia::UI::playerStatsText(const Lenia::Animal& animal, const Lenia::Simulation& sim, const ImVec2 screenPosition) {
    char buffer[1024];
    ImGui::SetNextWindowPos(ImVec2(screenPosition.x - 40, screenPosition.y - 100), ImGuiCond_Always);
    constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("PlayerText", nullptr, window_flags);
    ImGui::SetWindowFontScale(0.4f);

    std::snprintf(buffer, sizeof(buffer), "%s\nmass: %f", animal.m_info.m_taxonomy.species.c_str(), sim.m_mass);
    ImGui::Text("%s", buffer);
    ImGui::End();
}

void Lenia::UI::directionVector(const ImVec2 screenPosition, const glm::vec2& direction) {
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    ImVec2 start = screenPosition;
    ImVec2 dir = ImVec2(direction[0], direction[1]);
    draw_list->AddLine(start, ImVec2(start.x + dir.x * 20, start.y + dir.y * 20), IM_COL32(255, 0, 0, 255));
    draw_list->AddLine(start, ImVec2(start.x - dir.y * 20, start.y + dir.x * 20), IM_COL32(255, 0, 0, 255));
    draw_list->AddLine(start, ImVec2(start.x + dir.y * 20, start.y - dir.x * 20), IM_COL32(255, 0, 0, 255));
}

void Lenia::UI::modeChangeText(const std::string& text) {
    const ImVec2 viewport = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(viewport.x - 200, 5), ImGuiCond_Always);
    constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoBackground |
                                    ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("ModeChangeText", nullptr, window_flags);
    ImGui::SetWindowFontScale(1.75f);
    ImGui::Text("%s", text.c_str());
    ImGui::End();
}

void Lenia::UI::pausedText() noexcept {
    const ImVec2 viewport = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(viewport.x - 300, 5), ImGuiCond_Always);
    constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoBackground |
                                    ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("Pause", nullptr, window_flags);
    ImGui::SetWindowFontScale(.5f);
    ImGui::Text("[PAUSED]");
    ImGui::End();
}

const Lenia::AnimalInfo& Lenia::UI::searchAnimal(const std::vector<AnimalInfo>& animals) noexcept {
    char inputText[256] = {};
    ImGui::Begin("Search Animal by Name");
    ImGui::InputText("Input Text", inputText, sizeof(inputText));
    ImGui::End();
    return animals[0];
}

std::string Lenia::UI::tolower(const std::string& str) noexcept {
    std::string lower = str;
    for (char& c : lower) {
        c = std::tolower(c);
    }
    return lower;
}

i32 Lenia::UI::lev(const std::string& a, const std::string& b) noexcept {
    i32 cost, x, y, z, m = a.size(), n = b.size();

    //TODO: benchmark this vs. vector vs unique vs std::array
    i32* d = new i32[n * m];

    for (i32 col = 1; col < m; ++col) {
        d[col] = col;
    }

    for (i32 row = 1; row < n; ++row) {
        d[row * m] = row;
    }

    for (i32 row = 1; row < n; ++row)
    for (i32 col = 1; col < m; ++col) {
        if (a.at(col) == b.at(row))
            cost = 0;
        else
            cost = 1;
        x = d[(row - 1) * m + col] + 1;
        y = d[row * m + col - 1] + 1;
        z = d[(row - 1) * m + col - 1] + cost;
        d[row * m + col] = std::min(std::min(x, y), z);
    }
    i32 res = d[m * n - 1];
    delete[] d;
    return res;
}

std::string Lenia::UI::fuzzysearch(const std::string& name, const std::vector<AnimalInfo>& animals) noexcept {
    std::string lower_animal, lower_name = tolower(name);
    std::pair<i32, std::string> best_match = {0xFFFF, ""};
    for (const auto& animal : animals) {
        lower_animal = tolower(animal.m_taxonomy.species);
        i32 score = lev(lower_name, lower_animal);
        if (score == 0)
            return animal.m_taxonomy.species;
        if (score < best_match.first)
            best_match = {score, animal.m_taxonomy.species};
    }
    return best_match.second;
}
