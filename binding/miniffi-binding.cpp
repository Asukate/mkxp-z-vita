// Most of the MiniFFI class was taken from Ruby 1.8's Win32API.c,
// it's just as basic but should work fine for the moment

#include <SDL.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "filesystem/filesystem.h"
#include "miniffi.h"
#include "binding-util.h"
#include "util/debugwriter.h"
#if defined(__vita__) || defined(__psp2__)
#include "util/win32-text.h"
#include "util/win32-appdata.h"
#include "display/bitmap.h"
#include "vita_video_decoder.h"
#include "sharedstate.h"
#include "input/input.h"
#include "input/vita_input.h"
#include "display/graphics.h"
#include <climits>
#include <algorithm>
#include <string>
#endif

#if defined(__vita__) || defined(__psp2__)
#include <psp2/power.h>
/* The Vita's own MiniFFI: no library can load, so each object only records which
 * Windows function it stands for, decided once in initialize (vitaFFIInitialize). */
static VALUE vitaFFIInitialize(int argc, VALUE *argv, VALUE self);
static VALUE vitaFFICall(int argc, VALUE *argv, VALUE self);
static VALUE vitaFFIAllocate(VALUE klass);
#endif

#if RAPI_MAJOR >= 2
#include <ruby/thread.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#define MVAL2RB(v) ULONG2NUM(v)
#define RB2MVAL(v) (mffi_value)NUM2ULONG(v)
#else
#ifdef __MINGW64__
#define MVAL2RB(v) ULL2NUM(v)
#define RB2MVAL(v) (mffi_value)NUM2ULL(v)
#else
#define MVAL2RB(v) UINT2NUM(v)
#define RB2MVAL(v) (mffi_value)NUM2UINT(v)
#endif
#endif

#define _T_VOID 0
#define _T_NUMBER 1
#define _T_POINTER 2
#define _T_INTEGER 3
#define _T_BOOL 4

#if RAPI_FULL > 187
DEF_TYPE_CUSTOMFREE(MiniFFI, SDL_UnloadObject);
#else
DEF_ALLOCFUNC_CUSTOMFREE(MiniFFI, SDL_UnloadObject);
#endif

static void *MiniFFI_GetFunctionHandle(void *libhandle, const char *func) {
    if (!libhandle)
        return 0;
    return SDL_LoadFunction(libhandle, func);
}

// MiniFFI.new(library, function[, imports[, exports]])
// Yields itself in blocks

RB_METHOD_GUARD(MiniFFI_initialize) {
#if defined(__vita__) || defined(__psp2__)
    return vitaFFIInitialize(argc, argv, self);
#endif
    VALUE libname, func, imports, exports;
    rb_scan_args(argc, argv, "22", &libname, &func, &imports, &exports);
    SafeStringValue(libname);
    SafeStringValue(func);
#if defined(__vita__) || defined(__psp2__)
    /* Vita has no loadable libraries, and its SDL_LoadObject /
     * SDL_LoadFunction stubs report success unconditionally, so any
     * handle resolved here would be garbage that crashes on call
     * (observed: every Win32API user died in MiniFFI_call). Stay a
     * dummy instead; MiniFFI_call turns calls into warned no-ops. */
    void *hlib = 0;
    void *hfunc = 0;
    rb_iv_set(self, "_dummy", Qtrue);
#elif defined(__APPLE__)
    void *hlib = SDL_LoadObject(mkxp_fs::normalizePath(RSTRING_PTR(libname), 1, 1).c_str());
    void *hfunc = MiniFFI_GetFunctionHandle(hlib, RSTRING_PTR(func));
#else
    void *hlib = SDL_LoadObject(RSTRING_PTR(libname));
    void *hfunc = MiniFFI_GetFunctionHandle(hlib, RSTRING_PTR(func));
#endif
    setPrivateData(self, hlib);
#ifdef __WIN32__
    if (hlib && !hfunc) {
        VALUE func_a = rb_str_new3(func);
        func_a = rb_str_cat(func_a, "A", 1);
        hfunc = SDL_LoadFunction(hlib, RSTRING_PTR(func_a));
    }
#endif
#if !defined(__vita__) && !defined(__psp2__)
    if (!hfunc)
        throw Exception(Exception::RuntimeError, "%s", SDL_GetError());
#endif

    rb_iv_set(self, "_func", MVAL2RB((mffi_value)hfunc));
    rb_iv_set(self, "_funcname", func);
    rb_iv_set(self, "_libname", libname);

    VALUE ary_imports = rb_ary_new();
    VALUE *entry;
    switch (TYPE(imports)) {
        case T_NIL:
            break;
        case T_ARRAY:
            entry = RARRAY_PTR(imports);
            for (int i = 0; i < RARRAY_LEN(imports); i++) {
                SafeStringValue(entry[i]);
                switch (*(char *)RSTRING_PTR(entry[i])) {
                    case 'N':
                    case 'n':
                    case 'L':
                    case 'l':
                        rb_ary_push(ary_imports, INT2FIX(_T_NUMBER));
                        break;

                    case 'P':
                    case 'p':
                        rb_ary_push(ary_imports, INT2FIX(_T_POINTER));
                        break;

                    case 'I':
                    case 'i':
                        rb_ary_push(ary_imports, INT2FIX(_T_INTEGER));
                        break;

                    case 'B':
                    case 'b':
                        rb_ary_push(ary_imports, INT2FIX(_T_BOOL));
                        break;
                }
            }
            break;
        default:
            SafeStringValue(imports);
            const char *s = RSTRING_PTR(imports);
            for (int i = 0; i < RSTRING_LEN(imports); i++) {
                switch (*s++) {
                    case 'N':
                    case 'n':
                    case 'L':
                    case 'l':
                        rb_ary_push(ary_imports, INT2FIX(_T_NUMBER));
                        break;

                    case 'P':
                    case 'p':
                        rb_ary_push(ary_imports, INT2FIX(_T_POINTER));
                        break;

                    case 'I':
                    case 'i':
                        rb_ary_push(ary_imports, INT2FIX(_T_INTEGER));
                        break;

                    case 'B':
                    case 'b':
                        rb_ary_push(ary_imports, INT2FIX(_T_BOOL));
                        break;
                }
            }
            break;
    }

    if (MINIFFI_MAX_ARGS < RARRAY_LEN(ary_imports))
        throw Exception(Exception::RuntimeError, "too many parameters: %ld/%ld\n",
                 RARRAY_LEN(ary_imports), MINIFFI_MAX_ARGS);

    rb_iv_set(self, "_imports", ary_imports);
    int ex;
    if (NIL_P(exports)) {
        ex = _T_VOID;
    } else {
        SafeStringValue(exports);
        switch (*RSTRING_PTR(exports)) {
            case 'V':
            case 'v':
                ex = _T_VOID;
                break;

            case 'N':
            case 'n':
            case 'L':
            case 'l':
                ex = _T_NUMBER;
                break;

            case 'P':
            case 'p':
                ex = _T_POINTER;
                break;

            case 'I':
            case 'i':
                ex = _T_INTEGER;
                break;

            case 'B':
            case 'b':
                ex = _T_BOOL;
                break;
        }
    }
    rb_iv_set(self, "_exports", INT2FIX(ex));
    if (rb_block_given_p())
        rb_yield(self);
    return Qnil;
}
RB_METHOD_GUARD_END

