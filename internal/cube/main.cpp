#include <iostream>
#include <chrono>
#include "lambertian.hpp"
#include "ggx.hpp"
#include "lut.hpp"

#define __STDC_LIB_EXT1__
#define STB_IMAGE_WRITE_IMPLEMENTATION

#define DEBUG true
int main(int argc, char **argv) {
    if (argc < 4) {
        std::cout << "Usage " << argv[0] << "<infile.png> <--lambertian | --ggx> <outfile.png>" << std::endl;
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
        std::cout << "Incorrect flag.\nUsage " << argv[0] << "<infile.png> <--lambertian | --ggx> <outfile.png>" << std::endl;
        return 0;
    }
    std::chrono::high_resolution_clock::time_point after = std::chrono::high_resolution_clock::now();
    if(DEBUG) std::cout << "[TIME] Execution time: " << std::chrono::duration<double>(after - before).count() << " seconds.";
}