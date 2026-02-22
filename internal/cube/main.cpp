#include "integration.hpp"
#include <cstdint>

#define STB_IMAGE_IMPLEMENTATION
#include "../../external/stb/stb_image.h"

int main (int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s <infile> --lambertian <outfile>", argv[0]);
    }

    const char *infile = argv[1];
    // std::string material_type = argv[2];
    //const char *outfile = argv[3];

    int x, y, c;
    void *img = (void *)stbi_load(infile, &x, &y, &c, 4);

    size_t ox, oy;
    integrate(img, (size_t)x, (size_t)y, &ox, &oy);

    return 0;
}