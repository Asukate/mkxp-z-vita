# Native JSON.decode (preload, every RGSS version). Many RPG Maker games carry game_guy's pure-Ruby
# "JSON Encoder/Decoder"; it reads one character at a time, so LonaRPG's 3.4 MB of configs took
# tens of seconds at every start on the Vita. The engine (binding/json-natives.cpp) has the same
# state machine in C.
#
# When a game opens `module JSON`, a wrapper goes in front of its JSON.decode. On the first call the
# wrapper runs the game's own decoder and the C one on a set of probe texts (comments, odd spacing,
# stray commas, missing colons, numbers with junk in them, unterminated strings...). Only a decoder
# that answers every probe exactly like the C one is replaced, and the first real calls are still
# decoded both ways and compared. Any difference, or the game redefining one of the decoder's
# methods, puts its Ruby back.
#
# Set $hardrpg_json_natives = false in a per-game shim to keep the game's Ruby decoder.
module HardRPGJsonNatives
  METHODS = %i[decode parse next_token parse_object parse_array parse_string parse_number].freeze
  CHECK_CALLS = 6

  PROBES = [
    '{"a":1,"b":[1,2,3],"c":{"d":"e"}}',
    "{\n\t\"a\" : [ 1 , -2 , 3.25 , -0.5 ],\n  \"b\":true, \"c\":false, \"d\":null\n}",
    "[true,false,null,\"x\",[],{}]",
    "// head comment\n{\"a\": 1, // tail\n \"b\": 2}\n",
    '{"a":1,,"b":2,}', '[1,,2,]', '[1 2 3]', '{"a" 1}', '{"a":}', '{a:1}',
    '"unterminated', '[ "a\\"b" ]', '{"k":"\\n\\u0041"}', '12abc', '-7', '1e5', '--3', '.5', '5.',
    '[1-2]', '{"x":1.2.3}', '', ' ', '/ x', '{"a":[{"b":[{"c":[-0.0]}]}]}',
    "[\"\xC3\xA9t\xC3\xA9\", \"plain\"]".dup.force_encoding('UTF-8'),
    "{\"\xE4\xB8\xAD\":\"\xE6\x96\x87\"}".dup.force_encoding('UTF-8'),
    "{\"bin\":\"\xFF\xFE\"}".b,
    '{"a":tru, "b":nul}', '[truefalse]', '{"a":1}}', ']', '}{', '{"a":{"b":{"c":"d"}}}',
    "[1,\n2\t,\n3]", '{"n":123456789012345678901234567890}', '{"f":-12345678901234567890.5}',
  ].freeze

  @state = {}   # JSON module => :unknown, :native, :ruby

  class << self
    def enabled?
      $hardrpg_json_natives != false && defined?(HardRPGJson) && HardRPGJson.respond_to?(:gg_decode)
    end

    def state(mod)
      @state[mod] ||= :unknown
    end

    def set(mod, st)
      @state[mod] = st
    end

    def reset(mod)
      @state[mod] = :unknown if @state.key?(mod)
    end

    def outcome
      begin
        yield
      rescue SystemStackError, NoMemoryError
        [:stack]
      rescue StandardError => e
        [:raise, e.class, e.message.to_s.b]
      end
    end

    def same?(a, b)
      a == b && Marshal.dump(a) == Marshal.dump(b)
    rescue StandardError
      false
    end

    # Checks the game's decoder against the C one. orig is a lambda calling the game's decode.
    def verify(mod, orig)
      return :ruby unless METHODS.all? { |m| mod.respond_to?(m, true) }
      sample = orig.call('["a"]')
      return :ruby unless sample.is_a?(Array) && sample[0].is_a?(String) && sample[0] == 'a'
      @sample = sample[0]
      PROBES.each do |text|
        ruby = outcome { [:ok, orig.call(text.dup), mod.instance_variable_get(:@index)] }
        native = outcome { [:ok, HardRPGJson.gg_decode(mod, text.dup, @sample), mod.instance_variable_get(:@index)] }
        next if same?(ruby, native)
        note("probe #{text.inspect[0, 40]} differs; keeping the game's decoder")
        return :ruby
      end
      @checks = CHECK_CALLS
      note('game_guy decoder matched; using the native one')
      :native
    end

    def native_ok?(text)
      text.is_a?(String) && text.encoding.ascii_compatible? && text.valid_encoding?
    end

    def decode(mod, text, orig)
      st = state(mod)
      st = set(mod, verify(mod, orig)) if st == :unknown
      return orig.call(text) unless st == :native && native_ok?(text)
      if @checks > 0
        @checks -= 1
        ruby = outcome { [:ok, orig.call(text), mod.instance_variable_get(:@index)] }
        native = outcome { [:ok, HardRPGJson.gg_decode(mod, text, @sample), mod.instance_variable_get(:@index)] }
        unless same?(ruby, native)
          set(mod, :ruby)
          note("call differs (#{text.bytesize} bytes); keeping the game's decoder")
          return orig.call(text)
        end
      end
      HardRPGJson.gg_decode(mod, text, @sample)
    end

    def note(msg)
      @notes ||= []
      @notes << msg
      $hardrpg_json_natives_notes = @notes
    end

    def install(mod)
      return if mod.singleton_class.include?(Wrapper)
      mod.singleton_class.send(:prepend, Wrapper)
    end
  end

  # Prepended to the game's JSON module.
  module Wrapper
    def decode(json)
      return super unless HardRPGJsonNatives.enabled?
      HardRPGJsonNatives.decode(self, json, ->(t) { super(t) })
    end

    private

    def singleton_method_added(name)
      HardRPGJsonNatives.reset(self) if HardRPGJsonNatives::METHODS.include?(name)
      super
    end
  end

  # Wrap JSON as soon as a script opens it (the decoder is defined right after).
  if defined?(TracePoint)
    @trace = TracePoint.new(:class) do |tp|
      mod = tp.self
      next unless mod.is_a?(Module) && mod.name == 'JSON'
      HardRPGJsonNatives.install(mod)
    end
    @trace.enable
  end
  install(::JSON) if defined?(::JSON) && ::JSON.is_a?(Module)
end