#if RAPI_MAJOR >= 2
typedef struct {
    MINIFFI_FUNC function;
    MiniFFIFuncArgs *args;
    int nparams;
} MFFICallCBArgs;

void* miniffi_call_cb(void *args) {
    MFFICallCBArgs *a = (MFFICallCBArgs*)args;
    return (void*)miniffi_call_intern(a->function, a->args, a->nparams);
    }
#endif

#if defined(__vita__) || defined(__psp2__)
/* Ruby 3.1 no longer ships DL, but VX Ace scripts may still allocate a
 * DL::CPtr for Win32API buffers.  Keep this compatibility surface in the
 * shared runtime so the original scripts and assets remain untouched. */
struct VitaDLPointer {
    unsigned char *data;
    size_t size;
    VALUE owner;
    bool owned;
};

static VALUE vitaDLPointerClass = Qnil;

static void vitaDLMark(void *raw) {
    VitaDLPointer *ptr = static_cast<VitaDLPointer *>(raw);
    if (ptr) rb_gc_mark(ptr->owner);
}

static void vitaDLFree(void *raw) {
    VitaDLPointer *ptr = static_cast<VitaDLPointer *>(raw);
    if (!ptr) return;
    if (ptr->owned) std::free(ptr->data);
    std::free(ptr);
}

static VALUE vitaDLAlloc(VALUE klass) {
    VitaDLPointer *ptr = static_cast<VitaDLPointer *>(std::calloc(1, sizeof(*ptr)));
    if (!ptr) rb_memerror();
    ptr->owner = Qnil;
    return Data_Wrap_Struct(klass, vitaDLMark, vitaDLFree, ptr);
}

static VitaDLPointer *vitaDLGet(VALUE self) {
    VitaDLPointer *ptr = nullptr;
    Data_Get_Struct(self, VitaDLPointer, ptr);
    return ptr;
}

static VALUE vitaDLMalloc(VALUE, VALUE length) {
    size_t size = NUM2SIZET(length);
    VALUE result = rb_obj_alloc(vitaDLPointerClass);
    VitaDLPointer *ptr = vitaDLGet(result);
    ptr->data = static_cast<unsigned char *>(std::calloc(size ? size : 1, 1));
    if (!ptr->data) rb_memerror();
    ptr->size = size;
    ptr->owned = true;
    return result;
}

static VALUE vitaDLInitialize(int argc, VALUE *argv, VALUE self) {
    VALUE address, length;
    rb_scan_args(argc, argv, "11", &address, &length);
    VitaDLPointer *ptr = vitaDLGet(self);
    if (ptr->data) rb_raise(rb_eArgError, "DL::CPtr already initialized");
    if (rb_obj_is_kind_of(address, vitaDLPointerClass)) {
        VitaDLPointer *source = vitaDLGet(address);
        ptr->data = source->data;
        ptr->size = NIL_P(length) ? source->size : NUM2SIZET(length);
        if (ptr->size > source->size) rb_raise(rb_eArgError, "DL::CPtr length exceeds owner");
        ptr->owner = address;
    } else {
        ptr->data = reinterpret_cast<unsigned char *>(static_cast<uintptr_t>(NUM2ULL(address)));
        ptr->size = NIL_P(length) ? 0 : NUM2SIZET(length);
    }
    return self;
}

static VALUE vitaDLToI(VALUE self) {
    return ULL2NUM(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(vitaDLGet(self)->data)));
}

static VALUE vitaDLRead(VALUE self, VALUE index) {
    VitaDLPointer *ptr = vitaDLGet(self);
    size_t at = NUM2SIZET(index);
    if (!ptr->data || at >= ptr->size) rb_raise(rb_eRangeError, "DL::CPtr index out of bounds");
    return UINT2NUM(ptr->data[at]);
}

static VALUE vitaDLWrite(VALUE self, VALUE index, VALUE value) {
    VitaDLPointer *ptr = vitaDLGet(self);
    size_t at = NUM2SIZET(index);
    if (!ptr->data || at >= ptr->size) rb_raise(rb_eRangeError, "DL::CPtr index out of bounds");
    ptr->data[at] = static_cast<unsigned char>(NUM2UINT(value));
    return value;
}

