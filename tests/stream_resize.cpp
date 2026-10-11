#include "stream_resize.hpp"
#include <iostream>
#include <vector>

int main() {
    int size, outputSize;
    if (!(std::cin >> size >> outputSize) || size < 1 || size > 1024 ||
        outputSize < 1 || outputSize > size) return 1;
    std::cin.get();
    std::vector<unsigned char> source(size * size * 3), rows(size * outputSize * 3), output(outputSize * outputSize * 3);
    if (!std::cin.read(reinterpret_cast<char*>(source.data()), source.size())) return 1;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < outputSize; ++x)
            for (int c = 0; c < 3; ++c)
                rows[(y * outputSize + x) * 3 + c] = Lenia::boxHorizontal(source.data(), size, outputSize, x, y, c);
    for (int y = 0; y < outputSize; ++y)
        for (int x = 0; x < outputSize; ++x)
            for (int c = 0; c < 3; ++c)
                output[(y * outputSize + x) * 3 + c] = Lenia::boxVertical(rows.data(), size, outputSize, x, y, c);
    std::cout.write(reinterpret_cast<char*>(output.data()), output.size());
    return std::cout ? 0 : 1;
}
