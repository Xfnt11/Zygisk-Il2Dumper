//
// Created by Perfare on 2020/7/4.
// Patched to be more robust against missing il2cpp APIs
//

#include "il2cpp_dump.h"
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include "xdl.h"
#include "log.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class.h"

#define DO_API(r, n, p) r (*n) p

#include "il2cpp-api-functions.h"

#undef DO_API

static uint64_t il2cpp_base = 0;

template<typename T>
T safe_xdl_sym(void* handle, const char* name) {
    T ptr = (T)xdl_sym(handle, name, nullptr);
    if (!ptr) {
        LOGW("api not found %s", name);
    }
    return ptr;
}

void init_il2cpp_api(void *handle) {
#define DO_API(r, n, p) {                      \
    n = safe_xdl_sym<r (*) p>(handle, #n);     \
}

#include "il2cpp-api-functions.h"

#undef DO_API
}

std::string get_method_modifier(uint32_t flags) {
    std::stringstream outPut;
    auto access = flags & METHOD_ATTRIBUTE_MEMBER_ACCESS_MASK;
    switch (access) {
        case METHOD_ATTRIBUTE_PRIVATE:
            outPut << "private ";
            break;
        case METHOD_ATTRIBUTE_PUBLIC:
            outPut << "public ";
            break;
        case METHOD_ATTRIBUTE_FAMILY:
            outPut << "protected ";
            break;
        case METHOD_ATTRIBUTE_ASSEM:
        case METHOD_ATTRIBUTE_FAM_AND_ASSEM:
            outPut << "internal ";
            break;
        case METHOD_ATTRIBUTE_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & METHOD_ATTRIBUTE_STATIC) {
        outPut << "static ";
    }
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "sealed override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_NEW_SLOT) {
            outPut << "virtual ";
        } else {
            outPut << "override ";
        }
    }
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) {
        outPut << "extern ";
    }
    return outPut.str();
}

bool _il2cpp_type_is_byref(const Il2CppType *type) {
    auto byref = type->byref;
    if (il2cpp_type_is_byref) {
        byref = il2cpp_type_is_byref(type);
    }
    return byref;
}

std::string dump_method(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Methods\n";

    if (!il2cpp_class_get_methods) {
        outPut << "\t// methods API not available\n";
        return outPut.str();
    }

    void *iter = nullptr;
    while (auto method = il2cpp_class_get_methods(klass, &iter)) {
        // safe guards for each used API
        outPut << "\t";
        if (method->methodPointer) {
            outPut << "\t// RVA: 0x";
            outPut << std::hex << ((uint64_t)method->methodPointer - il2cpp_base);
            outPut << " VA: 0x";
            outPut << std::hex << (uint64_t) method->methodPointer;
        } else {
            outPut << "\t// RVA: 0x VA: 0x0";
        }
        outPut << "\n\t";

        uint32_t iflags = 0;
        uint32_t flags = 0;
        if (il2cpp_method_get_flags) {
            flags = il2cpp_method_get_flags(method, &iflags);
        }
        outPut << get_method_modifier(flags);

        Il2CppType* return_type = nullptr;
        if (il2cpp_method_get_return_type) {
            return_type = il2cpp_method_get_return_type(method);
        }
        if (return_type && _il2cpp_type_is_byref(return_type)) {
            outPut << "ref ";
        }
        Il2CppClass* return_class = nullptr;
        if (il2cpp_class_from_type && return_type) {
            return_class = il2cpp_class_from_type(return_type);
        }
        if (il2cpp_class_get_name && return_class) {
            outPut << il2cpp_class_get_name(return_class) << " ";
        } else {
            outPut << "void ";
        }
        if (il2cpp_method_get_name) {
            outPut << il2cpp_method_get_name(method);
        } else {
            outPut << "Method";
        }
        outPut << "(";

        int param_count = 0;
        if (il2cpp_method_get_param_count) {
            param_count = il2cpp_method_get_param_count(method);
        }
        for (int i = 0; i < param_count; ++i) {
            auto param = il2cpp_method_get_param ? il2cpp_method_get_param(method, i) : nullptr;
            if (param) {
                auto attrs = param->attrs;
                if (_il2cpp_type_is_byref(param)) {
                    if (attrs & PARAM_ATTRIBUTE_OUT && !(attrs & PARAM_ATTRIBUTE_IN)) {
                        outPut << "out ";
                    } else if (attrs & PARAM_ATTRIBUTE_IN && !(attrs & PARAM_ATTRIBUTE_OUT)) {
                        outPut << "in ";
                    } else {
                        outPut << "ref ";
                    }
                } else {
                    if (attrs & PARAM_ATTRIBUTE_IN) {
                        outPut << "[In] ";
                    }
                    if (attrs & PARAM_ATTRIBUTE_OUT) {
                        outPut << "[Out] ";
                    }
                }
                Il2CppClass *parameter_class = il2cpp_class_from_type ? il2cpp_class_from_type(param) : nullptr;
                if (parameter_class && il2cpp_class_get_name) {
                    outPut << il2cpp_class_get_name(parameter_class) << " ";
                } else {
                    outPut << "object ";
                }
                if (il2cpp_method_get_param_name) {
                    outPut << il2cpp_method_get_param_name(method, i);
                } else {
                    outPut << "arg" << i;
                }
                outPut << ", ";
            }
        }
        if (param_count > 0) {
            outPut.seekp(-2, outPut.cur);
        }
        outPut << ") { }\n";
    }
    return outPut.str();
}