static int vitaVKScancode(unsigned int key) {
    if (key >= 'A' && key <= 'Z') return SDL_SCANCODE_A + key - 'A';
    if (key >= '1' && key <= '9') return SDL_SCANCODE_1 + key - '1';
    if (key == '0') return SDL_SCANCODE_0;
    if (key >= 0x70 && key <= 0x7b) return SDL_SCANCODE_F1 + key - 0x70;
    if (key >= 0x61 && key <= 0x69) return SDL_SCANCODE_KP_1 + key - 0x61;
    if (key == 0x60) return SDL_SCANCODE_KP_0;
    switch (key) {
        case 0x08: return SDL_SCANCODE_BACKSPACE;
        case 0x09: return SDL_SCANCODE_TAB;
        case 0x0d: return SDL_SCANCODE_RETURN;
        case 0x10: case 0xa0: return SDL_SCANCODE_LSHIFT;
        case 0x11: case 0xa2: return SDL_SCANCODE_LCTRL;
        case 0x12: case 0xa4: return SDL_SCANCODE_LALT;
        case 0xa1: return SDL_SCANCODE_RSHIFT;
        case 0xa3: return SDL_SCANCODE_RCTRL;
        case 0xa5: return SDL_SCANCODE_RALT;
        case 0x14: return SDL_SCANCODE_CAPSLOCK;
        case 0x21: return SDL_SCANCODE_PAGEUP;
        case 0x22: return SDL_SCANCODE_PAGEDOWN;
        case 0x23: return SDL_SCANCODE_END;
        case 0x24: return SDL_SCANCODE_HOME;
        case 0x2d: return SDL_SCANCODE_INSERT;
        case 0x2e: return SDL_SCANCODE_DELETE;
        case 0x6a: return SDL_SCANCODE_KP_MULTIPLY;
        case 0x6b: return SDL_SCANCODE_KP_PLUS;
        case 0x6d: return SDL_SCANCODE_KP_MINUS;
        case 0x6e: return SDL_SCANCODE_KP_PERIOD;
        case 0x6f: return SDL_SCANCODE_KP_DIVIDE;
        case 0xba: return SDL_SCANCODE_SEMICOLON;
        case 0xbb: return SDL_SCANCODE_EQUALS;
        case 0xbc: return SDL_SCANCODE_COMMA;
        case 0xbd: return SDL_SCANCODE_MINUS;
        case 0xbe: return SDL_SCANCODE_PERIOD;
        case 0xbf: return SDL_SCANCODE_SLASH;
        case 0xc0: return SDL_SCANCODE_GRAVE;
        case 0xdb: return SDL_SCANCODE_LEFTBRACKET;
        case 0xdc: return SDL_SCANCODE_BACKSLASH;
        case 0xdd: return SDL_SCANCODE_RIGHTBRACKET;
        case 0xde: return SDL_SCANCODE_APOSTROPHE;
        case 0x1b: return SDL_SCANCODE_ESCAPE;
        case 0x20: return SDL_SCANCODE_SPACE;
        case 0x25: return SDL_SCANCODE_LEFT;
        case 0x26: return SDL_SCANCODE_UP;
        case 0x27: return SDL_SCANCODE_RIGHT;
        case 0x28: return SDL_SCANCODE_DOWN;
        default: return SDL_SCANCODE_UNKNOWN;
    }
}

/* Set by the input code: keys held through a key mapping in the profile. */
bool vitaMappedKey(int scancode);

/* Some games replace Input with their own Win32 keyboard reader and never
 * call the engine's Input.update (Finding Paradise's WASD module), so the
 * pad was never read and every key looked released. Once the game has not
 * updated the engine's input for a moment while it polls keys, update it
 * here once per frame and answer the keys RPG Maker's default keyboard
 * layout gives each button. Letters a game may also use to walk (WASD, Q)
 * are left out. */
static int vitaVKButton(unsigned int key) {
    switch (key) {
        case 0x0d: case 0x20: case 'C': return Input::C;
        case 0x1b: case 'X': case 0x60: return Input::B;
        case 0x10: case 0xa0: case 0xa1: case 'Z': return Input::A;
        case 0x21: return Input::L;
        case 0x22: return Input::R;
        case 0x25: return Input::Left;
        case 0x26: return Input::Up;
        case 0x27: return Input::Right;
        case 0x28: return Input::Down;
        default: return 0;
    }
}

static bool vitaKeyboardOnlyGame() {
    if (SDL_GetTicks() - vitaGameInputTicks < 250)
        return false;
    static int lastFrame = INT_MIN;
    const int frame = shState->graphics().getFrameCount();
    if (frame != lastFrame) {
        lastFrame = frame;
        static bool logged = false;
        if (!logged) {
            logged = true;
            Debug() << "Input: the game reads the keyboard itself; the pad now answers its keys";
        }
        vitaInputAutoUpdate = true;
        try {
            shState->input().update();
        } catch (...) {
            vitaInputAutoUpdate = false;
            throw;
        }
        vitaInputAutoUpdate = false;
    }
    return true;
}

static bool vitaVKPressed(unsigned int key) {
    int count = 0;
    const Uint8 *state = SDL_GetKeyboardState(&count);
    int scancode = vitaVKScancode(key);
    if (scancode <= SDL_SCANCODE_UNKNOWN)
        return false;
    /* The generic Shift, Ctrl and Alt codes answer for either side. */
    if (vitaMappedKey(scancode) ||
        (key == 0x10 && vitaMappedKey(SDL_SCANCODE_RSHIFT)) ||
        (key == 0x11 && vitaMappedKey(SDL_SCANCODE_RCTRL)) ||
        (key == 0x12 && vitaMappedKey(SDL_SCANCODE_RALT)) ||
        (key == 0x0d && vitaMappedKey(SDL_SCANCODE_KP_ENTER)))
        return true;
    if (state && scancode < count && state[scancode] != 0)
        return true;
    const int button = vitaVKButton(key);
    return button && vitaKeyboardOnlyGame() && shState->input().isPressed(button);
}

/* The last Windows message box a game showed. The Vita cannot display it, so
 * it is logged and, when the game quits right after it, written as the error
 * report (binding-mri.cpp). WF-RGSS shows its fatal errors this way and then
 * calls exit(1), which otherwise looks like a clean quit. */
static std::string g_messageBoxCaption, g_messageBoxText;
static Uint32 g_messageBoxTicks = 0;
static bool g_messageBoxShown = false;

void hardrpgRememberMessageBox(const std::string &caption, const std::string &text) {
    g_messageBoxCaption = caption;
    g_messageBoxText = text;
    g_messageBoxTicks = SDL_GetTicks();
    g_messageBoxShown = true;
    Debug() << "MessageBox (the Vita cannot show it):" << caption << "|"
            << text.substr(0, 600);
}

bool hardrpgLastMessageBox(std::string &caption, std::string &text, double &secondsAgo) {
    if (!g_messageBoxShown)
        return false;
    caption = g_messageBoxCaption;
    text = g_messageBoxText;
    secondsAgo = (SDL_GetTicks() - g_messageBoxTicks) / 1000.0;
    return true;
}

/* A narrow Win32 string argument: the Ruby String's bytes up to its first NUL. */
static std::string vitaNarrowArg(VALUE value) {
    if (!RB_TYPE_P(value, RUBY_T_STRING))
        return std::string();
    const char *data = RSTRING_PTR(value);
    return std::string(data, hardrpg::win32text::narrowSpan(data, RSTRING_LEN(value), -1));
}

/* A wide argument, copied out: a Ruby String's bytes need not be 2-aligned. */
static std::u16string vitaWideArg(VALUE value) {
    if (!RB_TYPE_P(value, RUBY_T_STRING))
        return std::u16string();
    std::u16string wide(RSTRING_LEN(value) / 2, u'\0');
    if (!wide.empty())
        memcpy(&wide[0], RSTRING_PTR(value), wide.size() * 2);
    return wide;
}

