// https://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float radical_inverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), radical_inverse_VdC(i));
}

// https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
vec3 importance_sample_ggx(vec2 xi, float roughness, vec3 normal) {
    float a = roughness * roughness;
    float phi = 2.0 * 3.14159265359 * xi.x;

    float cos_theta = sqrt( (1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y) );
    float sin_theta = sqrt( 1.0 - cos_theta * cos_theta );

    vec3 h;
    h.x = sin_theta * cos(phi);
    h.y = sin_theta * sin(phi);
    h.z = cos_theta;

    vec3 up = abs(normal.z) < .999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent_x = normalize( cross( up, normal ) );
    vec3 tangent_y = cross( normal, tangent_x );
    return normalize(tangent_x * h.x + tangent_y * h.y + normal * h.z);
}

// specular ibl and related functions -- reference s72-viewer

highp float G_1(highp float NoX, highp float k) {
    return NoX / (NoX * (1.0 - k) + k);
}

highp float G_smith(highp float roughness, highp float NoV, highp float NoL) {
	highp float k = roughness * roughness / 2.0;
	return G_1(NoL, k) * G_1(NoV, k);
}

highp float D(highp float roughness, highp float NoH) {
    highp float a = roughness * roughness;
    highp float a2 = a * a;
    highp float c = (NoH * NoH * (a2 - 1.0) + 1.0);
    return a2 / (3.141592653589 * c * c);
}