std::string dump_property(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Properties\n";

    if (!il2cpp_class_get_properties) {
        outPut << "\t// properties API not available\n";
        return outPut.str();
    }

    void *iter = nullptr;
    while (auto prop_const = il2cpp_class_get_properties(klass, &iter)) {
        auto prop = const_cast<PropertyInfo *>(prop_const);
        auto get = il2cpp_property_get_get_method ? il2cpp_property_get_get_method(prop) : nullptr;
        auto set = il2cpp_property_get_set_method ? il2cpp_property_get_set_method(prop) : nullptr;
        auto prop_name = il2cpp_property_get_name ? il2cpp_property_get_name(prop) : nullptr;
        outPut << "\t";
        Il2CppClass *prop_class = nullptr;
        uint32_t iflags = 0;
        if (get) {
            if (il2cpp_method_get_flags) {
                outPut << get_method_modifier(il2cpp_method_get_flags(get, &iflags));
            }
            if (il2cpp_method_get_return_type) {
                prop_class = il2cpp_class_from_type ? il2cpp_class_from_type(il2cpp_method_get_return_type(get)) : nullptr;
            }
        } else if (set) {
            if (il2cpp_method_get_flags) {
                outPut << get_method_modifier(il2cpp_method_get_flags(set, &iflags));
            }
            auto param = il2cpp_method_get_param ? il2cpp_method_get_param(set, 0) : nullptr;
            if (param) {
                prop_class = il2cpp_class_from_type ? il2cpp_class_from_type(param) : nullptr;
            }
        }
        if (prop_class) {
            if (il2cpp_class_get_name && prop_name) {
                outPut << il2cpp_class_get_name(prop_class) << " " << prop_name << " { ";
            } else {
                outPut << "unknown ";
                if (prop_name) outPut << prop_name;
                outPut << " { ";
            }
            if (get) {
                outPut << "get; ";
            }
            if (set) {
                outPut << "set; ";
            }
            outPut << "}\n";
        } else {
            if (prop_name) {
                outPut << " // unknown property " << prop_name << "\n";
            } else {
                outPut << " // unknown property\n";
            }
        }
    }
    return outPut.str();
}

