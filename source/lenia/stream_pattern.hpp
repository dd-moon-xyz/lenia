#pragma once

#include <array>
#include <vector>

namespace Lenia {
    inline bool scatteredRingPattern(std::array<unsigned int, 16384> mask) {
        unsigned int components = 0;
        for (int index = 0; index < 16384; ++index) {
            if (!mask[index]) continue;
            std::vector<int> pending{index};
            mask[index] = 0;
            unsigned int area = 0;
            while (!pending.empty()) {
                const int point = pending.back();
                pending.pop_back();
                ++area;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int x = point % 128 + dx, y = point / 128 + dy;
                        if (x < 0 || x >= 128 || y < 0 || y >= 128) continue;
                        const int neighbor = y * 128 + x;
                        if (!mask[neighbor]) continue;
                        mask[neighbor] = 0;
                        pending.push_back(neighbor);
                    }
                }
            }
            if (area >= 3 && ++components >= 20) return true;
        }
        return false;
    }
}
