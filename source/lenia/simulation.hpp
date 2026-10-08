#pragma once
#include "core.hpp"
#include "boundingbox.hpp"
#include "buffer.hpp"
#include "animal.hpp"
#include "layerinfo.hpp"
#include <cuda_gl_interop.h>

#include <chrono>
#include <thread>

namespace Lenia
{

	class Simulation
	{
	public:
		std::size_t m_w;
		std::size_t m_h;
		std::size_t m_size;
		std::size_t m_scale = 1;
		f32 m_norm;
		f64 m_mass = 0;
		f64 m_massDelta = 0;
		glm::vec2 m_centerOfMass = {0, 0};
		glm::vec2 m_direction = {0, 0};

		std::chrono::microseconds m_updateTimeBoxes{};
		std::chrono::microseconds m_updateTimeTotal{};

		explicit Simulation(const size_t W, const size_t H, const size_t scale, const std::size_t layer_count);
		~Simulation() noexcept;
		void clearCells() noexcept;
		void placeCells(const std::vector<f32> &cells, const size_t c_w, const size_t c_h, const u32 x, const u32 y) noexcept;
		void placeCellsCircle(const u16 x, const u16 y, const u16 radius, const f32 value) noexcept;
		void setPersistentBuffer(const std::vector<f32> &cells, const size_t c_w, const size_t c_h) noexcept;
		void clearPersistentBuffer() noexcept;
		void setShowDebugInfo(const bool showDebugInfo) noexcept;
		void update(const Lenia::Animal &animal, const f32 dt, const f32 maximumState = 1.f, const f32 *envelope = nullptr) noexcept;
		void bindField() const noexcept;
		c64* deviceField() const noexcept { return m_fragBuffer; }
		void loadFFT() noexcept;
		void updateFFT(const Lenia::c64 *animalKernel, const f32 dt, const f32 mu, const f32 sigma, const f32 maximumState = 1.f, const f32 *envelope = nullptr) noexcept;
		void stepLayer(c64 *layerResult, const std::size_t fftOffset, const Lenia::c64 *animalKernel, const f32 dt, const f32 mu, const f32 sigma, const f32 maximumState = 1.f) noexcept;
		f32 getMoveScalar(const glm::vec2 dest) const noexcept;
		size_t getNBoundingBoxes() const noexcept;
		f32 calcAreaComputed() const noexcept;

		// static constexpr u8 getNChunks() {
		// 	return c_threadSplits * c_threadSplits;
		// }

	private:
		Buffer<f32> m_readBuffer;
		Buffer<f32> m_writeBuffer;
		Buffer<LayerInfo> m_hostLayerInfoBuffer;

		c64 *m_fftField = nullptr;
		c64 *m_mulfftField = nullptr;
		c64 *m_invfftField = nullptr;
		c64 *m_worldResult = nullptr;

		LayerInfo *m_gpuLayerInfoBuffer = nullptr;
		c64 *m_fragBuffer = nullptr;
		c64 *m_fluidFragBuffer = nullptr;

		LayerInfo m_previousStepInfo;

		std::size_t m_layerCount;
		u64 m_layerFlags = 0;

		c64 *m_persistentInitial = nullptr;

		cufftHandle m_plan;

		cudaGraphicsResource *m_cudaGraphicsResource = nullptr;
		cudaGraphicsResource *m_cudaLayerDataResource = nullptr;
		cudaGraphicsResource *m_cudaFluidGraphicsResource = nullptr;

		std::size_t m_numBytesField;
		std::size_t m_numBytesLayerData;

		dim3 m_threadsPerBlock;
		dim3 m_blocksInGrid;

		const u32 m_maxTimesCenterOfMassCalculate = 30;
		u32 m_timesCenterOfMassCalculated = 0;
		f32 m_averageCenterOfMassChange = 0;
		u32 m_showDebugInfo = 0;

		static constexpr u8 c_threadSplits = 6;
		static constexpr u8 c_padding = 80;
		static constexpr u8 c_resizeFactor = 40;

		void swapBuffers() noexcept;
		void allocBuffers() noexcept;
		void freeBuffers() noexcept;
		void processLayerInfo() noexcept;
		void calculateBoundingBoxes() noexcept;
		void processBoundingBoxesChunk(const std::vector<f32> &sourceBuffer, std::vector<BoundingBox> &out, const u32 chunk_size_h, const u32 chunk_size_v, const u32 x, const u32 y) const;
	};
}
