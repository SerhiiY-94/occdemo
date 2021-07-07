#include "SceneManager.h"

#include <cassert>
#include <fstream>
#include <functional>
#include <map>

#include <Ren/Context.h>
#include <Ren/Utils.h>
#include <Snd/Context.h>
#include <Sys/AssetFile.h>
#include <Sys/AssetFileIO.h>
#include <Sys/Json.h>
#include <Sys/MemBuf.h>
#include <Sys/Time_.h>

#include "TexUpdateFileBuf.h"

#include <vtune/ittnotify.h>
extern __itt_domain *__g_itt_domain;

#ifdef _MSC_VER
#include <intrin.h>

#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_bittestandcomplement)
#endif

#include "../Utils/Load.h"
#include "../Utils/ShaderLoader.h"

namespace SceneManagerConstants {
const float NEAR_CLIP = 0.1f;
const float FAR_CLIP = 10000.0f;

#if defined(__ANDROID__)
const char *MODELS_PATH = "./assets/models/";
const char *TEXTURES_PATH = "./assets/textures/";
const char *MATERIALS_PATH = "./assets/materials/";
const char *SHADERS_PATH = "./assets/shaders/";
#else
const char *MODELS_PATH = "./assets_pc/models/";
const char *TEXTURES_PATH = "./assets_pc/textures/";
const char *MATERIALS_PATH = "./assets_pc/materials/";
const char *SHADERS_PATH = "./assets_pc/shaders/";
#endif

extern const int DECALS_ATLAS_RESX = 4096, DECALS_ATLAS_RESY = 2048;
extern const int LIGHTMAP_ATLAS_RESX = 2048, LIGHTMAP_ATLAS_RESY = 1024;

const int PROBE_RES = 512;
const int PROBE_COUNT = 16;

__itt_string_handle *itt_load_scene_str = __itt_string_handle_create("SceneManager::LoadScene");
__itt_string_handle *itt_serve_str = __itt_string_handle_create("SceneManager::Serve");
__itt_string_handle *itt_on_loaded_str = __itt_string_handle_create("SceneManager::OnTextureDataLoaded");
} // namespace SceneManagerConstants

namespace SceneManagerInternal {
std::unique_ptr<uint8_t[]> Decode_KTX_ASTC(const uint8_t *image_data, int data_size, int &width, int &height);

template <typename T> class DefaultCompStorage : public CompStorage {
    Ren::SparseArray<T> data_;

  public:
    const char *name() const override { return T::name(); }

    uint32_t Create() override { return data_.emplace(); }
    void Delete(const uint32_t i) override { data_.erase(i); }

    const void *Get(uint32_t i) const override { return data_.GetOrNull(i); }
    void *Get(uint32_t i) override { return data_.GetOrNull(i); }

    uint32_t First() const override { return data_.empty() ? 0xffffffff : data_.cbegin().index(); }

    uint32_t Next(uint32_t i) const override {
        auto it = data_.citer_at(i);
        ++it;
        return (it == data_.cend()) ? 0xffffffff : it.index();
    }

    int Count() const override { return (int)data_.size(); }

    void ReadFromJs(const JsObjectP &js_obj, void *comp) override { T::Read(js_obj, *(T *)comp); }

    void WriteToJs(const void *comp, JsObjectP &js_obj) const override { T::Write(*(T *)comp, js_obj); }

    const void *SequentialData() const override { return data_.data(); }
    void *SequentialData() override { return data_.data(); }
};

// bit scan forward
long GetFirstBit(long mask) {
#ifdef _MSC_VER
    unsigned long ret;
    _BitScanForward(&ret, (unsigned long)mask);
    return long(ret);
#else
    return long(__builtin_ffsl(mask) - 1);
#endif
}

// bit test and complement
long ClearBit(long mask, long index) {
#ifdef _MSC_VER
    _bittestandcomplement(&mask, index);
    return mask;
#else
    return (mask & ~(1 << index));
#endif
}

#include "__cam_rig.inl"
} // namespace SceneManagerInternal

SceneManager::SceneManager(Ren::Context &ren_ctx, ShaderLoader &sh, Snd::Context &snd_ctx,
                           Ray::RendererBase &ray_renderer, Sys::ThreadPool &threads)
    : ren_ctx_(ren_ctx), sh_(sh), snd_ctx_(snd_ctx), ray_renderer_(ray_renderer), threads_(threads),
      cam_(Ren::Vec3f{0.0f, 0.0f, 1.0f}, Ren::Vec3f{0.0f, 0.0f, 0.0f}, Ren::Vec3f{0.0f, 1.0f, 0.0f}),
      mp_alloc_(32, 512) {
    using namespace SceneManagerConstants;
    using namespace SceneManagerInternal;

    { // Alloc texture for decals atlas
        const Ren::eTexFormat formats[] = {Ren::DefaultCompressedRGBA, Ren::eTexFormat::Undefined};
        const uint32_t flags[] = {0};
        scene_data_.decals_atlas = Ren::TextureAtlas{DECALS_ATLAS_RESX,          DECALS_ATLAS_RESY, 64, formats, flags,
                                                     Ren::eTexFilter::Trilinear, ren_ctx_.log()};
    }

    { // Create splitter for lightmap atlas
        scene_data_.lm_splitter = Ren::TextureSplitter(SceneManagerConstants::LIGHTMAP_ATLAS_RESX,
                                                       SceneManagerConstants::LIGHTMAP_ATLAS_RESY);
    }

    { // Allocate cubemap array
        scene_data_.probe_storage.Resize(Ren::DefaultCompressedRGBA, PROBE_RES, PROBE_COUNT, ren_ctx_.log());
    }

    { // Register default components
        using namespace std::placeholders;

        default_comp_storage_[CompTransform].reset(new DefaultCompStorage<Transform>);
        RegisterComponent(CompTransform, default_comp_storage_[CompTransform].get(), nullptr);

        default_comp_storage_[CompDrawable].reset(new DefaultCompStorage<Drawable>);
        RegisterComponent(CompDrawable, default_comp_storage_[CompDrawable].get(),
                          std::bind(&SceneManager::PostloadDrawable, this, _1, _2, _3));

        default_comp_storage_[CompOccluder].reset(new DefaultCompStorage<Occluder>);
        RegisterComponent(CompOccluder, default_comp_storage_[CompOccluder].get(),
                          std::bind(&SceneManager::PostloadOccluder, this, _1, _2, _3));

        default_comp_storage_[CompLightmap].reset(new DefaultCompStorage<Lightmap>);
        RegisterComponent(CompLightmap, default_comp_storage_[CompLightmap].get(),
                          std::bind(&SceneManager::PostloadLightmap, this, _1, _2, _3));

        default_comp_storage_[CompLightSource].reset(new DefaultCompStorage<LightSource>);
        RegisterComponent(CompLightSource, default_comp_storage_[CompLightSource].get(),
                          std::bind(&SceneManager::PostloadLightSource, this, _1, _2, _3));

        default_comp_storage_[CompDecal].reset(new DefaultCompStorage<Decal>);
        RegisterComponent(CompDecal, default_comp_storage_[CompDecal].get(),
                          std::bind(&SceneManager::PostloadDecal, this, _1, _2, _3));

        default_comp_storage_[CompProbe].reset(new DefaultCompStorage<LightProbe>);
        RegisterComponent(CompProbe, default_comp_storage_[CompProbe].get(),
                          std::bind(&SceneManager::PostloadLightProbe, this, _1, _2, _3));

        default_comp_storage_[CompAnimState].reset(new DefaultCompStorage<AnimState>);
        RegisterComponent(CompAnimState, default_comp_storage_[CompAnimState].get(), nullptr);

        default_comp_storage_[CompVegState].reset(new DefaultCompStorage<VegState>);
        RegisterComponent(CompVegState, default_comp_storage_[CompVegState].get(), nullptr);

        default_comp_storage_[CompSoundSource].reset(new DefaultCompStorage<SoundSource>);
        RegisterComponent(CompSoundSource, default_comp_storage_[CompSoundSource].get(),
                          std::bind(&SceneManager::PostloadSoundSource, this, _1, _2, _3));

        default_comp_storage_[CompPhysics].reset(new DefaultCompStorage<Physics>);
        RegisterComponent(CompPhysics, default_comp_storage_[CompPhysics].get(), nullptr);
    }

    Sys::MemBuf buf{__cam_rig_mesh, size_t(__cam_rig_mesh_size)};
    std::istream in_mesh(&buf);

    Ren::eMeshLoadStatus status;
    cam_rig_ = ren_ctx.LoadMesh(
        "__cam_rig", &in_mesh,
        [this](const char *name) -> Ren::MaterialRef {
            Ren::eMatLoadStatus status;
            return ren_ctx_.LoadMaterial(name, nullptr, &status, nullptr, nullptr, nullptr);
        },
        &status);
    assert(status == Ren::eMeshLoadStatus::CreatedFromData);

    requested_textures_.reserve(262144);
    finished_textures_.reserve(262144);

    for (int i = 0; i < MaxSimultaneousRequests; i++) {
        // io_pending_tex_[i].buf.reset(new Sys::DefaultFileReadBuf);
        io_pending_tex_[i].buf.reset(new TextureUpdateFileBuf(ren_ctx_.api_ctx()));
    }

    const float pos[] = {0.0f, 0.0f, 0.0f};
    amb_sound_.Init(1.0f, pos);

    StartTextureLoader();
}

