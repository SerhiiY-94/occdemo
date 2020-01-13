#include "Renderer.h"

#include <Ren/Context.h>
#include <Sys/Log.h>
#include <Sys/ThreadPool.h>
#include <Sys/Time_.h>

namespace RendererInternal {
bool bbox_test(const float p[3], const float bbox_min[3], const float bbox_max[3]);

extern const uint8_t bbox_indices[];

#if defined(__ANDROID__)
const int SHADOWMAP_WIDTH = REN_SHAD_RES_ANDROID;
#else
const int SHADOWMAP_WIDTH = REN_SHAD_RES_PC;
#endif
const int SHADOWMAP_HEIGHT = SHADOWMAP_WIDTH / 2;

// Sun shadow occupies half of atlas
const int SUN_SHADOW_RES = SHADOWMAP_WIDTH / 2;

int upper_power_of_two(int v) {
    int res = 1;
    while (res < v) {
        res *= 2;
    }
    return res;
}

const Ren::Vec2f HaltonSeq23[] = { Ren::Vec2f{ 0.0000000000f, 0.0000000000f }, Ren::Vec2f{ 0.5000000000f, 0.3333333430f },
                                   Ren::Vec2f{ 0.2500000000f, 0.6666666870f }, Ren::Vec2f{ 0.7500000000f, 0.1111111190f },
                                   Ren::Vec2f{ 0.1250000000f, 0.4444444780f }, Ren::Vec2f{ 0.6250000000f, 0.7777778510f },
                                   Ren::Vec2f{ 0.3750000000f, 0.2222222390f }, Ren::Vec2f{ 0.8750000000f, 0.5555555820f },
                                   Ren::Vec2f{ 0.0625000000f, 0.8888889550f }, Ren::Vec2f{ 0.5625000000f, 0.0370370410f },
                                   Ren::Vec2f{ 0.3125000000f, 0.3703704180f }, Ren::Vec2f{ 0.8125000000f, 0.7037037610f },
                                   Ren::Vec2f{ 0.1875000000f, 0.1481481640f }, Ren::Vec2f{ 0.6875000000f, 0.4814815220f },
                                   Ren::Vec2f{ 0.4375000000f, 0.8148149250f }, Ren::Vec2f{ 0.9375000000f, 0.2592592840f },
                                   Ren::Vec2f{ 0.0312500000f, 0.5925926570f }, Ren::Vec2f{ 0.5312500000f, 0.9259260300f },
                                   Ren::Vec2f{ 0.2812500000f, 0.0740740821f }, Ren::Vec2f{ 0.7812500000f, 0.4074074630f },
                                   Ren::Vec2f{ 0.1562500000f, 0.7407408360f }, Ren::Vec2f{ 0.6562500000f, 0.1851852090f },
                                   Ren::Vec2f{ 0.4062500000f, 0.5185185670f }, Ren::Vec2f{ 0.9062500000f, 0.8518519400f },
                                   Ren::Vec2f{ 0.0937500000f, 0.2962963280f }, Ren::Vec2f{ 0.5937500000f, 0.6296296720f },
                                   Ren::Vec2f{ 0.3437500000f, 0.9629630450f }, Ren::Vec2f{ 0.8437500000f, 0.0123456810f },
                                   Ren::Vec2f{ 0.2187500000f, 0.3456790750f }, Ren::Vec2f{ 0.7187500000f, 0.6790124770f },
                                   Ren::Vec2f{ 0.4687500000f, 0.1234568060f }, Ren::Vec2f{ 0.9687500000f, 0.4567902090f },
                                   Ren::Vec2f{ 0.0156250000f, 0.7901235820f }, Ren::Vec2f{ 0.5156250000f, 0.2345679400f },
                                   Ren::Vec2f{ 0.2656250000f, 0.5679013130f }, Ren::Vec2f{ 0.7656250000f, 0.9012346860f },
                                   Ren::Vec2f{ 0.1406250000f, 0.0493827239f }, Ren::Vec2f{ 0.6406250000f, 0.3827161190f },
                                   Ren::Vec2f{ 0.3906250000f, 0.7160494920f }, Ren::Vec2f{ 0.8906250000f, 0.1604938510f },
                                   Ren::Vec2f{ 0.0781250000f, 0.4938272240f }, Ren::Vec2f{ 0.5781250000f, 0.8271605970f },
                                   Ren::Vec2f{ 0.3281250000f, 0.2716049850f }, Ren::Vec2f{ 0.8281250000f, 0.6049383880f },
                                   Ren::Vec2f{ 0.2031250000f, 0.9382717610f }, Ren::Vec2f{ 0.7031250000f, 0.0864197686f },
                                   Ren::Vec2f{ 0.4531250000f, 0.4197531640f }, Ren::Vec2f{ 0.9531250000f, 0.7530865670f },
                                   Ren::Vec2f{ 0.0468750000f, 0.1975308950f }, Ren::Vec2f{ 0.5468750000f, 0.5308642980f },
                                   Ren::Vec2f{ 0.2968750000f, 0.8641976710f }, Ren::Vec2f{ 0.7968750000f, 0.3086420300f },
                                   Ren::Vec2f{ 0.1718750000f, 0.6419754030f }, Ren::Vec2f{ 0.6718750000f, 0.9753087760f },
                                   Ren::Vec2f{ 0.4218750000f, 0.0246913619f }, Ren::Vec2f{ 0.9218750000f, 0.3580247460f },
                                   Ren::Vec2f{ 0.1093750000f, 0.6913581490f }, Ren::Vec2f{ 0.6093750000f, 0.1358024920f },
                                   Ren::Vec2f{ 0.3593750000f, 0.4691358800f }, Ren::Vec2f{ 0.8593750000f, 0.8024692540f },
                                   Ren::Vec2f{ 0.2343750000f, 0.2469136120f }, Ren::Vec2f{ 0.7343750000f, 0.5802469850f },
                                   Ren::Vec2f{ 0.4843750000f, 0.9135804180f }, Ren::Vec2f{ 0.9843750000f, 0.0617284030f } };

float RadicalInverse_VdC(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);

