# Check launcher/legacy_ruby_compat.rb's byte-regexp conversion. Ruby 1.8
# matched bytes; the same matching runs here as a byte (/n) regexp over a
# binary copy of the text, so every conversion is compared with what 1.8
# found, at the same byte offsets.
require_relative '../launcher/legacy_ruby_compat'

TEXT = [
  'Pokémon ポケモン 漢字テスト', 'ﾃｽﾄ ＡＢＣ　１２３ abc', '한국어 ㄱㄴ ᄀ', '“quoted” ‘x’ 「括弧」【注】',
  "plain ascii, 0123", "control \u0085 next \u009f", 'ｶﾀｶﾅ ー ・', '﻿<?xml version="1.0"?>',
  'ÀÉÎÕÜ àéîõü ß æ ø'
].freeze

def spans_bytes(regexp, text)
  out = []
  text.b.scan(regexp) { out << [$~.begin(0), $~.end(0)] }
  out
end

def spans_chars(regexp, text)
  out = []
  text.scan(regexp) { out << [text[0, $~.begin(0)].bytesize, text[0, $~.end(0)].bytesize] }
  out
end

def check(label, body)
  converted = HardRPGLegacyRegexp.convert(body)
  raise "#{label}: not converted" unless converted
  old = Regexp.new(body.b, Regexp::NOENCODING)
  new = Regexp.new(converted.dup.force_encoding('UTF-8'))
  TEXT.each do |text|
    a = spans_bytes(old, text)
    b = spans_chars(new, text)
    raise "#{label}: #{text.inspect}\n  1.8 #{a.inspect}\n  now #{b.inspect}\n  #{converted}" unless a == b
  end
end

check('kana', '(?:\xE3\x81[\x81-\xBF]|\xE3\x82[\x80-\x93]|\xE3\x83\xBC|\xE3\x82[\xA1-\xBF]|\xE3\x83[\x80-\xB6])')
check('han', '(?:\xe4[\xb8-\xbf][\x80-\xbf]|[\xe5-\xe8][\x80-\xbf][\x80-\xbf]|\xe9[\x80-\xbd][\x80-\xbf]|\xe9\xbe[\x80-\xa5])')
check('hangul', '(?:\xEA[\xB0-\xBF][\x80-\xBF]|[\xEB-\xEC][\x80-\xBF][\x80-\xBF]|\xED[\x80-\x9D][\x80-\xBF]|\xE1[\x84-\x87][\x80-\xBF]|\xE3\x84[\xB1-\xBF]|\xE3\x85[\x80-\xBF])')
check('lead before group', '(?:\xef(?:\xbc[\xa1-\xba]|\xbd[\x81-\x9a]))')
check('fullwidth digits', '(?:\xef\xbc[\x90-\x99])+')
check('punctuation', '(?:\xe2\x80[\x99\x9d\xa6\xba]|\xe3\x80[\x81\x82\x89\x8b\x8d\x8f\x91\x95\x97])')
check('bom', '\A(\xef\xbb\xbf)?<\?xml')
# A class of high bytes that spells no UTF-8 character excludes Latin-1
# controls. 1.8 split UTF-8 characters at their \x80-\x9f bytes here (the
# JSON string pattern in Essentials' PSystem_Utilities); now they stay whole.
latin1 = Regexp.new(HardRPGLegacyRegexp.convert('"[^"\\\\\n\r\x00-\x1f\x7f-\x9f]*"'))
raise 'latin1: text' unless '"ポケモン é"'.match?(latin1)
raise 'latin1: control' if "\"a\u0085b\"".match?(latin1)
# Matched text sliced at 1.8 byte offsets becomes characters.
src = 'X = /(?:\xef\xbc[\x90-\x99])/' + "\n" + 'Y = /(#{X}|a)/n' + "\n"
out = HardRPGLegacyRegexp.prepare(src)
raise "prepare: #{out.inspect}" unless out == "X = /(?:[\\u{FF10}-\\u{FF19}])/\nY = /(\#{X}|a)/\n"
raise 'prepare touched a section without byte escapes' if HardRPGLegacyRegexp.prepare("a = b / c\n")
raise 'division' if HardRPGLegacyRegexp.prepare("x = 4 / 2 # \\xff\n")
# A section with no byte escapes that interpolates a converted regexp from another section (DrawText).
draw = 'while (c = t.slice!(/(?:\n|[ \r\t\f]+|#{CJKUtils::WordRegexp::WordRegexp}[ \r\t\f]*)/n)) != nil' + "\n"
out = HardRPGLegacyRegexp.prepare(draw)
raise "interpolating /n: #{out.inspect}" unless out == draw.sub(')/n)', ')/)')
module CJKUtils; module WordRegexp; WordRegexp = /[\u{3041}-\u{307F}]+/; end; end
t = "あい x"
eval(out.sub('while', 'if').sub(' != nil', ' then end'))
raise "interpolated regexp: #{t.inspect}" unless t == 'x'
raise 'plain /n regexp changed' if HardRPGLegacyRegexp.prepare("a = /ab/n\n")
puts 'legacy-regexp: ok'