std::string dump_field(Il2CppClass *klass) {
    std::stringstream outPut;
    outPut << "\n\t// Fields\n";

    if (!il2cpp_class_get_fields) {
        outPut << "\t// fields API not available\n";
        return outPut.str();
    }

    auto is_enum = il2cpp_class_is_enum ? il2cpp_class_is_enum(klass) : false;
    void *iter = nullptr;
    while (auto field = il2cpp_class_get_fields(klass, &iter)) {
        outPut << "\t";
        auto attrs = il2cpp_field_get_flags ? il2cpp_field_get_flags(field) : 0;
        auto access = attrs & FIELD_ATTRIBUTE_FIELD_ACCESS_MASK;
        switch (access) {
            case FIELD_ATTRIBUTE_PRIVATE:
                outPut << "private ";
                break;
            case FIELD_ATTRIBUTE_PUBLIC:
                outPut << "public ";
                break;
            case FIELD_ATTRIBUTE_FAMILY:
                outPut << "protected ";
                break;
            case FIELD_ATTRIBUTE_ASSEMBLY:
            case FIELD_ATTRIBUTE_FAM_AND_ASSEM:
                outPut << "internal ";
                break;
            case FIELD_ATTRIBUTE_FAM_OR_ASSEM:
                outPut << "protected internal ";
                break;
        }
        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            outPut << "const ";
        } else {
            if (attrs & FIELD_ATTRIBUTE_STATIC) {
                outPut << "static ";
            }
            if (attrs & FIELD_ATTRIBUTE_INIT_ONLY) {
                outPut << "readonly ";
            }
        }
        auto field_type = il2cpp_field_get_type ? il2cpp_field_get_type(field) : nullptr;
        auto field_class = (il2cpp_class_from_type && field_type) ? il2cpp_class_from_type(field_type) : nullptr;
        if (il2cpp_class_get_name && field_class) {
            outPut << il2cpp_class_get_name(field_class) << " " << (il2cpp_field_get_name ? il2cpp_field_get_name(field) : "field");
        } else {
            outPut << "object " << (il2cpp_field_get_name ? il2cpp_field_get_name(field) : "field");
        }
        if (attrs & FIELD_ATTRIBUTE_LITERAL && is_enum && il2cpp_field_static_get_value) {
            uint64_t val = 0;
            il2cpp_field_static_get_value(field, &val);
            outPut << " = " << std::dec << val;
        }
        if (il2cpp_field_get_offset) {
            outPut << "; // 0x" << std::hex << il2cpp_field_get_offset(field) << "\n";
        } else {
            outPut << ";\n";
        }
    }
    return outPut.str();
}

std::string dump_type(const Il2CppType *type) {
    std::stringstream outPut;
    if (!type) return outPut.str();
    auto *klass = il2cpp_class_from_type ? il2cpp_class_from_type(type) : nullptr;
    if (!klass) return outPut.str();

    outPut << "\n// Namespace: " << (il2cpp_class_get_namespace ? il2cpp_class_get_namespace(klass) : "Unknown") << "\n";
    auto flags = il2cpp_class_get_flags ? il2cpp_class_get_flags(klass) : 0;
    if (flags & TYPE_ATTRIBUTE_SERIALIZABLE) {
        outPut << "[Serializable]\n";
    }
    auto is_valuetype = il2cpp_class_is_valuetype ? il2cpp_class_is_valuetype(klass) : false;
    auto is_enum = il2cpp_class_is_enum ? il2cpp_class_is_enum(klass) : false;
    auto visibility = flags & TYPE_ATTRIBUTE_VISIBILITY_MASK;
    switch (visibility) {
        case TYPE_ATTRIBUTE_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_PUBLIC:
            outPut << "public ";
            break;
        case TYPE_ATTRIBUTE_NOT_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_FAM_AND_ASSEM:
        case TYPE_ATTRIBUTE_NESTED_ASSEMBLY:
            outPut << "internal ";
            break;
        case TYPE_ATTRIBUTE_NESTED_PRIVATE:
            outPut << "private ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAMILY:
            outPut << "protected ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & TYPE_ATTRIBUTE_ABSTRACT && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "static ";
    } else if (!(flags & TYPE_ATTRIBUTE_INTERFACE) && flags & TYPE_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
    } else if (!is_valuetype && !is_enum && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "sealed ";
    }
    if (flags & TYPE_ATTRIBUTE_INTERFACE) {
        outPut << "interface ";
    } else if (is_enum) {
        outPut << "enum ";
    } else if (is_valuetype) {
        outPut << "struct ";
    } else {
        outPut << "class ";
    }
    outPut << (il2cpp_class_get_name ? il2cpp_class_get_name(klass) : "Unknown");
    std::vector<std::string> extends;
    auto parent = il2cpp_class_get_parent ? il2cpp_class_get_parent(klass) : nullptr;
    if (!is_valuetype && !is_enum && parent) {
        auto parent_type = il2cpp_class_get_type ? il2cpp_class_get_type(parent) : nullptr;
        if (parent_type && parent_type->type != IL2CPP_TYPE_OBJECT) {
            if (il2cpp_class_get_name) extends.emplace_back(il2cpp_class_get_name(parent));
        }
    }
    void *iter = nullptr;
    if (il2cpp_class_get_interfaces) {
        while (auto itf = il2cpp_class_get_interfaces(klass, &iter)) {
            if (il2cpp_class_get_name) extends.emplace_back(il2cpp_class_get_name(itf));
        }
    }
    if (!extends.empty()) {
        outPut << " : " << extends[0];
        for (int i = 1; i < extends.size(); ++i) {
            outPut << ", " << extends[i];
        }
    }
    outPut << "\n{";
    outPut << dump_field(klass);
    outPut << dump_property(klass);
    outPut << dump_method(klass);
    outPut << "}\n";
    return outPut.str();
}

