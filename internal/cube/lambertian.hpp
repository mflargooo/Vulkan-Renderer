#pragma once

#include <cstdint>
#include "integration.hpp"

#include "../../external/stb/stb_image.h"

#include "../../external/stb/stb_image_write.h"


void lambertian (const char *infile, const char *outfile) {
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
}