SceneManager::~SceneManager() {
    StopTextureLoader();
    ClearScene();
}

void SceneManager::RegisterComponent(uint32_t index, CompStorage *storage,
                                     const std::function<PostLoadFunc> &post_init) {
    scene_data_.comp_store[index] = storage;
    component_post_load_[index] = post_init;
}

void SceneManager::LoadScene(const JsObjectP &js_scene) {
    using namespace SceneManagerConstants;

    __itt_task_begin(__g_itt_domain, __itt_null, __itt_null, itt_load_scene_str);

    Ren::ILog *log = ren_ctx_.log();

    log->Info("SceneManager: Loading scene!");
    if (!scene_data_.objects.empty()) {
        StopTextureLoader();
        ClearScene();
        StartTextureLoader();
    }

    std::map<std::string, Ren::Vec4f> decals_textures;

    if (js_scene.Has("name")) {
        const JsStringP &js_name = js_scene.at("name").as_str();
        scene_data_.name = Ren::String{js_name.val.c_str()};
    } else {
        throw std::runtime_error("Level has no name!");
    }

    scene_texture_load_counter_ = 0;

    { // load lightmaps
        std::string lm_base_tex_name = "lightmaps/";
        lm_base_tex_name += scene_data_.name.c_str();

        const char tex_ext[] =
#if !defined(__ANDROID__)
            ".dds";
#else
            ".ktx";
#endif

        std::string lm_direct_tex_name = lm_base_tex_name;
        lm_direct_tex_name += "_lm_direct";
        lm_direct_tex_name += tex_ext;

        std::string lm_indir_tex_name = lm_base_tex_name;
        lm_indir_tex_name += "_lm_indirect";
        lm_indir_tex_name += tex_ext;

        const uint8_t default_l0_color[] = {255, 255, 255, 255};
        scene_data_.env.lm_direct = OnLoadTexture(lm_direct_tex_name.c_str(), default_l0_color, 0);
        // scene_data_.env.lm_indir = OnLoadTexture(lm_indir_tex_name.c_str(), 0);
        for (int sh_l = 0; sh_l < 4; sh_l++) {
            std::string lm_indir_sh_tex_name = lm_base_tex_name;
            lm_indir_sh_tex_name += "_lm_sh_";
            lm_indir_sh_tex_name += std::to_string(sh_l);
            lm_indir_sh_tex_name += tex_ext;

            const uint8_t default_l1_color[] = {0, 0, 0, 0};
            scene_data_.env.lm_indir_sh[sh_l] =
                OnLoadTexture(lm_indir_sh_tex_name.c_str(), default_l1_color, Ren::TexNoRepeat);
        }
    }

    const JsArrayP &js_objects = js_scene.at("objects").as_arr();
    for (const JsElementP &js_elem : js_objects.elements) {
        const JsObjectP &js_obj = js_elem.as_obj();

        scene_data_.objects.emplace_back();
        SceneObject &obj = scene_data_.objects.back();

        Ren::Vec3f obj_bbox[2] = {Ren::Vec3f{std::numeric_limits<float>::max()},
                                  Ren::Vec3f{std::numeric_limits<float>::lowest()}};

        for (const auto &js_comp : js_obj.elements) {
            if (js_comp.second.type() != JsType::Object) {
                continue;
            }
            const JsObjectP &js_comp_obj = js_comp.second.as_obj();
            const auto &js_comp_name = js_comp.first;

            for (unsigned i = 0; i < MAX_COMPONENT_TYPES; i++) {
                CompStorage *store = scene_data_.comp_store[i];
                if (!store) {
                    continue;
                }

                if (js_comp_name == store->name()) {
                    const uint32_t index = store->Create();

                    void *new_component = store->Get(index);
                    store->ReadFromJs(js_comp_obj, new_component);

                    obj.components[i] = index;
                    obj.comp_mask |= (1u << i);

                    if (component_post_load_[i]) {
                        component_post_load_[i](js_comp_obj, new_component, obj_bbox);
                    }

                    break;
                }
            }
        }

        auto *tr = (Transform *)scene_data_.comp_store[CompTransform]->Get(obj.components[CompTransform]);
        tr->bbox_min = obj_bbox[0];
        tr->bbox_max = obj_bbox[1];
        tr->UpdateBBox();

        if (js_obj.Has("name")) {
            const JsStringP &js_name = js_obj.at("name").as_str();
            obj.name = Ren::String{js_name.val.c_str()};
            scene_data_.name_to_object[obj.name] = uint32_t(scene_data_.objects.size() - 1);
        }
    }

    if (js_scene.Has("environment")) {
        const JsObjectP &js_env = js_scene.at("environment").as_obj();
        if (js_env.Has("sun_dir")) {
            const JsArrayP &js_dir = js_env.at("sun_dir").as_arr();

            const double x = js_dir.at(0).as_num().val, y = js_dir.at(1).as_num().val, z = js_dir.at(2).as_num().val;

            scene_data_.env.sun_dir = Ren::Vec3f{float(x), float(y), float(z)};
            scene_data_.env.sun_dir = -Ren::Normalize(scene_data_.env.sun_dir);
        }
        if (js_env.Has("sun_col")) {
            const JsArrayP &js_col = js_env.at("sun_col").as_arr();

            const double r = js_col.at(0).as_num().val, g = js_col.at(1).as_num().val, b = js_col.at(2).as_num().val;

            scene_data_.env.sun_col = Ren::Vec3f{float(r), float(g), float(b)};
        }
        if (js_env.Has("sun_softness")) {
            const JsNumber &js_sun_softness = js_env.at("sun_softness").as_num();
            scene_data_.env.sun_softness = float(js_sun_softness.val);
        }
        if (js_env.Has("env_map")) {
            const JsStringP &js_env_map = js_env.at("env_map").as_str();

            scene_data_.env.env_map_name = Ren::String{js_env_map.val.c_str()};

            const std::string tex_names[6] = {
#if !defined(__ANDROID__)
                std::string(TEXTURES_PATH) + js_env_map.val.c_str() + "_PX.dds",
                std::string(TEXTURES_PATH) + js_env_map.val.c_str() + "_NX.dds",
                std::string(TEXTURES_PATH) + js_env_map.val.c_str() + "_PY.dds",
                std::string(TEXTURES_PATH) + js_env_map.val.c_str() + "_NY.dds",
                std::string(TEXTURES_PATH) + js_env_map.val.c_str() + "_PZ.dds",
                std::string(TEXTURES_PATH) + js_env_map.val.c_str() + "_NZ.dds"
#else
                std::string(TEXTURES_PATH) + js_env_map.val.c_str() + "_PX.ktx",
                std::string(TEXTURES_PATH) + js_env_map.val.c_str() + "_NX.ktx",
                std::string(TEXTURES_PATH) + js_env_map.val.c_str() + "_PY.ktx",
                std::string(TEXTURES_PATH) + js_env_map.val.c_str() + "_NY.ktx",
                std::string(TEXTURES_PATH) + js_env_map.val.c_str() + "_PZ.ktx",
                std::string(TEXTURES_PATH) + js_env_map.val.c_str() + "_NZ.ktx"
#endif
            };

            std::vector<uint8_t> tex_data[6];
            const void *data[6];
            int size[6];
            int res = 0;

            for (int i = 0; i < 6; i++) {
                Sys::AssetFile in_file(tex_names[i]);
                size_t in_file_size = in_file.size();

                tex_data[i].resize(in_file_size);
                in_file.Read((char *)&tex_data[i][0], in_file_size);

#if !defined(__ANDROID__)
                Ren::DDSHeader header;
                memcpy(&header, &tex_data[i][0], sizeof(Ren::DDSHeader));

                const int w = int(header.dwWidth), h = int(header.dwHeight);

                assert(w == h);
                res = w;
#else

#endif

                data[i] = (const void *)&tex_data[i][0];
                size[i] = int(tex_data[i].size());
            }

            Ren::Tex2DParams p;
            p.w = res;
            p.h = res;
            p.format = Ren::DefaultCompressedRGBA;
            p.sampling.filter = Ren::eTexFilter::Bilinear;
            p.sampling.repeat = Ren::eTexRepeat::ClampToEdge;

            const std::string tex_name = std::string(js_env_map.val.c_str()) +
#if !defined(__ANDROID__)
                                         "_*.dds";
#else
                                         "_*.ktx";
#endif

            Ren::eTexLoadStatus load_status;
            scene_data_.env.env_map =
                ren_ctx_.LoadTextureCube(tex_name.c_str(), data, size, p, ren_ctx_.default_stage_bufs(),
                                         ren_ctx_.default_mem_allocs(), &load_status);
        }
        if (js_env.Has("env_map_pt")) {
            scene_data_.env.env_map_name_pt = Ren::String{js_env.at("env_map_pt").as_str().val.c_str()};
        }
        if (js_env.Has("sun_shadow_bias")) {
            const JsArrayP &js_sun_shadow_bias = js_env.at("sun_shadow_bias").as_arr();
            scene_data_.env.sun_shadow_bias[0] = float(js_sun_shadow_bias.at(0).as_num().val);
            scene_data_.env.sun_shadow_bias[1] = float(js_sun_shadow_bias.at(1).as_num().val);
        } else {
            scene_data_.env.sun_shadow_bias[0] = 4.0f;
            scene_data_.env.sun_shadow_bias[1] = 8.0f;
        }
    } else {
        scene_data_.env = {};
    }

    LoadProbeCache();

    scene_data_.decals_atlas.Finalize();

    log->Info("SceneManager: RebuildBVH!");

    RebuildBVH();
    RebuildMaterialTextureGraph();

    __itt_task_end(__g_itt_domain);
}