    return float(bits) * 2.3283064365386963e-10f; // / 0x100000000
}

Ren::Vec2f Hammersley2D(int i, int N) {
    return Ren::Vec2f{ float(i) / float(N), RadicalInverse_VdC((uint32_t)i) };
}

Ren::Vec3f ImportanceSampleGGX(const Ren::Vec2f &Xi, float roughness, const Ren::Vec3f &N) {
    float a = roughness * roughness;

    float Phi = 2.0f * Ren::Pi<float>() * Xi[0];
    float CosTheta = std::sqrt((1.0f - Xi[1]) / (1.0f + (a * a - 1.0f) * Xi[1]));
    float SinTheta = std::sqrt(1.0f - CosTheta * CosTheta);

    auto H = Ren::Vec3f{ SinTheta * std::cos(Phi), SinTheta * std::sin(Phi), CosTheta };

    Ren::Vec3f up = std::abs(N[1]) < 0.999f ? Ren::Vec3f{ 0.0, 1.0, 0.0 } : Ren::Vec3f{ 1.0, 0.0, 0.0 };
    Ren::Vec3f TangentX = Ren::Normalize(Ren::Cross(up, N));
    Ren::Vec3f TangentY = Ren::Cross(N, TangentX);
    // Tangent to world space
    return TangentX * H[0] + TangentY * H[1] + N * H[2];
}

float GeometrySchlickGGX(float NdotV, float k) {
    float nom = NdotV;
    float denom = NdotV * (1.0f - k) + k;

    return nom / denom;
}

float GeometrySmith(const Ren::Vec3f &N, const Ren::Vec3f &V, const Ren::Vec3f &L, float k) {
    float NdotV = std::max(Ren::Dot(N, V), 0.0f);
    float NdotL = std::max(Ren::Dot(N, L), 0.0f);
    float ggx1 = GeometrySchlickGGX(NdotV, k);
    float ggx2 = GeometrySchlickGGX(NdotL, k);

    return ggx1 * ggx2;
}

