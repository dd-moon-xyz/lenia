#include <thrust/execution_policy.h>
#include <thrust/transform.h>
#include <thrust/transform_reduce.h>
#include "simulation.hpp"
#include "toroidal_math.hpp"
#include <cmath>
#include "cuda_runtime.h"
#include <vector>

thrust::counting_iterator idx_first(0);

__device__ f32 angleForCoordinate(const i32 coordinate, const std::size_t extent)
{
    return Lenia::TAU * (static_cast<f32>(coordinate) / static_cast<f32>(extent));
}

__device__ f32 growth(const f32 f, const f32 mu, const f32 sigma)
{
    const f32 diff = f - mu;
    return pow(max(0.0f, 1.0f - (diff * diff) / (9.0f * sigma * sigma)), 4.0f) * 2.0f - 1.0f;
}

__device__ Lenia::c64 leniastep(Lenia::c64 resultField, Lenia::c64 inv, f32 norm, f32 dt, f32 mu, f32 sigma, f32 maximumState)
{
    if (maximumState <= 0.f) return {0.f, 0.f};
    f32 val = min(max(resultField.x + dt * maximumState * growth(inv.x * norm / maximumState, mu, sigma), 0.f), maximumState);
    return {val, 0.f};
}

__global__ static void fftshiftFast(
    Lenia::c64 *resultField,
    Lenia::c64 *inv,
    i32 N,
    f32 norm,
    f32 dt,
    f32 mu,
    f32 sigma,
    f32 maximumState)
{

    i32 sEq1 = (N * N + N) / 2;
    i32 sEq2 = (N * N - N) / 2;

    i32 xIndex = blockIdx.x * blockDim.x + threadIdx.x;
    i32 yIndex = blockIdx.y * blockDim.y + threadIdx.y;

    i32 index = (yIndex * N) + xIndex;

    if (xIndex < N / 2)
    {
        if (yIndex < N / 2)
        {
            const Lenia::c64 top_left = leniastep(resultField[index], inv[index + sEq1], norm, dt, mu, sigma, maximumState);
            const Lenia::c64 bottom_right = leniastep(resultField[index + sEq1], inv[index], norm, dt, mu, sigma, maximumState);
            resultField[index] = top_left;
            resultField[index + sEq1] = bottom_right;
        }
    }
    else
    {
        if (yIndex < N / 2)
        {
            const Lenia::c64 top_right = leniastep(resultField[index], inv[index + sEq2], norm, dt, mu, sigma, maximumState);
            const Lenia::c64 bottom_left = leniastep(resultField[index + sEq2], inv[index], norm, dt, mu, sigma, maximumState);
            resultField[index] = top_right;
            resultField[index + sEq2] = bottom_left;
        }
    }
}

void Lenia::Simulation::loadFFT() noexcept
{
    glFinish();
    f32 *buffer_gpu;
    cudaMalloc(&buffer_gpu, m_size * sizeof(f32));
    cudaMemcpy(buffer_gpu, m_readBuffer.m_data.data(), m_size * sizeof(f32), cudaMemcpyHostToDevice);
    thrust::transform(
        thrust::device,
        buffer_gpu,
        buffer_gpu + m_size,
        m_fragBuffer,
        [] __device__(const f32 real)
        { return c64{real, 0}; });
    cudaFree(buffer_gpu);
}

void Lenia::Simulation::allocBuffers() noexcept
{
    const std::size_t layerCount = m_hostLayerInfoBuffer.m_data.size();
    cudaMalloc(&m_fftField, m_size * layerCount * sizeof(c64));
    cudaMalloc(&m_mulfftField, m_size * layerCount * sizeof(c64));
    cudaMalloc(&m_invfftField, m_size * layerCount * sizeof(c64));
    cudaMalloc(&m_worldResult, m_size * sizeof(c64));
}

void Lenia::Simulation::freeBuffers() noexcept
{
    cudaFree(m_fftField);
    cudaFree(m_mulfftField);
    cudaFree(m_invfftField);
    cudaFree(m_worldResult);
    if (m_persistentInitial)
    {
        cudaFree(m_persistentInitial);
        m_persistentInitial = nullptr;
    }
}