static long vitaLongArg(VALUE value) {
    return RB_INTEGER_TYPE_P(value) ? NUM2LONG(value) : 0;
}

DECL_TYPE(Bitmap);

static VALUE vitaIdToRef(VALUE id) {
    VALUE objectSpace = rb_const_get(rb_cObject, rb_intern("ObjectSpace"));
    return rb_funcall(objectSpace, rb_intern("_id2ref"), 1, id);
}

/* RPGXPVideoDecoder.dll (see vita_video_decoder.h). The DLL is handed the
 * target Bitmap as `bitmap.object_id << 1`, Ruby 1.8's object address. */
static bool vitaVideoDecoderCall(const char *name, VALUE args, VALUE *result) {
    using namespace VitaVideoDecoder;
    /* Keeps the target Bitmap alive while a video draws into it. */
    static VALUE heldBitmap = Qnil;
    static bool registered = false;
    if (!registered) {
        rb_gc_register_address(&heldBitmap);
        registered = true;
    }

    const long argc = RARRAY_LEN(args);
    int code;
    if (std::strcmp(name, "ViDecCreateContext") == 0 && argc == 3) {
        VALUE address = rb_ary_entry(args, 2);
        Bitmap *bitmap = nullptr;
        VALUE object = Qnil;
        if (RB_INTEGER_TYPE_P(address)) {
            int state = 0;
            VALUE id = rb_funcall(address, rb_intern(">>"), 1, INT2FIX(1));
            object = rb_protect(vitaIdToRef, id, &state);
            if (state) {
                rb_set_errinfo(Qnil);
                object = Qnil;
            }
        }
        if (!NIL_P(object) && rb_typeddata_is_kind_of(object, &BitmapType))
            bitmap = getPrivateData<Bitmap>(object);
        code = createContext(vitaNarrowArg(rb_ary_entry(args, 0)),
                             (int)vitaLongArg(rb_ary_entry(args, 1)), bitmap);
        if (code == Success)
            heldBitmap = object;
    } else if (std::strcmp(name, "ViDecStartRender") == 0) {
        code = startRender();
    } else if (std::strcmp(name, "ViDecGetVideoState") == 0) {
        code = getVideoState();
    } else if (std::strcmp(name, "ViDecSetVolume") == 0 && argc == 1) {
        code = setVolume((int)vitaLongArg(rb_ary_entry(args, 0)));
    } else if (std::strcmp(name, "ViDecCloseContext") == 0) {
        code = closeContext();
        heldBitmap = Qnil;
    } else if (std::strcmp(name, "ViDecWasBadTermination") == 0) {
        code = wasBadTermination();
    } else if (std::strcmp(name, "ViDecGetInternalError") == 0) {
        code = getInternalError();
    } else if (std::strcmp(name, "ViDecGetInternalErrorMessage") == 0) {
        *result = rb_str_new_cstr(getInternalErrorMessage());
        return true;
    } else {
        return false;
    }
    *result = INT2NUM(code);
    return true;
}

/* Freebird's Windows utility exports AVSetEnv to tell Ruby where settings
 * and saves belong. Supply an actual writable application-data directory,
 * rather than the generic DLL no-op (which leaves ENV['AV_APPDATA'] nil).
 * The game's own subdirectory keeps its data separate from other games. */
static bool vitaUtilsCall(const char *name, VALUE args, VALUE *result) {
    if (std::strcmp(name, "AVSetEnv") != 0 || RARRAY_LEN(args) != 0)
        return false;
    const char *failed = "AVSetEnv";
    if (!hardrpg::prepareWindowsAppData(&failed))
        rb_sys_fail(failed);
    *result = INT2NUM(1);
    return true;
}

/* The Vita has no Steam client. Games that call steam_api.dll through
 * Win32API (Finding Paradise and other Freebird releases) quit unless
 * SteamAPI_Init reports 1. Report the API as started and make the rest
 * no-ops, as the Linux builds of those games run without Steam; achievements
 * stay off because their helper DLLs keep answering 0 (no Steam data). */
static bool vitaSteamApiCall(const char *name, VALUE *result) {
    if (!std::strcmp(name, "SteamAPI_Init") || !std::strcmp(name, "SteamAPI_InitSafe")) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            Debug() << "steam_api: no Steam client on the Vita; reporting" << name << "as started";
        }
        *result = INT2NUM(1);
        return true;
    }
    if (!std::strcmp(name, "SteamAPI_RestartAppIfNecessary") ||
        !std::strcmp(name, "SteamAPI_Shutdown") ||
        !std::strcmp(name, "SteamAPI_RunCallbacks")) {
        *result = INT2NUM(0);
        return true;
    }
    return false;
}

/* kernel32 text conversion (see util/win32-text.h). */
/* kernel32 GetSystemPowerStatus, filled from the Vita battery. Pokemon Essentials asks once a
 * minute on the map and says "the battery is low, you should save soon" when the Low or Critical
 * flag is set, under 3 %, or under 5 minutes left. Windows sets Low under 33 %, which would
 * repeat the Vita's own early warning; here Low means 5 % or less, a last call to save.
 * On the charger: no low flags and an unknown time left, as Windows reports it. */
static const int kVitaBatteryLowPercent = 5;
static bool vitaPowerStatus(VALUE buffer, VALUE *result) {
    const int percent = scePowerGetBatteryLifePercent();
    if (percent < 0 || percent > 100) {
        *result = INT2NUM(0);
        return true;
    }
    const bool online = scePowerIsPowerOnline() > 0;
    const bool charging = scePowerIsBatteryCharging() > 0;
    const int minutes = scePowerGetBatteryLifeTime();
    uint8_t flag = 0;
    if (charging)
        flag |= 8;
    else if (!online && percent <= kVitaBatteryLowPercent)
        flag |= percent <= 2 ? 4 : 2;
    const uint32_t lifeTime = (online || minutes < 0) ? 0xFFFFFFFFu : (uint32_t)minutes * 60u;
    uint8_t s[12];
    s[0] = online ? 1 : 0;   /* ACLineStatus */
    s[1] = flag;             /* BatteryFlag */
    s[2] = (uint8_t)percent; /* BatteryLifePercent */
    s[3] = 0;                /* SystemStatusFlag */
    memcpy(s + 4, &lifeTime, 4);  /* BatteryLifeTime, seconds */
    const uint32_t full = 0xFFFFFFFFu;
    memcpy(s + 8, &full, 4);      /* BatteryFullLifeTime: unknown */
    if (!RB_TYPE_P(buffer, T_STRING) || RSTRING_LEN(buffer) < 12) {
        *result = INT2NUM(0);
        return true;
    }
    rb_str_modify(buffer);
    memcpy(RSTRING_PTR(buffer), s, 12);
    *result = INT2NUM(1);
    return true;
}

