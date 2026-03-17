vec3 tm_reinhard(vec3 color) {
    return color / (1.0 + color);
}

vec3 tm_green(vec3 color) {
    return color * vec3(.4, 1.0, .2);
}

vec3 do_tonemap(vec3 color, int type) {
    if (type == 0) { // linear
        return color;
    } else if (type == 1) { // reinhard
        return tm_reinhard(color);
    } else if (type == 2) { // green
        return tm_green(color);
    }

    return color;
}