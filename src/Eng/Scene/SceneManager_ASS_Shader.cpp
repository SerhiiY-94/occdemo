#include "SceneManager.h"

#include <fstream>

#include "../Renderer/Renderer_GL_Defines.inl"

void SceneManager::WriteCommonShaderIncludes(const char *in_folder) {
    {   // common header
        static const char vs_common[] =
#include "../Renderer/Shaders/_common.glsl"
            ;

        std::string out_file_name = in_folder;
        out_file_name += "/shaders/internal/_common.glsl";

        std::ofstream out_file(out_file_name, std::ios::binary);
        out_file.write(vs_common, sizeof(vs_common) - 1);
    }

    {   // common vertex shader header
        static const char vs_common[] =
#include "../Renderer/Shaders/_vs_common.glsl"
            ;

        std::string out_file_name = in_folder;
        out_file_name += "/shaders/internal/_vs_common.glsl";

        std::ofstream out_file(out_file_name, std::ios::binary);
        out_file.write(vs_common, sizeof(vs_common) - 1);
    }

    {   // common fragment shader header
        static const char fs_common[] =
#include "../Renderer/Shaders/_fs_common.glsl"
            ;

        std::string out_file_name = in_folder;
        out_file_name += "/shaders/internal/_fs_common.glsl";

        std::ofstream out_file(out_file_name, std::ios::binary);
        out_file.write(fs_common, sizeof(fs_common) - 1);
    }
}

void SceneManager::InlineShaderConstants(assets_context_t &ctx, std::string &line) {
    static bool constants_initialized = false;
    static Ren::HashMap32<std::string, std::string> shader_constants;
    if (!constants_initialized) {
        shader_constants.Insert("$ModifyWarning",
                "/***********************************************/\r\n"
                "/* This file was autogenerated, do not modify! */\r\n"
                "/***********************************************/");

        // Uniform block locations
        shader_constants.Insert("$ubSharedDataLoc", AS_STR(REN_UB_SHARED_DATA_LOC));

        // Shadow properties
        if (strcmp(ctx.platform, "pc") == 0) {
            shader_constants.Insert("$ShadRes",     AS_STR(REN_SHAD_RES_PC));
        } else if (strcmp(ctx.platform, "android") == 0) {
            shader_constants.Insert("$ShadRes",     AS_STR(REN_SHAD_RES_ANDROID));
        } else {
            ctx.log->Error("Unknown platform %s", ctx.platform);
            return;
        }

        constants_initialized = true;
    }

    size_t n = 0;
    while ((n = line.find('$', n)) != std::string::npos) {
        size_t l = 1;

        const char punctuation_chars[] = ".,(); $*[]\r\n";
        while (std::find(std::begin(punctuation_chars), std::end(punctuation_chars), line[n + l]) == std::end(punctuation_chars)) {
            l++;
        }

        const std::string var = line.substr(n, l);

        const std::string *it = shader_constants.Find(var);
        if (it) {
            line.replace(n, l, *it);
        } else {
            ctx.log->Error("Unknown variable %s", var.c_str());
            throw std::runtime_error("Unknown variable!");
        }
    }
}

