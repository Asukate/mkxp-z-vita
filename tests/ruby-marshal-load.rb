# Marshal.load behaviour that the Vita patch's chunked link table must keep.
# Run it with a host Ruby built from the patched tree (tests/ruby-marshal-load.py
# --ruby), or with any Ruby to check the test itself.

def check(name)
  raise "FAIL #{name}" unless yield
end

class Command
  attr_accessor :code, :indent, :parameters
  def initialize(code, indent, parameters)
    @code, @indent, @parameters = code, indent, parameters
  end
end

class Custom
  attr_reader :value
  def initialize(value); @value = value; end
  def marshal_dump; [@value]; end
  def marshal_load(a); @value = a[0]; end
end

class Legacy
  attr_reader :bytes
  def initialize(bytes); @bytes = bytes; end
  def _dump(_level); @bytes; end
  def self._load(bytes); new(bytes); end
end

# Links that point back across many chunk boundaries (a chunk is 4096 links).
shared = 'shared string'
shared_list = [1, 2, 3]
first = Command.new(0, 0, shared_list)
rows = (0...20_000).map do |i|
  Command.new(i % 400, i % 7, i % 3 == 0 ? shared_list : [i, shared, first, i.to_s])
end
loaded = Marshal.load(Marshal.dump([first, rows, shared]))
check('first back-link') { loaded[1].each_with_index.all? { |c, i| i % 3 == 0 || c.parameters[2].equal?(loaded[0]) } }
check('shared array') { loaded[1].each_with_index.all? { |c, i| i % 3 != 0 || c.parameters.equal?(loaded[0].parameters) } }
check('shared string') { loaded[1][1].parameters[1].equal?(loaded[2]) && loaded[2] == shared }
check('values') { loaded[1][19_999].code == 19_999 % 400 && loaded[1][19_999].parameters[3] == '19999' }

# Cycles, user-defined dump formats, and compat loaders (Rational, Complex).
a = []; h = { self: nil }; a << a << h; h[:self] = h; h[:list] = a
custom = Custom.new('payload'); legacy = Legacy.new("\x00\x01raw".b)
mixed = [a, h, custom, legacy, Rational(3, 7), Complex(1, 2), custom, legacy, 1.5, :sym, 2**70]
m = Marshal.load(Marshal.dump(mixed))
check('array cycle') { m[0][0].equal?(m[0]) && m[0][1].equal?(m[1]) }
check('hash cycle') { m[1][:self].equal?(m[1]) && m[1][:list].equal?(m[0]) }
check('marshal_load') { m[2].value == 'payload' && m[6].equal?(m[2]) }
check('_load') { m[3].bytes == "\x00\x01raw".b && m[7].equal?(m[3]) }
check('compat') { m[4] == Rational(3, 7) && m[5] == Complex(1, 2) }
check('plain') { m[8] == 1.5 && m[9] == :sym && m[10] == 2**70 }

# Class and ivar-name lookups are cached per load: several classes and ivar
# names, Structs, subclasses of core types, and string encodings all still
# come back as dumped.
Pair = Struct.new(:left, :right)
class TaggedArray < Array; end
class TaggedString < String; end
class Other; attr_accessor :code, :extra; end
o = Other.new; o.code = 7; o.extra = 'x'
strings = ['plain ascii', "utf8 café", 'sjis'.encode('Shift_JIS'), "bin\xff".b, 'ascii'.encode('US-ASCII')]
tagged = TaggedArray.new([1, 2]); tstr = TaggedString.new('tag')
c = Marshal.load(Marshal.dump([Command.new(1, 2, []), o, Command.new(3, 4, [o]), Pair.new(1, o),
                               tagged, tstr, *strings, Pair.new(2, 3), Other.new]))
check('ivars per class') { c[0].code == 1 && c[0].indent == 2 && c[1].code == 7 && c[1].extra == 'x' && c[2].parameters[0].equal?(c[1]) }
check('struct') { c[3].is_a?(Pair) && c[3].right.equal?(c[1]) && c[11] == Pair.new(2, 3) }
check('subclasses') { c[4].instance_of?(TaggedArray) && c[4] == [1, 2] && c[5].instance_of?(TaggedString) && c[5] == 'tag' }
check('encodings') { c[6, 5].map(&:encoding) == strings.map(&:encoding) && c[6, 5] == strings }
check('no ivars') { c[12].instance_of?(Other) && c[12].instance_variables.empty? }

# The proc form sees every object, as mkxp's load_data wrapper relies on it.
seen = 0
p_loaded = Marshal.load(Marshal.dump([first, rows.first(100)]), proc { |o| seen += 1; o })
check('proc') { seen > 300 && p_loaded[1][1].parameters[2].equal?(p_loaded[0]) }

# Damaged data still raises instead of reading past the table.
check('bad link') do
  begin
    Marshal.load("\x04\b[\x06@\x06".b); false
  rescue ArgumentError => e
    e.message.include?('unlinked')
  end
end

# A map-sized graph: a few hundred thousand objects load the same way twice,
# with a garbage collection while the second one is half built.
big = (0...300_000).map { |i| Command.new(505, 0, [i & 1 == 0 ? shared : first]) }
bytes = Marshal.dump(big)
one = Marshal.load(bytes)
calls = 0
two = Marshal.load(bytes, proc { |o| GC.start if (calls += 1) == 400_000; o })
check('map-sized') { one.size == 300_000 && Marshal.dump(two) == bytes }
puts 'Marshal.load link table checks passed'
