#include "app.hpp"

int main()
{
    try {
        FluidApp app{};
        app.run();
    } catch (const std::exception& e) {
        spdlog::error(e.what());
    }
}