void SceneManager::SaveScene(JsObjectP &js_scene) {
    // write name
    js_scene.Push("name", JsStringP(scene_data_.name.c_str(), mp_alloc_));

    { // write environment
        JsObjectP js_env(mp_alloc_);

        { // write sun direction
            JsArrayP js_sun_dir(mp_alloc_);
            js_sun_dir.Push(JsNumber(-scene_data_.env.sun_dir[0]));
            js_sun_dir.Push(JsNumber(-scene_data_.env.sun_dir[1]));
            js_sun_dir.Push(JsNumber(-scene_data_.env.sun_dir[2]));

            js_env.Push("sun_dir", std::move(js_sun_dir));
        }

        { // write sun color
            JsArrayP js_sun_col(mp_alloc_);
            js_sun_col.Push(JsNumber(scene_data_.env.sun_col[0]));
            js_sun_col.Push(JsNumber(scene_data_.env.sun_col[1]));
            js_sun_col.Push(JsNumber(scene_data_.env.sun_col[2]));

            js_env.Push("sun_col", std::move(js_sun_col));
        }

        { // write sun softness
            js_env.Push("sun_softness", JsNumber(scene_data_.env.sun_softness));
        }

        { // write env map names
            js_env.Push("env_map", JsStringP{scene_data_.env.env_map_name.c_str(), mp_alloc_});
            js_env.Push("env_map_pt", JsStringP{scene_data_.env.env_map_name_pt.c_str(), mp_alloc_});
        }

        js_scene.Push("environment", std::move(js_env));
    }

    { // write objects
        JsArrayP js_objects(mp_alloc_);

        const CompStorage *const *comp_storage = scene_data_.comp_store;

        for (const SceneObject &obj : scene_data_.objects) {
            JsObjectP js_obj(mp_alloc_);

            for (unsigned i = 0; i < MAX_COMPONENT_TYPES; i++) {
                if (obj.comp_mask & (1u << i)) {
                    const uint32_t comp_id = obj.components[i];
                    const void *p_comp = comp_storage[i]->Get(comp_id);

                    JsObjectP js_comp(mp_alloc_);
                    comp_storage[i]->WriteToJs(p_comp, js_comp);

                    js_obj.Push(comp_storage[i]->name(), std::move(js_comp));
                }
            }

            js_objects.Push(std::move(js_obj));
        }

        js_scene.Push("objects", std::move(js_objects));
    }
}

