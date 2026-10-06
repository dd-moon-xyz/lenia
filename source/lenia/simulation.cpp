#include "simulation.hpp"
#include "toroidal_math.hpp"
#include "glm/glm.hpp"
#include "glm/vec2.hpp"
#include <functional>
#include <execution>
#include <iostream>
#include <cmath>

namespace {
	glm::vec2 normalizeOrZero(const glm::vec2& value) noexcept {
		const f32 length = glm::length(value);
		if (length <= 0.f) {
			return glm::vec2{0.f, 0.f};
		}

		return value / length;
	}
}


Lenia::Simulation::Simulation(const size_t w, const size_t h, const size_t scale, const std::size_t layer_count = 1):
	m_w(w),
	m_h(h),
	m_size(w * h),
	m_scale(scale),
	m_norm(1.f / (f32)m_size),
	m_readBuffer(Buffer<f32>(BufferBinding::READ, w * h * 2 * layer_count)),
	m_writeBuffer(Buffer<f32>(BufferBinding::WRITE, w * h * 2 * layer_count)),
	m_hostLayerInfoBuffer(Buffer<LayerInfo>(BufferBinding::DATA, layer_count)),
	m_layerCount(layer_count),
	m_layerFlags(static_cast<u64>(LAYER_ID::PLAYER)),
	m_threadsPerBlock(32, 32),
	m_blocksInGrid((m_w + m_threadsPerBlock.x - 1) / m_threadsPerBlock.x, (m_h + m_threadsPerBlock.y - 1) / m_threadsPerBlock.y) {

	cudaGraphicsGLRegisterBuffer(&m_cudaLayerDataResource, m_hostLayerInfoBuffer.m_ID, cudaGraphicsRegisterFlagsNone);
	cudaGraphicsGLRegisterBuffer(&m_cudaGraphicsResource, m_readBuffer.m_ID, cudaGraphicsRegisterFlagsNone);

	cudaGraphicsResource* resources[] = { m_cudaLayerDataResource, m_cudaGraphicsResource };
	cudaGraphicsMapResources(2, resources, 0);

	cudaGraphicsResourceGetMappedPointer((void**)&m_gpuLayerInfoBuffer, &m_numBytesLayerData, m_cudaLayerDataResource);
	cudaGraphicsResourceGetMappedPointer((void**)&m_fragBuffer, &m_numBytesField, m_cudaGraphicsResource);
	cufftPlan2d(&m_plan, m_w, m_h, CUFFT_C2C);
	allocBuffers();
	glNamedBufferSubData(m_readBuffer.m_ID, 0, m_readBuffer.m_data.size() * sizeof(f32), m_readBuffer.m_data.data());
	glNamedBufferSubData(m_writeBuffer.m_ID, 0, m_writeBuffer.m_data.size() * sizeof(f32), m_writeBuffer.m_data.data());
}

Lenia::Simulation::~Simulation() noexcept {
	cudaGraphicsUnmapResources(1, &m_cudaGraphicsResource);
	cudaGraphicsUnmapResources(1, &m_cudaLayerDataResource);
	cudaGraphicsUnregisterResource(m_cudaGraphicsResource);
	cudaGraphicsUnregisterResource(m_cudaLayerDataResource);
	cufftDestroy(m_plan);
	freeBuffers();
}



void Lenia::Simulation::placeCells(const std::vector<f32> &cells, const size_t c_w, const size_t c_h, const u32 x, const u32 y) noexcept {
	for (size_t i = 0; i < c_h; i++)
	for (size_t j = 0; j < c_w; j++)
	for (size_t k = 0; k < m_scale; k++)
	for (size_t l = 0; l < m_scale; l++) {
		m_readBuffer[(y + i * m_scale + k) % m_h * m_w + (x + j * m_scale + l) % m_w] = cells[i * c_w + j];
	}
	glNamedBufferSubData(m_readBuffer.m_ID, 0, m_readBuffer.m_data.size() * sizeof(f32), m_readBuffer.m_data.data());
}

void Lenia::Simulation::processLayerInfo() noexcept {
	m_mass = m_hostLayerInfoBuffer[layerIndex(LAYER_ID::PLAYER)].m_mass;
 	m_massDelta = m_hostLayerInfoBuffer[layerIndex(LAYER_ID::PLAYER)].m_mass - m_previousStepInfo.m_mass;
	m_centerOfMass = m_hostLayerInfoBuffer[layerIndex(LAYER_ID::PLAYER)].m_centerOfMass;
	const glm::vec2 movement = Lenia::shortestToroidalDelta(m_previousStepInfo.m_centerOfMass, m_centerOfMass, static_cast<f32>(m_w), static_cast<f32>(m_h));
	m_direction = normalizeOrZero(movement);
}