float G1V_Epic(float roughness, float n_dot_v) {
    float k = roughness * roughness;
    return n_dot_v / (n_dot_v * (1.0f - k) + k);
}

float G_Smith(float roughness, float n_dot_v, float n_dot_l) {
    return G1V_Epic(roughness, n_dot_v) * G1V_Epic(roughness, n_dot_v);
}

Ren::Vec2f IntegrateBRDF(float NdotV, float roughness) {
    Ren::Vec3f V;
    V[0] = std::sqrt(1.0f - NdotV * NdotV);
    V[1] = 0.0f;
    V[2] = NdotV;

    float A = 0.0f;
    float B = 0.0f;

    auto N = Ren::Vec3f{ 0.0f, 0.0f, 1.0f };

    const int SampleCount = 1024;
    for (int i = 0; i < SampleCount; ++i) {
        Ren::Vec2f Xi = Hammersley2D(i, SampleCount);
        Ren::Vec3f H = ImportanceSampleGGX(Xi, roughness, N);
        Ren::Vec3f L = Ren::Normalize(2.0f * Ren::Dot(V, H) * H - V);

        float NdotL = std::max(L[2], 0.0f);
        float NdotH = std::max(H[2], 0.0f);
        float VdotH = std::max(Ren::Dot(V, H), 0.0f);

        if (NdotL > 0.0f) {
            float G = G1V_Epic(roughness, NdotV);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = std::pow(1.0f - VdotH, 5.0f);

            A += (1.0f - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    return Ren::Vec2f{ A, B } / float(SampleCount);
}

#include "__brdf_lut.inl"
}

#define BBOX_POINTS(min, max) \
    (min)[0], (min)[1], (min)[2],     \
    (max)[0], (min)[1], (min)[2],     \
    (min)[0], (min)[1], (max)[2],     \
    (max)[0], (min)[1], (max)[2],     \
    (min)[0], (max)[1], (min)[2],     \
    (max)[0], (max)[1], (min)[2],     \
    (min)[0], (max)[1], (max)[2],     \
    (max)[0], (max)[1], (max)[2]

Renderer::Renderer(Ren::Context &ctx, std::shared_ptr<Sys::ThreadPool> &threads)
    : ctx_(ctx), threads_(threads), shadow_splitter_(RendererInternal::SHADOWMAP_WIDTH, RendererInternal::SHADOWMAP_HEIGHT) {
    using namespace RendererInternal;

    {
        const float NEAR_CLIP = 0.5f;
        const float FAR_CLIP = 10000.0f;
        SWfloat z = FAR_CLIP / (FAR_CLIP - NEAR_CLIP) + (NEAR_CLIP - (2.0f * NEAR_CLIP)) / (0.15f * (FAR_CLIP - NEAR_CLIP));
        swCullCtxInit(&cull_ctx_, 256, 128, z);
    }

    {   // shadow map buffer
        shadow_buf_ = FrameBuf(SHADOWMAP_WIDTH, SHADOWMAP_HEIGHT, nullptr, 0, { FrameBuf::Depth16, Ren::BilinearNoMipmap }, 1, ctx.log());
    }

    {   // aux buffer which gathers frame luminance
        FrameBuf::ColorAttachmentDesc desc;
        desc.format = Ren::RawR16F;
        desc.filter = Ren::BilinearNoMipmap;
        desc.repeat = Ren::ClampToEdge;
        reduced_buf_ = FrameBuf(16, 8, &desc, 1, { FrameBuf::DepthNone }, 1, ctx.log());
    }

    {   // buffer used to sample probes
        FrameBuf::ColorAttachmentDesc desc;
        desc.format = Ren::RawRGBA32F;
        desc.filter = Ren::NoFilter;
        desc.repeat = Ren::ClampToEdge;
        probe_sample_buf_ = FrameBuf(24, 8, &desc, 1, { FrameBuf::DepthNone }, 1, ctx.log());
    }

    uint8_t black[] = { 0, 0, 0, 0 }, white[] = { 255, 255, 255, 255 };

    Ren::Texture2DParams p;
    p.w = p.h = 1;
    p.format = Ren::RawRGBA8888;
    p.filter = Ren::Bilinear;
    p.repeat = Ren::ClampToEdge;

    Ren::eTexLoadStatus status;
    dummy_black_ = ctx_.LoadTexture2D("dummy_black", black, sizeof(black), p, &status);
    assert(status == Ren::TexCreatedFromData);

    dummy_white_ = ctx_.LoadTexture2D("dummy_white", white, sizeof(white), p, &status);
    assert(status == Ren::TexCreatedFromData);

    p.w = p.h = 8;
    p.format = Ren::RawRG32F;
    p.filter = Ren::NoFilter;
    rand2d_8x8_ = ctx_.LoadTexture2D("rand2d_8x8", &HaltonSeq23[0], sizeof(HaltonSeq23), p, &status);
    assert(status == Ren::TexCreatedFromData);

    {   
#if 0
        const int res = 256;
        std::string str;

        str += "static const uint32_t __brdf_lut_res = " + std::to_string(res) + ";\n";
        str += "static const uint16_t __brdf_lut[] = {\n";

        for (int j = 0; j < res; j++) {
            str += '\t';
            for (int i = 0; i < res; i++) {
                Ren::Vec2f val = IntegrateBRDF((float(i) + 0.5f) / res, (float(j) + 0.5f) / res);

                uint16_t r = (uint16_t)std::min(std::max(int(val[0] * 65535), 0), 65535);
                uint16_t g = (uint16_t)std::min(std::max(int(val[1] * 65535), 0), 65535);

                //uint8_t r = (uint8_t)std::min(std::max(int(val[0] * 255), 0), 255);
                //uint8_t g = (uint8_t)std::min(std::max(int(val[1] * 255), 0), 255);

                str += std::to_string(r) + ", " + std::to_string(g) + ", ";
            }
            str += '\n';
        }

        str += "};\n";
#endif

        p.w = p.h = RendererInternal::__brdf_lut_res;
        p.format = Ren::RawRG16U;
        p.filter = Ren::BilinearNoMipmap;
        brdf_lut_ = ctx_.LoadTexture2D("brdf_lut", &RendererInternal::__brdf_lut[0], sizeof(__brdf_lut), p, &status);
        assert(status == Ren::TexCreatedFromData);
    }

    // Compile built-in shaders etc.
    InitRendererInternal();

    temp_sub_frustums_.realloc(REN_CELLS_COUNT);
    temp_sub_frustums_.count = REN_CELLS_COUNT;

    decals_boxes_.realloc(REN_MAX_DECALS_TOTAL);
    litem_to_lsource_.realloc(REN_MAX_LIGHTS_TOTAL);
    ditem_to_decal_.realloc(REN_MAX_DECALS_TOTAL);
    allocated_shadow_regions_.realloc(REN_MAX_SHADOWMAPS_TOTAL);

    for (int i = 0; i < 2; i++) {
        temp_sort_spans_32_[i].realloc(REN_MAX_SHADOW_BATCHES);
        temp_sort_spans_64_[i].realloc(REN_MAX_MAIN_BATCHES);
    }
}

Renderer::~Renderer() {
    DestroyRendererInternal();
    swCullCtxDestroy(&cull_ctx_);
}

void Renderer::PrepareDrawList(const SceneData &scene, const Ren::Camera &cam, DrawList &list) {
    GatherDrawables(scene, cam, list);
}

void Renderer::ExecuteDrawList(const DrawList &list, const FrameBuf *target) {
    using namespace RendererInternal;

    const int cur_scr_w = ctx_.w(), cur_scr_h = ctx_.h();
    Ren::ILog *log = ctx_.log();

    if (!cur_scr_w || !cur_scr_h) {
        // view is minimized?
        return;
    }

    uint64_t gpu_draw_start = 0;
    if (list.render_flags & DebugTimings) {
        gpu_draw_start = GetGpuTimeBlockingUs();
    }
    uint64_t cpu_draw_start_us = Sys::GetTimeUs();
    
    if (cur_scr_w != scr_w_ || cur_scr_h != scr_h_) {
        {   // Main buffer for raw frame before tonemapping
            FrameBuf::ColorAttachmentDesc desc[3];
            {   // Main color
#if (REN_OIT_MODE == REN_OIT_WEIGHTED_BLENDED) || (REN_OIT_MODE == REN_OIT_MOMENT_BASED && REN_OIT_MOMENT_RENORMALIZE)
                // renormalization requires buffer with alpha channel
                desc[0].format = Ren::RawRGBA16F;
#else
                desc[0].format = Ren::RawRG11F_B10F;
#endif
                desc[0].filter = Ren::NoFilter;
                desc[0].repeat = Ren::ClampToEdge;
            }
            {   // 4-component world-space normal (alpha is 'ssr' flag)
                desc[1].format = Ren::RawRGB10_A2;
                desc[1].filter = Ren::NoFilter;
                desc[1].repeat = Ren::ClampToEdge;
            }
            {   // 4-component specular (alpha is roughness)
                desc[2].format = Ren::RawRGBA8888;
                desc[2].filter = Ren::NoFilter;
                desc[2].repeat = Ren::ClampToEdge;
            }
            clean_buf_ = FrameBuf(cur_scr_w, cur_scr_h, desc, 3, { FrameBuf::Depth24, Ren::NoFilter }, 4, log);
        }

#if (REN_OIT_MODE == REN_OIT_MOMENT_BASED)
        {   // Buffer that holds moments (used for transparency)
            FrameBuf::ColorAttachmentDesc desc[3];
            {   // b0
                desc[0].format = Ren::RawR32F;
                desc[0].filter = Ren::NoFilter;
                desc[0].repeat = Ren::ClampToEdge;
            }
            {   // z and z^2
                desc[1].format = Ren::RawRG16F;
                desc[1].filter = Ren::NoFilter;
                desc[1].repeat = Ren::ClampToEdge;
            }
            {   // z^3 and z^4
                desc[2].format = Ren::RawRG16F;
                desc[2].filter = Ren::NoFilter;
                desc[2].repeat = Ren::ClampToEdge;
            }
            moments_buf_ = FrameBuf(cur_scr_w, cur_scr_h, desc, 3, { FrameBuf::DepthNone }, clean_buf_.sample_count, log);
        }
#endif

        {   // Buffer that holds resolved color
            FrameBuf::ColorAttachmentDesc desc;
            desc.format = Ren::RawRG11F_B10F;
            desc.filter = Ren::NoFilter;
            desc.repeat = Ren::ClampToEdge;
            resolved_or_transparent_buf_ = FrameBuf(clean_buf_.w, clean_buf_.h, &desc, 1, { FrameBuf::DepthNone }, 1, log);
        }

        {   // Buffer that holds downsampled linear depth
            FrameBuf::ColorAttachmentDesc desc;
            desc.format = Ren::RawR32F;
            desc.filter = Ren::NoFilter;
            desc.repeat = Ren::ClampToEdge;
            down_depth_ = FrameBuf(clean_buf_.w / 2, clean_buf_.h / 2, &desc, 1, { FrameBuf::DepthNone }, 1, log);
        }

        {   // Buffer that holds tonemapped ldr frame before fxaa applied
            FrameBuf::ColorAttachmentDesc desc;
            desc.format = Ren::RawRGB888;
            desc.filter = Ren::BilinearNoMipmap;
            desc.repeat = Ren::ClampToEdge;
            combined_buf_ = FrameBuf(clean_buf_.w, clean_buf_.h, &desc, 1, { FrameBuf::DepthNone }, 1, log);
        }
        {   // Buffer for SSAO
            FrameBuf::ColorAttachmentDesc desc;
            desc.format = Ren::RawR8;
            desc.filter = Ren::BilinearNoMipmap;
            desc.repeat = Ren::ClampToEdge;
            ssao_buf1_ = FrameBuf(clean_buf_.w / 2, clean_buf_.h / 2, &desc, 1, { FrameBuf::DepthNone }, 1, log);
            ssao_buf2_ = FrameBuf(clean_buf_.w / 2, clean_buf_.h / 2, &desc, 1, { FrameBuf::DepthNone }, 1, log);
        }
        {   // Auxilary buffer for reflections (rg - uvs, b - influence)
            FrameBuf::ColorAttachmentDesc desc;
            desc.format = Ren::RawRGB10_A2;
            desc.filter = Ren::BilinearNoMipmap;
            desc.repeat = Ren::ClampToEdge;
            ssr_buf1_ = FrameBuf(clean_buf_.w / 2, clean_buf_.h / 2, &desc, 1, { FrameBuf::DepthNone }, 1, log);
            ssr_buf2_ = FrameBuf(clean_buf_.w / 2, clean_buf_.h / 2, &desc, 1, { FrameBuf::DepthNone }, 1, log);
        }
        {   // Buffer that holds previous frame (used for SSR)
            FrameBuf::ColorAttachmentDesc desc;
            desc.format = Ren::RawRG11F_B10F;
            desc.filter = Ren::BilinearNoMipmap;
            desc.repeat = Ren::ClampToEdge;
            down_buf_ = FrameBuf(clean_buf_.w / 4, clean_buf_.h / 4, &desc, 1, { FrameBuf::DepthNone }, 1, log);
        }
        {   // Auxilary buffers for bloom effect
            FrameBuf::ColorAttachmentDesc desc;
            desc.format = Ren::RawRG11F_B10F;
            desc.filter = Ren::BilinearNoMipmap;
            desc.repeat = Ren::ClampToEdge;
            blur_buf1_ = FrameBuf(clean_buf_.w / 4, clean_buf_.h / 4, &desc, 1, { FrameBuf::DepthNone }, 1, log);
            blur_buf2_ = FrameBuf(clean_buf_.w / 4, clean_buf_.h / 4, &desc, 1, { FrameBuf::DepthNone }, 1, log);
        }

        // Memory consumption for FullHD frame (except clean_buf_):
        // resolved_buf1_   : ~7.91 Mb
        // down_depth_      : ~1.97 Mb
        // combined_buf_    : ~5.93 Mb
        // ssao_buf1_       : ~0.49 Mb
        // ssao_buf2_       : ~0.49 Mb
        // ssr_buf1_        : ~1.97 Mb
        // ssr_buf2_        : ~1.97 Mb
        // down_buf_        : ~0.49 Mb
        // blur_buf1_       : ~0.49 Mb
        // blur_buf2_       : ~0.49 Mb
        // Total            : ~22.2 Mb

        bool init_success = InitFramebuffersInternal();
        if (init_success) {
            scr_w_ = cur_scr_w;
            scr_h_ = cur_scr_h;
            log->Info("Successfully resized to %ix%i", scr_w_, scr_h_);
        } else {
            log->Error("InitFramebuffersInternal failed, frame will not be drawn!\n");
            return;
        }
    }

    if (!target) {
        // TODO: Change actual resolution dynamically
#if defined(__ANDROID__)
        act_w_ = int((float)scr_w_ * 0.4f);
        act_h_ = int((float)scr_h_ * 0.6f);
#else
        act_w_ = int((float)scr_w_ * 1.0f);
        act_h_ = int((float)scr_h_ * 1.0f);
#endif
    } else {
        act_w_ = target->w;
        act_h_ = target->h;
        assert(act_w_ <= scr_w_ && act_h_ <= scr_h_);
    }

    DrawObjectsInternal(list, target);
    
    uint64_t cpu_draw_end_us = Sys::GetTimeUs();

    // store values for current frame
    backend_cpu_start_ = cpu_draw_start_us;
    backend_cpu_end_ = cpu_draw_end_us;
    backend_time_diff_ = int64_t(gpu_draw_start) - int64_t(backend_cpu_start_);
    frame_counter_++;
}

#undef BBOX_POINTS