void SceneManager::ClearScene() {
    using namespace SceneManagerInternal;

    scene_data_.name = {};

    ren_ctx_.default_vertex_buf1()->Print(ren_ctx_.log());

    for (auto &obj : scene_data_.objects) {
        while (obj.comp_mask) {
            const long i = GetFirstBit(obj.comp_mask);
            obj.comp_mask = ClearBit(obj.comp_mask, i);

            scene_data_.comp_store[i]->Delete(obj.components[i]);
        }
    }

    for (int i = 0; i < MAX_COMPONENT_TYPES; i++) {
        if (scene_data_.comp_store[i]) {
            assert(scene_data_.comp_store[i]->Count() == 0);
        }
    }

    scene_data_.env = {};

    assert(scene_data_.meshes.empty());
    assert(scene_data_.materials.empty());
    assert(scene_data_.textures.empty());

    scene_data_.objects.clear();
    scene_data_.name_to_object.clear();
    scene_data_.lm_splitter.Clear();
    scene_data_.probe_storage.Clear();
    scene_data_.nodes.clear();
    scene_data_.free_nodes.clear();
    scene_data_.update_counter = 0;

    scene_data_.materials_buf = {};
    scene_data_.materials_stage_buf = {};
    scene_data_.textures_buf = {};
    scene_data_.textures_stage_buf = {};

    changed_objects_.clear();
    last_changed_objects_.clear();

    ren_ctx_.default_vertex_buf1()->Print(ren_ctx_.log());

    ray_scene_ = {};
}

void SceneManager::LoadProbeCache() {
    const int res = scene_data_.probe_storage.res(), capacity = scene_data_.probe_storage.capacity();

    if (scene_data_.probe_storage.format() != Ren::DefaultCompressedRGBA) {
        // switch to compressed texture format
        scene_data_.probe_storage.Resize(Ren::DefaultCompressedRGBA, res, capacity, ren_ctx_.log());
    }

    CompStorage *probe_storage = scene_data_.comp_store[CompProbe];

    uint32_t probe_id = probe_storage->First();
    while (probe_id != 0xffffffff) {
        auto *lprobe = (LightProbe *)probe_storage->Get(probe_id);
        assert(lprobe);

        std::string file_path;

        for (int face_index = 0; face_index < 6; face_index++) {
            file_path.clear();

#if !defined(__ANDROID__)
            file_path += "assets_pc/textures/probes_cache/";
#else
            file_path += "assets/textures/probes_cache/";
#endif
            file_path += scene_data_.name.c_str();
            file_path += std::to_string(lprobe->layer_index);
            file_path += "_";
            file_path += std::to_string(face_index);
#if !defined(__ANDROID__)
            file_path += ".dds";
#else
            file_path += ".ktx";
#endif

            std::weak_ptr<SceneManager> _self = shared_from_this();
            Sys::LoadAssetComplete(
                file_path.c_str(),
                [_self, probe_id, face_index](void *data, int size) {
                    std::shared_ptr<SceneManager> self = _self.lock();
                    if (!self) {
                        return;
                    }

                    self->ren_ctx_.ProcessSingleTask([&self, probe_id, face_index, data, size]() {
                        Ren::ILog *log = self->ren_ctx_.log();

                        const int res = self->scene_data_.probe_storage.res();
                        CompStorage *probe_storage = self->scene_data_.comp_store[CompProbe];

                        auto *lprobe = (LightProbe *)probe_storage->Get(probe_id);
                        assert(lprobe);

#if !defined(__ANDROID__)
                        const uint8_t *p_data = (uint8_t *)data + sizeof(Ren::DDSHeader);
                        int data_len = size - int(sizeof(Ren::DDSHeader));

                        int _res = res;
                        int level = 0;

                        while (_res >= 16) {
                            const int len = ((_res + 3) / 4) * ((_res + 3) / 4) * 16;

                            if (len > data_len || !self->scene_data_.probe_storage.SetPixelData(
                                                      level, lprobe->layer_index, face_index,
                                                      Ren::DefaultCompressedRGBA, p_data, len, self->ren_ctx_.log())) {
                                log->Error("Failed to load probe texture!");
                            }

                            p_data += len;
                            data_len -= len;

                            _res /= 2;
                            level++;
                        }
#else
                        const uint8_t *p_data = (uint8_t *)data;
                        int data_offset = sizeof(Ren::KTXHeader);
                        int data_len = size - int(sizeof(Ren::KTXHeader));

                        int _res = res;
                        int level = 0;

                        while (_res >= 16) {
                            uint32_t len;
                            memcpy(&len, &p_data[data_offset], sizeof(uint32_t));
                            data_offset += sizeof(uint32_t);
                            data_len -= sizeof(uint32_t);

                            if ((int)len > data_len ||
                                !self->scene_data_.probe_storage.SetPixelData(
                                    level, lprobe->layer_index, face_index, Ren::eTexFormat::Compressed,
                                    &p_data[data_offset], len, self->ren_ctx_.log())) {
                                log->Error("Failed to load probe texture!");
                            }

                            data_offset += len;
                            data_len -= len;

                            const int pad = (data_offset % 4) ? (4 - (data_offset % 4)) : 0;
                            data_offset += pad;

                            _res = _res / 2;
                            level++;
                        }
#endif
                    });
                },
                [_self, probe_id, face_index]() {
                    std::shared_ptr<SceneManager> self = _self.lock();
                    if (!self) {
                        return;
                    }

                    self->ren_ctx_.log()->Error("Failed to load probe %i face %i", probe_id, face_index);
                });
        }

        probe_id = probe_storage->Next(probe_id);
    }
}

