#pragma once

#include "core.hpp"
#include "colors.hpp"
#include "simulation.hpp"
#include "animal.hpp"
#include "ui.hpp"
#include "glm/vec2.hpp"
#include <map>
#include <memory>
#include <optional>

namespace Lenia {

    class Engine {
        friend class StreamArena;

        enum DrawMode {
            NONE, 
            CIRCLE,
            STENCIL
        };

        enum RenderMode {
            NORMAL,
            BOUNDINGBOXES,
            CUDA
        };

        public:
            explicit Engine() noexcept;
            explicit Engine(const u32 w, const u32 h, const u8 scale) noexcept;
            explicit Engine(const u32 w, const u32 h, const u8 scale, const f32 dtOverride) noexcept;
            explicit Engine(const u32 winW, const u32 winH, const u32 simW, const u32 simH, const u8 scale, const f32 dtOverride, const bool streaming = false) noexcept;
            void configureStream(const std::size_t animalIdx, const std::vector<glm::uvec2>& positions, const bool centered = false);
            std::vector<u8> streamFrame();
            glm::vec2 streamDirection() const noexcept;
            f32 streamMass() const noexcept;
            ~Engine() noexcept;
            [[nodiscard]] bool shouldRun() const noexcept;
            void update() noexcept;
            void updateGL();
            void applyColorPalette(const ColorPalette &colorPalette) noexcept;
            const std::vector<AnimalInfo> &getAnimalInfo() const noexcept;
        private:
            void loadShaderControls() noexcept;
            void saveShaderControls() const noexcept;
            void applyShaderControls() noexcept;
            void reset() noexcept;
            void handleKeyboardInputs() noexcept;
            void move(const bool right, const f32 value);
            void moveToMouseToroidal() noexcept;
            void moveToMouseLinear() noexcept;
            void handleDrawMode() noexcept;
            void loadAnimalInfo() noexcept;
            void initGL() noexcept;
            void dumpAnimals();
            void dumpAnimalsEdited();
            void syncStatsEditable() noexcept;
            [[nodiscard]] f32 getCurrentDt() const noexcept;
            [[nodiscard]] glm::vec2 getCameraOffset() const noexcept;
            [[nodiscard]] glm::vec2 screenToWorld(const glm::vec2& screenPosition) const noexcept;
            [[nodiscard]] ImVec2 worldToScreen(const glm::vec2& worldPosition) const noexcept;

            RenderMode m_renderMode;

            u32 m_windowWidth = 1024;
            u32 m_windowHeight = 1024;
            u32 m_simWidth = 1024;
            u32 m_simHeight = 1024;
            u8 m_scale = 10;
            u32 count = 0;

            bool m_paused = false;
            bool m_streaming = false;
            bool m_showInfo = false;
            bool m_showBoundingBoxes = false;
            bool m_showGrid = true;
            bool m_showCenterOfMass = true;
            bool m_controlMode = false;
            bool m_mouseControlMode = false;
            bool m_focusMode = false;

            DrawMode m_drawMode = DrawMode::NONE;
            f32 m_drawRadius = 10.f;

            GLFWwindow* m_window;
            GLuint m_shaderProgram;
            GLuint m_computeProgram;
            GLuint m_VAO, m_VBO;

            std::unique_ptr<Simulation> m_simulation = nullptr;
            std::vector<Lenia::AnimalInfo> m_animals;
            std::unique_ptr<Lenia::Animal> m_currentAnimal;
            std::size_t m_animalIdx = 0;
            
            std::unique_ptr<Buffer<ColorPalette>> m_colorBuffer;
            ShaderControls m_shaderControls;
            ShaderControls m_loadedShaderControls;
            std::unique_ptr<UI::InfoPanelState> m_infoPanelState;
            std::optional<f32> m_dtOverride;

            f64 m_updateTime;

            static constexpr GLubyte ce_indices[] = {
                0, 1, 2,
                0, 2, 3
            };

            GLuint m_numGroupsX;
            GLuint m_numGroupsY;
        };
}