void Lenia::Simulation::update(const Animal &animal, const f32 dt, const f32 maximumState) noexcept {
	updateFFT(thrust::raw_pointer_cast(animal.m_GPUfftKernel.data()), dt, animal.m_info.m_mu, animal.m_info.m_sigma, maximumState);
	processLayerInfo();
}

void Lenia::Simulation::bindField() const noexcept {
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_readBuffer.m_ID);
}



void Lenia::Simulation::swapBuffers() noexcept {
	m_readBuffer.m_binding = static_cast<BufferBinding>(1 - static_cast<i8>(m_readBuffer.m_binding));
	m_writeBuffer.m_binding = static_cast<BufferBinding>(1 - static_cast<i8>(m_writeBuffer.m_binding));
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(m_readBuffer.m_binding), m_writeBuffer.m_ID);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(m_writeBuffer.m_binding), m_readBuffer.m_ID);
}


size_t Lenia::Simulation::getNBoundingBoxes() const noexcept {
	return m_hostLayerInfoBuffer.m_data.size();
}

f32 Lenia::Simulation::calcAreaComputed() const noexcept {
	// f32 areaBB;
	// return f32(m_w * m_h) / areaBB;
	return 0;
}

f32 Lenia::Simulation::getMoveScalar(const glm::vec2 dest) const noexcept {
	const glm::vec2 forward_norm = normalizeOrZero(m_direction);
	const glm::vec2 to_dest = normalizeOrZero(Lenia::shortestToroidalDelta(m_centerOfMass, dest, static_cast<f32>(m_w), static_cast<f32>(m_h)));

	if (glm::length(forward_norm) <= 0.f || glm::length(to_dest) <= 0.f) {
		return 0.f;
	}

    const f32 dot = glm::dot(forward_norm, to_dest);
    const f32 cross = forward_norm.x * to_dest.y - forward_norm.y * to_dest.x;

    const f32 angle = std::atan2(cross, dot); 
	const f32 res = angle / 3.1415f;
    return res;
}


// void Lenia::Simulation::processBoundingBoxesChunk(const std::vector<f32> &sourceBuffer, std::vector<BoundingBox> &out, const u32 chunk_size_h, const u32 chunk_size_v, const u32 x, const u32 y) const {
// 	for (u32 i = y; i < y + chunk_size_h; ++i) 
// 	for (u32 j = x; j < x + chunk_size_v; ++j) {
// 		b8 new_point = true;
// 		for (const auto& box : out) {
// 			if (box.contains(j, i, m_w, m_h)) {
// 				new_point = false;
// 				break;
// 			}
// 		}
// 		if (new_point && sourceBuffer[i * m_w + j]) {
// 			out.emplace_back(j - c_padding / 2, i - c_padding / 2, j + c_padding, i + c_padding);
// 		}
// 	}
// 	for (auto& box : out) {
// 		box.resize(c_resizeFactor);
// 	}
// }


// void Lenia::Simulation::calculateBoundingBoxes() noexcept {
// 	auto start = std::chrono::high_resolution_clock::now();

// 	m_boundingBoxBuffer.m_data.clear();
// 	const u16 chunk_size_h = m_w / c_threadSplits;
// 	const u16 chunk_size_v = m_h / c_threadSplits;

// 	Buffer<f32>* const current_buffer = (m_readBuffer.m_binding == BufferBinding::WRITE) 
//                            ? &m_readBuffer 
//                            : &m_writeBuffer;
// 	current_buffer->loadDataFromShader();

// 	std::vector<BoundingBox> in_box_vectors[getNChunks()];
// 	auto tasks = std::vector<std::function<void()>>();

// 	for (i32 row = 0; row < c_threadSplits; ++row)
// 	for (i32 col = 0; col < c_threadSplits; ++col) {
// 		tasks.push_back([&, row, col]() {
// 			std::vector<BoundingBox>& boundingBoxes = in_box_vectors[row * c_threadSplits + col];
// 			processBoundingBoxesChunk(current_buffer->m_data, boundingBoxes, chunk_size_h, chunk_size_v, row * chunk_size_h, col * chunk_size_v);
// 		});
// 	}

// 	std::for_each(std::execution::par_unseq, tasks.begin(), tasks.end(), [](std::function<void()>& task) { task(); });
// 	size_t totalSize = 0;
//     for (const auto &vec : in_box_vectors) {
//         totalSize += vec.size();
//     }
// 	m_boundingBoxBuffer.m_data.reserve(totalSize);
// 	for (const auto &vec : in_box_vectors) {
//         m_boundingBoxBuffer.m_data.insert(m_boundingBoxBuffer.m_data.end(), vec.begin(), vec.end());
//     }
// 	m_updateTimeBoxes = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start);
// 	// for (size_t i = 0; i < m_boundingBoxBuffer.m_data.size() - 1; ++i) {
// 	// 	if (m_boundingBoxBuffer[i].overlap(m_boundingBoxBuffer[i + 1]) > 0.9f) {
			
// 	// 	}
// 	// }
// }