void SceneManager::SetupView(const Ren::Vec3f &origin, const Ren::Vec3f &target, const Ren::Vec3f &up, const float fov,
                             const float max_exposure) {
    using namespace SceneManagerConstants;

    const int cur_scr_w = ren_ctx_.w(), cur_scr_h = ren_ctx_.h();
    if (!cur_scr_w || !cur_scr_h) {
        // view is minimized?
        return;
    }

    cam_.SetupView(origin, target, up);
    cam_.Perspective(fov, float(cur_scr_w) / float(cur_scr_h), NEAR_CLIP, FAR_CLIP);
    cam_.UpdatePlanes();

    cam_.max_exposure = max_exposure;

    const double cur_time_s = Sys::GetTimeS();
    const Ren::Vec3f velocity = (origin - last_cam_pos_) / float(cur_time_s - last_cam_time_s_);
    last_cam_pos_ = origin;
    last_cam_time_s_ = cur_time_s;

    const Ren::Vec3f fwd_up[2] = {cam_.fwd(), cam_.up()};
    snd_ctx_.SetupListener(Ren::ValuePtr(origin), Ren::ValuePtr(velocity), Ren::ValuePtr(fwd_up[0]));
}

void SceneManager::PostloadDrawable(const JsObjectP &js_comp_obj, void *comp, Ren::Vec3f obj_bbox[2]) {
    using namespace SceneManagerConstants;

    auto *dr = (Drawable *)comp;

    if (js_comp_obj.Has("mesh_file")) {
        const JsStringP &js_mesh_file_name = js_comp_obj.at("mesh_file").as_str();

        const char *js_mesh_lookup_name = js_mesh_file_name.val.c_str();
        if (js_comp_obj.Has("mesh_name")) {
            js_mesh_lookup_name = js_comp_obj.at("mesh_name").as_str().val.c_str();
        }

        Ren::eMeshLoadStatus status;
        dr->mesh = LoadMesh(js_mesh_lookup_name, nullptr, nullptr, &status);

        if (status != Ren::eMeshLoadStatus::Found) {
            const std::string mesh_path = std::string(MODELS_PATH) + js_mesh_file_name.val.c_str();

#if defined(__ANDROID__)
            Sys::AssetFile in_file(mesh_path.c_str());
            if (!in_file) {
                ren_ctx_.log()->Error("Failed to open %s", mesh_path.c_str());
                return;
            }
            size_t in_file_size = in_file.size();

            std::unique_ptr<uint8_t[]> in_file_data(new uint8_t[in_file_size]);
            in_file.Read((char *)&in_file_data[0], in_file_size);

            Sys::MemBuf mem = {&in_file_data[0], in_file_size};
            std::istream in_file_stream(&mem);
#else
            std::ifstream in_file_stream(mesh_path.c_str(), std::ios::binary);
#endif

            using namespace std::placeholders;
            dr->mesh = LoadMesh(js_mesh_lookup_name, &in_file_stream,
                                std::bind(&SceneManager::OnLoadMaterial, this, _1), &status);
            assert(status == Ren::eMeshLoadStatus::CreatedFromData);
        }
    } else {
        assert(false && "Not supported anymore, update scene file!");
    }

    if (js_comp_obj.Has("pt_mesh_file")) {
        const JsStringP &js_pt_mesh_file_name = js_comp_obj.at("pt_mesh_file").as_str();

        Ren::eMeshLoadStatus status;
        dr->pt_mesh = LoadMesh(js_pt_mesh_file_name.val.c_str(), nullptr, nullptr, &status);

        if (status != Ren::eMeshLoadStatus::Found) {
            const std::string mesh_path = std::string(MODELS_PATH) + js_pt_mesh_file_name.val.c_str();

            Sys::AssetFile in_file(mesh_path.c_str());
            size_t in_file_size = in_file.size();

            std::unique_ptr<uint8_t[]> in_file_data(new uint8_t[in_file_size]);
            in_file.Read((char *)&in_file_data[0], in_file_size);

            Sys::MemBuf mem = {&in_file_data[0], in_file_size};
            std::istream in_file_stream(&mem);

            using namespace std::placeholders;
            dr->pt_mesh = LoadMesh(js_pt_mesh_file_name.val.c_str(), &in_file_stream,
                                   std::bind(&SceneManager::OnLoadMaterial, this, _1), &status);
            assert(status == Ren::eMeshLoadStatus::CreatedFromData);
        }
    }

    if (js_comp_obj.Has("material_override")) {
        const JsArrayP &js_materials = js_comp_obj.at("material_override").as_arr();

        int index = 0;
        for (const JsElementP &js_mat_el : js_materials.elements) {
            if (js_mat_el.type() == JsType::String) {
                const Ren::TriGroup &grp = dr->mesh->groups()[index];
                const_cast<Ren::TriGroup &>(grp).mat = OnLoadMaterial(js_mat_el.as_str().val.c_str());
            }
            index++;
        }
    }

    if (js_comp_obj.Has("anims")) {
        const JsArrayP &js_anims = js_comp_obj.at("anims").as_arr();

        assert(dr->mesh->type() == Ren::eMeshType::Skeletal);
        Ren::Skeleton *skel = dr->mesh->skel();

        for (const auto &js_anim : js_anims.elements) {
            const JsStringP &js_anim_name = js_anim.as_str();
            const std::string anim_path = std::string(MODELS_PATH) + js_anim_name.val.c_str();

            Sys::AssetFile in_file(anim_path.c_str());
            size_t in_file_size = in_file.size();

            std::unique_ptr<uint8_t[]> in_file_data(new uint8_t[in_file_size]);
            in_file.Read((char *)&in_file_data[0], in_file_size);

            Sys::MemBuf mem = {&in_file_data[0], in_file_size};
            std::istream in_file_stream(&mem);

            Ren::AnimSeqRef anim_ref = ren_ctx_.LoadAnimSequence(js_anim_name.val.c_str(), in_file_stream);
            skel->AddAnimSequence(anim_ref);
        }
    }

    if (dr->mesh->type() == Ren::eMeshType::Skeletal) {
        const Ren::Skeleton *skel = dr->mesh->skel();

        // Attach ellipsoids to bones
        for (int i = 0; i < dr->ellipsoids_count; i++) {
            Drawable::Ellipsoid &e = dr->ellipsoids[i];
            if (e.bone_name.empty()) {
                e.bone_index = -1;
                continue;
            }

            for (int j = 0; j < skel->bones_count; j++) {
                if (e.bone_name == skel->bones[j].name) {
                    e.bone_index = j;
                    break;
                }
            }
        }
    }

    obj_bbox[0] = Ren::Min(obj_bbox[0], dr->mesh->bbox_min());
    obj_bbox[1] = Ren::Max(obj_bbox[1], dr->mesh->bbox_max());
}