__device__ Lenia::LayerInfo Lenia::LayerInfo::operator+(const LayerInfo &rhs) const
{
    const f32 mass = m_mass + rhs.m_mass;
    const glm::vec2 centerOfMass = m_centerOfMass + rhs.m_centerOfMass;
    const glm::vec2 toroidalCosineSum = m_toroidalCosineSum + rhs.m_toroidalCosineSum;
    const glm::vec2 toroidalSineSum = m_toroidalSineSum + rhs.m_toroidalSineSum;
    const Lenia::BoundingBox bb = Lenia::BoundingBox(
        min(m_boundingBox.m_x0, rhs.m_boundingBox.m_x0),
        min(m_boundingBox.m_y0, rhs.m_boundingBox.m_y0),
        max(m_boundingBox.m_x1, rhs.m_boundingBox.m_x1),
        max(m_boundingBox.m_y1, rhs.m_boundingBox.m_y1));
    return LayerInfo{bb, centerOfMass, mass, 0u, toroidalCosineSum, toroidalSineSum};
}

struct FieldTupleToLayerInfoFunctor
{
    std::size_t width;
    std::size_t world_width;
    std::size_t world_height;
    i32 x_offset;
    i32 y_offset;

    __device__ Lenia::LayerInfo operator()(const thrust::tuple<i32, Lenia::c64> &c)
    {
        Lenia::c64 val = thrust::get<1>(c);
        if (val.x == 0.f)
        {
            return Lenia::LayerInfo{
                Lenia::BoundingBox{INT_MAX, INT_MAX, INT_MIN, INT_MIN},
                glm::vec2{0.f, 0.f},
                0.f,
                0u,
                glm::vec2{0.f, 0.f},
                glm::vec2{0.f, 0.f}};
        }

        i32 yIndex = i32(thrust::get<0>(c) / width) + y_offset;
        i32 xIndex = i32(thrust::get<0>(c) % width) + x_offset;

        const f32 xAngle = angleForCoordinate(xIndex, world_width);
        const f32 yAngle = angleForCoordinate(yIndex, world_height);
        f32 xSine, xCosine, ySine, yCosine;
        sincosf(xAngle, &xSine, &xCosine);
        sincosf(yAngle, &ySine, &yCosine);

        return Lenia::LayerInfo{
            Lenia::BoundingBox{xIndex, yIndex, xIndex, yIndex},
            glm::vec2{xIndex * val.x, yIndex * val.x},
            val.x,
            0u,
            glm::vec2{xCosine * val.x, yCosine * val.x},
            glm::vec2{xSine * val.x, ySine * val.x}};
    }
};

struct LayerInfoReducer
{
    __device__ Lenia::LayerInfo operator()(const Lenia::LayerInfo &lhs, const Lenia::LayerInfo &rhs) const
    {
        return lhs + rhs;
    }
};

void Lenia::Simulation::setPersistentBuffer(const std::vector<f32> &cells, const size_t c_w, const size_t c_h) noexcept
{
    std::vector<f32> padded(m_size, 0.f);
    for (size_t i = 0; i < c_h && i < m_h; ++i)
        for (size_t j = 0; j < c_w && j < m_w; ++j)
        {
            padded[i * m_w + j] = cells[i * c_w + j];
        }
    m_layerFlags |= LAYER_ID::WORLD;

    if (m_persistentInitial)
        cudaFree(m_persistentInitial);
    cudaMalloc(&m_persistentInitial, m_size * sizeof(c64));
    f32 *buffer_gpu;
    cudaMalloc(&buffer_gpu, m_size * sizeof(f32));
    cudaMemcpy(buffer_gpu, padded.data(), m_size * sizeof(f32), cudaMemcpyHostToDevice);
    thrust::transform(
        thrust::device,
        buffer_gpu,
        buffer_gpu + m_size,
        m_persistentInitial,
        [] __device__(const f32 real)
        { return c64{real, 0}; });
    cudaFree(buffer_gpu);

    cudaMemcpy(m_worldResult, m_persistentInitial, m_size * sizeof(c64), cudaMemcpyDeviceToDevice);
}