static bool vitaKernel32Call(const char *name, VALUE args, VALUE *result) {
    using namespace hardrpg::win32text;
    if (std::strcmp(name, "GetSystemPowerStatus") == 0 && RARRAY_LEN(args) == 1)
        return vitaPowerStatus(rb_ary_entry(args, 0), result);
    if (std::strcmp(name, "MultiByteToWideChar") == 0 && RARRAY_LEN(args) == 6) {
        VALUE src = rb_ary_entry(args, 2), dst = rb_ary_entry(args, 4);
        if (!RB_TYPE_P(src, RUBY_T_STRING)) { *result = INT2NUM(0); return true; }
        long srcLen = vitaLongArg(rb_ary_entry(args, 3));
        long dstUnits = vitaLongArg(rb_ary_entry(args, 5));
        size_t room = RB_TYPE_P(dst, RUBY_T_STRING) ? RSTRING_LEN(dst) / 2 : 0;
        size_t cap = dstUnits > 0 ? std::min(size_t(dstUnits), room) : 0;
        std::u16string out(cap, u'\0');
        long n = multiByteToWideChar(RSTRING_PTR(src), RSTRING_LEN(src), srcLen,
                                     cap ? &out[0] : nullptr, dstUnits > 0 ? cap : 0);
        if (dstUnits > 0 && cap == 0) n = 0; // a size was given but no buffer
        if (n > 0 && cap) {
            rb_str_modify(dst);
            memcpy(RSTRING_PTR(dst), out.data(), size_t(n) * 2);
        }
        *result = LONG2NUM(n);
        return true;
    }
    if (std::strcmp(name, "WideCharToMultiByte") == 0 && RARRAY_LEN(args) == 8) {
        VALUE src = rb_ary_entry(args, 2), dst = rb_ary_entry(args, 4);
        if (!RB_TYPE_P(src, RUBY_T_STRING)) { *result = INT2NUM(0); return true; }
        std::u16string wide = vitaWideArg(src);
        long srcLen = vitaLongArg(rb_ary_entry(args, 3));
        long dstBytes = vitaLongArg(rb_ary_entry(args, 5));
        size_t room = RB_TYPE_P(dst, RUBY_T_STRING) ? RSTRING_LEN(dst) : 0;
        size_t cap = dstBytes > 0 ? std::min(size_t(dstBytes), room) : 0;
        std::string out(cap, '\0');
        long n = wideCharToMultiByte(wide.data(), wide.size(), srcLen,
                                     cap ? &out[0] : nullptr, dstBytes > 0 ? cap : 0);
        if (dstBytes > 0 && cap == 0) n = 0;
        if (n > 0 && cap) {
            rb_str_modify(dst);
            memcpy(RSTRING_PTR(dst), out.data(), size_t(n));
        }
        *result = LONG2NUM(n);
        return true;
    }
    return false;
}

static bool vitaUser32Call(const char *name, VALUE args, VALUE *result) {
    if ((std::strcmp(name, "MessageBoxA") == 0 || std::strcmp(name, "MessageBoxW") == 0) &&
        RARRAY_LEN(args) == 4) {
        using namespace hardrpg::win32text;
        if (name[10] == 'W') {
            std::u16string text = vitaWideArg(rb_ary_entry(args, 1));
            std::u16string caption = vitaWideArg(rb_ary_entry(args, 2));
            g_messageBoxText = utf16ToUtf8(text.data(), wideSpan(text.data(), text.size(), -1));
            g_messageBoxCaption = utf16ToUtf8(caption.data(), wideSpan(caption.data(), caption.size(), -1));
        } else {
            g_messageBoxText = vitaNarrowArg(rb_ary_entry(args, 1));
            g_messageBoxCaption = vitaNarrowArg(rb_ary_entry(args, 2));
        }
        hardrpgRememberMessageBox(g_messageBoxCaption, g_messageBoxText);
        *result = INT2NUM(messageBoxAnswer(static_cast<unsigned long>(vitaLongArg(rb_ary_entry(args, 3)))));
        return true;
    }
    if (std::strcmp(name, "GetAsyncKeyState") == 0 || std::strcmp(name, "GetKeyState") == 0) {
        unsigned int key = NUM2UINT(rb_ary_entry(args, 0));
        *result = INT2NUM(vitaVKPressed(key) ? 0x8000 : 0);
        return true;
    }
    /* There is no mouse: report the cursor as parked outside the game window,
     * as on a PC whose pointer is elsewhere. Mouse scripts (Finding Paradise's
     * Near Fantastica/SephirothSpawn module) pack the POINT these fill and
     * crashed on the nil a failed call left; ScreenToClient keeps the point. */
    if (std::strcmp(name, "GetCursorPos") == 0 && RARRAY_LEN(args) == 1) {
        VALUE point = rb_ary_entry(args, 0);
        if (!RB_TYPE_P(point, RUBY_T_STRING) || RSTRING_LEN(point) < 8)
            return false;
        rb_str_modify(point);
        const int32_t outside[2] = {-32768, -32768};
        memcpy(RSTRING_PTR(point), outside, sizeof outside);
        *result = INT2NUM(1);
        return true;
    }
    if (std::strcmp(name, "ScreenToClient") == 0 && RARRAY_LEN(args) == 2) {
        *result = INT2NUM(1);
        return true;
    }
    if (std::strcmp(name, "GetKeyboardState") == 0) {
        VALUE argument = rb_ary_entry(args, 0);
        unsigned char *buffer;
        if (RB_TYPE_P(argument, RUBY_T_STRING)) {
            if (RSTRING_LEN(argument) < 256)
                rb_raise(rb_eArgError, "GetKeyboardState needs a 256-byte buffer");
            rb_str_modify(argument);
            buffer = reinterpret_cast<unsigned char *>(RSTRING_PTR(argument));
        } else {
            uintptr_t address = static_cast<uintptr_t>(NUM2ULL(argument));
            if (!address) rb_raise(rb_eArgError, "GetKeyboardState needs a buffer");
            buffer = reinterpret_cast<unsigned char *>(address);
        }
        for (unsigned int key = 0; key < 256; ++key)
            buffer[key] = vitaVKPressed(key) ? 0x80 : 0;
        *result = INT2NUM(1);
        return true;
    }
    return false;
}