bool SceneManager::HPreprocessShader(assets_context_t &ctx, const char *in_file, const char *out_file) {
    ctx.log->Info("[PrepareAssets] Prep %s", out_file);
    std::remove(out_file);

    std::vector<std::string> permutations;
    permutations.emplace_back();

    {   // resolve includes, inline constants
        std::ifstream src_stream(in_file, std::ios::binary);
        if (!src_stream) {
            return false;
        }
        std::ofstream dst_stream(out_file, std::ios::binary);
        std::string line;

        int line_counter = 0;

        while (std::getline(src_stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line = line.substr(0, line.size() - 1);
            }

            if (line.rfind("#version ") == 0) {
                if (strcmp(ctx.platform, "pc") == 0) {
                    line = "#version 430";
                }
                dst_stream << line << "\r\n";
            } else if (line.rfind("#include ") == 0) {
                size_t n1 = line.find_first_of('\"');
                size_t n2 = line.find_last_of('\"');

                std::string file_name = line.substr(n1 + 1, n2 - n1 - 1);

                auto slash_pos = (size_t)intptr_t(strrchr(in_file, '/') - in_file);
                std::string full_path = std::string(in_file, slash_pos + 1) + file_name;

                dst_stream << "#line 0\r\n";

                std::ifstream incl_stream(full_path, std::ios::binary);
                while (std::getline(incl_stream, line)) {
                    if (!line.empty() && line.back() == '\r') {
                        line = line.substr(0, line.size() - 1);
                    }

                    InlineShaderConstants(ctx, line);

                    dst_stream << line << "\r\n";
                }

                dst_stream << "\r\n#line " << line_counter << "\r\n";
            } else if (line.find("PERM ") == 0) { // NOLINT
                permutations.emplace_back(std::move(line.substr(5)));
            } else {
                InlineShaderConstants(ctx, line);

                dst_stream << line << "\r\n";
            }

            line_counter++;
        }
    }

    if (strcmp(ctx.platform, "pc") == 0) {
        for (const std::string &perm : permutations) {
            std::string spv_file = out_file + perm;

            size_t n;
            if ((n = spv_file.find(".glsl")) != std::string::npos) {
                spv_file.replace(n + 1, 4, "spv", 3);
            }

            std::remove(spv_file.c_str());

            std::string compile_cmd = "src/libs/spirv/glslangValidator";

            if (!perm.empty()) {
                const char *params = perm.c_str();
                if (!params || params[0] != '@') {
                    continue;
                }

                int count = 0;

                const char *p1 = params + 1;
                const char *p2 = p1 + 1;
                while (*p2) {
                    if (*p2 == '=') {
                        compile_cmd += " -D";
                        compile_cmd += std::string(p1, p2);

                        p1 = p2 + 1;
                        while (p2 && *p2 && *p2 != ';') {
                            ++p2;
                        }

                        compile_cmd += std::string(p1, p2);

                        if (*p2) {
                            p1 = ++p2;
                        }
                        ++count;
                    } else if (*p2 == ';') {
                        compile_cmd += " -D";
                        compile_cmd += std::string(p1, p2);
                        p1 = ++p2;
                        ++count;
                    }

                    if (*p2) {
                        ++p2;
                    }
                }

                if (p1 != p2) {
                    compile_cmd += " -D";
                    compile_cmd += std::string(p1, p2);
                    ++count;
                }
            }

            compile_cmd += " -G ";
            compile_cmd += out_file;
            compile_cmd += " -o \"";
            compile_cmd += spv_file;
            compile_cmd += '\"';

#ifdef _WIN32
            std::replace(compile_cmd.begin(), compile_cmd.end(), '/', '\\');
#endif
            int res = 0;// system(compile_cmd.c_str());
            if (res != 0) {
                ctx.log->Error("[PrepareAssets] Failed to compile %s", spv_file.c_str());
#if !defined(NDEBUG) && defined(_WIN32)
                __debugbreak();
#endif
                return false;
            }

            std::string optimize_cmd = "src/libs/spirv/spirv-opt "
                                       "--eliminate-dead-branches "
                                       "--merge-return "
                                       "--inline-entry-points-exhaustive "
                                       "--loop-unswitch --loop-unroll "
                                       "--eliminate-dead-code-aggressive "
                                       "--private-to-local "
                                       "--eliminate-local-single-block "
                                       "--eliminate-local-single-store "
                                       "--eliminate-dead-code-aggressive "
                                       //"--scalar-replacement=100 "
                                       "--convert-local-access-chains "
                                       "--eliminate-local-single-block "
                                       "--eliminate-local-single-store "
                                       //"--eliminate-dead-code-aggressive "
                                       //"--eliminate-local-multi-store "
                                       //"--eliminate-dead-code-aggressive "
                                       "--ccp "
                                       //"--eliminate-dead-code-aggressive "
                                       "--redundancy-elimination "
                                       "--combine-access-chains "
                                       "--simplify-instructions "
                                       "--vector-dce "
                                       "--eliminate-dead-inserts "
                                       "--eliminate-dead-branches "
                                       "--simplify-instructions "
                                       "--if-conversion "
                                       "--copy-propagate-arrays "
                                       "--reduce-load-size "
                                       //"--eliminate-dead-code-aggressive "
                                       //"--merge-blocks "
                                       "--redundancy-elimination "
                                       "--eliminate-dead-branches "
                                       //"--merge-blocks "
                                       "--simplify-instructions "
                                       "--validate-after-all ";

            optimize_cmd += '\"';
            optimize_cmd += spv_file;
            optimize_cmd += "\" -o \"";
            optimize_cmd += spv_file;
            optimize_cmd += '\"';

#ifdef _WIN32
            std::replace(optimize_cmd.begin(), optimize_cmd.end(), '/', '\\');
#endif
            //res = system(optimize_cmd.c_str());
            if (res != 0) {
                ctx.log->Error("[PrepareAssets] Failed to optimize %s", spv_file.c_str());
#if !defined(NDEBUG) && defined(_WIN32)
                __debugbreak();
#endif
                return false;
            }

            std::string cross_cmd = "src/libs/spirv/spirv-cross ";
            if (strcmp(ctx.platform, "pc") == 0) {
                cross_cmd += "--version 430 ";
            } else if (strcmp(ctx.platform, "android") == 0) {
                cross_cmd += "--version 310 --es ";
                cross_cmd += "--extension GL_EXT_texture_buffer ";
            }
            cross_cmd +=
                "--no-support-nonzero-baseinstance --glsl-emit-push-constant-as-ubo ";
            cross_cmd += spv_file;
            cross_cmd += " --output ";
            cross_cmd += out_file;

#ifdef _WIN32
            std::replace(cross_cmd.begin(), cross_cmd.end(), '/', '\\');
#endif
            // res = system(cross_cmd.c_str());
            if (res != 0) {
                ctx.log->Error("[PrepareAssets] Failed to cross-compile %s",
                               spv_file.c_str());
#if !defined(NDEBUG) && defined(_WIN32)
                __debugbreak();
#endif
                return false;
            }
        }
    }

    return true;
}