void Lenia::Simulation::clearPersistentBuffer() noexcept
{
    cudaMemset(m_worldResult, 0, m_size * sizeof(c64));
    m_layerFlags &= ~static_cast<u64>(LAYER_ID::WORLD);
    if (m_persistentInitial)
    {
        cudaFree(m_persistentInitial);
        m_persistentInitial = nullptr;
    }
}

void Lenia::Simulation::stepLayer(c64 *layerResult, const std::size_t fftOffset, const Lenia::c64 *animalKernel, const f32 dt, const f32 mu, const f32 sigma, const f32 maximumState) noexcept
{
    using namespace thrust::placeholders;

    cufftExecC2C(m_plan, layerResult, m_fftField + fftOffset, CUFFT_FORWARD);
    thrust::transform(
        thrust::device,
        animalKernel,
        animalKernel + m_size,
        m_fftField + fftOffset,
        m_mulfftField + fftOffset,
        _1 * _2);
    cufftExecC2C(m_plan, m_mulfftField + fftOffset, m_invfftField + fftOffset, CUFFT_INVERSE);
    fftshiftFast<<<m_blocksInGrid, m_threadsPerBlock>>>(
        layerResult,
        m_invfftField + fftOffset,
        m_w,
        m_norm,
        dt,
        mu,
        sigma,
        maximumState);
}

void Lenia::Simulation::updateFFT(const Lenia::c64 *animalKernel, const f32 dt, const f32 mu, const f32 sigma, const f32 maximumState, const f32 *envelope) noexcept
{
    using namespace thrust::placeholders;

    m_previousStepInfo = m_hostLayerInfoBuffer[layerIndex(LAYER_ID::PLAYER)];

    const std::size_t playerFFTOffset = layerIndex(LAYER_ID::PLAYER) * m_size;
    const std::size_t worldFFTOffset = layerIndex(LAYER_ID::WORLD) * m_size;

    if (m_layerFlags & LAYER_ID::WORLD)
    {
        thrust::transform(
            thrust::device,
            m_worldResult,
            m_worldResult + m_size,
            m_persistentInitial,
            m_worldResult,
            [] __device__(const c64 &current, const c64 &initial)
            { return c64{max(current.x, initial.x), 0.f}; });
    }

    if (m_layerFlags & LAYER_ID::WORLD)
    {
        stepLayer(m_worldResult, worldFFTOffset, animalKernel, dt, mu, sigma);
    }

    stepLayer(m_fragBuffer, playerFFTOffset, animalKernel, dt, mu, sigma, maximumState);

    if (m_layerFlags & LAYER_ID::WORLD)
    {
        thrust::transform(
            thrust::device,
            m_fragBuffer,
            m_fragBuffer + m_size,
            m_worldResult,
            m_fragBuffer,
            [] __device__(const c64 &player, const c64 &world)
            { return c64{max(player.x - world.x, 0.f), 0.f}; });
    }

    if (envelope) {
        thrust::transform(thrust::device, m_fragBuffer, m_fragBuffer + m_size, envelope, m_fragBuffer,
            [maximumState] __device__(const c64 &cell, const f32 limit) {
                return c64{min(cell.x, limit * maximumState), 0.f};
            });
    }

    auto zipped_begin = thrust::make_zip_iterator(thrust::make_tuple(idx_first, m_fragBuffer));
    Lenia::LayerInfo info = thrust::transform_reduce(
        thrust::device,
        zipped_begin,
        zipped_begin + m_size,
        FieldTupleToLayerInfoFunctor{m_w, m_w, m_h, 0, 0},
        Lenia::LayerInfo{
            Lenia::BoundingBox{INT_MAX, INT_MAX, INT_MIN, INT_MIN},
            glm::vec2{},
            0.f,
            0u,
            glm::vec2{},
            glm::vec2{}},
        LayerInfoReducer{});
    const glm::vec2 linearCenterOfMass = info.m_centerOfMass / (info.m_mass > 0.f ? info.m_mass : 0.001f);
    info.m_centerOfMass = glm::vec2{
        Lenia::centerOfMassFromToroidalMoments(info.m_toroidalSineSum.x, info.m_toroidalCosineSum.x, static_cast<f32>(m_w), linearCenterOfMass.x),
        Lenia::centerOfMassFromToroidalMoments(info.m_toroidalSineSum.y, info.m_toroidalCosineSum.y, static_cast<f32>(m_h), linearCenterOfMass.y)};
    m_hostLayerInfoBuffer[layerIndex(LAYER_ID::PLAYER)] = info;
    m_hostLayerInfoBuffer[layerIndex(LAYER_ID::PLAYER)].m_showDebugInfo = m_showDebugInfo;

    if (m_layerFlags & LAYER_ID::WORLD)
    {
        thrust::transform(
            thrust::device,
            m_fragBuffer,
            m_fragBuffer + m_size,
            m_worldResult,
            m_fragBuffer,
            [] __device__(const c64 &player, const c64 &world)
            { return c64{min(player.x + world.x, 1.f), 0.f}; });
    }

    cudaMemcpy(m_gpuLayerInfoBuffer, m_hostLayerInfoBuffer.m_data.data(), m_numBytesLayerData, cudaMemcpyHostToDevice);
}

