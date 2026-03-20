#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <CSP/Common/List.h>
#include <CSP/Common/String.h>
#include <CSP/Common/Vector.h>
#include <CSP/Common/SharedEnums.h>
#include <CSP/Common/Systems/Log/LogSystem.h>
#include <CSP/Common/Interfaces/IJSScriptRunner.h>
#include <CSP/Common/Interfaces/IRealtimeEngine.h>
#include <CSP/Common/Optional.h>
#include <CSP/Multiplayer/SpaceTransform.h>
#include <CSP/Multiplayer/SpaceEntity.h>
#include <CSP/Multiplayer/CSPSceneDescription.h>
#include <CSP/Multiplayer/OfflineRealtimeEngine.h>
#include "EmscriptenBindings/CallbackQueue.h"

using namespace emscripten;

// Transparent conversion: csp::common::String <-> JS string
// Tells embind to treat csp::common::String as the same wire type as std::string,
// so JS sees plain strings wherever CSP APIs use csp::common::String.
namespace emscripten::internal {

template<>
struct BindingType<csp::common::String>
{
    using WireType = BindingType<std::string>::WireType;

    static WireType toWireType(const csp::common::String& str)
    {
        return BindingType<std::string>::toWireType(std::string(str.c_str()), rvp::default_tag{});
    }

    template<typename Policy>
    static WireType toWireType(const csp::common::String& str, Policy)
    {
        return BindingType<std::string>::toWireType(std::string(str.c_str()), rvp::default_tag{});
    }

    static csp::common::String fromWireType(WireType wire)
    {
        std::string s = BindingType<std::string>::fromWireType(wire);
        return csp::common::String(s.c_str());
    }
};

template<>
struct TypeID<csp::common::String>
{
    static constexpr TYPEID get() { return TypeID<std::string>::get(); }
};

template<>
struct TypeID<const csp::common::String&>
{
    static constexpr TYPEID get() { return TypeID<std::string>::get(); }
};

// Transparent conversion: csp::common::List<csp::common::String> <-> JS string[]
// Uses emscripten::val as the wire type to marshal between a JS Array and the CSP List.
// This is explicit for string, but i'm sure we can make this more generic
template<>
struct BindingType<csp::common::List<csp::common::String>>
{
    using ValBinding = BindingType<val>;
    using WireType = ValBinding::WireType;

    static WireType toWireType(const csp::common::List<csp::common::String>& list)
    {
        val arr = val::array();
        for (size_t i = 0; i < list.Size(); ++i)
        {
            arr.call<void>("push", std::string(list[i].c_str()));
        }
        return ValBinding::toWireType(arr, rvp::default_tag{});
    }

    static csp::common::List<csp::common::String> fromWireType(WireType wire)
    {
        val arr = ValBinding::fromWireType(wire);
        unsigned len = arr["length"].as<unsigned>();
        csp::common::List<csp::common::String> list;
        for (unsigned i = 0; i < len; ++i)
        {
            std::string s = arr[i].as<std::string>();
            list.Append(csp::common::String(s.c_str()));
        }
        return list;
    }
};

template<>
struct TypeID<csp::common::List<csp::common::String>>
{
    static constexpr TYPEID get() { return TypeID<val>::get(); }
};

template<>
struct TypeID<const csp::common::List<csp::common::String>&>
{
    static constexpr TYPEID get() { return TypeID<val>::get(); }
};

//Register wire types for List<SpaceEntity*>*, again, _super_ specific and not how we'd actually do this.
//Proto stuff be proto
template<>
struct BindingType<const csp::common::List<csp::multiplayer::SpaceEntity*>*>
{
    using ValBinding = BindingType<val>;
    using WireType = ValBinding::WireType;

    static WireType toWireType(const csp::common::List<csp::multiplayer::SpaceEntity*>* list)
    {
        val arr = val::array();
        if (list)
        {
            for (size_t i = 0; i < list->Size(); ++i)
            {
                arr.call<void>("push", (*list)[i]);
            }
        }
        return ValBinding::toWireType(arr, rvp::default_tag{});
    }

