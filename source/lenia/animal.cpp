#include "animal.hpp"
#include <cmath>

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

#undef min
#undef max

void Lenia::Animal::resize(const u8 scale) {
	m_scale = scale;
	m_kernelBuffer.m_data.resize((2 * m_info.m_r * scale + 1) * (2 * m_info.m_r * scale + 1));
	computeKernel();
	computeCellTexture();
}

std::vector<f32> Lenia::Animal::getCells() const noexcept {
	auto str = m_info.m_rle.begin();
	u32 count = 0, num = 0, w = m_info.m_w;
	auto buffer = std::vector<f32>(m_info.m_w * m_info.m_h, 0.f); 
	u32 row = 0, col = 0;
	while (str != m_info.m_rle.end() && *str != '!') {
		count = num = 0;
		if (*str == '$') {
			if (col != w)
				std::fill(buffer.begin() + (row * w + col), buffer.begin() + (row * w + w - 1), 0);
			col = 0;
			str++;
			row++;
		}
		while (isdigit(*str))
			count = count * 10 + (*str++ - '0');
		if (*str >= 'p' && *str <= 'y') {
			num = (*str - 'p') * 24 + *(str + 1) - 'A' + 25;
			str++;
		}
		else if (*str == 'o') {
			num = 255;
		}
		else if (*str == '.' || *str == 'b') {
			num = 0;
		}
		else if (*str >= 'A' && *str <= 'X') {
			num = *str - 'A' + 1;
		}
		count = count ? count : 1;
		for (u32 i = 0; i < count; i++)
			buffer[row * w + col++] = num / 255.f;
		str++;
	}
	return buffer;
}

f32 Lenia::Animal::applyKernelCore(const f32 r, const f32 q) const noexcept {
	switch (m_info.m_kn) {
		default:
		case KernelCore::QUAD4:
			return (r > 0.f) * (r < 1.f) * (f32)pow(4.f * r * (1.f - r), 4.f);
		case KernelCore::BUMP4:
			return (r > 0.f) * (r < 1.f) * (f32)exp(4.f - 1.f / (r * (1.f - r)));
		case KernelCore::STPZ:
			return f32(r >= q) * (r <= 1.f - q);
		case KernelCore::LEAK:
			return (r > 0.f) * (r < 1.f) * (f32)exp(-(f32)pow(((r - .5f) / .15f), 2.f) / 2.f);
	}
}

f32 Lenia::Animal::applyGrowthFunction(const f32 n) const noexcept {
	switch (m_info.m_gn) {
		default:
		case GrowthFunction::QUAD4:
			return (f32)pow(std::max(0.f, 1.f - (f32)pow(n - m_info.m_mu, 2.f) / (9.f * m_info.m_sigma * m_info.m_sigma)), 4.f) * 2.f - 1.f;
		case GrowthFunction::GAUSS:
			return (f32)exp(-(f32)pow(n - m_info.m_mu, 2.f) / (2.f * m_info.m_sigma * m_info.m_sigma)) * 2.f - 1.f;
		case GrowthFunction::STPZ:
			return std::fabs(n - m_info.m_mu) * 2.f - 1.f;
	}
}

f32 Lenia::Animal::applyKernelShell(const f32 r) const noexcept {
	const f32 Br = m_info.m_beta.size() * (r / (f32)(m_info.m_r * m_scale));
	const i32 floored = std::min(static_cast<i32>(floor(Br)), static_cast<i32>(m_info.m_beta.size() - 1));
	const f32 Kc = applyKernelCore(std::min(fmodf(Br, 1.0), 1.f));
	return (r < (m_info.m_r * m_scale)) * m_info.m_beta[floored] * Kc;
}

void Lenia::Animal::computeNormalization() noexcept {
	m_normalization = 0;
	i16 iR = (i16)(m_info.m_r * m_scale);
	for (i16 i = -iR; i <= iR; i++)
	for (i16 j = -iR; j <= iR; j++) {
		f32 dist = (f32)sqrt(i * i + j * j);
		if (!dist || dist > (f32)(m_info.m_r * m_scale)) continue;
		m_normalization += applyKernelShell(dist);
	}
	m_normalization *= m_info.m_dx2;
}


void Lenia::Animal::computeKernel() noexcept {
	computeNormalization();
	const size_t r = m_info.m_r * m_scale;
	const size_t w = 2 * r + 1;
	for (size_t i = 0; i <= r; ++i)
	for (size_t j = 0; j <= r; ++j) {
		f32 kernel_shell = applyKernelShell((f32)sqrt(i * i + j * j)) / m_normalization;
		m_kernelBuffer[(r + i) * w + (r + j)] = kernel_shell;
		m_kernelBuffer[(r + i) * w + (r - j)] = kernel_shell;
		m_kernelBuffer[(r - i) * w + (r + j)] = kernel_shell;
		m_kernelBuffer[(r - i) * w + (r - j)] = kernel_shell;
	};
	constexpr GLint mask[] = {GL_RED, GL_RED, GL_RED, GL_ONE};
	Lenia::createTexture(&m_kernelTexture, m_kernelBuffer.m_data.data(), w, w, mask);
	m_kernelBuffer.storeDataInShader();
}

void Lenia::Animal::computeCellTexture() noexcept {
	const std::vector<f32> cells = getCells();
	std::vector<f32> upscaled_cells = std::vector<f32>(m_info.m_w * m_scale * m_info.m_h * m_scale);
	for (size_t i = 0; i < m_info.m_h; ++i)
	for (size_t j = 0; j < m_info.m_w; ++j)
	for (size_t k = 0; k < m_scale; ++k)
	for (size_t l = 0; l < m_scale; ++l) {
		const size_t index = (i * m_scale + k) * m_info.m_w * m_scale + (j * m_scale + l);
		upscaled_cells[index] = cells[i * m_info.m_w + j];
	}
	const GLint mask[] = {GL_RED, GL_RED, GL_RED, GL_RED};
	Lenia::createTexture(&m_cellTexture, upscaled_cells.data(), m_info.m_w * m_scale, m_info.m_h * m_scale, mask);
}

void Lenia::Animal::computePadded(const std::size_t w) noexcept {
	computePaddedKernel(w);
	computeFFTKernel(w);
}

void Lenia::Animal::computePaddedKernel(const std::size_t w) noexcept {
	m_paddedKernel.assign(w * w, 0.0f);
	m_absfftKernel.resize(w * w);

	const i32 R = static_cast<i32>(m_info.m_r * m_scale);
	const i32 old_w = 2 * R + 1;
	const i32 start_w = static_cast<i32>(w) / 2 - R;

	for (i32 i = 0; i < old_w; ++i) {
        for (i32 j = 0; j < old_w; ++j) {
            i32 old_idx = i * old_w + j;
            i32 new_i = start_w + i;
            i32 new_j = start_w + j;
            i32 new_idx = new_i * static_cast<i32>(w) + new_j;
            m_paddedKernel[new_idx] = m_kernelBuffer[old_idx];
        }
    }

	const GLint mask[] = { GL_RED, GL_RED, GL_RED, GL_RED };
	Lenia::createTexture(&m_paddedKernelTexture, m_paddedKernel.data(), w, w, mask);
}

std::string Lenia::Taxonomy::to_string() const noexcept {
	return "\n species: " + species + "\n  class: " + _class + "\n  order: " + order + "\n  family: " + family + "\n  subfamily: " + subfamily;
}
