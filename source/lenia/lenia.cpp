#include "engine.hpp"
#include "stream_arena.hpp"
#include "stream_hexapod.hpp"
#include <iostream>

#define SEARCH_DEBUG

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--stream-hexapod-preview") return Lenia::streamHexapod(true);
    if (argc == 2 && std::string(argv[1]) == "--stream-hexapod") return Lenia::streamHexapod();
    if (argc == 2 && std::string(argv[1]) == "--stream-arena") return Lenia::streamArena();
    if (argc == 2 && (std::string(argv[1]) == "--stream" || std::string(argv[1]) == "--stream-centered")) {
        u32 size, animalIdx, scale, organisms;
        f32 dt;
        if (!(std::cin >> size >> animalIdx >> scale >> dt >> organisms) ||
            size < 256 || size > 1024 || scale < 1 || scale > 10 || organisms < 1 || organisms > 32 ||
            !std::isfinite(dt) || dt <= 0.f || dt > 0.2f) {
            std::cerr << "Invalid stream configuration" << std::endl;
            return 1;
        }

        std::vector<glm::uvec2> positions(organisms);
        for (auto& position : positions) {
            if (!(std::cin >> position.x >> position.y) || position.x >= size || position.y >= size) {
                std::cerr << "Invalid organism position" << std::endl;
                return 1;
            }
        }

        int devices = 0;
        if (cudaGetDeviceCount(&devices) != cudaSuccess || devices == 0) {
            std::cerr << "Streaming requires an available CUDA GPU" << std::endl;
            return 1;
        }

        Lenia::Engine engine(size, size, size, size, static_cast<u8>(scale), dt, true);
        if (animalIdx >= engine.getAnimalInfo().size() ||
            2 * engine.getAnimalInfo()[animalIdx].m_r * scale + 1 > size) {
            std::cerr << "Invalid organism type or kernel size" << std::endl;
            return 1;
        }

        engine.configureStream(animalIdx, positions, std::string(argv[1]) == "--stream-centered");
        std::cout << "READY\n" << std::flush;
        char command;
        while (std::cin >> command && command == 'n' && engine.shouldRun()) {
            const auto frame = engine.streamFrame();
            if (std::string(argv[1]) == "--stream-centered") {
                const auto direction = engine.streamDirection();
                const f32 heading[] = {direction.x, direction.y, engine.streamMass()};
                std::cout.write(reinterpret_cast<const char*>(heading), sizeof(heading));
            }
            std::cout.write(reinterpret_cast<const char*>(frame.data()), frame.size());
            std::cout.flush();
        }
        return 0;
    }

    Lenia::Engine engine(1600, 900, 4096, 4096, 10, 0.1f);
    while (engine.shouldRun())
        engine.update();
    return 0;
}