    template<typename Policy>
    static WireType toWireType(const csp::common::List<csp::multiplayer::SpaceEntity*>* list, Policy)
    {
        return toWireType(list);
    }
};

template<>
struct TypeID<const csp::common::List<csp::multiplayer::SpaceEntity*>*>
{
    static constexpr TYPEID get() { return TypeID<val>::get(); }
};


// Macro: allow a registered class pointer to pass through val (needed for callbacks).
// Without this, embind's TypeID<T*> static_asserts on raw pointers.
// Usage: CSP_ALLOW_RAW_POINTER_IN_VAL(csp::multiplayer::SpaceEntity)
#define CSP_ALLOW_RAW_POINTER_IN_VAL(Type)                                       \
    template<> struct BindingType<Type*> {                                        \
        using WireType = Type*;                                                   \
        static WireType toWireType(Type* p) { return p; }                        \
        template<typename P> static WireType toWireType(Type* p, P) { return p; }\
        static Type* fromWireType(WireType w) { return w; }                      \
    };                                                                            \
    template<> struct TypeID<Type*> {                                             \
        static constexpr TYPEID get() { return LightTypeID<Type*>::get(); }      \
    };

CSP_ALLOW_RAW_POINTER_IN_VAL(csp::multiplayer::SpaceEntity)

// Transparent conversion: csp::common::Optional<uint64_t> <-> JS null | BigInt
// JS null/undefined → empty Optional, JS number/BigInt → Optional with value.
template<>
struct BindingType<csp::common::Optional<uint64_t>>
{
    using ValBinding = BindingType<val>;
    using WireType = ValBinding::WireType;

    static WireType toWireType(const csp::common::Optional<uint64_t>& opt)
    {
        if (!opt.HasValue())
        {
            return ValBinding::toWireType(val::null(), rvp::default_tag{});
        }
        return ValBinding::toWireType(val(*opt), rvp::default_tag{});
    }

    static csp::common::Optional<uint64_t> fromWireType(WireType wire)
    {
        val v = ValBinding::fromWireType(wire);
        if (v.isNull() || v.isUndefined())
        {
            return csp::common::Optional<uint64_t>(nullptr);
        }
        uint64_t value = v.as<uint64_t>();
        return csp::common::Optional<uint64_t>(value);
    }
};

template<>
struct TypeID<csp::common::Optional<uint64_t>>
{
    static constexpr TYPEID get() { return TypeID<val>::get(); }
};

template<>
struct TypeID<const csp::common::Optional<uint64_t>&>
{
    static constexpr TYPEID get() { return TypeID<val>::get(); }
};

} // namespace emscripten::internal


// No-op script runner for use in the WASM context where ScriptSystem is not available.
// Every virtual in IJSScriptRunner throws by default, so we must override them all.
class NoOpScriptRunner : public csp::common::IJSScriptRunner
{
public:
    NoOpScriptRunner() = default;

    bool RunScript(int64_t, const csp::common::String&) override { return false; }
    void RegisterScriptBinding(csp::common::IScriptBinding*) override {}
    void UnregisterScriptBinding(csp::common::IScriptBinding*) override {}
    bool BindContext(int64_t) override { return false; }
    bool ResetContext(int64_t) override { return false; }
    void* GetContext(int64_t) override { return nullptr; }
    void* GetModule(int64_t, const csp::common::String&) override { return nullptr; }
    bool CreateContext(int64_t) override { return false; }
    bool DestroyContext(int64_t) override { return false; }
    void SetModuleSource(csp::common::String, csp::common::String) override {}
    void ClearModuleSource(csp::common::String) override {}
};

//Convert an JS val object to a capturing lambda compatible with std::function
template <typename Ret, typename... Args>
std::function<Ret(Args...)> val_to_function(emscripten::val v) {
    return [v](Args... args) -> Ret {
        if constexpr (std::is_void_v<Ret>) {
            v(std::forward<Args>(args)...);
        } else {
            // not fully sure what this .template as <Ret> thing is, it's a JS thing. Beware, LLM slop.
            return v(std::forward<Args>(args)...).template as<Ret>();
        }
    };
}

//Make a JS promise object
EM_JS(EM_VAL, make_promise_with_resolver, (), {
      let resolve, reject;
      const promise = new Promise((res, rej) => { resolve = res; reject = rej; });
      return Emval.toHandle({promise, resolve, reject});
  });

