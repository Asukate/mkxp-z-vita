# Map fixture; loaded after user-supplied VX Ace player classes.
# The command file is a test-only input boundary, never packaged in the launcher.
module ProbeInput
  COMMAND = 'ux0:/data/hardrpg/tap-walk-command.txt'
  def update
    super
    @probe_tap = false
    @probe_frames = [(@probe_frames || 0) - 1, 0].max
    if File.exist?(COMMAND)
      parts = File.read(COMMAND).split
      if parts.size >= 4 && parts[0] != @probe_id
        @probe_id = parts[0]
        if parts[1] == 'tap'
          @probe_x, @probe_y = parts[2].to_i, parts[3].to_i
          @probe_tap = true
        elsif parts[1] == 'direction'
          @probe_direction, @probe_frames = parts[2].to_i, parts[3].to_i
        end
      end
    end
  end
  def trigger?(key)
    return true if key == :MOUSELEFT && @probe_tap
    super
  end
  def mouse_x; @probe_tap ? @probe_x : super; end
  def mouse_y; @probe_tap ? @probe_y : super; end
  def mouse_in_window?; @probe_tap || super; end
  def dir4; (@probe_frames || 0) > 0 ? @probe_direction : super; end
  def probe_id; @probe_id; end
end
simulated = File.exist?('ux0:/data/hardrpg/tap-walk-simulated.txt')
if simulated
  Input.singleton_class.prepend(ProbeInput)
  # Vita3K reports repeated resume/time discontinuities. Route tests use a
  # logical frame clock; native suspend detection remains a separate gate.
  module ProbeClock
    def tick(map, player, options = {})
      super(map, player, options.merge(:now => Graphics.frame_count / 60.0))
    end
  end
  HardRPGTapWalk::Driver.prepend(ProbeClock)
end
kind = $hardrpg_probe_engine || :ace
Graphics.resize_screen(*(kind == :xp ? [640,480] : [544,416]))
data = load_data('Data/Map001.rvdata2')
kind == :ace ? setup_walk_world(data) : setup_legacy_walk_world(data,kind)
HardRPGTapWalk.install!(kind)
background = Sprite.new
background.bitmap = Bitmap.new($game_map.width * 32, $game_map.height * 32)
$game_map.height.times do |y|
  $game_map.width.times do |x|
    wall = $game_map.walls.include?([x,y])
    color = wall ? Color.new(100,55,70) : Color.new(32+(x+y)%2*8,50,43)
    background.bitmap.fill_rect(x*32,y*32,31,31,color)
  end
end
actor = Sprite.new
actor.bitmap = Bitmap.new(18,24)
actor.bitmap.fill_rect(0,0,18,24,Color.new(60,190,255))
actor.z = 4
marker = HardRPGTapWalk::DestinationMarker.new
item = Sprite.new
item.bitmap = Bitmap.new(12,12)
item.bitmap.fill_rect(0,0,12,12,Color.new(255,200,40))
item.z = 2
hud = Sprite.new
hud.bitmap = Bitmap.new(Graphics.width,32)
hud.bitmap.font.size = 14
hud.y, hud.z = Graphics.height-32, 9
clicks, manual_frames, frame = 0, 0, 0
trace = []
output = 'ux0:/data/hardrpg/tap-walk-state.txt'
begin
  run_probe = proc do
    loop do
      Input.update
      frame += 1
      if Input.trigger?(:MOUSELEFT)
        clicks += 1
        trace << [Input.mouse_x, Input.mouse_y]
      end
      manual_frames += 1 if Input.dir4 != 0
      $game_player.update
      map = HardRPGTapWalk.engine.map_for($game_map)
      actor.x, actor.y = map.adjust_x($game_player.real_x/map.units.to_f)*32+7,
                         map.adjust_y($game_player.real_y/map.units.to_f)*32+4
      background.ox, background.oy = map.display_x*32, map.display_y*32
      item.x, item.y = map.adjust_x(12)*32+10, map.adjust_y(4)*32+10
      target = HardRPGTapWalk.driver.target
      marker.update(map, HardRPGTapWalk.driver)
      if frame % 8 == 0 || Input.trigger?(:MOUSELEFT) || Input.dir4 != 0
        state = HardRPGTapWalk.driver
        position = [$game_player.x,$game_player.y]
        lines = {
          'frame'=>frame, 'x'=>position[0], 'y'=>position[1],
          'engine'=>kind,
          'input_source'=>simulated ? 'simulated_rgss' : 'native',
          'clock_source'=>simulated ? 'logical_frame' : 'wall_clock',
          'command_id'=>simulated ? Input.probe_id : '',
          'moving'=>$game_player.moving? ? 1 : 0,
          'status'=>state.status, 'reason'=>state.reason,
          'target'=>target ? target.join(',') : '',
          'marker_visible'=>marker.sprite && marker.sprite.visible ? 1 : 0,
          'marker_opacity'=>marker.sprite ? marker.sprite.opacity : 0,
          'marker_zoom'=>marker.sprite ? marker.sprite.zoom_x : 0,
          'marker_x'=>marker.sprite ? marker.sprite.x : '',
          'marker_y'=>marker.sprite ? marker.sprite.y : '',
          'display_x'=>map.display_x, 'display_y'=>map.display_y,
          'clicks'=>clicks, 'mouse_x'=>Input.mouse_x, 'mouse_y'=>Input.mouse_y,
          'manual_frames'=>manual_frames, 'steps'=>$game_party.steps,
          'walk_hooks'=>$game_party.walks,
          'follower_steps'=>$game_player.respond_to?(:followers) ? $game_player.followers.moves : 0,
          'slip_checks'=>$game_party.respond_to?(:slip_checks) ? $game_party.slip_checks : 0,
          'touch_events'=>$game_map.events[1].starts,
          'encounters'=>$game_player.instance_variable_get(:@encounter_count),
          'front_mode'=>Input.respond_to?(:vita_controls) ? Input.vita_controls[1] : -1,
          'profile_on'=>Input.respond_to?(:vita_controls) && Input.vita_controls[0] ? 1 : 0,
          'tap_trace'=>trace.map { |t| t.join(',') }.join(';')
        }
        File.open(output,'w') { |f| lines.each { |k,v| f.write(k.to_s+'='+v.to_s+"\n") } }
        b = hud.bitmap
        b.fill_rect(0,0,Graphics.width,32,Color.new(12,12,20))
        b.draw_text(4,0,Graphics.width-8,16,"Tap a floor tile. D-pad/stick takes over. Walls block routes.")
        b.draw_text(4,16,Graphics.width-8,16,"#{kind} #{position.join(',')}  #{state.status} #{state.reason}  taps=#{clicks} steps=#{$game_party.steps}")
      end
      Graphics.update
    end
  end
  kind == :ace ? rgss_main(&run_probe) : run_probe.call
ensure
  marker.dispose
  [background,actor,item,hud].each { |s| s.bitmap.dispose; s.dispose }
end
