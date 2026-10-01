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
        case 0x1b: return SDL_SCANCODE_ESCAPE;
        case 0x20: return SDL_SCANCODE_SPACE;
        case 0x25: return SDL_SCANCODE_LEFT;
        case 0x26: return SDL_SCANCODE_UP;
        case 0x27: return SDL_SCANCODE_RIGHT;
        case 0x28: return SDL_SCANCODE_DOWN;
        default: return SDL_SCANCODE_UNKNOWN;
    }
}

static bool vitaVKPressed(unsigned int key) {
    int count = 0;
    const Uint8 *state = SDL_GetKeyboardState(&count);
    int scancode = vitaVKScancode(key);
    return state && scancode > SDL_SCANCODE_UNKNOWN && scancode < count && state[scancode] != 0;
}

static bool vitaUser32Call(VALUE function, VALUE args, VALUE *result) {
    const char *name = StringValueCStr(function);
    if (std::strcmp(name, "GetAsyncKeyState") == 0 || std::strcmp(name, "GetKeyState") == 0) {
        unsigned int key = NUM2UINT(rb_ary_entry(args, 0));
        *result = INT2NUM(vitaVKPressed(key) ? 0x8000 : 0);
        return true;
    }
    if (std::strcmp(name, "GetKeyboardState") == 0) {
        uintptr_t address = static_cast<uintptr_t>(NUM2ULL(rb_ary_entry(args, 0)));
        if (!address) rb_raise(rb_eArgError, "GetKeyboardState needs a buffer");
        unsigned char *buffer = reinterpret_cast<unsigned char *>(address);
        for (unsigned int key = 0; key < 256; ++key)
            buffer[key] = vitaVKPressed(key) ? 0x80 : 0;
        *result = INT2NUM(1);
        return true;
    }
    return false;
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
        if (std::strcmp(StringValueCStr(libv), "user32.dll") == 0) {
            VALUE handled;
            if (vitaUser32Call(rb_iv_get(self, "_funcname"), args, &handled))
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
