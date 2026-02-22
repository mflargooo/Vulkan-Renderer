#pragma once

#include <iostream>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "../../rgbe.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

static void texcoords_from_index(size_t i, size_t width, size_t height, float *cx, float *cy) {
    size_t s = i % width;
    size_t t = i / width;

    *cx = 2.f * (s + .5f) / width - 1.f;
    *cy = 2.f * (t + .5f) / height - 1.f;
};

// 0: +x, 1: -x, 2: +y, 3: -y, 4: +z, 5: -z
static glm::vec3 direction_from_texcoords(float cx, float cy, size_t face) {
    glm::vec3 dir;
    if (face == 0) { // +x
        dir = glm::vec3(1.f, -cy, -cx); // rotated cw 90
    } else if (face == 1) { // -x
        dir = glm::vec3(-1.f, -cy, cx); // rotated ccw 90
    } else if (face == 2) { // +y
        dir = glm::vec3(cx, 1.f, cy); // already up-right
    } else if (face == 3) { // -y
        dir = glm::vec3(cx, -1.f, -cy); // rotated 180 deg
    } else if (face == 4) { // +z
        dir = glm::vec3(cx, -cy, 1.f);
    } else if (face == 5) { // -z
        dir = glm::vec3(cx, -cy, -1.f);
    }

    return glm::normalize(dir);
}

// assuming image in 0-255 range with 4 channels; may generalize later
// expects in +x, -x, +y, -y, +z, -z order, per s72 convention
// equiv to left, right, front, back, top, bottom
// size of data is width * height
// outsize is x * y;
static std::vector< uint32_t > integrate_rgbe(void const *data, size_t width, size_t height, size_t *out_width, size_t *out_height) {
    size_t face_width = width;
    size_t face_height = height / 6;

    // static size, for now
    constexpr size_t out_face_width = 16, out_face_height = 16;
    *out_width = out_face_width;
    *out_height = out_face_height * 6;
    std::vector< glm::vec3 > out(out_face_width * out_face_height * 6, glm::vec3(0.f)); 
    
    float in_energy = 0.f;
    for (size_t i = 0; i < width * height; ++i) {
        // get linear pixel data
        uint32_t rgbe = static_cast<uint32_t const *>(data)[i];
        glm::vec3 rgb = linear_from_rgbe(glm::u8vec4(
             rgbe        & 0xff,
            (rgbe >>  8) & 0xff,
            (rgbe >> 16) & 0xff,
            (rgbe >> 24) & 0xff
        ));

        size_t f = i / (face_width * face_height); // get face (0: +x, 1: -x, 2: +y, 3: -y, 4: +z, 5: -z)
        size_t p = i % (face_width * face_height); // pth pixel in fth face

        // compute direction given s72 tl origin convention

        // inspiration: https://www.rorydriscoll.com/wp-content/uploads/2012/01/TexelProjection.png

        // we push a unit cube to a unit sphere:
        // want to map each p to 2d coordinates on unit square -> 3d coordinates on corresponding plane
        // then normalize to get direction

        // unflattened coords (s,t) of pth pixel in fth face
        float cx, cy;
        texcoords_from_index(p, face_width, face_height, &cx, &cy);
        glm::vec3 dir = direction_from_texcoords(cx, cy, f);

        // https://www.activision.com/cdn/research/paper_egsr.pdf
        float diff_solid_angle = 4.f / std::pow((cx * cx + cy * cy + 1.f), 1.5f) / (float)(face_width * face_height);

        for (size_t j = 0; j < (*out_width) * (*out_height); ++j) {
            size_t of = j / (out_face_width * out_face_height); // get face (0: +x, 1: -x, 2: +y, 3: -y, 4: +z, 5: -z)
            size_t op = j % (out_face_width * out_face_height); // pth pixel in fth face
            
            float ocx, ocy;
            texcoords_from_index(op, out_face_width, out_face_height, &ocx, &ocy);
            glm::vec3 odir = direction_from_texcoords(ocx, ocy, of);
            
            float cos_weight = glm::dot(dir, odir);

            out[j] += rgb * std::max(0.f, cos_weight) * diff_solid_angle / (float)M_PI;
        }

        in_energy += (rgb.r + rgb.g + rgb.b) * diff_solid_angle;
    }

    if (/*debug*/ true) {
        float out_energy = 0.f;
        for (size_t f = 0; f < 6; ++f) {
            for (size_t p = 0; p < out_face_width * out_face_height; ++p) {
                glm::vec3 pix = out[f * out_face_width * out_face_height + p];

                float ocx, ocy;
                texcoords_from_index(p, out_face_width, out_face_height, &ocx, &ocy);

                float odiff_solid_angle = 4.f / pow(ocx * ocx + ocy * ocy + 1.f, 1.5f) / (out_face_width * out_face_height);
                out_energy += (pix.r + pix.g + pix.b) * odiff_solid_angle;
            }
        }
        std::cout << "[DEBUG] in energy " << in_energy << ", out energy " << out_energy << std::endl;
    }

    std::vector< uint32_t > rgbe_out((*out_width) * (*out_height));
    for (size_t i = 0; i < (*out_width) * (*out_height); ++i) {
        glm::u8vec4 rgbe = rgbe_from_linear(out[i]);
        rgbe_out[i] = 
            (((uint32_t) (rgbe.a & 0xff)) << 24) | 
            (((uint32_t) (rgbe.b & 0xff)) << 16) |
            (((uint32_t) (rgbe.g & 0xff)) <<  8) |
            (((uint32_t) (rgbe.r & 0xff))      ) ;
    }

    return std::move(rgbe_out);
};