#pragma once

// RGB box filtering shared by CUDA kernels and the CPU reference tests.
#ifdef __CUDACC__
#define LENIA_RESIZE_HD __host__ __device__
#else
#define LENIA_RESIZE_HD
#endif

namespace Lenia {
    LENIA_RESIZE_HD inline int boxEdge(int pixel, int size, int outputSize) {
        return (2 * pixel * size + outputSize) / (2 * outputSize);
    }

    LENIA_RESIZE_HD inline unsigned char boxHorizontal(
        const unsigned char* source, int size, int outputSize, int x, int y, int channel
    ) {
        const int first = boxEdge(x, size, outputSize);
        const int last = boxEdge(x + 1, size, outputSize);
        unsigned int sum = 0;
        for (int column = first; column < last; ++column)
            sum += source[(y * size + column) * 3 + channel];
        const int count = last - first;
        return static_cast<unsigned char>((sum + count / 2) / count);
    }

    LENIA_RESIZE_HD inline unsigned char boxVertical(
        const unsigned char* rows, int size, int outputSize, int x, int y, int channel
    ) {
        // Filter in logical top-down coordinates, retaining bottom-up wire rows.
        const int top = outputSize - 1 - y;
        const int first = boxEdge(top, size, outputSize);
        const int last = boxEdge(top + 1, size, outputSize);
        unsigned int sum = 0;
        for (int row = first; row < last; ++row)
            sum += rows[((size - 1 - row) * outputSize + x) * 3 + channel];
        const int count = last - first;
        return static_cast<unsigned char>((sum + count / 2) / count);
    }
}

#undef LENIA_RESIZE_HD