void il2cpp_api_init(void *handle) {
    LOGI("il2cpp_handle: %p", handle);
    init_il2cpp_api(handle);

    // Check essential APIs: domain_get and either domain_get_assemblies or image_get_class must exist
    bool has_domain = (il2cpp_domain_get != nullptr);
    bool has_assemblies = (il2cpp_domain_get_assemblies != nullptr);
    bool has_image_get_class = (il2cpp_image_get_class != nullptr);

    if (!has_domain || (!has_assemblies && !has_image_get_class)) {
        LOGE("Failed to initialize il2cpp api. essential symbols missing (domain=%d, assemblies=%d, image_get_class=%d)",
             has_domain, has_assemblies, has_image_get_class);
        return;
    }

    Dl_info dlInfo;
    if (il2cpp_domain_get_assemblies) {
        if (dladdr((void *) il2cpp_domain_get_assemblies, &dlInfo)) {
            il2cpp_base = reinterpret_cast<uint64_t>(dlInfo.dli_fbase);
        }
    } else if (il2cpp_image_get_class) {
        if (dladdr((void *) il2cpp_image_get_class, &dlInfo)) {
            il2cpp_base = reinterpret_cast<uint64_t>(dlInfo.dli_fbase);
        }
    }
    LOGI("il2cpp_base: %" PRIx64, il2cpp_base);

    // wait for VM ready (with timeout)
    int waited = 0;
    const int max_wait = 30;
    while (il2cpp_is_vm_thread && !il2cpp_is_vm_thread(nullptr)) {
        LOGI("Waiting for il2cpp_init... (%d/%d)", waited, max_wait);
        sleep(1);
        if (++waited > max_wait) {
            LOGE("timeout waiting for il2cpp_init");
            return;
        }
    }

    auto domain = il2cpp_domain_get ? il2cpp_domain_get() : nullptr;
    if (il2cpp_thread_attach && domain) {
        il2cpp_thread_attach(domain);
    }
}

