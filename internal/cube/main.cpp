#include "integration.hpp"
#include <cstdint>

#define STB_IMAGE_IMPLEMENTATION
#include "../../external/stb/stb_image.h"

#define __STDC_LIB_EXT1__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../external/stb/stb_image_write.h"

int main (int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s <infile> --lambertian <outfile>", argv[0]);
    }

    const char *infile = argv[1];
    // std::string material_type = argv[2];
    const char *outfile = argv[3];

    int x, y, c;
    void *img = (void *)stbi_load(infile, &x, &y, &c, 4);
    if (!img) {
        std::cerr << "Failed to load " << infile << std::endl;
        exit(1);
    }

    size_t ox, oy;
    auto integrated = integrate_rgbe(img, (size_t)x, (size_t)y, &ox, &oy);
    void const *lamb = integrated.data();

    if (!stbi_write_png(outfile, (int)ox, (int)oy, c, lamb, (int)(ox * sizeof(uint32_t)))) {
        std::cerr << "Failed to save to " << outfile << std::endl;
        exit(1);
    }

    return 0;
}