/* What a Vita MiniFFI object stands for. Win32API.new runs often (Pokemon Essentials makes
 * new objects on every key and window check), so it does the work once: the library and
 * function are classified here, and call only switches on the result. */
enum VitaFFILib { VL_OTHER, VL_USER32, VL_KERNEL32, VL_UTILS, VL_STEAM, VL_VIDEO };
enum VitaFFIFn {
    VF_GENERIC = 0,    /* the library's dispatcher below, then a typed zero */
    VF_KEY_STATE = 1,  /* user32 GetAsyncKeyState / GetKeyState (native-events.cpp reads 1 and 2) */
    VF_KEYBOARD = 2,   /* user32 GetKeyboardState */
    VF_RUBY,           /* a handler from launcher/rgss_extensions.rb (HardRPGWin32::HANDLERS) */
    VF_WINDOW,         /* FindWindow, GetForegroundWindow, GetActiveWindow, GetFocus */
    VF_WINDOW_AFTER,   /* FindWindowEx: the window when no "after" window is given */
    VF_THREAD,         /* kernel32 GetCurrentThreadId / GetCurrentProcessId */
    VF_WINDOW_THREAD,  /* user32 GetWindowThreadProcessId */
    VF_CLIENT_RECT,    /* user32 GetClientRect / GetWindowRect */
    VF_METRICS,        /* user32 GetSystemMetrics */
};
/* Same values as HardRPGWindow in launcher/rgss_extensions.rb (its Ruby reference). */
static const long kVitaHwnd = 0x00010010;
static const long kVitaThread = 1;

struct VitaFFI {
    bool ready;
    uint8_t lib, fn, exports, nimport;
    VALUE handler;
    char library[48];
    char function[64];
};

static void vitaFFIMark(void *p) {
    if (p) rb_gc_mark(static_cast<VitaFFI *>(p)->handler);
}
static void vitaFFIFree(void *p) { xfree(p); }
static size_t vitaFFISize(const void *) { return sizeof(VitaFFI); }
static const rb_data_type_t VitaFFIType = {
    "MiniFFI", {vitaFFIMark, vitaFFIFree, vitaFFISize, 0, {0}}, 0, 0, RUBY_TYPED_FREE_IMMEDIATELY};

static VALUE vitaFFIAllocate(VALUE klass) {
    VitaFFI *f;
    VALUE obj = TypedData_Make_Struct(klass, VitaFFI, &VitaFFIType, f);
    f->handler = Qnil;
    return obj;
}

static VitaFFI *vitaFFIGet(VALUE obj) {
    if (RB_SPECIAL_CONST_P(obj) || !RB_TYPE_P(obj, T_DATA) || !RTYPEDDATA_P(obj) ||
        RTYPEDDATA_TYPE(obj) != &VitaFFIType)
        return nullptr;
    return static_cast<VitaFFI *>(RTYPEDDATA_DATA(obj));
}

static int vitaFFIType(VALUE spec) {
    switch (RSTRING_LEN(spec) ? *RSTRING_PTR(spec) : 'v') {
        case 'N': case 'n': case 'L': case 'l': return _T_NUMBER;
        case 'P': case 'p': return _T_POINTER;
        case 'I': case 'i': return _T_INTEGER;
        case 'B': case 'b': return _T_BOOL;
        default: return _T_VOID;
    }
}

/* Number of parameters an import spec names ("ip", ["i", "p"], %w(l p) as a string, nil). */
static int vitaFFICountImports(VALUE imports) {
    int n = 0;
    auto counts = [](char c) {
        return strchr("NnLlPpIiBb", c) != nullptr && c != '\0';
    };
    if (NIL_P(imports))
        return 0;
    if (RB_TYPE_P(imports, T_ARRAY)) {
        for (long i = 0; i < RARRAY_LEN(imports); i++) {
            VALUE e = RARRAY_AREF(imports, i);
            SafeStringValue(e);
            if (RSTRING_LEN(e) && counts(*RSTRING_PTR(e)))
                ++n;
        }
        return n;
    }
    SafeStringValue(imports);
    const char *c = RSTRING_PTR(imports);
    for (long i = 0; i < RSTRING_LEN(imports); i++)
        if (counts(c[i]))
            ++n;
    return n;
}

