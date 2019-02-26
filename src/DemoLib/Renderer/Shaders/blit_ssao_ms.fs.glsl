R"(
#version 310 es

#ifdef GL_ES
    precision mediump float;
#endif

layout(binding = 0) uniform mediump sampler2DMS depth_texture;

layout(location = 0) uniform vec2 uScreenSize;
layout(location = 1) uniform vec4 uClipInfo;

in vec2 aVertexUVs_;

out vec4 outColor;

float LinearDepthTexelFetch(ivec2 hit_pixel) {
    float depth = texelFetch(depth_texture, hit_pixel, 0).r;
    return uClipInfo[0] / (depth * (uClipInfo[1] - uClipInfo[2]) + uClipInfo[2]);
}

float BilinearSampleDepthTexel(vec2 texcoord) {
    ivec2 coord = ivec2(floor(texcoord));

    float texel00 = texelFetch(depth_texture, coord + ivec2(0, 0), 0).r;
    float texel10 = texelFetch(depth_texture, coord + ivec2(1, 0), 0).r;
    float texel11 = texelFetch(depth_texture, coord + ivec2(1, 1), 0).r;
    float texel01 = texelFetch(depth_texture, coord + ivec2(0, 1), 0).r;
            
    vec2 sample_coord = fract(texcoord.xy);
            
    float texel0 = mix(texel00, texel01, sample_coord.y);
    float texel1 = mix(texel10, texel11, sample_coord.y);
            
    float depth = mix(texel0, texel1, sample_coord.x);
    return uClipInfo[0] / (depth * uClipInfo[1] + uClipInfo[2]);
}

int hash(int x) {
    x += ( x << 10 );
    x ^= ( x >>  6 );
    x += ( x <<  3 );
    x ^= ( x >> 11 );
    x += ( x << 15 );
    return x;
}

int hash(ivec2 v) { return hash(v.x ^ hash(v.y)); }

void main() {
    const mat2 transforms[8] = mat2[8](
        mat2(1.0, 0.0, 0.0, 1.0),
        mat2(0.70710, 0.70710, -0.70710, 0.70710),
        mat2(0.0, 1.0, -1.0, 0.0),
        mat2(-0.70710, 0.70710, -0.70710, -0.70710),
        mat2(0.92388, 0.382683, -0.382683, 0.92388),
        mat2(0.390335, 0.920673, -0.920673, 0.390335),
        mat2(-0.372479, 0.928041, -0.928041, -0.372479),
        mat2(-0.91451, 0.404562, -0.404562, -0.91451)
    );

    const vec2 sample_points[3] = vec2[3](
        vec2(1.0/8.0, 0.0),
        vec2(3.0/8.0, 0.0),
        vec2(6.0/8.0, 0.0)
    );

    const float sphere_widths[3] = float[3](
        0.99215, 0.92702, 0.66144
    );

    const float fadeout_start = 16.0;
    const float fadeout_end = 64.0;

    float depth = BilinearSampleDepthTexel(aVertexUVs_);
    if (depth > fadeout_end) {
        outColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    float initial_radius = 0.2;
    float ss_radius = min(initial_radius / depth, 0.1);

    const float sample_weight = 1.0 / 7.0;
    float occlusion = 0.5 * sample_weight;

    for (int i = 0; i < 3; i++) {
        //int c = (int(gl_FragCoord.x + gl_FragCoord.y)) % 8;
        int c = (hash(ivec2(gl_FragCoord.xy)) + i) % 8;
        vec2 sample_point = transforms[c] * sample_points[i];

        vec2 depth_values = vec2(BilinearSampleDepthTexel(aVertexUVs_ + ss_radius * sample_point * uScreenSize),
                                 BilinearSampleDepthTexel(aVertexUVs_ - ss_radius * sample_point * uScreenSize));
        float sphere_width = initial_radius * sphere_widths[i];

        vec2 depth_diff = vec2(depth) - depth_values;

        vec2 occ_values = clamp(0.5 * depth_diff / sphere_width + vec2(0.5), vec2(0.0), vec2(1.0));

        const float max_dist = 1.0;
        vec2 dist_mod = clamp((vec2(max_dist) - depth_diff) / max_dist, vec2(0.0), vec2(1.0));

        vec2 mod_cont = mix(mix(vec2(0.5), 1.0 - occ_values.yx, dist_mod.yx),
                            occ_values.xy, dist_mod.xy);
        
        occlusion += sample_weight * mod_cont.x;
        occlusion += sample_weight * mod_cont.y;
    }

    occlusion = clamp(1.0 - 2.0 * (occlusion - 0.5), 0.0, 1.0);

    // smooth fadeout
    float k = max((depth - fadeout_start) / (fadeout_end - fadeout_start), 0.0);
    occlusion = mix(occlusion, 1.0, k);

    outColor = vec4(occlusion, occlusion, occlusion, 1.0);
}
)"