void SceneManager::PostloadOccluder(const JsObjectP &js_comp_obj, void *comp, Ren::Vec3f obj_bbox[2]) {
    using namespace SceneManagerConstants;

    auto *occ = (Occluder *)comp;

    const JsStringP &js_mesh_file_name = js_comp_obj.at("mesh_file").as_str();

    Ren::eMeshLoadStatus status;
    occ->mesh = LoadMesh(js_mesh_file_name.val.c_str(), nullptr, nullptr, &status);

    if (status != Ren::eMeshLoadStatus::Found) {
        const std::string mesh_path = std::string(MODELS_PATH) + js_mesh_file_name.val.c_str();

        Sys::AssetFile in_file(mesh_path.c_str());
        size_t in_file_size = in_file.size();

        std::unique_ptr<uint8_t[]> in_file_data(new uint8_t[in_file_size]);
        in_file.Read((char *)&in_file_data[0], in_file_size);

        Sys::MemBuf mem = {&in_file_data[0], in_file_size};
        std::istream in_file_stream(&mem);

        using namespace std::placeholders;
        occ->mesh = LoadMesh(js_mesh_file_name.val.c_str(), &in_file_stream,
                             std::bind(&SceneManager::OnLoadMaterial, this, _1), &status);
        assert(status == Ren::eMeshLoadStatus::CreatedFromData);
    }

    obj_bbox[0] = Ren::Min(obj_bbox[0], occ->mesh->bbox_min());
    obj_bbox[1] = Ren::Max(obj_bbox[1], occ->mesh->bbox_max());
}

void SceneManager::PostloadLightmap(const JsObjectP &js_comp_obj, void *comp, Ren::Vec3f obj_bbox[2]) {
    using namespace SceneManagerConstants;

    auto *lm = (Lightmap *)comp;

    const int node_id = scene_data_.lm_splitter.Allocate(lm->size, lm->pos);
    if (node_id == -1) {
        throw std::runtime_error("Cannot allocate lightmap region!");
    }

    lm->xform = Ren::Vec4f{
        float(lm->pos[0]) / LIGHTMAP_ATLAS_RESX,
        float(lm->pos[1]) / LIGHTMAP_ATLAS_RESY,
        float(lm->size[0]) / LIGHTMAP_ATLAS_RESX,
        float(lm->size[1]) / LIGHTMAP_ATLAS_RESY,
    };
}

void SceneManager::PostloadLightSource(const JsObjectP &js_comp_obj, void *comp, Ren::Vec3f obj_bbox[2]) {
    auto *ls = (LightSource *)comp;

    // Compute bounding box of light source
    const auto pos = Ren::Vec4f{ls->offset[0], ls->offset[1], ls->offset[2], 1.0f},
               dir = Ren::Vec4f{ls->dir[0], ls->dir[1], ls->dir[2], 0.0f};

    Ren::Vec3f bbox_min, bbox_max;

    const auto _dir = Ren::Vec3f{dir[0], dir[1], dir[2]};
    const Ren::Vec3f p1 = _dir * ls->influence;

    bbox_min = Ren::Min(bbox_min, p1);
    bbox_max = Ren::Max(bbox_max, p1);

    const Ren::Vec3f p2 = _dir * ls->spot * ls->influence;

    const float d = std::sqrt(1.0f - ls->spot * ls->spot) * ls->influence;

    bbox_min = Ren::Min(bbox_min, p2 - Ren::Vec3f{d, 0.0f, d});
    bbox_max = Ren::Max(bbox_max, p2 + Ren::Vec3f{d, 0.0f, d});

    if (ls->spot < 0.0f) {
        bbox_min = Ren::Min(bbox_min, p1 - Ren::Vec3f{ls->influence, 0.0f, ls->influence});
        bbox_max = Ren::Max(bbox_max, p1 + Ren::Vec3f{ls->influence, 0.0f, ls->influence});
    }

    auto up = Ren::Vec3f{1.0f, 0.0f, 0.0f};
    if (std::abs(_dir[1]) < std::abs(_dir[2]) && std::abs(_dir[1]) < std::abs(_dir[0])) {
        up = Ren::Vec3f{0.0f, 1.0f, 0.0f};
    } else if (std::abs(_dir[2]) < std::abs(_dir[0]) && std::abs(_dir[2]) < std::abs(_dir[1])) {
        up = Ren::Vec3f{0.0f, 0.0f, 1.0f};
    }

    const Ren::Vec3f side = Ren::Cross(_dir, up);

    Transform ls_transform;
    ls_transform.world_from_object = Ren::Mat4f{
        Ren::Vec4f{side[0], -_dir[0], up[0], 0.0f}, Ren::Vec4f{side[1], -_dir[1], up[1], 0.0f},
        Ren::Vec4f{side[2], -_dir[2], up[2], 0.0f}, Ren::Vec4f{ls->offset[0], ls->offset[1], ls->offset[2], 1.0f}};

    ls_transform.bbox_min = bbox_min;
    ls_transform.bbox_max = bbox_max;
    ls_transform.UpdateBBox();

    // Combine light's bounding box with object's
    obj_bbox[0] = Ren::Min(obj_bbox[0], ls_transform.bbox_min_ws);
    obj_bbox[1] = Ren::Max(obj_bbox[1], ls_transform.bbox_max_ws);
}

void SceneManager::PostloadDecal(const JsObjectP &js_comp_obj, void *comp, Ren::Vec3f obj_bbox[2]) {
    auto *de = (Decal *)comp;

    if (js_comp_obj.Has("diff")) {
        const JsStringP &js_diff = js_comp_obj.at("diff").as_str();

        const Ren::Vec4f *diff_tr = scene_data_.decals_textures.Find(js_diff.val.c_str());
        if (!diff_tr) {
            de->diff = LoadDecalTexture(js_diff.val.c_str());
            scene_data_.decals_textures.Insert(Ren::String{js_diff.val.c_str()}, de->diff);
        } else {
            de->diff = *diff_tr;
        }
    }

    if (js_comp_obj.Has("norm")) {
        const JsStringP &js_norm = js_comp_obj.at("norm").as_str();

        const Ren::Vec4f *norm_tr = scene_data_.decals_textures.Find(js_norm.val.c_str());
        if (!norm_tr) {
            de->norm = LoadDecalTexture(js_norm.val.c_str());
            scene_data_.decals_textures.Insert(Ren::String{js_norm.val.c_str()}, de->norm);
        } else {
            de->norm = *norm_tr;
        }
    }

    if (js_comp_obj.Has("spec")) {
        const JsStringP &js_spec = js_comp_obj.at("spec").as_str();

        const Ren::Vec4f *spec_tr = scene_data_.decals_textures.Find(js_spec.val.c_str());
        if (!spec_tr) {
            de->spec = LoadDecalTexture(js_spec.val.c_str());
            scene_data_.decals_textures.Insert(Ren::String{js_spec.val.c_str()}, de->spec);
        } else {
            de->spec = *spec_tr;
        }
    }

    const Ren::Mat4f world_from_clip = Ren::Inverse(de->proj * de->view);

    Ren::Vec4f points[] = {Ren::Vec4f{-1.0f, -1.0f, -1.0f, 1.0f}, Ren::Vec4f{-1.0f, 1.0f, -1.0f, 1.0f},
                           Ren::Vec4f{1.0f, 1.0f, -1.0f, 1.0f},   Ren::Vec4f{1.0f, -1.0f, -1.0f, 1.0f},

                           Ren::Vec4f{-1.0f, -1.0f, 1.0f, 1.0f},  Ren::Vec4f{-1.0f, 1.0f, 1.0f, 1.0f},
                           Ren::Vec4f{1.0f, 1.0f, 1.0f, 1.0f},    Ren::Vec4f{1.0f, -1.0f, 1.0f, 1.0f}};

    for (Ren::Vec4f &point : points) {
        point = world_from_clip * point;
        point /= point[3];

        // Combine decals's bounding box with object's
        obj_bbox[0] = Ren::Min(obj_bbox[0], Ren::Vec3f{point});
        obj_bbox[1] = Ren::Max(obj_bbox[1], Ren::Vec3f{point});
    }
}