static VALUE vitaFFIInitialize(int argc, VALUE *argv, VALUE self) {
    VALUE libname, func, imports, exports;
    rb_scan_args(argc, argv, "22", &libname, &func, &imports, &exports);
    SafeStringValue(libname);
    SafeStringValue(func);
    VitaFFI *f = vitaFFIGet(self);
    if (!f)
        rb_raise(rb_eTypeError, "not a MiniFFI object");
    const int n = vitaFFICountImports(imports);
    if (!NIL_P(exports))
        SafeStringValue(exports);

    std::string library(RSTRING_PTR(libname), RSTRING_LEN(libname));
    auto slash = library.find_last_of("/\\");
    if (slash != std::string::npos) library.erase(0, slash + 1);
    for (char &c : library)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    std::string base = library;
    if (base.size() > 4 && base.compare(base.size() - 4, 4, ".dll") == 0)
        base.erase(base.size() - 4);
    std::string name(RSTRING_PTR(func), strnlen(RSTRING_PTR(func), RSTRING_LEN(func)));

    if (MINIFFI_MAX_ARGS < n)
        throw Exception(Exception::RuntimeError, "too many parameters: %ld/%ld\n",
                 (long)n, (long)MINIFFI_MAX_ARGS);

    f->lib = base == "user32" ? VL_USER32 : base == "kernel32" ? VL_KERNEL32 :
             base == "utils" ? VL_UTILS :
             (base == "steam_api" || base == "steam_api64") ? VL_STEAM :
             base == "rpgxpvideodecoder" ? VL_VIDEO : VL_OTHER;
    f->nimport = (uint8_t)n;
    f->exports = NIL_P(exports) ? _T_VOID : vitaFFIType(exports);
    snprintf(f->library, sizeof f->library, "%s", RSTRING_PTR(libname));
    snprintf(f->function, sizeof f->function, "%s", name.c_str());
    f->handler = Qnil;
    f->fn = VF_GENERIC;

    /* The window and thread answers Essentials asks for on every key check are made
     * here; for anything else a Ruby handler wins, as the Ruby layer it replaces did. */
    const char *s = name.c_str();
    if (f->lib == VL_USER32) {
        if (!strcmp(s, "FindWindow") || !strcmp(s, "FindWindowA") || !strcmp(s, "FindWindowW") ||
            !strcmp(s, "GetForegroundWindow") || !strcmp(s, "GetActiveWindow") || !strcmp(s, "GetFocus"))
            f->fn = VF_WINDOW;
        else if (!strcmp(s, "FindWindowEx") || !strcmp(s, "FindWindowExA") || !strcmp(s, "FindWindowExW"))
            f->fn = VF_WINDOW_AFTER;
        else if (!strcmp(s, "GetWindowThreadProcessId"))
            f->fn = VF_WINDOW_THREAD;
        else if (!strcmp(s, "GetClientRect") || !strcmp(s, "GetWindowRect"))
            f->fn = VF_CLIENT_RECT;
        else if (!strcmp(s, "GetSystemMetrics"))
            f->fn = VF_METRICS;
    } else if (f->lib == VL_KERNEL32) {
        if (!strcmp(s, "GetCurrentThreadId") || !strcmp(s, "GetCurrentProcessId"))
            f->fn = VF_THREAD;
    }
    static ID idWin32 = rb_intern("HardRPGWin32"), idHandlers = rb_intern("HANDLERS");
    if (f->fn == VF_GENERIC && rb_const_defined(rb_cObject, idWin32)) {
        VALUE mod = rb_const_get(rb_cObject, idWin32);
        if (rb_const_defined(mod, idHandlers)) {
            VALUE table = rb_const_get(mod, idHandlers);
            if (RB_TYPE_P(table, T_HASH)) {
                std::string key = base + "!" + name;
                for (char &c : key)
                    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                VALUE h = rb_hash_lookup2(table, rb_str_new(key.data(), key.size()), Qnil);
                if (!NIL_P(h)) {
                    f->handler = h;
                    f->fn = VF_RUBY;
                }
            }
        }
    }
    if (f->fn == VF_GENERIC && f->lib == VL_USER32) {
        if (!strcmp(s, "GetAsyncKeyState") || !strcmp(s, "GetKeyState"))
            f->fn = n == 1 ? VF_KEY_STATE : VF_GENERIC;
        else if (!strcmp(s, "GetKeyboardState"))
            f->fn = n == 1 ? VF_KEYBOARD : VF_GENERIC;
    }
    f->ready = true;
    if (rb_block_given_p())
        rb_yield(self);
    return Qnil;
}

/* Writes packed bytes into a buffer the game passed as a String; 1 when they fit. */
static int vitaFFIWrite(VALUE buffer, const void *bytes, long size) {
    if (!RB_TYPE_P(buffer, T_STRING) || RSTRING_LEN(buffer) < size)
        return 0;
    rb_str_modify(buffer);
    memcpy(RSTRING_PTR(buffer), bytes, size);
    return 1;
}

static long vitaFFIToI(VALUE v) {
    if (RB_INTEGER_TYPE_P(v)) return NUM2LONG(v);
    if (NIL_P(v)) return 0;
    return NUM2LONG(rb_funcall(v, rb_intern("to_i"), 0));
}

static VALUE vitaFFICall(int argc, VALUE *argv, VALUE self) {
    VitaFFI *f = vitaFFIGet(self);
    if (!f || !f->ready)
        throw Exception(Exception::RuntimeError, "MiniFFI function was not initialized");
    switch (f->fn) {
        case VF_KEY_STATE:
            if (argc == 1)
                return INT2NUM(vitaVKPressed(NUM2UINT(argv[0])) ? 0x8000 : 0);
            break;
        case VF_RUBY: {
            /* call(*args), or call([args]) as one array, like Win32API */
            VALUE args = argc == 1 && RB_TYPE_P(argv[0], T_ARRAY) ? argv[0] : rb_ary_new_from_values(argc, argv);
            return rb_funcallv(f->handler, rb_intern("call"), (int)RARRAY_LEN(args), RARRAY_CONST_PTR(args));
        }
        case VF_WINDOW:
            return LONG2NUM(kVitaHwnd);
        case VF_WINDOW_AFTER:
            return LONG2NUM(argc >= 2 && vitaFFIToI(argv[1]) != 0 ? 0 : kVitaHwnd);
        case VF_THREAD:
            return LONG2NUM(kVitaThread);
        case VF_WINDOW_THREAD:
            if (argc < 1 || vitaFFIToI(argv[0]) != kVitaHwnd)
                return INT2FIX(0);
            if (argc >= 2) {
                int32_t thread = (int32_t)kVitaThread;
                vitaFFIWrite(argv[1], &thread, 4);
            }
            return LONG2NUM(kVitaThread);
        case VF_CLIENT_RECT: {
            if (argc < 2 || vitaFFIToI(argv[0]) != kVitaHwnd)
                return INT2FIX(0);
            int32_t rect[4] = {0, 0, 640, 480};
            if (shState) {
                rect[2] = shState->graphics().width();
                rect[3] = shState->graphics().height();
            }
            return INT2FIX(vitaFFIWrite(argv[1], rect, sizeof rect));
        }
        case VF_METRICS: {
            long index = argc >= 1 ? vitaFFIToI(argv[0]) : -1;
            return INT2FIX(index == 0 || index == 16 ? 960 : index == 1 || index == 17 ? 544 : 0);
        }
        default:
            break;
    }
    if (argc != f->nimport)
        throw Exception(Exception::RuntimeError,
                 "wrong number of parameters: expected %d, got %d", (int)f->nimport, argc);
    VALUE args = rb_ary_new_from_values(argc, argv);
    VALUE handled;
    switch (f->lib) {
        case VL_USER32:
            if (vitaUser32Call(f->function, args, &handled)) return handled;
            break;
        case VL_KERNEL32:
            if (vitaKernel32Call(f->function, args, &handled)) return handled;
            break;
        case VL_UTILS:
            if (vitaUtilsCall(f->function, args, &handled)) return handled;
            break;
        case VL_STEAM:
            if (vitaSteamApiCall(f->function, &handled)) return handled;
            break;
        case VL_VIDEO:
            if (vitaVideoDecoderCall(f->function, args, &handled)) return handled;
            break;
        default:
            break;
    }
    /* No library can load here: warn once and return a typed zero. */
    static bool warned = false;
    if (!warned) {
        warned = true;
        Debug() << "MiniFFI: Vita has no loadable libraries; ignoring call "
                << f->library << "!" << f->function;
    }
    switch (f->exports) {
        case _T_POINTER: return rb_str_new_cstr("");
        case _T_BOOL: return Qfalse;
        default: return MVAL2RB(0);
    }
}

