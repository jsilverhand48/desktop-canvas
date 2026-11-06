// Implementation of ScriptEngine declared in ScriptEngine.h; section
// numbers refer to src/scene/script/README.md.
#include "scene/script/ScriptEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <utility>

#include "scene/render/Animation.h"

#ifdef CANVAS_SCENE_HAVE_QUICKJS
#include <quickjs.h>
#endif

namespace canvas::scene {

#ifndef CANVAS_SCENE_HAVE_QUICKJS

struct ScriptEngine::Impl {};

bool ScriptEngine::available() { return false; }

std::unique_ptr<ScriptEngine> ScriptEngine::create(LoadedScene&, const AssetLocator&, Animator*, ScriptHost&,
                                                   Options, std::string& error) {
    error = "built without QuickJS";
    return nullptr;
}

ScriptEngine::ScriptEngine(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ScriptEngine::~ScriptEngine() = default;
size_t ScriptEngine::scriptCount() const { return 0; }
size_t ScriptEngine::failedCount() const { return 0; }
bool ScriptEngine::usesAudio() const { return false; }
void ScriptEngine::setMedia(const MediaInfo&, const AlbumArtColors&) {}
void ScriptEngine::update(const ScriptFrame&) {}
void ScriptEngine::userPropertiesChanged(const std::vector<std::string>&) {}
std::vector<std::string> ScriptEngine::takeLog() { return {}; }
std::vector<std::string> ScriptEngine::takeProblems() { return {}; }

#else

namespace {

using nlohmann::json;
using Clock = std::chrono::steady_clock;

// This project's globals over a native bridge `n` (sections 3 and 4).
// Wallpaper Engine's base classes are already loaded.
const char* const kPrelude = R"JS((function (n) {
'use strict';
const g = globalThis;
const layers = [];
function layer(i) {
  if (typeof i !== 'number' || i < 0 || i >= n.layerCount()) return undefined;
  if (!layers[i]) layers[i] = new Layer(i);
  return layers[i];
}
function indexOf(x) {
  if (x instanceof Layer) return x.__i;
  if (typeof x === 'number') return Math.trunc(x);
  if (typeof x === 'string') return n.layerByName(x);
  return -1;
}
const hidden = (o, key, value) => Object.defineProperty(o, key, { value });
const ro = (o, name, get) => Object.defineProperty(o, name, { get, set(v) {}, enumerable: true, configurable: true });
class Layer {
  constructor(i) { hidden(this, '__i', i); }
  getParent() { return layer(n.parent(this.__i)); }
  getChildren() { return n.children(this.__i).map(layer); }
  getEffect(x) { const e = n.effectIndex(this.__i, x); return e < 0 ? undefined : new Effect(this.__i, e); }
  getEffectCount() { return n.effectCount(this.__i); }
  getAnimation(name) {
    const a = n.animation(this.__i, name === undefined ? '' : String(name));
    return a < 0 ? undefined : new Animation(a);
  }
  getTextureAnimation() { return still; }
  getVideoTexture() { return undefined; }
  getAnimationLayerCount() { return 0; }
  getAnimationLayer() { return undefined; }
  play() { n.particle(this.__i, 0); }
  pause() { n.particle(this.__i, 1); }
  stop() { n.particle(this.__i, 2); }
  isPlaying() { return n.particlePlaying(this.__i); }
  emitParticles() {}
}
for (const p of ['origin', 'angles', 'scale', 'visible', 'parallaxDepth', 'alpha', 'color', 'alignment', 'text',
                 'pointsize', 'padding', 'horizontalalign', 'verticalalign', 'anchor', 'limitrows', 'maxrows',
                 'limitwidth', 'maxwidth', 'opaquebackground', 'backgroundcolor', 'volume'])
  Object.defineProperty(Layer.prototype, p, { get() { return n.get(this.__i, p); },
                                              set(v) { n.set(this.__i, p, v); }, configurable: true });
for (const p of ['name', 'size', 'font', 'id'])
  ro(Layer.prototype, p, function () { return n.get(this.__i, p); });
ro(Layer.prototype, 'instance', function () { return n.isParticle(this.__i) ? new Instance(this.__i) : undefined; });
class Instance { constructor(i) { hidden(this, '__i', i); } }
for (const p of ['alpha', 'size', 'count', 'speed', 'lifetime', 'rate', 'colorn', 'color', 'brightness',
                 'controlpoint0', 'controlpoint1', 'controlpoint2', 'controlpoint3', 'controlpoint4',
                 'controlpoint5', 'controlpoint6', 'controlpoint7'])
  Object.defineProperty(Instance.prototype, p, { get() { return n.instanceGet(this.__i, p); },
                                                 set(v) { n.instanceSet(this.__i, p, v); }, configurable: true });
class Effect {
  constructor(i, e) { hidden(this, '__i', i); hidden(this, '__e', e); }
  get visible() { return n.effectGet(this.__i, this.__e, 'visible'); }
  set visible(v) { n.effectSet(this.__i, this.__e, 'visible', v); }
  get name() { return n.effectGet(this.__i, this.__e, 'name'); }
  set name(v) {}
  getMaterialCount() { return 0; }
  getMaterial() { return undefined; }
  setMaterialProperty(name, value) { n.effectConstant(this.__i, this.__e, String(name), value); }
  executeMaterialFunction() {}
}
class Animation {
  constructor(a) { hidden(this, '__a', a); }
  play() { n.animationCall(this.__a, 0); }
  pause() { n.animationCall(this.__a, 1); }
  stop() { n.animationCall(this.__a, 2); }
  isPlaying() { return n.animationGet(this.__a, 'playing'); }
  getFrame() { return n.animationGet(this.__a, 'frame'); }
  setFrame(f) { n.animationSet(this.__a, 'frame', f); }
  get rate() { return n.animationGet(this.__a, 'rate'); }
  set rate(v) { n.animationSet(this.__a, 'rate', v); }
}
for (const p of ['fps', 'frameCount', 'duration', 'name'])
  ro(Animation.prototype, p, function () { return n.animationGet(this.__a, p); });
const still = { frameCount: 1, duration: 0, rate: 1, play() {}, pause() {}, stop() {},
                isPlaying() { return false; }, getFrame() { return 0; }, setFrame() {}, join() {} };
const audio = [];
const engine = {
  isRunningInEditor() { return false; },
  isWallpaper() { return true; },
  isScreensaver() { return false; },
  isDesktopDevice() { return true; },
  isMobileDevice() { return false; },
  isPortrait() { return n.frame.screenH > n.frame.screenW; },
  isLandscape() { return n.frame.screenW >= n.frame.screenH; },
  registerAudioBuffers(r) {
    r = (r === 16 || r === 32 || r === 64) ? r : 16;
    const b = { left: new Float32Array(r), right: new Float32Array(r), average: new Float32Array(r) };
    audio.push(b);
    return b;
  },
  registerAsset() { return null; },
  setTimeout(fn, ms) { const id = n.timer(fn, Number(ms) || 0, false); return () => n.cancel(id); },
  setInterval(fn, ms) { const id = n.timer(fn, Number(ms) || 0, true); return () => n.cancel(id); },
  openUserShortcut() { return false; },
};
ro(engine, 'frametime', () => n.frame.frametime);
ro(engine, 'runtime', () => n.frame.runtime);
ro(engine, 'timeOfDay', () => n.frame.timeOfDay);
ro(engine, 'canvasSize', () => new Vec2(n.frame.canvasW, n.frame.canvasH));
ro(engine, 'screenResolution', () => new Vec2(n.frame.screenW, n.frame.screenH));
ro(engine, 'userProperties', () => _Internal.convertUserProperties(n.userProperties()));
ro(engine, 'AUDIO_RESOLUTION_16', () => 16);
ro(engine, 'AUDIO_RESOLUTION_32', () => 32);
ro(engine, 'AUDIO_RESOLUTION_64', () => 64);
const scene = {
  getLayer(x) { return layer(indexOf(x)); },
  getLayerByID(id) { return layer(n.layerById(String(id))); },
  getLayerCount() { return n.layerCount(); },
  enumerateLayers() { const out = []; for (let i = 0; i < n.layerCount(); i++) out.push(layer(i)); return out; },
  getLayerIndex(x) { return indexOf(x); },
  getInitialLayerConfig(x) { const i = indexOf(x); return layer(i) ? JSON.parse(n.initialConfig(i)) : undefined; },
  sortLayer() { return false; },
  createLayer() { return undefined; },
  destroyLayer() { return false; },
  getAnimation(name) {
    const a = n.animation(-1, name === undefined ? '' : String(name));
    return a < 0 ? undefined : new Animation(a);
  },
  getCameraTransforms() { return undefined; },
  setCameraTransforms() {},
  createModelData() { return undefined; },
  destroyModelData() {},
};
for (const p of ['bloom', 'bloomstrength', 'bloomthreshold', 'clearcolor', 'clearenabled', 'camerafade',
                 'camerashake', 'camerashakeamplitude', 'camerashakeroughness', 'camerashakespeed',
                 'cameraparallax', 'cameraparallaxamount', 'cameraparallaxdelay', 'cameraparallaxmouseinfluence',
                 'ambientcolor', 'skylightcolor', 'fov', 'nearz', 'farz'])
  Object.defineProperty(scene, p, { get() { return n.sceneGet(p); }, set(v) { n.sceneSet(p, v); },
                                    enumerable: true });
const input = {};
ro(input, 'cursorWorldPosition', () => new Vec3(n.frame.cursorX, n.frame.cursorY, n.frame.cursorZ));
ro(input, 'cursorScreenPosition', () => new Vec2(n.frame.screenX, n.frame.screenY));
ro(input, 'cursorLeftDown', () => n.frame.cursorDown);
const text = (args) => args.map((a) => {
  if (typeof a === 'string') return a;
  try { return (a !== null && typeof a === 'object' && a.toString === Object.prototype.toString) ? JSON.stringify(a) : String(a); }
  catch (e) { return String(a); }
}).join(' ');
const log = {
  log(...a) { n.log(0, text(a)); }, info(...a) { n.log(0, text(a)); }, debug(...a) {},
  warn(...a) { n.log(1, text(a)); }, error(...a) { n.log(1, text(a)); },
};
const where = (loc) => loc === 'screen' ? 'screen' : 'global';
const storage = {
  LOCATION_GLOBAL: 'global',
  LOCATION_SCREEN: 'screen',
  get(key, loc) { const v = n.storageGet(where(loc), String(key)); return v === undefined ? undefined : JSON.parse(v); },
  set(key, value, loc) { n.storageSet(where(loc), String(key), value === undefined ? undefined : JSON.stringify(value)); },
  delete(key, loc) { return n.storageDelete(where(loc), String(key)); },
  remove(key, loc) { return n.storageDelete(where(loc), String(key)); },
  clear(loc) { n.storageClear(where(loc)); },
};
const fixed = (name, value) => Object.defineProperty(g, name, { value, enumerable: true });
fixed('engine', engine);
fixed('thisScene', scene);
fixed('input', input);
fixed('localStorage', storage);
fixed('ILocalStorage', { LOCATION_GLOBAL: 'global', LOCATION_SCREEN: 'screen' });
g.console = log;
g.shared = {};
g.thisLayer = undefined;
g.thisObject = undefined;
n.layer = layer;
n.scene = scene;
n.cursorEvent = (wx, wy, wz, lx, ly, lz) => ({ worldPosition: new Vec3(wx, wy, wz), localPosition: new Vec3(lx, ly, lz) });
n.audio = audio;
}))JS";

