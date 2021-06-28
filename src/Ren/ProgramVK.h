#pragma once

#include <cstdint>
#include <cstring>

#include <array>
#include <string>

#include "Shader.h"
#include "SmallVector.h"
#include "Storage.h"
#include "String.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

namespace Ren {
class ILog;

enum class eProgLoadStatus { Found, SetToDefault, CreatedFromData };

class Program : public RefCounter {
    uint32_t flags_ = 0;
    std::array<ShaderRef, int(eShaderType::_Count)> shaders_;
    SmallVector<Attribute, 8> attributes_;
    SmallVector<Uniform, 16> uniforms_;
    SmallVector<UniformBlock, 4> uniform_blocks_;
    String name_;

    void InitBindings(ILog *log);

  public:
    Program() = default;
    Program(const char *name, ShaderRef vs_ref, ShaderRef fs_ref, ShaderRef tcs_ref, ShaderRef tes_ref,
            eProgLoadStatus *status, ILog *log);
    Program(const char *name, ShaderRef cs_ref, eProgLoadStatus *status, ILog *log);

    Program(const Program &rhs) = delete;
    Program(Program &&rhs) noexcept { (*this) = std::move(rhs); }
    ~Program();

    Program &operator=(const Program &rhs) = delete;
    Program &operator=(Program &&rhs) noexcept;

    // uint32_t id() const { return id_; }
    uint32_t flags() const { return flags_; }
    bool ready() const { return false; }
    bool has_tessellation() const { return shaders_[int(eShaderType::Tesc)] && shaders_[int(eShaderType::Tese)]; }
    const String &name() const { return name_; }

    const Attribute &attribute(const int i) const { return attributes_[i]; }

    const Attribute &attribute(const char *name) const {
        for (int i = 0; i < int(attributes_.size()); i++) {
            if (attributes_[i].name == name) {
                return attributes_[i];
            }
        }
        return attributes_[0];
    }

    const Uniform &uniform(const int i) const { return uniforms_[i]; }

    const Uniform &uniform(const char *name) const {
        for (int i = 0; i < int(uniforms_.size()); i++) {
            if (uniforms_[i].name == name) {
                return uniforms_[i];
            }
        }
        return uniforms_[0];
    }

    const UniformBlock &uniform_block(const int i) const { return uniform_blocks_[i]; }

    const UniformBlock &uniform_block(const char *name) const {
        for (int i = 0; i < int(uniform_blocks_.size()); i++) {
            if (uniform_blocks_[i].name == name) {
                return uniform_blocks_[i];
            }
        }
        return uniform_blocks_[0];
    }

    const ShaderRef &shader(eShaderType type) { return shaders_[int(type)]; }

    void Init(ShaderRef vs_ref, ShaderRef fs_ref, ShaderRef tcs_ref, ShaderRef tes_ref, eProgLoadStatus *status,
              ILog *log);
    void Init(ShaderRef cs_ref, eProgLoadStatus *status, ILog *log);
};

typedef StrongRef<Program> ProgramRef;
typedef Storage<Program> ProgramStorage;
} // namespace Ren

#ifdef _MSC_VER
#pragma warning(pop)
#endif