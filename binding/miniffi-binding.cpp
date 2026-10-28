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
#include <algorithm>
#include <string>
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
    return state && scancode < count && state[scancode] != 0;
}

/* The last Windows message box a game showed. The Vita cannot display it, so
 * it is logged and, when the game quits right after it, written as the error
 * report (binding-mri.cpp). WF-RGSS shows its fatal errors this way and then
 * calls exit(1), which otherwise looks like a clean quit. */
static std::string g_messageBoxCaption, g_messageBoxText;
static Uint32 g_messageBoxTicks = 0;
static bool g_messageBoxShown = false;

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

/* kernel32 text conversion (see util/win32-text.h). */
static bool vitaKernel32Call(VALUE function, VALUE args, VALUE *result) {
    const char *name = StringValueCStr(function);
    using namespace hardrpg::win32text;
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

static bool vitaUser32Call(VALUE function, VALUE args, VALUE *result) {
    const char *name = StringValueCStr(function);
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
        g_messageBoxTicks = SDL_GetTicks();
        g_messageBoxShown = true;
        Debug() << "MessageBox (the Vita cannot show it):" << g_messageBoxCaption << "|"
                << g_messageBoxText.substr(0, 600);
        *result = INT2NUM(messageBoxAnswer(static_cast<unsigned long>(vitaLongArg(rb_ary_entry(args, 3)))));
        return true;
    }
    if (std::strcmp(name, "GetAsyncKeyState") == 0 || std::strcmp(name, "GetKeyState") == 0) {
        unsigned int key = NUM2UINT(rb_ary_entry(args, 0));
        *result = INT2NUM(vitaVKPressed(key) ? 0x8000 : 0);
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

/* For the native script paths (native-events.cpp): what a MiniFFI object calls on the Vita
 * (1: user32 GetAsyncKeyState/GetKeyState with one argument, 2: user32 GetKeyboardState with
 * one argument, 0: anything else), the key state those calls read, and a DL::CPtr's buffer. */
static VALUE g_miniFFIClass = Qnil;
int hardrpgMiniFFIKeyKind(VALUE obj) {
    if (NIL_P(g_miniFFIClass) || !rb_obj_is_kind_of(obj, g_miniFFIClass) || !RTEST(rb_iv_get(obj, "_dummy")))
        return 0;
    VALUE imports = rb_iv_get(obj, "_imports");
    VALUE libv = rb_iv_get(obj, "_libname");
    VALUE funcv = rb_iv_get(obj, "_funcname");
    if (!RB_TYPE_P(imports, T_ARRAY) || RARRAY_LEN(imports) != 1 || !RB_TYPE_P(libv, T_STRING) ||
        !RB_TYPE_P(funcv, T_STRING))
        return 0;
    std::string library(RSTRING_PTR(libv), RSTRING_LEN(libv));
    auto slash = library.find_last_of("/\\");
    if (slash != std::string::npos) library.erase(0, slash + 1);
    for (char &character : library)
        if (character >= 'A' && character <= 'Z')
            character += 'a' - 'A';
    if (library != "user32.dll" && library != "user32")
        return 0;
    std::string name(RSTRING_PTR(funcv), RSTRING_LEN(funcv));
    if (name.size() != strlen(name.c_str()))
        return 0;
    if (name == "GetAsyncKeyState" || name == "GetKeyState")
        return 1;
    if (name == "GetKeyboardState")
        return 2;
    return 0;
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

    if (RTEST(rb_iv_get(self, "_dummy")))
    {
#if defined(__vita__) || defined(__psp2__)
        VALUE libv = rb_iv_get(self, "_libname");
        std::string library = StringValueCStr(libv);
        auto slash = library.find_last_of("/\\");
        if (slash != std::string::npos) library.erase(0, slash + 1);
        for (char &character : library)
            if (character >= 'A' && character <= 'Z')
                character += 'a' - 'A';
        if (library == "user32.dll" || library == "user32") {
            VALUE handled;
            if (vitaUser32Call(rb_iv_get(self, "_funcname"), args, &handled))
                return handled;
        }
        if (library == "kernel32.dll" || library == "kernel32") {
            VALUE handled;
            if (vitaKernel32Call(rb_iv_get(self, "_funcname"), args, &handled))
                return handled;
        }
#endif
        /* Vita dummy handle: no libraries can load here. Warn once
         * and return a type-appropriate zero instead of calling
         * into garbage. */
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            VALUE libv = rb_iv_get(self, "_libname");
            VALUE funcv = rb_iv_get(self, "_funcname");
            Debug() << "MiniFFI: Vita has no loadable libraries; ignoring call "
                    << RSTRING_PTR(libv) << "!" << RSTRING_PTR(funcv);
        }
        switch (FIX2INT(own_exports))
        {
            case _T_POINTER:
                return rb_str_new_cstr("");
            case _T_BOOL:
                return Qfalse;
            case _T_NUMBER:
            case _T_INTEGER:
            case _T_VOID:
            default:
                return MVAL2RB(0);
        }
    }

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
#if RAPI_FULL > 187
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