// Native bridge operations (the `magic` of one dispatcher).
enum Op {
    kLayerCount, kLayerByName, kLayerById, kParent, kChildren, kGet, kSet, kIsParticle, kInstanceGet,
    kInstanceSet, kEffectIndex, kEffectCount, kEffectGet, kEffectSet, kEffectConstant, kAnimation,
    kAnimationCall, kAnimationGet, kAnimationSet, kParticle, kParticlePlaying, kSceneGet, kSceneSet,
    kInitialConfig, kUserProperties, kTimer, kCancel, kLog, kStorageGet, kStorageSet, kStorageDelete,
    kStorageClear,
};

struct OpName {
    const char* name;
    Op op;
    int length;
};

const OpName kOps[] = {
    {"layerCount", kLayerCount, 0},     {"layerByName", kLayerByName, 1},
    {"layerById", kLayerById, 1},       {"parent", kParent, 1},
    {"children", kChildren, 1},         {"get", kGet, 2},
    {"set", kSet, 3},                   {"isParticle", kIsParticle, 1},
    {"instanceGet", kInstanceGet, 2},   {"instanceSet", kInstanceSet, 3},
    {"effectIndex", kEffectIndex, 2},   {"effectCount", kEffectCount, 1},
    {"effectGet", kEffectGet, 3},       {"effectSet", kEffectSet, 4},
    {"effectConstant", kEffectConstant, 4}, {"animation", kAnimation, 2},
    {"animationCall", kAnimationCall, 2}, {"animationGet", kAnimationGet, 2},
    {"animationSet", kAnimationSet, 3}, {"particle", kParticle, 2},
    {"particlePlaying", kParticlePlaying, 1}, {"sceneGet", kSceneGet, 1},
    {"sceneSet", kSceneSet, 2},         {"initialConfig", kInitialConfig, 1},
    {"userProperties", kUserProperties, 0}, {"timer", kTimer, 3},
    {"cancel", kCancel, 1},             {"log", kLog, 2},
    {"storageGet", kStorageGet, 2},     {"storageSet", kStorageSet, 3},
    {"storageDelete", kStorageDelete, 2}, {"storageClear", kStorageClear, 1},
};

int interruptHandler(JSRuntime* rt, void*);

}  // namespace

struct ScriptEngine::Impl {
    struct Script {
        std::string where;
        int64_t object = -1;  // -1: scene settings or materials
        std::string source;
        json properties;  // scene file scriptproperties, null without
        std::function<JSValue()> get;
        std::function<void(JSValueConst)> set;
        JSValue ns = JS_UNDEFINED;
        JSValue init = JS_UNDEFINED, update = JS_UNDEFINED, apply = JS_UNDEFINED, destroy = JS_UNDEFINED;
        std::map<std::string, JSValue> events;  // cursor, resize and media events by name
        bool started = false, failed = false;
        // Media state this script was told (src/scene/audio/README.md
        // section 7); nothing yet until mediaTold.
        bool mediaTold = false, mediaEnabled = false;
        int mediaState = -1;
        MediaInfo mediaProps;
        bool propsTold = false;
        std::string artTold;
        bool artToldOnce = false;
        uint64_t timelineTold = 0;
    };
    struct Timer {
        uint64_t id = 0;
        double due = 0, interval = 0;
        bool repeat = false, cancelled = false;
        int64_t object = -1;
        JSValue fn = JS_UNDEFINED;
    };

    LoadedScene& scene;
    const AssetLocator& assets;
    Animator* animator;
    ScriptHost& host;
    ScriptEngine::Options options;
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;
    JSValue global = JS_UNDEFINED, native = JS_UNDEFINED, frameObject = JS_UNDEFINED;
    JSValue vecCtor[3] = {JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED};  // Vec2, Vec3, Vec4
    JSValue internal = JS_UNDEFINED;
    std::vector<Script> scripts;
    std::vector<Timer> timers;
    uint64_t nextTimer = 1;
    std::vector<AnimationPlayback*> animations;
    ScriptFrame frame;
    std::set<std::string> changed;
    int64_t hovered = -1, pressed = -1;
    bool wasDown = false;
    Vec3 lastCursor{0, 0, 0};
    bool haveCursor = false;
    Vec2 lastScreen{0, 0};
    bool haveScreen = false;
    int64_t current = -1;  // object of the running script (timers keep it)
    Script* running = nullptr;
    std::vector<std::string> log;       // console lines
    MediaInfo media;                    // the latest media state (section 7)
    AlbumArtColors art;
    bool mediaKnown = false;
    std::vector<std::string> problems;  // failures, each once
    std::map<const Script*, std::pair<double, int>> logBudget;  // minute start, lines
    json storage = json{{"global", json::object()}, {"screen", json::object()}};
    bool storageDirty = false;
    Clock::time_point deadline = Clock::time_point::max();

    Impl(LoadedScene& s, const AssetLocator& a, Animator* anim, ScriptHost& h, ScriptEngine::Options o)
        : scene(s), assets(a), animator(anim), host(h), options(std::move(o)) {}

