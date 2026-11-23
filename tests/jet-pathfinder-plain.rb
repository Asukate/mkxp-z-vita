# Check HardRPGJetPath (launcher/vxace_map_speedups.rb) on Jet's plain pathfinder with the distance
# change and no jump argument (Cat in the Box): it turns on, and on random mazes it returns exactly
# the routes the game's own search returns, reachable or not.
src = File.read(File.expand_path('../launcher/native_events.rb', __dir__), encoding: 'UTF-8')
eval("module HardRPGNative\n#{src[/  module Norm.*?\n  end\n/m]}\nend")
module Graphics; def self.update; end; end
module Kernel; private def rgss_main; end; end
module RPG
  class MoveCommand
    attr_accessor :code, :parameters
    def initialize(code = 0, parameters = [])
      @code = code
      @parameters = parameters
    end
  end
end
$RGSS_SCRIPTS = []
require_relative '../launcher/vxace_map_speedups'

def check(label, ok)
  raise "#{label} failed" unless ok
end

STOCK_MAP = <<~RUBY
  class Game_Map
    def round_x(x)
      loop_horizontal? ? (x + width) % width : x
    end
    def round_y(y)
      loop_vertical? ? (y + height) % height : y
    end
    def round_x_with_direction(x, d)
      round_x(x + (d == 6 ? 1 : d == 4 ? -1 : 0))
    end
    def round_y_with_direction(y, d)
      round_y(y + (d == 2 ? 1 : d == 8 ? -1 : 0))
    end
  end
RUBY
JET = <<~'RUBY'
module Jet
  module Pathfinder
    
    MAXIMUM_ITERATIONS = 1000
    
  end
end

class Node
  
  include Comparable

  attr_accessor :point, :parent, :cost, :cost_estimated

  def initialize(point)
    @point = point
    @cost = 0
    @cost_estimated = 0
    @on_path = false
    @parent = nil
  end

  def mark_path
    @on_path = true
    @parent.mark_path if @parent
  end
   
  def total_cost
    cost + cost_estimated
  end

  def <=>(other)
    total_cost <=> other.total_cost
  end
   
  def ==(other)
    point == other.point
  end
end

class Point
  
  attr_accessor :x, :y
  
  def initialize(x, y)
    @x, @y = x, y
  end

  def ==(other)
    return false unless Point === other
    @x == other.x && @y == other.y
  end

  def distance(other)
    (@x - other.x).abs + (@y - other.y).abs
  end

  def relative(xr, yr)
    Point.new(x + xr, y + yr)
  end
end

class Game_Map
  
  def each_neighbor(node, char = $game_player)
    x = node.point.x
    y = node.point.y
    nodes = []
    4.times {|i|
      i += 1
      new_x = round_x_with_direction(x, i * 2)
      new_y = round_y_with_direction(y, i * 2)
      next unless char.passable?(x, y, i * 2)
      nodes.push(Node.new(Point.new(new_x, new_y)))
    }
    nodes
  end
  
  def find_path(tx, ty, sx, sy, dist, char = $game_player)
    start = Node.new(Point.new(sx, sy))
    goal = Node.new(Point.new(tx, ty))
    return [] if start == goal or (dist > 0 and start.point.distance(goal.point) <= dist)
    return [] if ![2, 4, 6, 8].any? {|i| char.passable?(tx, ty, i) }
    open_set = [start]
    closed_set = []
    path = []
    iterations = 0
    loop do
      return [] if iterations == Jet::Pathfinder::MAXIMUM_ITERATIONS
      iterations += 1
      current = open_set.min
      return [] unless current
      each_neighbor(current, char).each {|node|
        if node == goal or (dist > 0 and node.point.distance(goal.point) <= dist)
          node.parent = current
          node.mark_path
          return recreate_path(node)
        end
        next if closed_set.include?(node)
        cost = current.cost + 1
        if open_set.include?(node)
          if cost < node.cost
            node.parent = current
            node.cost = cost
          end
        else
          open_set << node
          node.parent = current
          node.cost = cost
          node.cost_estimated = node.point.distance(goal.point)
        end
      }
      closed_set << open_set.delete(current)
    end
  end
  
  def recreate_path(node)
    path = []
    hash = {[1, 0] => 6, [-1, 0] => 4, [0, 1] => 2, [0, -1] => 8}
    until node.nil?
      pos = node.point
      node = node.parent
      next if node.nil?
      ar = [pos.x <=> node.point.x, pos.y <=> node.point.y]
      path.push(RPG::MoveCommand.new(hash[ar] / 2))
    end
    return path
  end
end

RUBY

[STOCK_MAP, JET].each_with_index do |text, i|
  $RGSS_SCRIPTS << [i, "s#{i}", '', text]
  eval(text, TOPLEVEL_BINDING, "{#{i}}")
end

class Game_Map
  attr_accessor :width, :height, :grid
  def loop_horizontal? = false
  def loop_vertical? = false
end

# A character that may step from (x, y) towards d when the tile it lands on is open.
class Walker
  def initialize(map) = @map = map
  def passable?(x, y, d)
    nx = x + (d == 6 ? 1 : d == 4 ? -1 : 0)
    ny = y + (d == 2 ? 1 : d == 8 ? -1 : 0)
    nx >= 0 && ny >= 0 && nx < @map.width && ny < @map.height && @map.grid[ny][nx]
  end
end

check('installs', HardRPGJetPath.install.start_with?('on (plain'))
check('plain module', Game_Map.ancestors.first == HardRPGJetPath::SearchPlain)

rng = Random.new(35)
map = Game_Map.new
walker = Walker.new(map)
runs = { found: 0, none: 0 }
400.times do |n|
  map.width = 10 + rng.rand(30)
  map.height = 8 + rng.rand(25)
  density = [0.1, 0.25, 0.35][n % 3]
  map.grid = Array.new(map.height) { Array.new(map.width) { rng.rand >= density } }
  sx, sy, tx, ty = rng.rand(map.width), rng.rand(map.height), rng.rand(map.width), rng.rand(map.height)
  map.grid[sy][sx] = true
  dist = [0, -1, 2][n % 3]
  HardRPGJetPath.mode = 0
  want = map.find_path(tx, ty, sx, sy, dist, walker).map(&:code)
  HardRPGJetPath.mode = 7
  got = map.find_path(tx, ty, sx, sy, dist, walker).map(&:code)
  check("route #{n}", want == got)
  runs[want.empty? ? :none : :found] += 1
end
check('both kinds seen', runs[:found] > 50 && runs[:none] > 50)
puts "Jet pathfinder (plain) checks passed: #{runs}"
