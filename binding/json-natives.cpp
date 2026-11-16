/*
** json-natives.cpp
**
** A C version of game_guy's "JSON Encoder/Decoder" for RPG Maker (JSON.decode), the pure-Ruby
** decoder many RGSS games carry. It reads one character at a time and grows each string with
** `string += char`, so a few megabytes of game data (LonaRPG decodes about 3.4 MB of configs at
** every start) take tens of seconds on the Vita.
**
** HardRPGJson.gg_decode(mod, json, sample) does what the Ruby state machine does, step for step,
** including how it treats malformed input: stray tokens skipped, unknown characters ending a value,
** numbers that ignore characters which are not digits, '.', '-' or a delimiter, `//` comments (the
** LonaRPG variant), no escape sequences in strings. Where the Ruby would recurse or loop without
** end it raises SystemStackError instead of running out of stack or memory.
**
** launcher/json_natives.rb decides when to use it: only after the game's own decoder gave the same
** answers on a set of probe inputs, and it keeps checking the first real calls against the Ruby.
** `sample` is a string the game's decoder returned for ASCII-only text; strings that stay ASCII get
** its encoding, as `"" + char` would give them.
*/

#include <ruby.h>
#include <ruby/encoding.h>

#include <stdlib.h>
#include <string.h>

namespace {

enum Token {
    TK_NONE = 0, TK_CURLY_OPEN, TK_CURLY_CLOSED, TK_SQUARED_OPEN, TK_SQUARED_CLOSED, TK_COLON, TK_COMMA,
    TK_STRING, TK_NUMBER, TK_TRUE, TK_FALSE, TK_NULL
};

const int kMaxDepth = 1000;

struct Parser {
    const char *s;
    long len;
    long i;              /* byte offset of the Ruby @index */
    rb_encoding *enc;    /* the input's encoding */
    rb_encoding *lit;    /* encoding of the decoder's "" literal */
    int depth;
};

[[noreturn]] void tooDeep() {
    rb_raise(rb_eSysStackError, "stack level too deep");
}

/* @index -= 1: one character back. */
inline long prevChar(const Parser &p, long at) {
    if (at <= 0)
        return at - 1;
    if (rb_enc_mbmaxlen(p.enc) == 1 || (unsigned char)p.s[at - 1] < 0x80)
        return at - 1;
    const char *h = rb_enc_left_char_head(p.s, p.s + at - 1, p.s + p.len, p.enc);
    return h - p.s;
}

/* Length in bytes of the character at `at` (the Ruby @json[@index, 1]). */
inline long charLen(const Parser &p, long at) {
    if ((unsigned char)p.s[at] < 0x80 || rb_enc_mbmaxlen(p.enc) == 1)
        return 1;
    int n = rb_enc_precise_mbclen(p.s + at, p.s + p.len, p.enc);
    return MBCLEN_CHARFOUND_P(n) ? MBCLEN_CHARFOUND_LEN(n) : 1;
}

inline bool matchAt(const Parser &p, long at, const char *w, long n) {
    return at >= 0 && at + n <= p.len && !memcmp(p.s + at, w, n);
}

Token nextToken(Parser &p) {
    for (;;) {
        if (p.i < 0 || p.i >= p.len) {
            /* @json[@index, 1] is "" or nil: no match, @index stays (+1 -1), no keyword. */
            return TK_NONE;
        }
        unsigned char c = (unsigned char)p.s[p.i];
        long start = p.i;
        p.i += charLen(p, start);
        switch (c) {
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8':
        case '9': case '-': case '.':
            return TK_NUMBER;
        case '{': return TK_CURLY_OPEN;
        case '}': return TK_CURLY_CLOSED;
        case '"': return TK_STRING;
        case ',': return TK_COMMA;
        case '[': return TK_SQUARED_OPEN;
        case ']': return TK_SQUARED_CLOSED;
        case ':': return TK_COLON;
        case ' ': case '\n': case '\t':
            continue;
        case '/':
            if (p.i < p.len && p.s[p.i] == '/') {
                while (p.i < p.len && p.s[p.i] != '\n')
                    p.i += 1;
                continue;
            }
            break;
        default:
            break;
        }
        p.i = start;
        if (matchAt(p, p.i, "false", 5)) { p.i += 5; return TK_FALSE; }
        if (matchAt(p, p.i, "true", 4)) { p.i += 4; return TK_TRUE; }
        if (matchAt(p, p.i, "null", 4)) { p.i += 4; return TK_NULL; }
        return TK_NONE;
    }
}

VALUE parseValue(Parser &p);

/* parse_string: everything up to the next '"'; nil if the text ends first. */
VALUE parseString(Parser &p) {
    if (p.i < 0)
        return Qnil; /* not reached by the Ruby either: @index is never negative here */
    const char *q = (const char *)memchr(p.s + p.i, '"', p.len - p.i);
    if (!q) {
        p.i = p.len;
        return Qnil;
    }
    long n = q - (p.s + p.i);
    bool ascii = true;
    for (long k = 0; k < n; ++k)
        if ((unsigned char)p.s[p.i + k] >= 0x80) { ascii = false; break; }
    VALUE str = rb_enc_str_new(p.s + p.i, n, ascii ? p.lit : p.enc);
    p.i += n + 1;
    return str;
}

VALUE parseNumber(Parser &p) {
    p.i = prevChar(p, p.i);
    bool negative = false, decimal = false;
    char small[64];
    long n = 0;
    VALUE big = Qnil;
    while (p.i < p.len) {
        unsigned char c = (unsigned char)p.s[p.i];
        long cl = charLen(p, p.i);
        p.i += cl;
        if (c == '{' || c == '}' || c == ':' || c == ',' || c == '[' || c == ']') {
            p.i -= 1;
            break;
        }
        if ((c >= '0' && c <= '9') || c == '.') {
            if (decimal && c == '.')
                rb_raise(rb_eRuntimeError, "Number Format error");
            if (!decimal)
                decimal = c == '.';
            if (n < (long)sizeof(small) - 1) {
                small[n++] = (char)c;
            } else {
                if (NIL_P(big)) {
                    big = rb_str_new(small, n);
                }
                rb_str_cat(big, (const char *)&c, 1);
                n++;
            }
        } else if (c == '-') {
            if (n != 0)
                rb_raise(rb_eRuntimeError, "Number Format error");
            negative = true;
        }
    }
    VALUE result;
    if (NIL_P(big) && !decimal && n <= 18) {
        long long v = 0;
        for (long k = 0; k < n; ++k)
            v = v * 10 + (small[k] - '0');
        result = LL2NUM(negative ? -v : v);
        return result;
    }
    VALUE str = NIL_P(big) ? rb_str_new(small, n) : big;
    if (decimal) {
        double d = rb_str_to_dbl(str, FALSE);
        return DBL2NUM(negative ? -d : d);
    }
    result = rb_str_to_inum(str, 10, FALSE);
    if (negative)
        result = rb_funcall(result, rb_intern("-@"), 0);
    return result;
}

struct DepthGuard {
    Parser &p;
    explicit DepthGuard(Parser &pp) : p(pp) {
        if (++p.depth > kMaxDepth)
            tooDeep();
    }
    ~DepthGuard() { p.depth--; }
};

VALUE parseObject(Parser &p) {
    DepthGuard guard(p);
    VALUE obj = rb_hash_new();
    for (;;) {
        Token t = nextToken(p);
        if (t == TK_CURLY_CLOSED)
            break;
        if (t == TK_NONE)
            return Qnil;
        if (t == TK_COMMA)
            continue;
        VALUE name = parseString(p);
        if (NIL_P(name))
            return Qnil;
        if (nextToken(p) != TK_COLON)
            return Qnil;
        VALUE value = parseValue(p);
        rb_hash_aset(obj, name, value);
    }
    return obj;
}

VALUE parseArray(Parser &p) {
    DepthGuard guard(p);
    VALUE obj = rb_ary_new();
    for (;;) {
        Token t = nextToken(p);
        switch (t) {
        case TK_SQUARED_CLOSED:
            return obj;
        case TK_COMMA:
            continue;
        case TK_NULL: rb_ary_push(obj, Qnil); continue;
        case TK_TRUE: rb_ary_push(obj, Qtrue); continue;
        case TK_FALSE: rb_ary_push(obj, Qfalse); continue;
        default: break;
        }
        long stuck = (t == TK_NONE) ? p.i : -1;
        p.i = prevChar(p, p.i);
        VALUE value = parseValue(p);
        /* The Ruby repeats exactly the same steps from here forever (or until its stack or
         * memory runs out) when an unknown character did not get consumed. */
        if (stuck >= 0 && p.i <= stuck)
            tooDeep();
        rb_ary_push(obj, value);
    }
}

VALUE parseValue(Parser &p) {
    for (;;) {
        if (p.i >= p.len)
            return Qnil;
        switch (nextToken(p)) {
        case TK_NONE: return Qnil;
        case TK_NUMBER: return parseNumber(p);
        case TK_CURLY_OPEN: return parseObject(p);
        case TK_STRING: return parseString(p);
        case TK_SQUARED_OPEN: return parseArray(p);
        case TK_TRUE: return Qtrue;
        case TK_FALSE: return Qfalse;
        case TK_NULL: return Qnil;
        default: break; /* }, ], :, ',' are skipped */
        }
    }
}

ID iv_json, iv_index, iv_length;

/* HardRPGJson.gg_decode(mod, json, sample) */
VALUE jsonGgDecode(VALUE, VALUE mod, VALUE json, VALUE sample) {
    Check_Type(json, T_STRING);
    rb_encoding *enc = rb_enc_get(json);
    if (!rb_enc_asciicompat(enc) || rb_enc_str_coderange(json) == ENC_CODERANGE_BROKEN)
        rb_raise(rb_eArgError, "unsupported encoding");
    /* Keep the text alive and unchanged while we read it. */
    VALUE text = rb_str_new_frozen(json);
    Parser p;
    p.s = RSTRING_PTR(text);
    p.len = RSTRING_LEN(text);
    p.i = 0;
    p.enc = enc;
    p.lit = NIL_P(sample) ? rb_utf8_encoding() : rb_enc_get(sample);
    p.depth = 0;
    rb_ivar_set(mod, iv_json, json);
    rb_ivar_set(mod, iv_length, LONG2NUM(rb_str_strlen(json)));
    VALUE result = parseValue(p);
    long idx = p.i;
    if (idx > p.len)
        idx = p.len;
    long chars = (rb_enc_mbmaxlen(enc) == 1 || rb_enc_str_coderange(json) == ENC_CODERANGE_7BIT)
                     ? idx
                     : rb_enc_strlen(p.s, p.s + idx, enc);
    rb_ivar_set(mod, iv_index, LONG2NUM(chars));
    RB_GC_GUARD(text);
    return result;
}

} // namespace

extern "C" void jsonNativesInit() {
    iv_json = rb_intern("@json");
    iv_index = rb_intern("@index");
    iv_length = rb_intern("@length");
    VALUE m = rb_define_module("HardRPGJson");
    rb_define_singleton_method(m, "gg_decode", RUBY_METHOD_FUNC(jsonGgDecode), 3);
}