    ~Impl() {
        if (!ctx) {
            if (rt) JS_FreeRuntime(rt);
            return;
        }
        for (auto& s : scripts) {
            if (s.started && !s.failed && JS_IsFunction(ctx, s.destroy)) call(s, s.destroy, 0, nullptr);
        }
        for (auto& s : scripts) releaseScript(s);
        for (auto& t : timers) JS_FreeValue(ctx, t.fn);
        for (JSValue& v : vecCtor) JS_FreeValue(ctx, v);
        for (JSValue v : {internal, frameObject, native, global}) JS_FreeValue(ctx, v);
        saveStorage();
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
    }

    void releaseScript(Script& s) {
        for (JSValue v : {s.ns, s.init, s.update, s.apply, s.destroy}) JS_FreeValue(ctx, v);
        for (auto& [name, fn] : s.events) JS_FreeValue(ctx, fn);
        s.ns = s.init = s.update = s.apply = s.destroy = JS_UNDEFINED;
        s.events.clear();
    }

    // ------------------------------------------------------------ text

    std::string exceptionText() {
        JSValue e = JS_GetException(ctx);
        std::string out;
        if (const char* s = JS_ToCString(ctx, e)) {
            out = s;
            JS_FreeCString(ctx, s);
        }
        if (JS_IsObject(e)) {
            JSValue stack = JS_GetPropertyStr(ctx, e, "stack");
            if (const char* s = JS_IsString(stack) ? JS_ToCString(ctx, stack) : nullptr) {
                std::string first = s;
                JS_FreeCString(ctx, s);
                if (auto nl = first.find('\n'); nl != std::string::npos) first.resize(nl);
                if (!first.empty()) out += " (" + first + ")";
            }
            JS_FreeValue(ctx, stack);
        }
        JS_FreeValue(ctx, e);
        return out.empty() ? "unknown error" : out;
    }

    void fail(Script& s, const std::string& what) {
        if (s.failed) return;
        s.failed = true;
        problems.push_back(s.where + ": " + what + "; script stopped");
    }

    // ------------------------------------------------------- conversion

    JSValue vec(const float* v, int count) {
        JSValue args[4];
        for (int i = 0; i < count; i++) args[i] = JS_NewFloat64(ctx, double(v[i]));
        JSValue out = JS_CallConstructor(ctx, vecCtor[count - 2], count, args);
        for (int i = 0; i < count; i++) JS_FreeValue(ctx, args[i]);
        return out;
    }
    JSValue toJs(bool v) { return JS_NewBool(ctx, v); }
    JSValue toJs(float v) { return JS_NewFloat64(ctx, double(v)); }
    JSValue toJs(int64_t v) { return JS_NewInt64(ctx, v); }
    JSValue toJs(const std::string& v) { return JS_NewStringLen(ctx, v.data(), v.size()); }
    JSValue toJs(const Vec2& v) { return vec(v.data(), 2); }
    JSValue toJs(const Vec3& v) { return vec(v.data(), 3); }
    JSValue toJs(const Vec4& v) { return vec(v.data(), 4); }
    JSValue toJs(const Floats& f) { return f.count <= 1 ? JS_NewFloat64(ctx, double(f.v[0])) : vec(f.v.data(), f.count); }

    std::optional<std::string> string(JSValueConst v) {
        if (!JS_IsString(v) && !JS_IsNumber(v) && !JS_IsBool(v)) return std::nullopt;
        const char* s = JS_ToCString(ctx, v);
        if (!s) return std::nullopt;
        std::string out = s;
        JS_FreeCString(ctx, s);
        return out;
    }
    std::optional<double> number(JSValueConst v) {
        if (JS_IsNumber(v) || JS_IsBool(v)) {
            double d = 0;
            if (JS_ToFloat64(ctx, &d, v) == 0 && std::isfinite(d)) return d;
            return std::nullopt;
        }
        if (JS_IsString(v)) {
            auto s = string(v);
            float f = 0;
            if (s && readText(*s, f)) return double(f);
        }
        return std::nullopt;
    }
    // Numeric x, y (z, w) of an object, as many as present.
    int components(JSValueConst v, float* out) {
        if (!JS_IsObject(v)) return 0;
        int n = 0;
        for (const char* key : {"x", "y", "z", "w"}) {
            JSValue c = JS_GetPropertyStr(ctx, v, key);
            double d = 0;
            const bool ok = JS_IsNumber(c) && JS_ToFloat64(ctx, &d, c) == 0 && std::isfinite(d);
            JS_FreeValue(ctx, c);
            if (!ok) break;
            out[n++] = float(d);
        }
        return n;
    }

    // Script values to field types (section 2: as the type allows).
    bool fromJs(JSValueConst v, bool& out) {
        if (JS_IsBool(v) || JS_IsNumber(v)) {
            out = JS_ToBool(ctx, v) > 0;
            return true;
        }
        if (auto s = JS_IsString(v) ? string(v) : std::nullopt) return readText(*s, out);
        return false;
    }
    bool fromJs(JSValueConst v, float& out) {
        if (auto d = number(v)) {
            out = float(*d);
            return true;
        }
        return false;
    }
    bool fromJs(JSValueConst v, int64_t& out) {
        if (auto d = number(v)) {
            out = int64_t(std::llround(*d));
            return true;
        }
        return false;
    }
    bool fromJs(JSValueConst v, std::string& out) {
        if (auto s = string(v)) {
            out = *s;
            return true;
        }
        return false;
    }
    template <size_t N>
    bool fromJs(JSValueConst v, std::array<float, N>& out) {
        if (auto d = number(v)) {
            out.fill(float(*d));
            return true;
        }
        float c[4];
        const int n = components(v, c);
        if (n < 2) return false;
        for (size_t i = 0; i < N && i < size_t(n); i++) out[i] = c[i];
        return true;
    }
    bool fromJs(JSValueConst v, Floats& out) {
        if (auto d = number(v)) {
            out.v[0] = float(*d);
            out.count = std::max(out.count, 1);
            return true;
        }
        float c[4];
        const int n = components(v, c);
        if (n < 2) return false;
        for (int i = 0; i < n; i++) out.v[size_t(i)] = c[i];
        out.count = std::max(out.count, n);
        return true;
    }
    // A field from a script value; null resets it to its scene file value.
    template <class T>
    void assign(Field<T>& field, JSValueConst v) {
        if (JS_IsUndefined(v)) return;
        if (JS_IsNull(v)) {
            field.value = field.stored;
            return;
        }
        T out = field.value;
        if (fromJs(v, out)) field.value = out;
    }

    // --------------------------------------------------------- user data

    // A user property's value as JSON for its type.
    json propertyJson(const UserProperty& p, const std::string& text) {
        if (p.type == "bool") return text == "true" || text == "1";
        if (p.type == "slider") return std::strtod(text.c_str(), nullptr);
        if (p.type == "combo") {
            char* end = nullptr;
            const double d = std::strtod(text.c_str(), &end);
            if (!text.empty() && end && *end == '\0') return d;
        }
        return text;
    }
    const UserProperty* property(const std::string& name) {
        for (const auto& p : scene.project.properties)
            if (p.name == name) return &p;
        return nullptr;
    }
    std::string propertyText(const UserProperty& p) {
        auto it = scene.values.find(p.name);
        return it != scene.values.end() ? it->second : p.value;
    }
    // {name: {type, value}} for _Internal.convertUserProperties.
    std::string userPropertiesJson(const std::set<std::string>* only) {
        json out = json::object();
        for (const auto& p : scene.project.properties) {
            if (p.type.empty() || p.type == "group" || p.type == "text") continue;
            if (only && !only->count(p.name)) continue;
            if (p.type == "usershortcut")
                out[p.name] = {{"type", p.type}, {"isbound", false}, {"commandtype", ""}, {"file", ""}};
            else
                out[p.name] = {{"type", p.type}, {"value", propertyJson(p, propertyText(p))}};
        }
        return out.dump();
    }
    // A script's scene file script properties, bound ones from their user
    // property; with `only`, just the ones bound to those properties.
    std::optional<std::string> scriptPropertiesJson(const Script& s, const std::set<std::string>* only) {
        if (!s.properties.is_object()) return std::nullopt;
        json out = json::object();
        for (const auto& [key, v] : s.properties.items()) {
            std::string bound;
            if (v.is_object() && v.contains("user")) {
                const json& u = v["user"];
                if (u.is_string()) bound = u.get<std::string>();
                else if (u.is_object() && u.contains("name") && u["name"].is_string())
                    bound = u["name"].get<std::string>();
            }
            if (only && (bound.empty() || !only->count(bound))) continue;
            const UserProperty* p = bound.empty() ? nullptr : property(bound);
            if (p) out[key] = propertyJson(*p, propertyText(*p));
            else if (v.is_object() && v.contains("value")) out[key] = v["value"];
            else if (!v.is_object()) out[key] = v;
        }
        if (only && out.empty()) return std::nullopt;
        return out.dump();
    }

