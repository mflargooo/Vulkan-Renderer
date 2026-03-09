
void specular_ibl( highp vec3 specular_color, highp float roughness, highp vec3 N, highp vec3 V ) {
    highp vec3 specular_lighting = vec3(0.0);
    const highp uint num_samples = 64u;

    highp vec2 shift = vec2(0.0);
    highp uint ix = uint(gl_FragCoord.x);
    highp uint iy = uint(gl_FragCoord.y);   

    for (highp uint i = 0u; i < num_samples; ++i) {
        highp vec2 xi = hammersley(i, num_samples);

        xi = fract(xi + shift);

        highp vec3 H = importance_sample_ggx(xi, roughness, N);
        highp vec3 L = 2.0 * dot( V, H ) * H - V;

        highp float NoV = clamp(dot(N, V), 0.0, 1.0);
        highp float NoL = clamp(dot(N, L), 0.0, 1.0);
        highp float NoH = clamp(dot(N, H), 0.0, 1.0);
        highp float VoH = clamp(dot(V, H), 0.0, 1.0);

        if (NoV > 0.0 && NoL > 0.0 && NoH > 0.0 && VoH > 0.0) {
            highp float p = NoH * D(roughness, NoH) / (4.0 * VoH);
            int size = imageSize(inFace, 0).x;
            hightp float l = .5 * log2(3.0 * float(size)*float(size)) - .5 * log2(float(num_samples) * p);
            l += .25;

            if (roughness < .01) l *= (roughness / .01);

            highp vec3 sample_color = imageLod(inFace, L, l).rgb;

            highp float G = G_smith(roughness, NoV, NoL);
            highp float Fc = pow(1.0 - VoH, 5.0);
            highp float F = (1.0 - Fc) * specular_color + Fc;

            specular_lighting += sample_color * F * G * VoH / (NoH * NoV); 
        }
    }

    return specular_lighting / float(num_samples);
}

void main() {
    int out_x = int(gl_GlobalInvocationID.x);
    int out_y = int(gl_GlobalInvocationID.y);
    ivec2 out_size = imageSize(outFace);
    if (out_x >= out_size.x || out_y >= out_size.y) return;

    vec3 R = normalize(WORLD_FROM_OUT_PX * vec3(out_x + .5, out_y + .5, 1.0));
    
    // prefilter per https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
    vec3 N = R;
    vec3 V = R;

    mat3 IN_PX_FROM_WORLD = inverse(WORLD_FROM_IN_PX);
    ivec2 in_size = imageSize(inFace);
    
    vec3 color = specular_ibl()

    vec3 final_color = weight > 0.0 ? prefiltered_color / weight : vec3(1.0);
    imageStore(outFace, ivec2(out_x, out_y), vec4(final_color, 1.0));
}