/* For the native script paths (native-events.cpp): what a MiniFFI object calls on the Vita
 * (1: user32 GetAsyncKeyState/GetKeyState with one argument, 2: user32 GetKeyboardState with
 * one argument, 0: anything else), the key state those calls read, and a DL::CPtr's buffer. */
static VALUE g_miniFFIClass = Qnil;
int hardrpgMiniFFIKeyKind(VALUE obj) {
    VitaFFI *f = vitaFFIGet(obj);
    if (!f || !f->ready)
        return 0;
    return f->fn == VF_KEY_STATE ? 1 : f->fn == VF_KEYBOARD ? 2 : 0;
}
bool hardrpgVitaKeyPressed(unsigned int key) { return vitaVKPressed(key); }
unsigned char *hardrpgDLData(VALUE obj, size_t *size) {
    if (NIL_P(vitaDLPointerClass) || rb_class_of(obj) != vitaDLPointerClass)
        return nullptr;
    VitaDLPointer *ptr = vitaDLGet(obj);
    if (!ptr || !ptr->data)
        return nullptr;
    *size = ptr->size;
    return ptr->data;
}

static void vitaDLBindingInit() {
    VALUE dl = rb_define_module("DL");
    vitaDLPointerClass = rb_define_class_under(dl, "CPtr", rb_cObject);
    rb_define_alloc_func(vitaDLPointerClass, vitaDLAlloc);
    rb_define_singleton_method(dl, "malloc", RUBY_METHOD_FUNC(vitaDLMalloc), 1);
    rb_define_method(vitaDLPointerClass, "initialize", RUBY_METHOD_FUNC(vitaDLInitialize), -1);
    rb_define_method(vitaDLPointerClass, "to_i", RUBY_METHOD_FUNC(vitaDLToI), 0);
    rb_define_method(vitaDLPointerClass, "[]", RUBY_METHOD_FUNC(vitaDLRead), 1);
    rb_define_method(vitaDLPointerClass, "[]=", RUBY_METHOD_FUNC(vitaDLWrite), 2);
}
#endif

RB_METHOD_GUARD(MiniFFI_call) {
#if defined(__vita__) || defined(__psp2__)
    return vitaFFICall(argc, argv, self);
#endif
    MiniFFIFuncArgs param;
#define params param.params
    VALUE func = rb_iv_get(self, "_func");
    VALUE own_imports = rb_iv_get(self, "_imports");
    VALUE own_exports = rb_iv_get(self, "_exports");
    MINIFFI_FUNC ApiFunction = (MINIFFI_FUNC)RB2MVAL(func);
    /* `_imports` is set by initialize: an object whose initialize raised or
     * was skipped has none, and params[] holds MINIFFI_MAX_ARGS. */
    if (TYPE(own_imports) != T_ARRAY || RARRAY_LEN(own_imports) > MINIFFI_MAX_ARGS)
        throw Exception(Exception::RuntimeError, "MiniFFI function was not initialized");
    VALUE args;
    int items = rb_scan_args(argc, argv, "0*", &args);
    int nimport = RARRAY_LEN(own_imports);
    if (items != nimport)
        throw Exception(Exception::RuntimeError,
                 "wrong number of parameters: expected %d, got %d", nimport, items);

    for (int i = 0; i < nimport; i++) {
        VALUE str = rb_ary_entry(args, i);
        mffi_value lParam = 0;
        switch (FIX2INT(rb_ary_entry(own_imports, i))) {
            case _T_POINTER:
                if (NIL_P(str)) {
                    lParam = 0;
                } else if (FIXNUM_P(str)) {
                    lParam = RB2MVAL(str);
                } else {
                    StringValue(str);
                    rb_str_modify(str);
                    lParam = (mffi_value)RSTRING_PTR(str);
                }
                break;

            case _T_BOOL:
                rb_bool_arg(rb_ary_entry(args, i), (bool*)&lParam);
                break;

            case _T_INTEGER:
#if INTPTR_MAX == INT64_MAX
                lParam = RB2MVAL(rb_ary_entry(args, i)) & UINT32_MAX;
                break;
#endif
            case _T_NUMBER:
            default:
                lParam = RB2MVAL(rb_ary_entry(args, i));
                break;
        }
        params[i] = lParam;
    }
#if RAPI_MAJOR >= 2
    MFFICallCBArgs cb_args {ApiFunction, &param, nimport};
    mffi_value ret = (mffi_value)rb_thread_call_without_gvl(miniffi_call_cb, &cb_args, 0, 0);
#else
    mffi_value ret = miniffi_call_intern(ApiFunction, &param, nimport);
#endif

    switch (FIX2INT(own_exports)) {
        case _T_NUMBER:
        case _T_INTEGER:
            return MVAL2RB(ret);

        case _T_POINTER:
            return rb_utf8_str_new_cstr((char *)ret);

        case _T_BOOL:
            return rb_bool_new(ret);

        case _T_VOID:
        default:
            return MVAL2RB(0);
    }
}
RB_METHOD_GUARD_END

void MiniFFIBindingInit() {
    VALUE cMiniFFI = rb_define_class("MiniFFI", rb_cObject);
#if defined(__vita__) || defined(__psp2__)
    g_miniFFIClass = cMiniFFI;
#endif
#if defined(__vita__) || defined(__psp2__)
    rb_define_alloc_func(cMiniFFI, vitaFFIAllocate);
    /* launcher/rgss_extensions.rb: the engine dispatches HardRPGWin32 handlers itself. */
    rb_define_const(cMiniFFI, "HARDRPG_NATIVE_DISPATCH", Qtrue);
#elif RAPI_FULL > 187
    rb_define_alloc_func(cMiniFFI, classAllocate<&MiniFFIType>);
#else
    rb_define_alloc_func(cMiniFFI, MiniFFIAllocate);
#endif
    _rb_define_method(cMiniFFI, "initialize", MiniFFI_initialize);
    _rb_define_method(cMiniFFI, "call", MiniFFI_call);
    rb_define_alias(cMiniFFI, "Call", "call");

    rb_define_const(rb_cObject, "Win32API", cMiniFFI);
#if defined(__vita__) || defined(__psp2__)
    vitaDLBindingInit();
#endif
}