    JSValue internalCall(const char* name, const std::string& arg) {
        JSValue fn = JS_GetPropertyStr(ctx, internal, name);
        JSValue a = JS_NewStringLen(ctx, arg.data(), arg.size());
        JSValue r = JS_Call(ctx, fn, internal, 1, &a);
        JS_FreeValue(ctx, a);
        JS_FreeValue(ctx, fn);
        return r;
    }

    // ---------------------------------------------------------- calling

    void setThis(int64_t object) {
        current = object;
        JSValue layer = JS_UNDEFINED, obj = JS_UNDEFINED;
        if (object >= 0) {
            JSValue fn = JS_GetPropertyStr(ctx, native, "layer");
            JSValue i = JS_NewInt64(ctx, object);
            layer = JS_Call(ctx, fn, JS_UNDEFINED, 1, &i);
            JS_FreeValue(ctx, fn);
            obj = JS_DupValue(ctx, layer);
        } else {
            obj = JS_GetPropertyStr(ctx, native, "scene");
        }
        JS_SetPropertyStr(ctx, global, "thisLayer", layer);
        JS_SetPropertyStr(ctx, global, "thisObject", obj);
    }

    void runJobs() {
        JSContext* c = nullptr;
        for (int i = 0; i < 10000; i++)
            if (JS_ExecutePendingJob(rt, &c) <= 0) break;
    }

