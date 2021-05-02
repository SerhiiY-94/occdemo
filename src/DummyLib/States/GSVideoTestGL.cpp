#include "GSVideoTest.h"

#ifdef ENABLE_ITT_API
#include <vtune/ittnotify.h>
extern __itt_domain *__g_itt_domain; // NOLINT
#endif

#include <Ren/Context.h>
#include <Ren/GL.h>
#include <Ren/Utils.h>

namespace GSVideoTestInternal {
const bool VerboseLogging = false;
} // namespace GSVideoTestInternal

void GSVideoTest::InitVideoTextures() {
    for (int tx = 0; tx < 5; tx++) {
        if (!vp_[tx].initialized()) {
            continue;
        }
        const VideoPlayer &vp = vp_[tx];

        std::vector<uint8_t> temp_buf(vp.w() * vp.h(), 0);

        char name_buf[128];

        { // create Y-channel texture (full res)
            Ren::Tex2DParams params;
            params.w = vp.w();
            params.h = vp.h();
            params.format = Ren::eTexFormat::RawR8;
            params.sampling.filter = Ren::eTexFilter::BilinearNoMipmap;
            params.sampling.repeat = Ren::eTexRepeat::ClampToEdge;

            for (int j = 0; j < TextureSyncWindow; j++) {
                sprintf(name_buf, "__video_Y_texture_%i_%i__", tx, j);
                Ren::eTexLoadStatus load_status;
                y_tex_[tx][j] =
                    ren_ctx_->textures().Add(name_buf, &temp_buf[0], int(temp_buf.size()),
                                             params, &load_status, log_.get());
            }
        }

        { // register U-channel texture (half res)
            Ren::Tex2DParams params;
            params.w = vp.w() / 2;
            params.h = vp.h() / 2;
            params.format = Ren::eTexFormat::RawRG88;
            params.sampling.filter = Ren::eTexFilter::BilinearNoMipmap;
            params.sampling.repeat = Ren::eTexRepeat::ClampToEdge;

            for (int j = 0; j < TextureSyncWindow; j++) {
                sprintf(name_buf, "__video_UV_texture_%i_%i__", tx, j);
                Ren::eTexLoadStatus load_status;
                uv_tex_[tx][j] = ren_ctx_->textures().Add(
                    name_buf, &temp_buf[0], int(temp_buf.size() / 2), params,
                    &load_status, log_.get());
            }
        }

        { // register material
            orig_vid_mat_[tx] =
                scene_manager_->scene_data().materials.FindByName("wall_picture_yuv.txt");
            if (!orig_vid_mat_[tx]) {
                log_->Error("Failed to find material wall_picture");
                return;
            }

            Ren::ProgramRef programs[Ren::MaxMaterialProgramCount];
            for (int j = 0; j < Ren::MaxMaterialProgramCount; j++) {
                programs[j] = orig_vid_mat_[tx]->programs[j];
            }

            Ren::Tex2DRef textures[Ren::MaxMaterialTextureCount];
            for (int j = 0; j < Ren::MaxMaterialTextureCount; j++) {
                textures[j] = orig_vid_mat_[tx]->textures[j];
            }

            Ren::Vec4f params[Ren::MaxMaterialParamCount];
            for (int j = 0; j < Ren::MaxMaterialParamCount; j++) {
                params[j] = orig_vid_mat_[tx]->params[j];
            }

            sprintf(name_buf, "__video_texture_material_%i__", tx);
            vid_mat_[tx] = scene_manager_->scene_data().materials.Add(
                name_buf,
                orig_vid_mat_[tx]->flags() | uint32_t(Ren::eMatFlags::TaaResponsive),
                programs, textures, params, log_.get());
        }

#if !defined(__ANDROID__)
        { // init PBOs
            const uint32_t y_buf_size = TextureSyncWindow * vp.w() * vp.h(),
                           uv_buf_size =
                               TextureSyncWindow * 2 * (vp.w() / 2) * (vp.h() / 2);

            y_sbuf_[tx].Alloc(y_buf_size, ren_ctx_->capabilities.persistent_buf_mapping);
            uv_sbuf_[tx].Alloc(uv_buf_size,
                               ren_ctx_->capabilities.persistent_buf_mapping);
        }
#endif
    }
}

void GSVideoTest::DestroyVideoTextures() {
    for (int i = 0; i < 5; i++) {
        y_sbuf_[i].Free();
        uv_sbuf_[i].Free();
        for (int j = 0; j < TextureSyncWindow; j++) {
            WaitVideoTextureUpdated(i, j);
        }
    }
}

void GSVideoTest::SetVideoTextureFence(const int tex_index, const int frame_index) {
    assert(!after_tex_update_fences_[tex_index][frame_index]);
    after_tex_update_fences_[tex_index][frame_index] = Ren::MakeFence();

    if (GSVideoTestInternal::VerboseLogging) { // NOLINT
        log_->Info("Setting VT fence %tx", frame_index);
    }
}

void GSVideoTest::InsertVideoTextureBarrier(const int tex_index, const int frame_index) {
    if (after_tex_update_fences_[tex_index][frame_index]) {
        after_tex_update_fences_[tex_index][frame_index].WaitSync();
    }
}

void GSVideoTest::WaitVideoTextureUpdated(const int tex_index, const int frame_index) {
    if (after_tex_update_fences_[tex_index][frame_index]) {
        if (after_tex_update_fences_[tex_index][frame_index].ClientWaitSync() ==
            Ren::SyncFence::WaitResult::WaitFailed) {
            log_->Error("[Renderer::DrawObjectsInternal]: Wait failed!");
        }
        assert(!after_tex_update_fences_[tex_index][frame_index]);

        if (GSVideoTestInternal::VerboseLogging) { // NOLINT
            log_->Info("Waiting PBO fence %tx", frame_index);
        }
    }
}

void GSVideoTest::FlushGPUCommands() { glFlush(); }
