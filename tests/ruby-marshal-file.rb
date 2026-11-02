# Marshal.load(file) on the patched Ruby reads a plain file at once. It must load the same objects,
# leave the file just after each object (RPG Maker saves hold a header object, then the contents),
# and leave every other source as it was.

def check(label, ok)
  raise "#{label} failed" unless ok
end

Point = Struct.new(:x, :y)
class Custom
  attr_reader :v
  def initialize(v); @v = v; end
  def marshal_dump; [@v, 'dumped']; end
  def marshal_load(a); @v = a; end
end
header = { characters: [['Actor1', 0]], playtime_s: '01:02:03' }
contents = { map: Array.new(2000) { |i| Point.new(i, "s#{i}" * 3) }, sym: :name, f: 1.5, big: 2**80,
             custom: Custom.new(7), shared: (s = 'x' * 50; [s, s]), utf8: 'ゲーム' }

# No tmpdir: a miniruby has no standard library.
dir = File.join(ENV['TMPDIR'] || '/tmp', "hardrpg-marshal-file-#{$$}")
Dir.mkdir(dir) unless File.directory?(dir)
begin
  path = File.join(dir, 'Save01.rvdata2')
  File.open(path, 'wb') { |f| Marshal.dump(header, f); Marshal.dump(contents, f); f.write('tail') }
  File.open(path, 'rb') do |f|
    h = Marshal.load(f)
    check('header', h == header)
    check('position after the header', f.pos == Marshal.dump(header).bytesize)
    c = Marshal.load(f)
    check('contents', c[:map] == contents[:map] && c[:sym] == :name && c[:f] == 1.5 && c[:big] == 2**80)
    check('custom marshal_load', c[:custom].v == [7, 'dumped'])
    check('shared links', c[:shared][0].equal?(c[:shared][1]))
    check('encoding', c[:utf8] == 'ゲーム' && c[:utf8].encoding == Encoding::UTF_8)
    check('position after the contents', f.read == 'tail')
    check('end of file', (Marshal.load(f) rescue $!.class) == EOFError)
  end
  # With a proc, and frozen.
  File.open(path, 'rb') do |f|
    seen = 0
    Marshal.load(f, proc { |o| seen += 1; o })
    check('proc', seen > 0 && f.pos == Marshal.dump(header).bytesize)
    c = Marshal.load(f, freeze: true)
    check('freeze', c.frozen? && c[:map][0].frozen?)
  end
  # A damaged file: the error is the stock one.
  bad = File.join(dir, 'bad.rvdata2')
  File.binwrite(bad, Marshal.dump(contents)[0, 5000])
  e = (File.open(bad, 'rb') { |f| Marshal.load(f) } rescue $!)
  check('cut-off file', e.is_a?(ArgumentError) && e.message.include?('marshal data too short'))
  File.binwrite(bad, "\x03\x00junk")
  e = (File.open(bad, 'rb') { |f| Marshal.load(f) } rescue $!)
  check('wrong version', e.is_a?(TypeError))
  # A File subclass and a read-write file keep the stock reads.
  sub = Class.new(File) { attr_reader :reads; def read(*a); @reads = (@reads || 0) + 1; super; end }
  sub.open(path, 'rb') do |f|
    check('subclass header', Marshal.load(f) == header)
    check('subclass reads its own way', f.reads.to_i > 1)
  end
  File.open(path, 'r+b') do |f|
    Marshal.load(f)
    check('read-write file', Marshal.load(f)[:sym] == :name && f.read == 'tail')
  end
  # A file of many objects is read once, and seeks, rewinds and changes are followed.
  many = File.join(dir, 'many.dat')
  File.open(many, 'wb') { |f| 300.times { |i| Marshal.dump([i, 'v' * (i % 7)], f) } }
  reads = 0
  tp = TracePoint.new(:c_call) { |t| reads += 1 if t.method_id == :read && t.self.is_a?(File) }
  File.open(many, 'rb') do |f|
    got = []
    tp.enable { 300.times { got << Marshal.load(f) } }
    check('many objects', got == Array.new(300) { |i| [i, 'v' * (i % 7)] } && f.eof?)
    check('many objects read once', reads <= 2) if ENV['HARDRPG_PATCHED_RUBY']
    f.rewind
    check('rewind', Marshal.load(f) == [0, ''])
    f.pos = 0
    Marshal.load(f)
    second = f.pos
    Marshal.load(f)
    f.pos = second
    check('seek back', Marshal.load(f) == [1, 'v'])
    f.read(Marshal.dump([2, 'vv']).bytesize)
    check('read between loads', Marshal.load(f) == [3, 'vvv'])
    f.pos = (0...250).sum { |i| Marshal.dump([i, 'v' * (i % 7)]).bytesize }
    check('seek ahead', Marshal.load(f) == [250, 'v' * (250 % 7)])
  end
  File.open(many, 'rb') do |f|
    Marshal.load(f)
    File.open(many, 'ab') { |w| w.write('x' * 10) }
    check('file grew between loads', Marshal.load(f) == [1, 'v'])
  end
  # Pipes and strings.
  r, w = IO.pipe
  Marshal.dump(header, w); w.close
  check('pipe', Marshal.load(r) == header)
  check('string', Marshal.load(Marshal.dump(contents))[:sym] == :name)
ensure
  Dir.children(dir).each { |n| File.delete(File.join(dir, n)) }
  Dir.rmdir(dir)
end
puts 'Marshal.load file checks passed'