    // Calls fn with the script's this, under the call budget; the result
    // (owned) or JS_EXCEPTION with the script failed.
    JSValue call(Script& s, JSValueConst fn, int argc, JSValueConst* argv) {
        setThis(s.object);
        running = &s;
        deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                      std::chrono::duration<double>(options.callBudget));
        JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);
        deadline = Clock::time_point::max();
        running = nullptr;
        if (JS_IsException(r)) fail(s, exceptionText());
        return r;
    }

    void callWithValue(Script& s, JSValueConst fn, bool keepResult) {
        JSValue arg = s.get();
        JSValue r = call(s, fn, 1, &arg);
        JS_FreeValue(ctx, arg);
        if (!JS_IsException(r) && keepResult) s.set(r);
        JS_FreeValue(ctx, r);
    }

    void callEvent(Script& s, const char* name, JSValueConst event) {
        auto it = s.events.find(name);
        if (s.failed || it == s.events.end()) return;
        JSValue r = call(s, it->second, 1, &event);
        JS_FreeValue(ctx, r);
    }

    void applyProperties(Script& s, const std::set<std::string>* only) {
        if (!JS_IsFunction(ctx, s.apply)) return;
        JSValue props = internalCall("convertUserProperties", userPropertiesJson(only));
        if (JS_IsException(props)) {
            fail(s, exceptionText());
            return;
        }
        JSValue r = call(s, s.apply, 1, &props);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, props);
    }

    void sendScriptProperties(Script& s, const std::set<std::string>* only) {
        auto text = scriptPropertiesJson(s, only);
        if (!text) return;
        JSValue fn = JS_GetPropertyStr(ctx, internal, "updateScriptProperties");
        JSValue args[2] = {JS_DupValue(ctx, s.ns), JS_NewStringLen(ctx, text->data(), text->size())};
        setThis(s.object);
        JSValue r = JS_Call(ctx, fn, internal, 2, args);
        if (JS_IsException(r)) problems.push_back(s.where + ": script properties: " + exceptionText());
        for (JSValue v : args) JS_FreeValue(ctx, v);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, fn);
    }

    // ---------------------------------------------------------- loading

    template <class T>
    void addScript(Field<T>& field, int64_t object, const std::string& where) {
        if (!field.scripted()) return;
        Script s;
        s.where = where;
        s.object = object;
        s.source = field.dynamics->script;
        s.properties = field.dynamics->scriptProperties;
        Field<T>* f = &field;
        s.get = [this, f]() { return toJs(f->value); };
        s.set = [this, f](JSValueConst v) { assign(*f, v); };
        scripts.push_back(std::move(s));
    }

    void collectScripts() {
        auto& objects = scene.scene.objects;
        for (size_t i = 0; i < objects.size(); i++) {
            const std::string where = "object " + std::to_string(objects[i].id) + " script ";
            int k = 0;
            objects[i].forEachField([&](auto& f) {
                if (f.scripted()) addScript(f, int64_t(i), where + std::to_string(k++));
            });
        }
        int k = 0;
        auto sceneField = [&](auto& f) {
            if (f.scripted()) addScript(f, -1, "scene script " + std::to_string(k++));
        };
        scene.scene.camera.forEachField(sceneField);
        scene.scene.general.forEachField(sceneField);
        for (auto& [path, material] : scene.materials)
            if (material) material->forEachField(sceneField);
    }

    // Evaluates a script's module (section 2: loading).
    void load(Script& s, size_t index) {
        const std::string name = "script" + std::to_string(index) + ".js";
        setThis(s.object);
        running = &s;
        deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                      std::chrono::duration<double>(options.callBudget * 5));
        JSValue fn = JS_Eval(ctx, s.source.data(), s.source.size(), name.c_str(),
                             JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(fn)) {
            deadline = Clock::time_point::max();
            running = nullptr;
            fail(s, "compile: " + exceptionText());
            return;
        }
        auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(fn));
        JSValue r = JS_EvalFunction(ctx, fn);
        runJobs();
        deadline = Clock::time_point::max();
        running = nullptr;
        if (JS_IsException(r)) {
            fail(s, "load: " + exceptionText());
            return;
        }
        if (JS_PromiseState(ctx, r) == JS_PROMISE_REJECTED) {
            JSValue reason = JS_PromiseResult(ctx, r);
            JS_Throw(ctx, reason);
            fail(s, "load: " + exceptionText());
            JS_FreeValue(ctx, r);
            return;
        }
        JS_FreeValue(ctx, r);
        s.ns = JS_GetModuleNamespace(ctx, module);
        auto exported = [&](const char* key) {
            JSValue v = JS_GetPropertyStr(ctx, s.ns, key);
            if (JS_IsFunction(ctx, v)) return v;
            JS_FreeValue(ctx, v);
            return JS_UNDEFINED;
        };
        s.init = exported("init");
        s.update = exported("update");
        s.apply = exported("applyUserProperties");
        s.destroy = exported("destroy");
        for (const char* e :
             {"cursorEnter", "cursorLeave", "cursorMove", "cursorDown", "cursorUp", "cursorClick", "resizeScreen",
              "mediaStatusChanged", "mediaPlaybackChanged", "mediaPropertiesChanged", "mediaThumbnailChanged",
              "mediaTimelineChanged"}) {
            JSValue v = exported(e);
            if (!JS_IsUndefined(v)) s.events[e] = v;
        }
        sendScriptProperties(s, nullptr);
    }

    // Wallpaper Engine's modules from the assets (section 1).
    static JSModuleDef* loadModule(JSContext* ctx, const char* name, void* opaque) {
        auto* m = static_cast<Impl*>(opaque);
        std::string file = name;
        std::transform(file.begin(), file.end(), file.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        static const std::set<std::string> known = {"wemath", "wecolor", "wevector"};
        if (!known.count(file)) {
            JS_ThrowReferenceError(ctx, "module %s is not available", name);
            return nullptr;
        }
        auto asset = m->assets.find("scripts/jsmodules/" + file + ".js");
        if (!asset) {
            JS_ThrowReferenceError(ctx, "module %s not found in the assets", name);
            return nullptr;
        }
        const std::string source(asset->bytes.begin(), asset->bytes.end());
        JSValue fn = JS_Eval(ctx, source.data(), source.size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(fn)) return nullptr;
        auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(fn));
        JS_FreeValue(ctx, fn);
        return module;
    }

    bool start(std::string& error) {
        rt = JS_NewRuntime();
        if (!rt) {
            error = "QuickJS could not start";
            return false;
        }
        JS_SetMemoryLimit(rt, options.memoryLimit);
        JS_SetMaxStackSize(rt, options.stackLimit);
        JS_SetInterruptHandler(rt, interruptHandler, this);
        JS_SetModuleLoaderFunc(rt, nullptr, loadModule, this);
        ctx = JS_NewContext(rt);
        if (!ctx) {
            error = "QuickJS could not start";
            return false;
        }
        JS_SetContextOpaque(ctx, this);
        global = JS_GetGlobalObject(ctx);
        // Wallpaper Engine's base classes (section 1).
        auto base = assets.find("scripts/jsclasses/baseclasses.js");
        if (!base) {
            error = "scripts/jsclasses/baseclasses.js not found (assets folder needed)";
            return false;
        }
        const std::string source(base->bytes.begin(), base->bytes.end());
        JSValue r = JS_Eval(ctx, source.data(), source.size(), "baseclasses.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(r)) {
            error = "baseclasses.js: " + exceptionText();
            return false;
        }
        JS_FreeValue(ctx, r);
        const char* names[3] = {"Vec2", "Vec3", "Vec4"};
        for (int i = 0; i < 3; i++) {
            JSValue v = JS_Eval(ctx, names[i], std::strlen(names[i]), "<vector>", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(v)) {
                error = std::string(names[i]) + ": " + exceptionText();
                return false;
            }
            vecCtor[i] = v;
        }
        internal = JS_GetPropertyStr(ctx, global, "_Internal");
        // The native bridge and the prelude.
        native = JS_NewObject(ctx);
        for (const auto& op : kOps)
            JS_SetPropertyStr(ctx, native, op.name,
                              JS_NewCFunctionMagic(ctx, dispatch, op.name, op.length, JS_CFUNC_generic_magic, op.op));
        frameObject = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, native, "frame", JS_DupValue(ctx, frameObject));
        writeFrame();
        JSValue prelude = JS_Eval(ctx, kPrelude, std::strlen(kPrelude), "desktop-canvas.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(prelude)) {
            error = "prelude: " + exceptionText();
            return false;
        }
        JSValue done = JS_Call(ctx, prelude, JS_UNDEFINED, 1, &native);
        JS_FreeValue(ctx, prelude);
        if (JS_IsException(done)) {
            error = "prelude: " + exceptionText();
            return false;
        }
        JS_FreeValue(ctx, done);
        loadStorage();
        collectScripts();
        for (size_t i = 0; i < scripts.size(); i++) load(scripts[i], i);
        return true;
    }

    // ------------------------------------------------------------ frame

    void setNumber(const char* key, double v) { JS_SetPropertyStr(ctx, frameObject, key, JS_NewFloat64(ctx, v)); }

    void writeFrame() {
        const General& g = scene.scene.general;
        setNumber("frametime", frame.frameTime);
        setNumber("runtime", frame.runtime);
        setNumber("timeOfDay", frame.timeOfDay);
        setNumber("canvasW", g.projectionWidth.value > 0 ? g.projectionWidth.value : 1920);
        setNumber("canvasH", g.projectionHeight.value > 0 ? g.projectionHeight.value : 1080);
        setNumber("screenW", frame.screen[0]);
        setNumber("screenH", frame.screen[1]);
        setNumber("cursorX", frame.cursorWorld[0]);
        setNumber("cursorY", frame.cursorWorld[1]);
        setNumber("cursorZ", frame.cursorWorld[2]);
        setNumber("screenX", frame.cursorScreen[0]);
        setNumber("screenY", frame.cursorScreen[1]);
        JS_SetPropertyStr(ctx, frameObject, "cursorDown", JS_NewBool(ctx, frame.cursorLeftDown));
    }

    // Copies n floats into a Float32Array a script holds; anything else
    // (a script replaced the member) is left alone.
    void fillFloats(JSValueConst array, const float* values, size_t n) {
        size_t offset = 0, length = 0, perElement = 0;
        JSValue buffer = JS_GetTypedArrayBuffer(ctx, array, &offset, &length, &perElement);
        if (JS_IsException(buffer)) {
            JS_FreeValue(ctx, JS_GetException(ctx));
            return;
        }
        size_t size = 0;
        uint8_t* bytes = JS_GetArrayBuffer(ctx, &size, buffer);
        if (bytes && perElement == 4 && offset + length <= size)
            std::memcpy(bytes + offset, values, std::min(length, n * sizeof(float)));
        JS_FreeValue(ctx, buffer);
    }

    size_t audioBufferCount() const {
        JSValue list = JS_GetPropertyStr(ctx, native, "audio");
        int64_t n = 0;
        JS_GetLength(ctx, list, &n);
        JS_FreeValue(ctx, list);
        return size_t(std::max<int64_t>(0, n));
    }

    // Registered audio buffers take this frame's levels (section 1).
    void fillAudio() {
        const size_t count = audioBufferCount();
        if (!count) return;
        const AudioSpectrum silence;
        const AudioSpectrum& a = frame.audio ? *frame.audio : silence;
        JSValue list = JS_GetPropertyStr(ctx, native, "audio");
        for (size_t i = 0; i < count; i++) {
            JSValue b = JS_GetPropertyUint32(ctx, list, uint32_t(i));
            JSValue left = JS_GetPropertyStr(ctx, b, "left");
            int64_t n = 0;
            JS_GetLength(ctx, left, &n);
            const float* l = n == 64 ? a.left64.data() : n == 32 ? a.left32.data() : a.left16.data();
            const float* r = n == 64 ? a.right64.data() : n == 32 ? a.right32.data() : a.right16.data();
            const size_t bands = n == 64 ? 64 : n == 32 ? 32 : 16;
            float average[64];
            for (size_t k = 0; k < bands; k++) average[k] = (l[k] + r[k]) / 2;
            JSValue right = JS_GetPropertyStr(ctx, b, "right");
            JSValue avg = JS_GetPropertyStr(ctx, b, "average");
            fillFloats(left, l, bands);
            fillFloats(right, r, bands);
            fillFloats(avg, average, bands);
            for (JSValue v : {left, right, avg, b}) JS_FreeValue(ctx, v);
        }
        JS_FreeValue(ctx, list);
    }

    void update(const ScriptFrame& f) {
        frame = f;
        writeFrame();
        fillAudio();
        // resizeScreen(size) when the output size changes (section 2).
        if (haveScreen && frame.screen != lastScreen) {
            JSValue size = toJs(frame.screen);
            for (auto& s : scripts)
                if (s.started) callEvent(s, "resizeScreen", size);
            JS_FreeValue(ctx, size);
        }
        haveScreen = true;
        lastScreen = frame.screen;
        // Live user property changes (section 2).
        if (!changed.empty()) {
            const std::set<std::string> names = std::move(changed);
            changed.clear();
            for (auto& s : scripts) {
                if (s.failed || !s.started) continue;
                sendScriptProperties(s, &names);
                applyProperties(s, &names);
            }
        }
        cursorEvents();
        // init, applyUserProperties and update (section 2).
        for (auto& s : scripts) {
            if (s.failed) continue;
            if (!s.started) {
                s.started = true;
                if (JS_IsFunction(ctx, s.init)) callWithValue(s, s.init, true);
                if (!s.failed) applyProperties(s, nullptr);
            }
            if (!s.failed && JS_IsFunction(ctx, s.update)) callWithValue(s, s.update, true);
        }
        mediaEvents();
        runTimers();
        runJobs();
        if (storageDirty) saveStorage();
    }

    // Media events after a script's first update, then on every change
    // (section 7).
    void mediaEvents() {
        if (!mediaKnown) return;
        auto has = [](const Script& s, const char* name) { return s.events.count(name) != 0; };
        for (auto& s : scripts) {
            if (s.failed || !s.started) continue;
            if (!s.mediaTold || s.mediaEnabled != media.enabled) {
                s.mediaTold = true;
                s.mediaEnabled = media.enabled;
                if (has(s, "mediaStatusChanged")) {
                    JSValue e = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, e, "enabled", JS_NewBool(ctx, media.enabled));
                    callEvent(s, "mediaStatusChanged", e);
                    JS_FreeValue(ctx, e);
                }
            }
            if (!media.enabled) continue;
            if (s.mediaState != media.state) {
                s.mediaState = media.state;
                if (has(s, "mediaPlaybackChanged")) {
                    JSValue e = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, e, "state", JS_NewInt32(ctx, media.state));
                    callEvent(s, "mediaPlaybackChanged", e);
                    JS_FreeValue(ctx, e);
                }
            }
            if (!s.propsTold || !s.mediaProps.sameProperties(media)) {
                s.propsTold = true;
                s.mediaProps = media;
                if (has(s, "mediaPropertiesChanged")) {
                    JSValue e = JS_NewObject(ctx);
                    const std::pair<const char*, const std::string*> fields[] = {
                        {"title", &media.title},           {"artist", &media.artist},
                        {"subTitle", &media.subTitle},     {"albumTitle", &media.albumTitle},
                        {"albumArtist", &media.albumArtist}, {"genres", &media.genres},
                        {"contentType", &media.contentType}};
                    for (const auto& [key, value] : fields) JS_SetPropertyStr(ctx, e, key, toJs(*value));
                    callEvent(s, "mediaPropertiesChanged", e);
                    JS_FreeValue(ctx, e);
                }
            }
            if (!s.artToldOnce || s.artTold != media.artPath) {
                s.artToldOnce = true;
                s.artTold = media.artPath;
                if (has(s, "mediaThumbnailChanged")) {
                    JSValue e = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, e, "hasThumbnail", JS_NewBool(ctx, art.hasThumbnail));
                    JS_SetPropertyStr(ctx, e, "primaryColor", toJs(art.primary));
                    JS_SetPropertyStr(ctx, e, "secondaryColor", toJs(art.secondary));
                    JS_SetPropertyStr(ctx, e, "tertiaryColor", toJs(art.tertiary));
                    JS_SetPropertyStr(ctx, e, "textColor", toJs(art.text));
                    JS_SetPropertyStr(ctx, e, "highContrastColor", toJs(art.highContrast));
                    callEvent(s, "mediaThumbnailChanged", e);
                    JS_FreeValue(ctx, e);
                }
            }
            if (media.timelineSerial && s.timelineTold != media.timelineSerial) {
                s.timelineTold = media.timelineSerial;
                if (has(s, "mediaTimelineChanged")) {
                    JSValue e = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, e, "position", JS_NewFloat64(ctx, media.position));
                    JS_SetPropertyStr(ctx, e, "duration", JS_NewFloat64(ctx, media.duration));
                    callEvent(s, "mediaTimelineChanged", e);
                    JS_FreeValue(ctx, e);
                }
            }
        }
    }

    void cursorEvents() {
        auto event = [&]() {
            JSValue fn = JS_GetPropertyStr(ctx, native, "cursorEvent");
            JSValue args[6];
            const float v[6] = {frame.cursorWorld[0], frame.cursorWorld[1], frame.cursorWorld[2],
                                frame.cursorLocal[0], frame.cursorLocal[1], frame.cursorLocal[2]};
            for (int i = 0; i < 6; i++) args[i] = JS_NewFloat64(ctx, double(v[i]));
            JSValue e = JS_Call(ctx, fn, JS_UNDEFINED, 6, args);
            for (JSValue a : args) JS_FreeValue(ctx, a);
            JS_FreeValue(ctx, fn);
            return e;
        };
        auto toLayer = [&](int64_t object, const char* name) {
            if (object < 0) return;
            JSValue e = event();
            for (auto& s : scripts)
                if (s.object == object) callEvent(s, name, e);
            JS_FreeValue(ctx, e);
        };
        if (frame.hovered != hovered) {
            toLayer(hovered, "cursorLeave");
            toLayer(frame.hovered, "cursorEnter");
            hovered = frame.hovered;
        }
        const bool moved = !haveCursor || frame.cursorWorld != lastCursor;
        haveCursor = true;
        lastCursor = frame.cursorWorld;
        if (moved) {
            // cursorMove reaches every script that exports it (section 5).
            JSValue e = event();
            for (auto& s : scripts) callEvent(s, "cursorMove", e);
            JS_FreeValue(ctx, e);
        }
        if (frame.cursorLeftDown && !wasDown) {
            toLayer(hovered, "cursorDown");
            pressed = hovered;
        } else if (!frame.cursorLeftDown && wasDown) {
            toLayer(hovered, "cursorUp");
            if (hovered >= 0 && hovered == pressed) toLayer(hovered, "cursorClick");
            pressed = -1;
        }
        wasDown = frame.cursorLeftDown;
    }

    void runTimers() {
        // Timers added by callbacks wait for the next frame.
        const size_t count = timers.size();
        for (size_t i = 0; i < count && i < timers.size(); i++) {
            if (timers[i].cancelled || timers[i].due > frame.runtime) continue;
            JSValue fn = JS_DupValue(ctx, timers[i].fn);
            const int64_t object = timers[i].object;
            if (timers[i].repeat)
                timers[i].due = std::max(timers[i].due + timers[i].interval, frame.runtime + 1e-6);
            else
                timers[i].cancelled = true;
            Script* owner = nullptr;
            for (auto& s : scripts)
                if (s.object == object) owner = &s;
            setThis(object);
            deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                          std::chrono::duration<double>(options.callBudget));
            JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 0, nullptr);
            deadline = Clock::time_point::max();
            if (JS_IsException(r)) {
                const std::string what = exceptionText();
                if (owner) fail(*owner, "timer: " + what);
                else problems.push_back("timer: " + what);
                for (auto& t : timers)
                    if (t.object == object) t.cancelled = true;
            }
            JS_FreeValue(ctx, r);
            JS_FreeValue(ctx, fn);
        }
        for (auto it = timers.begin(); it != timers.end();) {
            if (it->cancelled) {
                JS_FreeValue(ctx, it->fn);
                it = timers.erase(it);
            } else {
                ++it;
            }
        }
    }

    // ---------------------------------------------------------- storage

    void loadStorage() {
        if (options.storageFile.empty()) return;
        std::ifstream in(options.storageFile);
        if (!in) return;
        json j = json::parse(in, nullptr, false);
        if (j.is_object())
            for (const char* loc : {"global", "screen"})
                if (j.contains(loc) && j[loc].is_object()) storage[loc] = j[loc];
    }

    void saveStorage() {
        storageDirty = false;
        if (options.storageFile.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(options.storageFile).parent_path(), ec);
        const std::string temp = options.storageFile + ".new";
        {
            std::ofstream out(temp, std::ios::trunc);
            if (!out) return;
            out << storage.dump();
        }
        std::filesystem::rename(temp, options.storageFile, ec);
    }

    // ------------------------------------------------------------ layers

    SceneObject* object(JSValueConst v) {
        auto d = number(v);
        if (!d) return nullptr;
        const int64_t i = int64_t(*d);
        if (i < 0 || size_t(i) >= scene.scene.objects.size()) return nullptr;
        return &scene.scene.objects[size_t(i)];
    }

    // Calls f(field) for a layer property (section 4); false when the
    // layer has no such property.
    template <class F>
    bool layerField(SceneObject& o, const std::string& name, F&& f) {
        if (name == "origin") return f(o.origin), true;
        if (name == "angles") return f(o.angles), true;
        if (name == "scale") return f(o.scale), true;
        if (name == "visible") return f(o.visible), true;
        if (name == "parallaxDepth") return f(o.parallaxDepth), true;
        if (o.image) {
            ImageLayer& im = *o.image;
            if (name == "alpha") return f(im.alpha), true;
            if (name == "color") return f(im.color), true;
            if (name == "alignment") return f(im.alignment), true;
        }
        if (o.sound && name == "volume") return f(o.sound->volume), true;
        if (o.text) {
            TextLayer& t = *o.text;
            if (name == "text") return f(t.text), true;
            if (name == "color") return f(t.color), true;
            if (name == "alpha") return f(t.alpha), true;
            if (name == "pointsize") return f(t.pointSize), true;
            if (name == "padding") return f(t.padding), true;
            if (name == "horizontalalign") return f(t.horizontalAlign), true;
            if (name == "verticalalign") return f(t.verticalAlign), true;
            if (name == "anchor") return f(t.anchor), true;
            if (name == "limitrows") return f(t.limitRows), true;
            if (name == "maxrows") return f(t.maxRows), true;
            if (name == "limitwidth") return f(t.limitWidth), true;
            if (name == "maxwidth") return f(t.maxWidth), true;
            if (name == "opaquebackground") return f(t.opaqueBackground), true;
            if (name == "backgroundcolor") return f(t.backgroundColor), true;
        }
        return false;
    }

    JSValue layerGet(JSValueConst indexValue, JSValueConst nameValue) {
        SceneObject* o = object(indexValue);
        auto name = string(nameValue);
        if (!o || !name) return JS_UNDEFINED;
        const size_t index = size_t(o - scene.scene.objects.data());
        if (*name == "name") return toJs(o->name);
        if (*name == "id") return JS_NewInt64(ctx, o->id);
        if (*name == "font") return o->text ? toJs(o->text->font) : JS_UNDEFINED;
        if (*name == "size") return toJs(host.layerSize(index));
        if (*name == "padding" && o->text) return toJs(o->text->padding.value[0]);  // a number [doc]
        JSValue out = JS_UNDEFINED;
        layerField(*o, *name, [&](auto& field) { out = toJs(field.value); });
        return out;
    }

    void layerSet(JSValueConst indexValue, JSValueConst nameValue, JSValueConst v) {
        SceneObject* o = object(indexValue);
        auto name = string(nameValue);
        if (!o || !name) return;
        layerField(*o, *name, [&](auto& field) { assign(field, v); });
    }

    template <class F>
    bool instanceField(SceneObject& o, const std::string& name, bool create, F&& f) {
        if (!o.particle) return false;
        if (!o.particle->overrides) {
            if (!create) {
                // The neutral factors without an override (section 2 of the particle spec).
                static ParticleOverride neutral;
                ParticleOverride copy = neutral;
                return instanceFieldOf(copy, name, f);
            }
            o.particle->overrides.emplace();
        }
        return instanceFieldOf(*o.particle->overrides, name, f);
    }
    template <class F>
    bool instanceFieldOf(ParticleOverride& p, const std::string& name, F&& f) {
        if (name == "alpha") return f(p.alpha), true;
        if (name == "size") return f(p.size), true;
        if (name == "count") return f(p.count), true;
        if (name == "speed") return f(p.speed), true;
        if (name == "lifetime") return f(p.lifetime), true;
        if (name == "rate") return f(p.rate), true;
        if (name == "brightness") return f(p.brightness), true;
        if (name == "colorn") return f(p.colorn), true;
        if (name == "color") return f(p.color), true;
        if (name.rfind("controlpoint", 0) == 0 && name.size() == 13 && name[12] >= '0' && name[12] <= '7') {
            auto& field = p.controlPoints[name];
            return f(field), true;
        }
        return false;
    }

    template <class F>
    bool sceneField(const std::string& name, F&& f) {
        General& g = scene.scene.general;
        if (name == "bloom") return f(g.bloom), true;
        if (name == "bloomstrength") return f(g.bloomStrength), true;
        if (name == "bloomthreshold") return f(g.bloomThreshold), true;
        if (name == "clearcolor") return f(g.clearColor), true;
        if (name == "clearenabled") return f(g.clearEnabled), true;
        if (name == "camerafade") return f(g.cameraFade), true;
        if (name == "camerashake") return f(g.cameraShake), true;
        if (name == "camerashakeamplitude") return f(g.cameraShakeAmplitude), true;
        if (name == "camerashakeroughness") return f(g.cameraShakeRoughness), true;
        if (name == "camerashakespeed") return f(g.cameraShakeSpeed), true;
        if (name == "cameraparallax") return f(g.cameraParallax), true;
        if (name == "cameraparallaxamount") return f(g.cameraParallaxAmount), true;
        if (name == "cameraparallaxdelay") return f(g.cameraParallaxDelay), true;
        if (name == "cameraparallaxmouseinfluence") return f(g.cameraParallaxMouseInfluence), true;
        if (name == "ambientcolor") return f(g.ambientColor), true;
        if (name == "skylightcolor") return f(g.skylightColor), true;
        if (name == "fov") return f(g.fov), true;
        if (name == "nearz") return f(g.nearZ), true;
        if (name == "farz") return f(g.farZ), true;
        return false;
    }

    int64_t effectIndex(SceneObject& o, JSValueConst v) {
        if (JS_IsNumber(v)) {
            auto d = number(v);
            const int64_t e = d ? int64_t(*d) : -1;
            return e >= 0 && size_t(e) < o.effects.size() ? e : -1;
        }
        auto name = string(v);
        if (!name) return -1;
        for (size_t e = 0; e < o.effects.size(); e++)
            if (o.effects[e].name == *name) return int64_t(e);
        for (size_t e = 0; e < o.effects.size(); e++)
            if (o.effects[e].username == *name) return int64_t(e);
        return -1;
    }

    std::string initialConfig(const SceneObject& o) {
        auto text3 = [](const Vec3& v) {
            std::ostringstream s;
            s << v[0] << ' ' << v[1] << ' ' << v[2];
            return s.str();
        };
        json j = {{"id", o.id},
                  {"name", o.name},
                  {"origin", text3(o.origin.stored)},
                  {"scale", text3(o.scale.stored)},
                  {"angles", text3(o.angles.stored)},
                  {"visible", o.visible.stored}};
        if (o.text) j["text"] = o.text->text.stored;
        if (o.image) j["alpha"] = o.image->alpha.stored;
        return j.dump();
    }

    // ---------------------------------------------------------- dispatch

    static JSValue dispatch(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int magic) {
        auto* m = static_cast<Impl*>(JS_GetContextOpaque(ctx));
        auto arg = [&](int i) -> JSValueConst { return i < argc ? argv[i] : JS_UNDEFINED; };
        return m->run(Op(magic), arg);
    }

    template <class A>
    JSValue run(Op op, A arg) {
        auto& objects = scene.scene.objects;
        switch (op) {
            case kLayerCount: return JS_NewInt64(ctx, int64_t(objects.size()));
            case kLayerByName: {
                auto name = string(arg(0));
                for (size_t i = 0; name && i < objects.size(); i++)
                    if (objects[i].name == *name) return JS_NewInt64(ctx, int64_t(i));
                return JS_NewInt64(ctx, -1);
            }
            case kLayerById: {
                auto text = string(arg(0));
                if (text) {
                    char* end = nullptr;
                    const long long id = std::strtoll(text->c_str(), &end, 10);
                    for (size_t i = 0; end && *end == '\0' && i < objects.size(); i++)
                        if (objects[i].id == id) return JS_NewInt64(ctx, int64_t(i));
                }
                return JS_NewInt64(ctx, -1);
            }
            case kParent: {
                SceneObject* o = object(arg(0));
                if (o && o->parent)
                    for (size_t i = 0; i < objects.size(); i++)
                        if (objects[i].id == *o->parent) return JS_NewInt64(ctx, int64_t(i));
                return JS_NewInt64(ctx, -1);
            }
            case kChildren: {
                JSValue out = JS_NewArray(ctx);
                SceneObject* o = object(arg(0));
                uint32_t k = 0;
                for (size_t i = 0; o && i < objects.size(); i++)
                    if (objects[i].parent && *objects[i].parent == o->id)
                        JS_SetPropertyUint32(ctx, out, k++, JS_NewInt64(ctx, int64_t(i)));
                return out;
            }
            case kGet: return layerGet(arg(0), arg(1));
            case kSet: layerSet(arg(0), arg(1), arg(2)); return JS_UNDEFINED;
            case kIsParticle: {
                SceneObject* o = object(arg(0));
                return JS_NewBool(ctx, o && o->particle);
            }
            case kInstanceGet: {
                SceneObject* o = object(arg(0));
                auto name = string(arg(1));
                JSValue out = JS_UNDEFINED;
                if (o && name) instanceField(*o, *name, false, [&](auto& f) { out = toJs(f.value); });
                return out;
            }
            case kInstanceSet: {
                SceneObject* o = object(arg(0));
                auto name = string(arg(1));
                if (o && name) instanceField(*o, *name, true, [&](auto& f) { assign(f, arg(2)); });
                return JS_UNDEFINED;
            }
            case kEffectIndex: {
                SceneObject* o = object(arg(0));
                return JS_NewInt64(ctx, o ? effectIndex(*o, arg(1)) : -1);
            }
            case kEffectCount: {
                SceneObject* o = object(arg(0));
                return JS_NewInt64(ctx, o ? int64_t(o->effects.size()) : 0);
            }
            case kEffectGet:
            case kEffectSet:
            case kEffectConstant: {
                SceneObject* o = object(arg(0));
                auto e = number(arg(1));
                auto name = string(arg(2));
                if (!o || !e || !name || *e < 0 || size_t(*e) >= o->effects.size()) return JS_UNDEFINED;
                EffectInstance& effect = o->effects[size_t(*e)];
                if (op == kEffectGet) {
                    if (*name == "visible") return toJs(effect.visible.value);
                    if (*name == "name") return toJs(effect.name.empty() ? effect.username : effect.name);
                    return JS_UNDEFINED;
                }
                if (op == kEffectSet) {
                    if (*name == "visible") assign(effect.visible, arg(3));
                    return JS_UNDEFINED;
                }
                for (auto& pass : effect.passes)
                    for (auto& [key, field] : pass.constants)
                        if (key.size() == name->size() &&
                            std::equal(key.begin(), key.end(), name->begin(), [](char a, char b) {
                                return std::tolower(static_cast<unsigned char>(a)) ==
                                       std::tolower(static_cast<unsigned char>(b));
                            }))
                            assign(field, arg(3));
                return JS_UNDEFINED;
            }
            case kAnimation: {
                auto index = number(arg(0));
                auto name = string(arg(1));
                if (!animator || !index || !name) return JS_NewInt64(ctx, -1);
                AnimationPlayback* p = animator->find(int64_t(*index), *name);
                if (!p) return JS_NewInt64(ctx, -1);
                auto it = std::find(animations.begin(), animations.end(), p);
                if (it == animations.end()) {
                    animations.push_back(p);
                    it = animations.end() - 1;
                }
                return JS_NewInt64(ctx, int64_t(it - animations.begin()));
            }
            case kAnimationCall:
            case kAnimationGet:
            case kAnimationSet: {
                auto a = number(arg(0));
                if (!a || *a < 0 || size_t(*a) >= animations.size()) return JS_UNDEFINED;
                AnimationPlayback& p = *animations[size_t(*a)];
                if (op == kAnimationCall) {
                    const int command = int(number(arg(1)).value_or(-1));
                    if (command == 0) p.playing = true;
                    if (command == 1) p.playing = false;
                    if (command == 2) {
                        p.playing = false;
                        p.time = 0;
                    }
                    return JS_UNDEFINED;
                }
                auto key = string(arg(1));
                if (!key) return JS_UNDEFINED;
                if (op == kAnimationSet) {
                    auto v = number(arg(2));
                    if (v && *key == "frame") p.time = std::max(0.0, *v) / std::max(1e-6, p.timeline.fps);
                    if (v && *key == "rate") p.rate = *v;
                    return JS_UNDEFINED;
                }
                if (*key == "playing") return JS_NewBool(ctx, p.playing);
                if (*key == "frame") return JS_NewFloat64(ctx, p.frame());
                if (*key == "rate") return JS_NewFloat64(ctx, p.rate);
                if (*key == "fps") return JS_NewFloat64(ctx, p.timeline.fps);
                if (*key == "frameCount") return JS_NewFloat64(ctx, p.timeline.length);
                if (*key == "duration") return JS_NewFloat64(ctx, p.timeline.length / std::max(1e-6, p.timeline.fps));
                if (*key == "name") return toJs(p.timeline.name);
                return JS_UNDEFINED;
            }
            case kParticle: {
                SceneObject* o = object(arg(0));
                auto command = number(arg(1));
                if (o && o->particle && command)
                    host.particleCommand(size_t(o - objects.data()), int(*command));
                else if (o && o->sound && command)
                    host.soundCommand(size_t(o - objects.data()), int(*command));
                return JS_UNDEFINED;
            }
            case kParticlePlaying: {
                SceneObject* o = object(arg(0));
                if (o && o->sound) return JS_NewBool(ctx, host.soundPlaying(size_t(o - objects.data())));
                return JS_NewBool(ctx, o && o->particle && host.particlePlaying(size_t(o - objects.data())));
            }
            case kSceneGet: {
                auto name = string(arg(0));
                JSValue out = JS_UNDEFINED;
                if (name) sceneField(*name, [&](auto& f) { out = toJs(f.value); });
                return out;
            }
            case kSceneSet: {
                auto name = string(arg(0));
                if (name) sceneField(*name, [&](auto& f) { assign(f, arg(1)); });
                return JS_UNDEFINED;
            }
            case kInitialConfig: {
                SceneObject* o = object(arg(0));
                return toJs(o ? initialConfig(*o) : std::string("{}"));
            }
            case kUserProperties: return toJs(userPropertiesJson(nullptr));
            case kTimer: {
                if (!JS_IsFunction(ctx, arg(0))) return JS_NewInt64(ctx, 0);
                Timer t;
                t.id = nextTimer++;
                t.interval = std::max(0.0, number(arg(1)).value_or(0)) / 1000.0;
                t.repeat = JS_ToBool(ctx, arg(2)) > 0;
                t.due = frame.runtime + t.interval;
                t.object = current;
                t.fn = JS_DupValue(ctx, arg(0));
                timers.push_back(t);
                return JS_NewInt64(ctx, int64_t(t.id));
            }
            case kCancel: {
                auto id = number(arg(0));
                for (auto& t : timers)
                    if (id && t.id == uint64_t(*id)) t.cancelled = true;
                return JS_UNDEFINED;
            }
            case kLog: {
                auto level = number(arg(0));
                auto text = string(arg(1));
                if (!text) return JS_UNDEFINED;
                // At most 50 lines per script per minute (section 3).
                auto& [start, lines] = logBudget[running];
                if (frame.runtime - start >= 60) {
                    start = frame.runtime;
                    lines = 0;
                }
                if (++lines > 50) return JS_UNDEFINED;
                const std::string who = running ? running->where : std::string("script");
                log.push_back(who + (level && *level > 0 ? " error: " : ": ") + *text);
                return JS_UNDEFINED;
            }
            case kStorageGet: {
                auto loc = string(arg(0));
                auto key = string(arg(1));
                if (!loc || !key || !storage[*loc].contains(*key)) return JS_UNDEFINED;
                return toJs(storage[*loc][*key].dump());
            }
            case kStorageSet: {
                auto loc = string(arg(0));
                auto key = string(arg(1));
                if (!loc || !key) return JS_UNDEFINED;
                auto text = JS_IsUndefined(arg(2)) ? std::nullopt : string(arg(2));
                if (!text) {
                    storage[*loc].erase(*key);
                } else {
                    json v = json::parse(*text, nullptr, false);
                    if (!v.is_discarded()) storage[*loc][*key] = v;
                }
                storageDirty = true;
                return JS_UNDEFINED;
            }
            case kStorageDelete: {
                auto loc = string(arg(0));
                auto key = string(arg(1));
                const bool had = loc && key && storage[*loc].erase(*key) > 0;
                storageDirty |= had;
                return JS_NewBool(ctx, had);
            }
            case kStorageClear: {
                auto loc = string(arg(0));
                if (loc) {
                    storage[*loc] = json::object();
                    storageDirty = true;
                }
                return JS_UNDEFINED;
            }
        }
        return JS_UNDEFINED;
    }
};