void SceneManager::PostloadLightProbe(const JsObjectP &js_comp_obj, void *comp, Ren::Vec3f obj_bbox[2]) {
    auto *pr = (LightProbe *)comp;

    pr->layer_index = scene_data_.probe_storage.Allocate();

    // Combine probe's bounding box with object's
    obj_bbox[0] = Ren::Min(obj_bbox[0], pr->offset - Ren::Vec3f{pr->radius});
    obj_bbox[1] = Ren::Max(obj_bbox[1], pr->offset + Ren::Vec3f{pr->radius});
}

void SceneManager::PostloadSoundSource(const JsObjectP &js_comp_obj, void *comp, Ren::Vec3f obj_bbox[2]) {
    auto *snd = (SoundSource *)comp;

    const Ren::Vec3f center = 0.5f * (obj_bbox[0] + obj_bbox[1]);
    snd->snd_src.Init(1.0f, Ren::ValuePtr(center));
}

Ren::MaterialRef SceneManager::OnLoadMaterial(const char *name) {
    using namespace SceneManagerConstants;

    Ren::eMatLoadStatus status;
    Ren::MaterialRef ret = LoadMaterial(name, nullptr, &status, nullptr, nullptr, nullptr);
    if (!ret->ready()) {
        Sys::AssetFile in_file(std::string(MATERIALS_PATH) + name);
        if (!in_file) {
            ren_ctx_.log()->Error("Error loading material %s", name);
            return ret;
        }

        size_t file_size = in_file.size();

        std::string mat_src;
        mat_src.resize(file_size);
        in_file.Read((char *)mat_src.data(), file_size);

        using namespace std::placeholders;

        ret = LoadMaterial(name, mat_src.data(), &status,
                           std::bind(&SceneManager::OnLoadProgram, this, _1, _2, _3, _4, _5),
                           std::bind(&SceneManager::OnLoadTexture, this, _1, _2, _3),
                           std::bind(&SceneManager::OnLoadSampler, this, _1));
        assert(status == Ren::eMatLoadStatus::CreatedFromData);
    }
    scene_data_.material_changes.push_back(ret.index());
    return ret;
}

Ren::ProgramRef SceneManager::OnLoadProgram(const char *name, const char *v_shader, const char *f_shader,
                                            const char *tc_shader, const char *te_shader) {
    using namespace SceneManagerConstants;

    return sh_.LoadProgram(ren_ctx_, name, v_shader, f_shader, tc_shader, te_shader);
}

Ren::Tex2DRef SceneManager::OnLoadTexture(const char *name, const uint8_t color[4], const uint32_t flags) {
    using namespace SceneManagerConstants;

    Ren::Tex2DParams p;
    p.flags = flags | Ren::TexUsageScene;
    memcpy(p.fallback_color, color, 4);

    p.sampling.filter = Ren::eTexFilter::Trilinear;
    if (p.flags & Ren::TexNoRepeat) {
        p.sampling.repeat = Ren::eTexRepeat::ClampToEdge;
    } else {
        p.sampling.repeat = Ren::eTexRepeat::Repeat;
    }
    p.sampling.min_lod.from_float(-1.0f);

    Ren::eTexLoadStatus status;
    Ren::Tex2DRef ret = LoadTexture(name, nullptr, 0, p, &status);

    if (status == Ren::eTexLoadStatus::CreatedDefault) {
        TextureRequest new_req;
        new_req.ref = ret;

        if (ret->name().StartsWith("lightmaps/")) {
            // set max initial priority for lightmaps
            new_req.sort_key = 0;
        }

        std::lock_guard<std::mutex> _(tex_requests_lock_);
        requested_textures_.push_back(std::move(new_req));
        tex_loader_cnd_.notify_one();
    }

    return ret;
}

Ren::SamplerRef SceneManager::OnLoadSampler(Ren::SamplingParams params) {
    Ren::eSamplerLoadStatus status;
    return ren_ctx_.LoadSampler(params, &status);
}

Ren::MeshRef SceneManager::LoadMesh(const char *name, std::istream *data,
                                    const Ren::material_load_callback &on_mat_load, Ren::eMeshLoadStatus *load_status) {
    Ren::MeshRef ref = scene_data_.meshes.FindByName(name);
    if (!ref) {
        auto &stage = ren_ctx_.default_stage_bufs();
        const int ndx = stage.next_index();
        stage.fences[ndx].ClientWaitSync();
        ren_ctx_.BegSingleTimeCommands(stage.cmd_bufs[ndx]);
        ref = scene_data_.meshes.Add(name, data, on_mat_load, *stage.bufs[ndx], stage.cmd_bufs[ndx],
                                     ren_ctx_.default_vertex_buf1(), ren_ctx_.default_vertex_buf2(),
                                     ren_ctx_.default_indices_buf(), ren_ctx_.default_skin_vertex_buf(),
                                     ren_ctx_.default_delta_buf(), load_status, ren_ctx_.log());
        stage.fences[ndx] = ren_ctx_.EndSingleTimeCommands(stage.cmd_bufs[ndx]);
    } else {
        if (ref->ready()) {
            if (load_status) {
                (*load_status) = Ren::eMeshLoadStatus::Found;
            }
        } else if (data) {
            auto &stage = ren_ctx_.default_stage_bufs();
            const int ndx = stage.next_index();
            stage.fences[ndx].ClientWaitSync();
            ren_ctx_.BegSingleTimeCommands(stage.cmd_bufs[ndx]);
            ref->Init(data, on_mat_load, *stage.bufs[ndx], stage.cmd_bufs[ndx], ren_ctx_.default_vertex_buf1(),
                      ren_ctx_.default_vertex_buf2(), ren_ctx_.default_indices_buf(),
                      ren_ctx_.default_skin_vertex_buf(), ren_ctx_.default_delta_buf(), load_status, ren_ctx_.log());
            stage.fences[ndx] = ren_ctx_.EndSingleTimeCommands(stage.cmd_bufs[ndx]);
        }
    }

    return ref;
}

