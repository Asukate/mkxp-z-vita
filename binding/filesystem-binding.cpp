/*
 ** filesystem-binding.cpp
 **
 ** This file is part of mkxp.
 **
 ** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
 **
 ** mkxp is free software: you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, either version 2 of the License, or
 ** (at your option) any later version.
 **
 ** mkxp is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "src/config.h"

#include "binding-util.h"

#include "filesystem.h"
#include "sharedstate.h"
#include "src/util/util.h"
#include "data-stream.h"
#include <climits>

#if RAPI_FULL > 187
#include "ruby/encoding.h"
#include "ruby/intern.h"
#else
#include "intern.h"
#endif

#if RAPI_MAJOR >= 2
#include <ruby/thread.h>
#endif

static void fileIntFreeInstance(void *inst) {
    delete static_cast<DataStream *>(inst);
}

#if RAPI_FULL > 187
DEF_TYPE_CUSTOMFREE(FileInt, fileIntFreeInstance);
#else
DEF_ALLOCFUNC_CUSTOMFREE(FileInt, fileIntFreeInstance);
#endif

static VALUE fileIntForPath(const char *path, bool rubyExc) {
    VALUE klass = rb_const_get(rb_cObject, rb_intern("FileInt"));
    
    VALUE obj = rb_obj_alloc(klass);
    
    SDL_RWops *ops = SDL_AllocRW();
    
    try {
        shState->fileSystem().openReadRaw(*ops, path);
    } catch (const Exception &e) {
        SDL_FreeRW(ops);
        
        throw e;
    }
    
    DataStream *stream;
    try {
        stream = new DataStream(ops);
    } catch (...) {
        SDL_RWclose(ops);
        SDL_FreeRW(ops);
        throw;
    }
    setPrivateData(obj, stream);
    
    return obj;
}

#if RAPI_MAJOR >= 2
typedef struct {
    DataStream *stream;
    void *dst;
    int length;
    size_t result;
} fileIntReadCbArgs;

void call_RWread_cb(fileIntReadCbArgs *args) {
    args->result = args->stream->read(args->dst, args->length);
}
#endif

RB_METHOD(fileIntRead) {
    
    int length = -1;
    rb_get_args(argc, argv, "|i", &length RB_ARG_END);
    
    DataStream *stream = getPrivateData<DataStream>(self);
    if (stream->closed()) rb_raise(rb_eIOError, "closed stream");
    if (argc && length < 0) rb_raise(rb_eArgError, "negative length");
    
    if (length == -1) {
        Sint64 remaining = stream->remaining();
        if (remaining < 0 || remaining > INT_MAX)
            rb_raise(rb_eIOError, "invalid stream size");
        length = int(remaining);
    }
    
    if (length == 0)
        return rb_str_new("", 0);
    
    VALUE data = rb_str_new(0, length);
    
    
    
#if RAPI_MAJOR >= 2
    fileIntReadCbArgs cbargs {stream, RSTRING_PTR(data), length, 0};
    rb_thread_call_without_gvl([](void* args) -> void* {
        call_RWread_cb((fileIntReadCbArgs*)args);
        return 0;
    }, (void*)&cbargs, 0, 0);
    size_t count = cbargs.result;
#else
    size_t count = stream->read(RSTRING_PTR(data), length);
#endif
    rb_str_set_len(data, count);
    if (!count && argc) return Qnil;
    
    return data;
}

RB_METHOD(fileIntClose) {
    RB_UNUSED_PARAM;
    
    getPrivateData<DataStream>(self)->close();
    
    return Qnil;
}

RB_METHOD(fileIntGetByte) {
    RB_UNUSED_PARAM;
    
    DataStream *stream = getPrivateData<DataStream>(self);
    if (stream->closed()) rb_raise(rb_eIOError, "closed stream");
    int byte = stream->getByte();
    return byte >= 0 ? rb_fix_new(byte) : Qnil;
}

RB_METHOD(fileIntBinmode) {
    RB_UNUSED_PARAM;
    
    return self;
}

#if RAPI_FULL <= 187
RB_METHOD(fileIntPos) {
    DataStream *stream = getPrivateData<DataStream>(self);
    if (stream->closed()) rb_raise(rb_eIOError, "closed stream");
    long long pos = stream->position();
    return LL2NUM(pos);
}
#endif

/* A big data file, such as an RPG Maker map with hundreds of events, turns
 * into a million or more Ruby objects (Hylics map 28 is 7.5 MB on disk and
 * about 50 MB in memory). When it is loaded, the map loaded before it is
 * usually garbage already, but Ruby has not collected it yet and grows the
 * heap for the new objects first. On the Vita's fixed heap the two maps do
 * not fit together, so collect before loading.
 * Nearly everything the load creates stays alive, so collections during it
 * only re-mark the growing map (about 2 of the 6 seconds that map 28 takes
 * on the Vita). Pause the collector until the load is done. */
static const Sint64 kLoadDataCollectBytes = 1024 * 1024;

static VALUE fileIntMarshalLoad(VALUE data) {
    VALUE marsh = rb_const_get(rb_cObject, rb_intern("Marshal"));
    return rb_funcall2(marsh, rb_intern("load"), 1, &data);
}
static VALUE fileIntEnableGC(VALUE) {
    rb_gc_enable();
    return Qnil;
}