void Lenia::Simulation::setShowDebugInfo(const bool showDebugInfo) noexcept
{
    m_showDebugInfo = showDebugInfo ? 1u : 0u;
    for (auto &layerInfo : m_hostLayerInfoBuffer.m_data)
    {
        layerInfo.m_showDebugInfo = m_showDebugInfo;
    }
    cudaMemcpy(m_gpuLayerInfoBuffer, m_hostLayerInfoBuffer.m_data.data(), m_numBytesLayerData, cudaMemcpyHostToDevice);
}

struct PlaceCircleFunctor
{
    std::size_t w;
    u16 x, y, radius;
    f32 value;

    __device__ Lenia::c64 operator()(const thrust::tuple<i32, Lenia::c64> &pair)
    {
        i32 yIndex = thrust::get<0>(pair) / w;
        i32 xIndex = thrust::get<0>(pair) % w;
        i32 i = xIndex - x;
        i32 j = yIndex - y;
        f32 out = thrust::get<1>(pair).x;
        if ((i * i + j * j) < radius * radius)
        {
            out *= value * value * value;
        }
        return Lenia::c64{out, 0};
    }
};

struct PlaceCellsFunctor
{
    std::size_t w, scale, c_w, c_h;
    u16 x, y;
    const std::vector<f32> &cells;
};

void Lenia::Simulation::clearCells() noexcept
{
    std::fill(m_readBuffer.m_data.begin(), m_readBuffer.m_data.end(), 0.f);
    std::fill(m_writeBuffer.m_data.begin(), m_writeBuffer.m_data.end(), 0.f);
    glNamedBufferSubData(m_readBuffer.m_ID, 0, m_readBuffer.m_data.size() * sizeof(f32), m_readBuffer.m_data.data());
    glNamedBufferSubData(m_writeBuffer.m_ID, 0, m_writeBuffer.m_data.size() * sizeof(f32), m_writeBuffer.m_data.data());
    cudaMemset(m_fragBuffer, 0, m_size * sizeof(c64));
    cudaMemset(m_worldResult, 0, m_size * sizeof(c64));
}

void Lenia::Simulation::placeCellsCircle(const u16 x, const u16 y, const u16 radius, const f32 value) noexcept
{
    thrust::counting_iterator idx_first(0);

    auto zipped_start = thrust::make_zip_iterator(thrust::make_tuple(idx_first, m_fragBuffer));
    auto zipped_end = thrust::make_zip_iterator(thrust::make_tuple(idx_first + m_size, m_fragBuffer + m_size));
    thrust::transform(
        thrust::device,
        zipped_start,
        zipped_end,
        m_fragBuffer,
        PlaceCircleFunctor{m_w, x, y, radius, value});
}