Ren::MaterialRef SceneManager::LoadMaterial(const char *name, const char *mat_src, Ren::eMatLoadStatus *status,
                                            const Ren::program_load_callback &on_prog_load,
                                            const Ren::texture_load_callback &on_tex_load,
                                            const Ren::sampler_load_callback &on_sampler_load) {
    Ren::MaterialRef ref = scene_data_.materials.FindByName(name);
    if (!ref) {
        ref = scene_data_.materials.Add(name, mat_src, status, on_prog_load, on_tex_load, on_sampler_load,
                                        ren_ctx_.log());
    } else {
        if (ref->ready()) {
            if (status) {
                (*status) = Ren::eMatLoadStatus::Found;
            }
        } else if (!ref->ready() && mat_src) {
            ref->Init(mat_src, status, on_prog_load, on_tex_load, on_sampler_load, ren_ctx_.log());
        }
    }

    return ref;
}

Ren::Tex2DRef SceneManager::LoadTexture(const char *name, const void *data, int size, const Ren::Tex2DParams &p,
                                        Ren::eTexLoadStatus *load_status) {
    Ren::Tex2DRef ref = scene_data_.textures.FindByName(name);
    if (!ref) {
        auto &stage = ren_ctx_.default_stage_bufs();
        const int ndx = stage.next_index();
        stage.fences[ndx].ClientWaitSync();
        ren_ctx_.BegSingleTimeCommands(stage.cmd_bufs[ndx]);
        ref = scene_data_.textures.Add(name, ren_ctx_.api_ctx(), data, size, p, *stage.bufs[ndx], stage.cmd_bufs[ndx],
                                       ren_ctx_.default_mem_allocs(), load_status, ren_ctx_.log());
        stage.fences[ndx] = ren_ctx_.EndSingleTimeCommands(stage.cmd_bufs[ndx]);
    } else {
        if (load_status) {
            (*load_status) = Ren::eTexLoadStatus::Found;
        }
        if (!ref->ready() && data) {
            auto &stage = ren_ctx_.default_stage_bufs();
            const int ndx = stage.next_index();
            stage.fences[ndx].ClientWaitSync();
            ren_ctx_.BegSingleTimeCommands(stage.cmd_bufs[ndx]);
            ref->Init(data, size, p, *stage.bufs[ndx], stage.cmd_bufs[ndx], ren_ctx_.default_mem_allocs(),
                      load_status, ren_ctx_.log());
            stage.fences[ndx] = ren_ctx_.EndSingleTimeCommands(stage.cmd_bufs[ndx]);
        }
    }

    return ref;
}

Ren::Vec4f SceneManager::LoadDecalTexture(const char *name) {
    using namespace SceneManagerConstants;

    const std::string file_name = TEXTURES_PATH + std::string(name);

    Sys::AssetFile in_file(file_name);
    size_t in_file_size = in_file.size();

    std::unique_ptr<uint8_t[]> in_file_data(new uint8_t[in_file_size]);
    in_file.Read((char *)&in_file_data[0], in_file_size);

    int res[2];
#if !defined(__ANDROID__)
    Ren::DDSHeader header;
    memcpy(&header, in_file_data.get(), sizeof(Ren::DDSHeader));

    const int px_format = int(header.sPixelFormat.dwFourCC >> 24u) - '0';
    assert(px_format == 5);

    res[0] = int(header.dwWidth);
    res[1] = int(header.dwHeight);

    const uint8_t *p_data = (uint8_t *)in_file_data.get() + sizeof(Ren::DDSHeader);
    int data_len = int(in_file_size) - int(sizeof(Ren::DDSHeader));

    int pos[2];
    const int rc = scene_data_.decals_atlas.AllocateRegion(res, pos);
    if (rc == -1) {
        ren_ctx_.log()->Error("Failed to allocate decal texture!");
        return Ren::Vec4f{};
    }

    int _pos[2] = {pos[0], pos[1]};
    int _res[2] = {res[0], res[1]};
    int level = 0;

    while (_res[0] >= 16 && _res[1] >= 16) {
        const int len = ((_res[0] + 3) / 4) * ((_res[1] + 3) / 4) * 16;

        if (len > data_len) {
            ren_ctx_.log()->Error("Invalid data count!");
            break;
        }

        scene_data_.decals_atlas.InitRegion(p_data, len, Ren::DefaultCompressedRGBA, 0, 0, level, _pos, _res,
                                            ren_ctx_.log());

        p_data += len;
        data_len -= len;

        _pos[0] = _pos[0] / 2;
        _pos[1] = _pos[1] / 2;
        _res[0] = _res[0] / 2;
        _res[1] = _res[1] / 2;
        level++;
    }
#else
    Ren::KTXHeader header;
    memcpy(&header, in_file_data.get(), sizeof(Ren::KTXHeader));

    assert(header.gl_internal_format == 0x93B0 /* GL_COMPRESSED_RGBA_ASTC_4x4_KHR */);

    res[0] = int(header.pixel_width);
    res[1] = int(header.pixel_height);

    int pos[2];
    const int rc = scene_data_.decals_atlas.AllocateRegion(res, pos);
    if (rc == -1) {
        ren_ctx_.log()->Error("Failed to allocate decal texture!");
        return Ren::Vec4f{};
    }

    const uint8_t *p_data = (uint8_t *)in_file_data.get();
    int data_offset = sizeof(Ren::KTXHeader);
    int data_len = int(in_file_size) - int(sizeof(Ren::KTXHeader));

    int _pos[2] = {pos[0], pos[1]};
    int _res[2] = {res[0], res[0]};
    int level = 0;

    while (_res[0] >= 16 && _res[1] >= 16) {
        uint32_t len;
        memcpy(&len, &p_data[data_offset], sizeof(uint32_t));
        data_offset += sizeof(uint32_t);
        data_len -= sizeof(uint32_t);

        if (int(len) > data_len) {
            ren_ctx_.log()->Error("Invalid data count!");
            break;
        }

        scene_data_.decals_atlas.InitRegion(p_data, len, Ren::eTexFormat::Compressed, 0, 0, level, _pos, _res,
                                            ren_ctx_.log());

        data_offset += len;
        data_len -= len;

        const int pad = (data_offset % 4) ? (4 - (data_offset % 4)) : 0;
        data_offset += pad;

        _pos[0] = _pos[0] / 2;
        _pos[1] = _pos[1] / 2;
        _res[0] = _res[0] / 2;
        _res[1] = _res[1] / 2;
        level++;
    }
#endif

    return Ren::Vec4f{float(pos[0]) / DECALS_ATLAS_RESX, float(pos[1]) / DECALS_ATLAS_RESY,
                      float(res[0]) / DECALS_ATLAS_RESX, float(res[1]) / DECALS_ATLAS_RESY};
}

void SceneManager::Serve(const int texture_budget) {
    using namespace SceneManagerConstants;

    __itt_task_begin(__g_itt_domain, __itt_null, __itt_null, itt_serve_str);

    EstimateTextureMemory(texture_budget);
    ProcessPendingTextures(texture_budget);

    UpdateMaterialsBuffer();

    __itt_task_end(__g_itt_domain);
}