void il2cpp_dump(const char *outDir) {
    LOGI("dumping...");

    if (!il2cpp_domain_get) {
        LOGE("il2cpp_domain_get missing, abort dump");
        return;
    }

    size_t size = 0;
    auto domain = il2cpp_domain_get();
    if (!domain) {
        LOGE("il2cpp_domain_get returned null, abort");
        return;
    }

    if (!il2cpp_domain_get_assemblies) {
        LOGE("il2cpp_domain_get_assemblies missing, abort");
        return;
    }

    auto assemblies = il2cpp_domain_get_assemblies(domain, &size);
    if (!assemblies) {
        LOGE("il2cpp_domain_get_assemblies returned null, abort");
        return;
    }

    std::stringstream imageOutput;
    for (size_t i = 0; i < size; ++i) {
        Il2CppImage *image = nullptr;
        if (il2cpp_assembly_get_image) {
            image = il2cpp_assembly_get_image(assemblies[i]);
        }
        const char* image_name = image && il2cpp_image_get_name ? il2cpp_image_get_name(image) : "Unknown";
        imageOutput << "// Image " << i << ": " << image_name << "\n";
    }

    std::vector<std::string> outPuts;

    if (il2cpp_image_get_class) {
        LOGI("Version greater than 2018.3 (using il2cpp_image_get_class)");
        for (size_t i = 0; i < size; ++i) {
            Il2CppImage *image = il2cpp_assembly_get_image ? il2cpp_assembly_get_image(assemblies[i]) : nullptr;
            if (!image) continue;
            std::stringstream imageStr;
            imageStr << "\n// Dll : " << (il2cpp_image_get_name ? il2cpp_image_get_name(image) : "Unknown");
            int classCount = 0;
            if (il2cpp_image_get_class_count) {
                classCount = il2cpp_image_get_class_count(image);
            } else if (il2cpp_image_get_type_count) {
                classCount = il2cpp_image_get_type_count(image);
            }
            for (int j = 0; j < classCount; ++j) {
                Il2CppClass *klass = il2cpp_image_get_class ? il2cpp_image_get_class(image, j) : nullptr;
                if (!klass) continue;
                auto type = il2cpp_class_get_type ? il2cpp_class_get_type(const_cast<Il2CppClass *>(klass)) : nullptr;
                auto outPut = imageStr.str() + dump_type(type);
                outPuts.push_back(outPut);
            }
        }
    } else {
        LOGI("Version less than 2018.3 (using reflection)");
        auto corlib = il2cpp_get_corlib ? il2cpp_get_corlib() : nullptr;
        if (!corlib) {
            LOGE("il2cpp_get_corlib missing, abort reflection dump");
            return;
        }
        auto assemblyClass = il2cpp_class_from_name ? il2cpp_class_from_name(corlib, "System.Reflection", "Assembly") : nullptr;
        if (!assemblyClass) {
            LOGE("Assembly class missing, abort");
            return;
        }
        auto assemblyLoad = il2cpp_class_get_method_from_name ? il2cpp_class_get_method_from_name(assemblyClass, "Load", 1) : nullptr;
        auto assemblyGetTypes = il2cpp_class_get_method_from_name ? il2cpp_class_get_method_from_name(assemblyClass, "GetTypes", 0) : nullptr;
        if (!assemblyLoad || !assemblyGetTypes) {
            LOGE("reflection helpers missing, abort");
            return;
        }
        typedef void *(*Assembly_Load_ftn)(void *, Il2CppString *, void *);
        typedef Il2CppArray *(*Assembly_GetTypes_ftn)(void *, void *);
        for (size_t i = 0; i < size; ++i) {
            Il2CppImage *image = il2cpp_assembly_get_image ? il2cpp_assembly_get_image(assemblies[i]) : nullptr;
            if (!image) continue;
            std::stringstream imageStr;
            auto image_name = il2cpp_image_get_name ? il2cpp_image_get_name(image) : "Unknown";
            imageStr << "\n// Dll : " << image_name;
            auto imageName = std::string(image_name);
            auto pos = imageName.rfind('.');
            auto imageNameNoExt = imageName.substr(0, pos);
            auto assemblyFileName = il2cpp_string_new ? il2cpp_string_new(imageNameNoExt.data()) : nullptr;
            if (!assemblyFileName) continue;
            auto reflectionAssembly = ((Assembly_Load_ftn) assemblyLoad->methodPointer)(nullptr, assemblyFileName, nullptr);
            if (!reflectionAssembly) continue;
            auto reflectionTypes = ((Assembly_GetTypes_ftn) assemblyGetTypes->methodPointer);
}
