#include <iostream>
#include <chrono>

#include "lambertian.hpp"
#include "ggx.hpp"
#include "lut.hpp"

#define DEBUG true
int main(int argc, char **argv) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <infile.png> <--lambertian | --ggx | --lut> <outfile.png> " << std::endl;
        std::cout << "  - <infile.png> specified only with --lambertian or --ggx modes." << std::endl;
        return 0;
    }

    std::chrono::high_resolution_clock::time_point before = std::chrono::high_resolution_clock::now();
    if (std::string(argv[2]).compare("--lambertian") == 0) {
        lambertian(argv[1], argv[3]);
    } else if (std::string(argv[2]).compare("--ggx") == 0) {
        ggx(argv[1], argv[3]);
    } else if (std::string(argv[1]).compare("--lut") == 0) {
        lut(argv[2]);
    } else {
        std::cout << "Incorrect flags." << std::endl;
        std::cout << "Usage: " << argv[0] << " <infile.png> <--lambertian | --ggx | --lut> <outfile.png> " << std::endl;
        std::cout << "  - <infile.png> specified only with --lambertian or --ggx modes." << std::endl;
        return 0;
    }
    std::chrono::high_resolution_clock::time_point after = std::chrono::high_resolution_clock::now();
    if(DEBUG) std::cout << "[TIME] Execution time: " << std::chrono::duration<double>(after - before).count() << " seconds.";
}