static VALUE fileIntLoad(VALUE port) {
    bool big = getPrivateData<DataStream>(port)->remaining() >= kLoadDataCollectBytes;
    if (big)
        rb_gc_start();
    VALUE data = fileIntRead(0, nullptr, port);
    if (!big || RTEST(rb_gc_disable()))
        return fileIntMarshalLoad(data);
    return rb_ensure(fileIntMarshalLoad, data, fileIntEnableGC, Qnil);
}
static VALUE fileIntRaw(VALUE port) {
    return fileIntRead(0, nullptr, port);
}
static VALUE fileIntEnsureClose(VALUE port) {
    return fileIntClose(0, nullptr, port);
}
VALUE kernelLoadDataInt(const char *filename, bool rubyExc, bool raw) {
    VALUE port = fileIntForPath(filename, rubyExc);
    return rb_ensure(raw ? fileIntRaw : fileIntLoad, port, fileIntEnsureClose, port);
}

RB_METHOD_GUARD(kernelLoadData) {
    RB_UNUSED_PARAM;
    
    VALUE filename;
    VALUE raw;
    rb_scan_args(argc, argv, "11", &filename, &raw);
    SafeStringValue(filename);
    
    bool rawv;
    rb_bool_arg(raw, &rawv);
    return kernelLoadDataInt(RSTRING_PTR(filename), true, rawv);
}
RB_METHOD_GUARD_END

RB_METHOD(kernelSaveData) {
    RB_UNUSED_PARAM;
    
    VALUE obj;
    VALUE filename;
    
    rb_get_args(argc, argv, "oS", &obj, &filename RB_ARG_END);
    
    VALUE file = rb_file_open_str(filename, "wb");
    
    VALUE marsh = rb_const_get(rb_cObject, rb_intern("Marshal"));
    
    VALUE v[] = {obj, file};
    rb_funcall2(marsh, rb_intern("dump"), ARRAY_SIZE(v), v);
    
    rb_io_close(file);
    
    return Qnil;
}
#if RAPI_FULL > 187
#if RAPI_FULL < 270
static VALUE stringForceUTF8(VALUE arg)
#else
static VALUE stringForceUTF8(RB_BLOCK_CALL_FUNC_ARGLIST(arg, callback_arg))
#endif
{
    if (RB_TYPE_P(arg, RUBY_T_STRING) && ENCODING_IS_ASCII8BIT(arg))
        rb_enc_associate_index(arg, rb_utf8_encindex());
    
    return arg;
}

#if RAPI_FULL < 270
static VALUE customProc(VALUE arg, VALUE proc) {
    VALUE obj = stringForceUTF8(arg);
    obj = rb_funcall2(proc, rb_intern("call"), 1, &obj);
    
    return obj;
}
#endif

RB_METHOD(_marshalLoad) {
    RB_UNUSED_PARAM;
#if RAPI_FULL < 270
    VALUE port, proc = Qnil;
    rb_get_args(argc, argv, "o|o", &port, &proc RB_ARG_END);
#else
    VALUE port;
    rb_get_args(argc, argv, "o", &port RB_ARG_END);
#endif
    
    VALUE utf8Proc;
#if RAPI_FULL < 270
    if (NIL_P(proc))
        
        utf8Proc = rb_proc_new(RUBY_METHOD_FUNC(stringForceUTF8), Qnil);
    else
        utf8Proc = rb_proc_new(RUBY_METHOD_FUNC(customProc), proc);
#else
    utf8Proc = rb_proc_new(stringForceUTF8, Qnil);
#endif
    
    VALUE marsh = rb_const_get(rb_cObject, rb_intern("Marshal"));
    
    VALUE v[] = {port, utf8Proc};
    return rb_funcall2(marsh, rb_intern("_mkxp_load_alias"), ARRAY_SIZE(v), v);
}
#endif

void fileIntBindingInit() {
    VALUE klass = rb_define_class("FileInt", rb_cIO);
#if RAPI_FULL > 187
    rb_define_alloc_func(klass, classAllocate<&FileIntType>);
#else
    rb_define_alloc_func(klass, FileIntAllocate);
#endif
    
    _rb_define_method(klass, "read", fileIntRead);
    _rb_define_method(klass, "getbyte", fileIntGetByte);
#if RAPI_FULL <= 187
    // Ruby doesn't see this as an initialized stream,
    // so either that has to be fixed or necessary
    // IO functions have to be overridden
    rb_define_alias(klass, "getc", "getbyte");
    _rb_define_method(klass, "pos", fileIntPos);
#endif
    _rb_define_method(klass, "binmode", fileIntBinmode);
    _rb_define_method(klass, "close", fileIntClose);
    
    _rb_define_module_function(rb_mKernel, "load_data", kernelLoadData);
    _rb_define_module_function(rb_mKernel, "save_data", kernelSaveData);
    
#if RAPI_FULL > 187
    /* We overload the built-in 'Marshal::load()' function to silently
     * insert our utf8proc that ensures all read strings will be
     * UTF-8 encoded */
    VALUE marsh = rb_const_get(rb_cObject, rb_intern("Marshal"));
    rb_define_alias(rb_singleton_class(marsh), "_mkxp_load_alias", "load");
    _rb_define_module_function(marsh, "load", _marshalLoad);
#endif
}