namespace {

int interruptHandler(JSRuntime*, void* opaque) {
    auto* m = static_cast<ScriptEngine::Impl*>(opaque);
    return Clock::now() > m->deadline ? 1 : 0;
}

}  // namespace

bool ScriptEngine::available() { return true; }

std::unique_ptr<ScriptEngine> ScriptEngine::create(LoadedScene& scene, const AssetLocator& assets,
                                                   Animator* animator, ScriptHost& host, Options options,
                                                   std::string& error) {
    auto impl = std::make_unique<Impl>(scene, assets, animator, host, std::move(options));
    if (!impl->start(error)) return nullptr;
    return std::unique_ptr<ScriptEngine>(new ScriptEngine(std::move(impl)));
}

ScriptEngine::ScriptEngine(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ScriptEngine::~ScriptEngine() = default;

size_t ScriptEngine::scriptCount() const {
    size_t n = 0;
    for (const auto& s : impl_->scripts) n += s.failed ? 0 : 1;
    return n;
}

size_t ScriptEngine::failedCount() const { return impl_->scripts.size() - scriptCount(); }

bool ScriptEngine::usesAudio() const { return impl_->audioBufferCount() > 0; }

void ScriptEngine::setMedia(const MediaInfo& media, const AlbumArtColors& art) {
    impl_->media = media;
    impl_->art = art;
    impl_->mediaKnown = true;
}

void ScriptEngine::update(const ScriptFrame& frame) { impl_->update(frame); }

void ScriptEngine::userPropertiesChanged(const std::vector<std::string>& names) {
    impl_->changed.insert(names.begin(), names.end());
}

std::vector<std::string> ScriptEngine::takeLog() { return std::exchange(impl_->log, {}); }

std::vector<std::string> ScriptEngine::takeProblems() { return std::exchange(impl_->problems, {}); }

#endif

}  // namespace canvas::scene