// Transform a callback into something that returns a JS promise
// single arg only for now, can do multi args but it's a bit more work, need
// to pack them into an object and figure out how to name them
template<typename CbArg, typename F>
val promisify(F&& f)
{
    val holder = val::take_ownership(make_promise_with_resolver());
    val resolve = holder["resolve"];

    std::function<void(CbArg)> callback = [resolve](CbArg arg) {
        // Arguably we could branch on failure here in some cases
        resolve(arg);
    };

    f(callback);

    return holder["promise"];
}

// C++ wrapper functions that bridge JS callbacks (emscripten::val) to std::function callbacks.

void OfflineRealtimeEngine_DestroyEntity(
    csp::multiplayer::OfflineRealtimeEngine& engine,
    csp::multiplayer::SpaceEntity* entity,
    val callback)
{
    engine.DestroyEntity(entity,
        [callback](bool success) {
            callback(success);
        });
}


EMSCRIPTEN_BINDINGS(csp_common) {
    // ---- Vector2 ----
    class_<csp::common::Vector2>("Vector2")
        .constructor<>()
        .constructor<float, float>()
        .property("X", &csp::common::Vector2::X)
        .property("Y", &csp::common::Vector2::Y)
        .class_function("Zero", &csp::common::Vector2::Zero)
        .class_function("One", &csp::common::Vector2::One)
        ;

    // ---- Vector3 ----
    class_<csp::common::Vector3>("Vector3")
        .constructor<>()
        .constructor<float, float, float>()
        .property("X", &csp::common::Vector3::X)
        .property("Y", &csp::common::Vector3::Y)
        .property("Z", &csp::common::Vector3::Z)
        .class_function("Zero", &csp::common::Vector3::Zero)
        .class_function("One", &csp::common::Vector3::One)
        ;

    // ---- Vector4 ----
    class_<csp::common::Vector4>("Vector4")
        .constructor<>()
        .constructor<float, float, float, float>()
        .property("X", &csp::common::Vector4::X)
        .property("Y", &csp::common::Vector4::Y)
        .property("Z", &csp::common::Vector4::Z)
        .property("W", &csp::common::Vector4::W)
        .class_function("Zero", &csp::common::Vector4::Zero)
        .class_function("One", &csp::common::Vector4::One)
        .class_function("Identity", &csp::common::Vector4::Identity)
        ;

    // ---- SpaceTransform ----
    class_<csp::multiplayer::SpaceTransform>("SpaceTransform")
        .constructor<>()
        .constructor<const csp::common::Vector3&, const csp::common::Vector4&, const csp::common::Vector3&>()
        .property("Position", &csp::multiplayer::SpaceTransform::Position)
        .property("Rotation", &csp::multiplayer::SpaceTransform::Rotation)
        .property("Scale", &csp::multiplayer::SpaceTransform::Scale)
        ;

    // ---- LogLevel ----
    enum_<csp::common::LogLevel>("LogLevel")
        .value("NoLogging", csp::common::LogLevel::NoLogging)
        .value("Fatal", csp::common::LogLevel::Fatal)
        .value("Error", csp::common::LogLevel::Error)
        .value("Warning", csp::common::LogLevel::Warning)
        .value("Display", csp::common::LogLevel::Display)
        .value("Log", csp::common::LogLevel::Log)
        .value("Verbose", csp::common::LogLevel::Verbose)
        .value("VeryVerbose", csp::common::LogLevel::VeryVerbose)
        .value("All", csp::common::LogLevel::All)
        ;

    // ---- LogSystem ----
    class_<csp::common::LogSystem>("LogSystem")
        .constructor<>()
        .function("SetSystemLevel", &csp::common::LogSystem::SetSystemLevel)
        .function("GetSystemLevel", &csp::common::LogSystem::GetSystemLevel)
        ;

    // ---- IJSScriptRunner ----
    class_<csp::common::IJSScriptRunner>("IJSScriptRunner")
        ;

    // ---- NoOpScriptRunner ----
    class_<NoOpScriptRunner, base<csp::common::IJSScriptRunner>>("NoOpScriptRunner")
        .constructor<>()
        ;

    // ---- CSPSceneDescription ----
    class_<csp::multiplayer::CSPSceneDescription>("CSPSceneDescription")
        .constructor<>()
        .constructor<const csp::common::List<csp::common::String>&>()
        ;

    // ---- RealtimeEngineType ----
    enum_<csp::common::RealtimeEngineType>("RealtimeEngineType")
        .value("Online", csp::common::RealtimeEngineType::Online)
        .value("Offline", csp::common::RealtimeEngineType::Offline)
        ;

    // ---- AvatarState ----
    enum_<csp::multiplayer::AvatarState>("AvatarState")
        .value("Idle", csp::multiplayer::AvatarState::Idle)
        .value("Walking", csp::multiplayer::AvatarState::Walking)
        .value("Running", csp::multiplayer::AvatarState::Running)
        .value("Flying", csp::multiplayer::AvatarState::Flying)
        .value("Jumping", csp::multiplayer::AvatarState::Jumping)
        .value("Falling", csp::multiplayer::AvatarState::Falling)
        ;

    // ---- AvatarPlayMode ----
    enum_<csp::multiplayer::AvatarPlayMode>("AvatarPlayMode")
        .value("Default", csp::multiplayer::AvatarPlayMode::Default)
        .value("AR", csp::multiplayer::AvatarPlayMode::AR)
        .value("VR", csp::multiplayer::AvatarPlayMode::VR)
        .value("Creator", csp::multiplayer::AvatarPlayMode::Creator)
        ;

    // ---- LocomotionModel ----
    enum_<csp::multiplayer::LocomotionModel>("LocomotionModel")
        .value("Grounded", csp::multiplayer::LocomotionModel::Grounded)
        .value("FreeCamera", csp::multiplayer::LocomotionModel::FreeCamera)
        ;

    // ---- ModifiableStatus ----
    enum_<csp::multiplayer::ModifiableStatus>("ModifiableStatus")
        .value("Modifiable", csp::multiplayer::ModifiableStatus::Modifiable)
        .value("EntityLocked", csp::multiplayer::ModifiableStatus::EntityLocked)
        .value("EntityNotOwnedAndUntransferable", csp::multiplayer::ModifiableStatus::EntityNotOwnedAndUntransferable)
        ;

    // ---- SpaceEntity ----
    class_<csp::multiplayer::SpaceEntity>("SpaceEntity")
        .function("GetId", &csp::multiplayer::SpaceEntity::GetId)
        .function("GetName", &csp::multiplayer::SpaceEntity::GetName)
        ;

    // ---- IRealtimeEngine ----
    class_<csp::common::IRealtimeEngine>("IRealtimeEngine")
        .function("GetRealtimeEngineType", &csp::common::IRealtimeEngine::GetRealtimeEngineType)
        .function("FindSpaceEntity", &csp::common::IRealtimeEngine::FindSpaceEntity, allow_raw_pointers())
        .function("FindSpaceEntityById", &csp::common::IRealtimeEngine::FindSpaceEntityById, allow_raw_pointers())
        .function("FindSpaceAvatar", &csp::common::IRealtimeEngine::FindSpaceAvatar, allow_raw_pointers())
        .function("FindSpaceObject", &csp::common::IRealtimeEngine::FindSpaceObject, allow_raw_pointers())
        .function("GetEntityByIndex", &csp::common::IRealtimeEngine::GetEntityByIndex, allow_raw_pointers())
        .function("GetAvatarByIndex", &csp::common::IRealtimeEngine::GetAvatarByIndex, allow_raw_pointers())
        .function("GetObjectByIndex", &csp::common::IRealtimeEngine::GetObjectByIndex, allow_raw_pointers())
        .function("GetNumEntities", &csp::common::IRealtimeEngine::GetNumEntities)
        .function("GetNumAvatars", &csp::common::IRealtimeEngine::GetNumAvatars)
        .function("GetNumObjects", &csp::common::IRealtimeEngine::GetNumObjects)
        .function("IsEntityModifiable", &csp::common::IRealtimeEngine::IsEntityModifiable, allow_raw_pointers())
        ;


    //Prove the point about pinning, we use it for the off-thread callback as it's actually neccesary there to even function (as we are deferring the callback to a queue)
    //Would be neccesary to pin for all actually async operations, the offline engine protects us sometwhat.
    //This will evolve into a buffer, or more realistically an entire `PinManager` type. But this proves that it works
    static std::unique_ptr<emscripten::val> pinnedCallback;


    // ---- OfflineRealtimeEngine ----
    class_<csp::multiplayer::OfflineRealtimeEngine, base<csp::common::IRealtimeEngine>>("OfflineRealtimeEngine")
        .constructor<csp::common::LogSystem&, csp::common::IJSScriptRunner&>()
        .constructor<const csp::multiplayer::CSPSceneDescription&, csp::common::LogSystem&, csp::common::IJSScriptRunner&>()
        .function("GetRealtimeEngineType", &csp::multiplayer::OfflineRealtimeEngine::GetRealtimeEngineType)
        .function("FindSpaceEntity", &csp::multiplayer::OfflineRealtimeEngine::FindSpaceEntity, allow_raw_pointers())
        .function("FindSpaceEntityById", &csp::multiplayer::OfflineRealtimeEngine::FindSpaceEntityById, allow_raw_pointers())
        .function("FindSpaceAvatar", &csp::multiplayer::OfflineRealtimeEngine::FindSpaceAvatar, allow_raw_pointers())
        .function("FindSpaceObject", &csp::multiplayer::OfflineRealtimeEngine::FindSpaceObject, allow_raw_pointers())
        .function("GetEntityByIndex", &csp::multiplayer::OfflineRealtimeEngine::GetEntityByIndex, allow_raw_pointers())
        .function("GetAvatarByIndex", &csp::multiplayer::OfflineRealtimeEngine::GetAvatarByIndex, allow_raw_pointers())
        .function("GetObjectByIndex", &csp::multiplayer::OfflineRealtimeEngine::GetObjectByIndex, allow_raw_pointers())
        .function("GetNumEntities", &csp::multiplayer::OfflineRealtimeEngine::GetNumEntities)
        .function("GetNumAvatars", &csp::multiplayer::OfflineRealtimeEngine::GetNumAvatars)
        .function("GetNumObjects", &csp::multiplayer::OfflineRealtimeEngine::GetNumObjects)
        .function("IsEntityModifiable", &csp::multiplayer::OfflineRealtimeEngine::IsEntityModifiable, allow_raw_pointers())
        .class_function("LocalClientId", &csp::multiplayer::OfflineRealtimeEngine::LocalClientId)

        // What's better, inline declarations or wrapper methods?, for callbacks.
        // The + is an implicit conversion trick to get out of having to type static_cast<void*>
        .function("CreateEntity", +[](csp::multiplayer::OfflineRealtimeEngine& self,
                                     const csp::common::String& name,
                                     const csp::multiplayer::SpaceTransform& transform,
                                     const csp::common::Optional<uint64_t>& parentID,
                                     val callback){
                                        self.CreateEntity(name, transform, parentID, val_to_function<void, csp::multiplayer::SpaceEntity*>(callback));
                                     }, allow_raw_pointers())
        .function("CreateEntityAsync", +[](csp::multiplayer::OfflineRealtimeEngine& self,
                                     const csp::common::String& name,
                                     const csp::multiplayer::SpaceTransform& transform,
                                     const csp::common::Optional<uint64_t>& parentID) -> val {
                                        //Not so bad as a way of bridging callbacks to async. A bit templatey...
                                        return promisify<csp::multiplayer::SpaceEntity*>([&](auto cb) {
                                            self.CreateEntity(name, transform, parentID, cb);
                                        });
                                     }, allow_raw_pointers())
        .function("DestroyEntity", &OfflineRealtimeEngine_DestroyEntity, allow_raw_pointers())
        .function("GetAllEntities", &csp::multiplayer::OfflineRealtimeEngine::GetAllEntities)
        .function("SpawnIntOffThreadAfterTimeWithoutOffThreadAdaptation", +[](csp::multiplayer::OfflineRealtimeEngine& self,
                                                    float time,
                                                    val callback){
                                                        //Doing this will cause a "Val accessed from wrong thread" crash
                                                        self.SpawnIntOffThreadAfterTime(time, callback);
            
                                                    })
        .function("SpawnIntOffThreadAfterTime", +[](csp::multiplayer::OfflineRealtimeEngine& self,
                                                    float time,
                                                    val callback){
                                                        pinnedCallback = std::make_unique<val>(std::move(callback));
                                                        auto onThreadCB = [](int outVal){
                                                            // This is actually calling a statically pinned callback, note no need to capture, probably what everything should do.
                                                            csp::Emscripten_CallbackOnThreadEmbind(*pinnedCallback, outVal);
                                                        };
                                                        self.SpawnIntOffThreadAfterTime(time, onThreadCB);
                                                    })
   
        ;
}
