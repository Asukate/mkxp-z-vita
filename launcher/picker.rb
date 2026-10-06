# HardRPG's VX Ace launcher, drawn with RGSS primitives. No artwork needed.
Graphics.resize_screen(960, 544)
Graphics.fixed_aspect_ratio = false
Font.default_name = "Arial"

module HardRPG
  class Picker
    MENU = %w[Games Settings About Exit]
    TAGS = {1 => "XP", 2 => "VX", 3 => "ACE"}
    ROW_HEIGHT = 36
    VISIBLE_ROWS = 10

    def initialize
      @menu, @focus, @selected, @first = 0, :menu, 0, 0
      @history = []
      @rtp_selected = 0
      @settings_page, @settings_selected, @settings_first = :home, 0, 0
      @message = nil
      begin
        HardRPG.setup
        @library = Library.new
        @error_report = HardRPG.take_error_report
        @error_scroll = 0
        @error_armed = false
      rescue SystemCallError => e
        @message = "Cannot create HardRPG folders: #{e.message}"
      end
      @background = Sprite.new
      @background.bitmap = Bitmap.new(960, 544)
      @content = Sprite.new
      @content.bitmap = Bitmap.new(960, 544)
      @content.z = 1
      paint_background
      redraw
      show_splash unless ARGV.include?("--hardrpg-return")
    end

    def show_splash
      sprite = Sprite.new
      begin
        sprite.bitmap = Bitmap.new(SPLASH_PATH)
        sprite.z = 100
        60.times { Graphics.update; Input.update }
      rescue StandardError
        # Optional artwork must not prevent the browser or a game from opening.
        @message = "Splash artwork could not be displayed."
      ensure
        sprite.bitmap.dispose if sprite.bitmap
        sprite.dispose
      end
    end

    def gradient(bitmap, x, y, width, height, top, bottom)
      height.times do |row|
        t = row.to_f / [height - 1, 1].max
        rgb = 3.times.map { |c| top[c] + ((bottom[c] - top[c]) * t).round }
        bitmap.fill_rect(x, y + row, width, 1, Color.new(*rgb))
      end
    end

    def frame(bitmap, x, y, width, height)
      bitmap.fill_rect(x, y, width, height, Color.new(242, 226, 227))
      bitmap.fill_rect(x + 2, y + 2, width - 4, height - 4, Color.new(137, 21, 39))
      bitmap.fill_rect(x + 4, y + 4, width - 8, height - 8, Color.new(248, 238, 236))
      gradient(bitmap, x + 6, y + 6, width - 12, height - 12, [108, 8, 25], [44, 3, 13])
    end

    def paint_background
      bitmap = @background.bitmap
      gradient(bitmap, 0, 0, 960, 544, [63, 3, 15], [23, 1, 9])
      frame(bitmap, 14, 14, 932, 58)
      frame(bitmap, 14, 82, 146, 448)
      frame(bitmap, 168, 82, 778, 448)
      bitmap.font.size = 26
      bitmap.font.bold = false
      bitmap.font.shadow = true
      bitmap.font.outline = false
      bitmap.font.color = Color.new(255, 244, 241)
      bitmap.draw_text(26, 24, 908, 38, "HardRPG - RPG Maker XP/VX/VX Ace launcher")
    end

    def text(x, y, width, height, string, size = 23, color = [255, 241, 237])
      bitmap = @content.bitmap
      bitmap.font.size = size
      bitmap.font.bold = false
      bitmap.font.shadow = true
      # RGSS outline cropping trims the first row/column of the tight Vita
      # glyph surfaces. Shadows preserve contrast without cutting their ink.
      bitmap.font.outline = false
      bitmap.font.color = Color.new(*color)
      bitmap.draw_text(x, y, width, height, string)
    end

    def highlight(x, y, width, active)
      bitmap = @content.bitmap
      bitmap.fill_rect(x, y, width, 35, Color.new(*(active ? [255, 231, 231] : [173, 93, 105])))
      gradient(bitmap, x + 2, y + 2, width - 4, 31, [177, 28, 52], [117, 12, 34])
    end

    def entries; @library ? @library.entries : []; end

    def redraw
      @content.bitmap.clear
      return redraw_error_report if @error_report
      return redraw_missing_rtp if @missing_rtp
      return redraw_browser if @browser
      MENU.each_with_index do |name, i|
        highlight(22, 92 + i * 38, 130, @focus == :menu) if i == @menu
        text(31, 92 + i * 38, 117, 35, name)
      end
      if @menu == 1
        redraw_settings
      elsif @menu == 2
        text(194, 106, 716, 42, "HardRPG #{VERSION} #{RELEASE_STAGE}", 32)
        text(194, 157, 716, 35, "Play RPG Maker XP, VX and VX Ace games", 25)
        text(194, 193, 716, 35, "on PlayStation Vita.", 25)
        text(194, 255, 716, 35, "Created by Asukate.", 25)
        text(194, 304, 716, 30, "GitHub repository", 21, [235, 188, 192])
        text(194, 338, 716, 32, REPOSITORY, 22)
        text(194, 397, 716, 32, "Based on mkxp-z and its Ruby/RGSS runtime.", 20)
      elsif @menu == 3
        text(194, 115, 716, 38, "Exit HardRPG?", 28)
        text(194, 166, 716, 32, "Cross: return to LiveArea    Circle: back", 22)
      else
        label = @library ? @library.current.label : GAMES_ROOT
        label = "..." + label[-77, 77] if label.length > 80
        text(188, 93, 735, 26, label, 17, [238, 188, 191])
        if entries.empty?
          text(194, 155, 716, 38, "No games found in this folder.", 25)
          text(194, 206, 716, 32, "Copy game folders or ZIP files into:", 21)
          text(194, 240, 716, 32, GAMES_ROOT, 21)
          text(194, 296, 716, 32, "Triangle: refresh    Circle: back", 21)
        else
          entries[@first, VISIBLE_ROWS].each_with_index do |entry, row|
            y = 125 + row * ROW_HEIGHT
            highlight(184, y, 744, @focus == :games) if @first + row == @selected
            text(194, y, 631, 35, entry.name)
            tag = entry.kind == :game ? TAGS[entry.version] : entry.kind == :zip ? "ZIP" : "DIR"
            text(857, y + 4, 66, 28, tag, 17, [242, 199, 199])
          end
          text(804, 487, 121, 20, "#{@selected + 1}/#{entries.size}", 15) if entries.size > VISIBLE_ROWS
        end
        message = @message || (@library && @library.error)
        text(188, 468, 740, 26, message, 17, [255, 211, 172]) if message
      end
      text(26, 467, 124, 22, "Start: add games", 14, [239, 188, 194])
      text(26, 487, 124, 22, "Cross: select", 15, [239, 188, 194])
      text(26, 508, 124, 17, "Circle: back", 14, [239, 188, 194])
      text(188, 505, 736, 18, "Up/Down: move   Cross: open/play   Circle: back   Triangle: refresh", 15, [239, 188, 194]) if @menu == 0
    end

    def redraw_error_report
      text(31, 92, 117, 35, "Games")
      text(190, 98, 724, 38, "The game ended with an error", 27)
      @error_report[@error_scroll, 12].each_with_index do |line, index|
        text(190, 147 + index * 27, 730, 27, line, 18)
      end
      text(190, 487, 730, 30, "Up/Down: scroll   Cross/Circle: game list", 20)
    end

    def redraw_settings
      return redraw_rtp_settings if @settings_page == :rtp
      if @settings_page == :display
        text(194, 104, 716, 35, "Game display", 28)
        DisplaySettings::OPTIONS.each_with_index do |(key, label, values, names), index|
          y = 151 + index * 92
          highlight(184, y, 744, @focus == :settings) if index == @settings_selected
          current = (@display_values || DisplaySettings::DEFAULTS).fetch(key, DisplaySettings::DEFAULTS[key])
          value_label = values.include?(current) ? names[values.index(current)] : "Custom"
          text(194, y, 411, 35, label, 24)
          text(630, y, 287, 35, value_label, 23)
        end
        text(194, 443, 716, 25, "Applies when a game is launched. Per-game options can override.", 18)
        hint = "Cross/Left/Right: change   Square: default   Circle: Settings"
      elsif @settings_page == :folders
        text(194, 104, 716, 35, "Game folders", 28)
        text(194, 142, 716, 26, "Default: " + GAMES_ROOT, 18, [239, 188, 194])
        paths = ["Add another folder..."] + @library.search_folders
        paths[@settings_first, 7].each_with_index do |path, row|
          y = 183 + row * 36
          highlight(184, y, 744, @focus == :settings) if @settings_first + row == @settings_selected
          path = "..." + path[-69, 69] if path.length > 72
          text(194, y, 716, 35, path, 21)
        end
        text(194, 443, 716, 25, "Extra folders are searched on launch and Triangle refresh.", 18)
        hint = "Cross: browse/add   Square: remove reference   Circle: Settings"
      else
        text(194, 104, 716, 35, "Settings", 28)
        ["Game display", "RTP locations", "Game folders"].each_with_index do |name, index|
          y = 151 + index * 62
          highlight(184, y, 744, @focus == :settings) if index == @settings_selected
          text(194, y, 716, 35, name, 25)
        end
        hint = "Up/Down: move   Cross: open   Circle: back"
      end
      text(188, 474, 740, 24, @message, 17, [255, 211, 172]) if @message
      text(188, 505, 736, 18, hint, 15)
    end

    def refresh_rtp_status
      @rtp_status = Rtp::PACKS.map { |pack| Rtp.status(pack) }
    end

    def open_settings_page(page)
      @display_values = DisplaySettings.values if page == :display
      refresh_rtp_status if page == :rtp
      @settings_page, @settings_selected, @settings_first, @message = page, 0, 0, nil
    end

    def change_display(step = 1, reset = false)
      key, _label, values, _names = DisplaySettings::OPTIONS[@settings_selected]
      value = reset ? DisplaySettings::DEFAULTS[key] : values[((values.index(@display_values[key]) || 0) + step) % values.size]
      @display_values = DisplaySettings.change(key, value)
      @message = "Display settings saved for the next game launch."
    end

    def redraw_rtp_settings
      text(194, 104, 716, 35, "RTP locations", 28)
      choices = Rtp.paths
      Rtp::PACKS.each_with_index do |pack, index|
        y = 151 + index * 92
        highlight(184, y, 744, @focus == :settings) if index == @rtp_selected
        text(194, y, 716, 35, Rtp.label(pack) + " RTP - " + (@rtp_status || [])[index].to_s, 24)
        path = choices[pack] || RTP_ROOT + "/" + pack + " (automatic)"
        path = "..." + path[-75, 75] if path.length > 78
        text(194, y + 39, 716, 28, path, 17, [239, 188, 194])
      end
      text(194, 443, 716, 25, "Packs stay where they are. RTP ZIPs are read directly.", 18)
      text(188, 474, 740, 24, @message, 17, [255, 211, 172]) if @message
      text(188, 505, 736, 18, "Cross: choose folder/ZIP   Square: automatic location   Circle: back", 15)
    end

    def redraw_missing_rtp
      frame(@content.bitmap, 194, 154, 720, 276)
      text(216, 177, 672, 40, "Missing #{Rtp.label(@missing_rtp)} RTP", 29)
      text(216, 228, 672, 32, "Choose an existing RTP folder or ZIP in Settings.", 21)
      text(216, 268, 672, 28, "Default location:", 19)
      path = RTP_ROOT + "/" + @missing_rtp
      text(216, 302, 672, 28, path, 19)
      text(216, 365, 672, 30, "Cross: Settings     Circle: back to games", 21)
    end

    def choose_rtp(path)
      pack = Rtp::PACKS[@rtp_selected]
      Rtp.set(pack, path)
      @browser, @browser_mode = nil, nil
      @menu, @focus = 1, :settings
      @settings_page = :rtp
      refresh_rtp_status
      @message = "#{Rtp.label(pack)} RTP location saved. Files stay in place."
    end

    def redraw_browser
      rtp = @browser_mode == :rtp
      path_picker = rtp || @browser_mode == :game_folder
      text(28, 94, 121, 35, rtp ? "Choose RTP" : path_picker ? "Game folder" : "Add games", 20)
      text(26, 466, 124, 23, path_picker ? "Square: folder" : "Square: scan", 14)
      text(26, 487, 124, 22, "Cross: folder", 14)
      text(26, 508, 124, 17, "Circle: back", 14)
      label = @browser.path || "Choose storage"
      label = "..." + label[-77, 77] if label.length > 80
      text(188, 93, 735, 26, label, 17, [238, 188, 191])
      @browser.entries[@browser_first, VISIBLE_ROWS].each_with_index do |entry, row|
        y = 125 + row * ROW_HEIGHT
        highlight(184, y, 744, true) if @browser_first + row == @browser_selected
        text(194, y + 6, 716, 35, entry.name)
      end
      text(194, 155, 716, 38, path_picker ? "Square selects this folder." : "No subfolders. Square scans this folder.", 22) if @browser.entries.empty?
      message = @message || @browser.error
      text(188, 468, 740, 26, message, 17, [255, 211, 172]) if message
      hint = rtp ? "Cross: open folder/select ZIP   Square: use folder   Start: close" : path_picker ? "Cross: open folder   Square: use folder   Start: close" : "Cross: open folder   Square: scan/add games   Start: close"
      text(188, 505, 736, 18, hint, 15)
    end

    def update_browser
      if Input.repeat?(:UP) || Input.repeat?(:DOWN)
        unless @browser.entries.empty?
          step = Input.repeat?(:UP) ? -1 : 1
          @browser_selected = (@browser_selected + step) % @browser.entries.size
          @browser_first = @browser_selected if @browser_selected < @browser_first
          @browser_first = @browser_selected - VISIBLE_ROWS + 1 if @browser_selected >= @browser_first + VISIBLE_ROWS
        end
      elsif Input.trigger?(:C) && !@browser.entries.empty?
        entry = @browser.entries[@browser_selected]
        begin
          if @browser_mode == :rtp && entry.kind == :zip
            choose_rtp(entry.node)
          else
            @browser.enter(entry)
            @browser_selected, @browser_first, @message = 0, 0, nil
          end
        rescue IOError, SystemCallError, ArgumentError => e
          @message = e.message
        end
      elsif Input.trigger?(:B) || Input.trigger?(:LEFT)
        @browser = nil unless @browser.back
        @browser_selected, @browser_first, @message = 0, 0, nil
      elsif Input.trigger?(:Y)
        @browser, @message = nil, nil
      elsif Input.trigger?(:A)
        begin
          if @browser_mode == :rtp
            raise ArgumentError, "Choose an RTP folder or ZIP first" unless @browser.path
            choose_rtp(@browser.path)
            redraw
            return
          end
          raise ArgumentError, "Choose a game folder or collection first" unless @browser.path
          @last_scan_time = nil
          method = @browser_mode == :game_folder ? :add_search_folder : :add_folder
          added = @library.public_send(method, @browser.path) do |count, path|
            now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
            if !@last_scan_time || now - @last_scan_time >= 0.05
              @last_scan_time = now
              @message = "Scanning folder #{count} - Circle: cancel"
              redraw
              Graphics.update
              Input.update
              raise PreparationCancelled, "Folder scan cancelled" if Input.trigger?(:B)
            end
          end
          if @browser_mode == :game_folder
            @menu, @focus, @settings_page, @settings_selected, @settings_first = 1, :settings, :folders, 0, 0
          else
            @menu, @focus = 0, :games
          end
          @browser = nil
          @history.clear
          reset_selection
          @message = method == :add_search_folder ? "Extra game folder saved. Found #{added} games; files stay in place." : "Added #{added} game#{added == 1 ? '' : 's'}. Files stay in their original folders."
        rescue IOError, SystemCallError, ArgumentError, PreparationCancelled => e
          @message = e.message
        end
      end
      redraw
    end

    def reset_selection; @selected, @first, @message = 0, 0, nil; end

    def prepare_progress(copied, total)
      percent = total.zero? ? 100 : copied * 100 / total
      now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      return if @last_percent == percent && @last_progress_time && now - @last_progress_time < 0.05
      @last_progress_time = now
      if @last_percent != percent
        @last_percent = percent
        @message = "Preparing ZIP: #{percent}% - Circle: cancel"
        redraw
      end
      Graphics.update
      Input.update
      raise PreparationCancelled, "ZIP preparation cancelled." if Input.trigger?(:B)
    end

    def select
      if @focus == :menu
        exit if @menu == 3
        @focus = :games if @menu == 0 && @library
        if @menu == 1
          @focus = :settings
          open_settings_page(:home)
        end
      elsif @focus == :settings
        begin
          case @settings_page
          when :home
            open_settings_page([:display, :rtp, :folders][@settings_selected])
          when :display
            change_display
          else
            @browser = FolderBrowser.new(@settings_page == :rtp)
            @browser_mode = @settings_page == :rtp ? :rtp : :game_folder
            if @settings_page == :folders && @settings_selected > 0
              path = @library.search_folders[@settings_selected - 1]
              @browser.enter(Entry.new(path, :folder, path, nil)) if path && File.directory?(path)
            end
            @browser_selected, @browser_first, @message = 0, 0, nil
          end
        rescue IOError, SystemCallError, ArgumentError => e
          @message = e.message
        end
      elsif !entries.empty?
        begin
          position = [@selected, @first]
          entry = @library.enter(entries[@selected])
          if entry
            @last_percent = nil
            @library.launch(entry) { |copied, total| prepare_progress(copied, total) }
            exit
          end
          @history << position
          reset_selection
        rescue MissingRtp => e
          @missing_rtp = e.pack
        rescue IOError, SystemCallError, ArgumentError, PreparationCancelled => e
          @message = e.message
        end
      end
      redraw
    end

    def back
      if @focus == :settings
        if @settings_page == :home
          @focus, @message = :menu, nil
        else
          open_settings_page(:home)
        end
      elsif @focus == :games
        if @library.back
          @selected, @first = @history.pop || [0, 0]
          @selected, @first = 0, 0 if @selected >= entries.size
          @message = nil
        else
          @focus = :menu
          reset_selection
        end
      else
        @menu, @message = 0, nil
      end
      redraw
    end

    def update
      if @error_report
        @error_armed = true unless Input.press?(:C) || Input.press?(:B)
        if @error_armed && (Input.trigger?(:C) || Input.trigger?(:B))
          @error_report = nil
          redraw
        elsif Input.repeat?(:UP) || Input.repeat?(:DOWN)
          step = Input.repeat?(:UP) ? -1 : 1
          @error_scroll = [[@error_scroll + step, 0].max, [@error_report.size - 12, 0].max].min
          redraw
        end
        return
      end
      if @missing_rtp
        if Input.trigger?(:C)
          @rtp_selected = Rtp::PACKS.index(@missing_rtp) || 0
          open_settings_page(:rtp)
          @menu, @focus, @missing_rtp = 1, :settings, nil
          redraw
        elsif Input.trigger?(:B)
          @missing_rtp = nil
          redraw
        end
        return
      end
      return update_browser if @browser
      if Input.trigger?(:Y) && @library
        @browser = FolderBrowser.new
        @browser_mode = :games
        @browser_selected, @browser_first, @message = 0, 0, nil
        redraw
        return
      end
      if Input.repeat?(:UP) || Input.repeat?(:DOWN)
        step = Input.repeat?(:UP) ? -1 : 1
        if @focus == :menu
          @menu = (@menu + step) % MENU.size
        elsif @focus == :settings
          if @settings_page == :rtp
            @rtp_selected = (@rtp_selected + step) % Rtp::PACKS.size
          else
            count = @settings_page == :folders ? 1 + @library.search_folders.size : 3
            @settings_selected = (@settings_selected + step) % count
            @settings_first = @settings_selected if @settings_selected < @settings_first
            @settings_first = @settings_selected - 6 if @settings_selected >= @settings_first + 7
          end
        elsif !entries.empty?
          @selected = (@selected + step) % entries.size
          @first = @selected if @selected < @first
          @first = @selected - VISIBLE_ROWS + 1 if @selected >= @first + VISIBLE_ROWS
        end
        @message = nil
        redraw
      elsif Input.trigger?(:C)
        select
      elsif Input.trigger?(:A) && @focus == :settings
        begin
          if @settings_page == :rtp
            Rtp.set(Rtp::PACKS[@rtp_selected], nil)
            refresh_rtp_status
            @message = "Automatic RTP location restored. Pack files were not removed."
          elsif @settings_page == :display
            change_display(1, true)
          elsif @settings_page == :folders && @settings_selected > 0
            @library.remove_search_folder(@library.search_folders[@settings_selected - 1])
            @settings_selected, @settings_first = 0, 0
            @message = "Game folder reference removed. Games and saves stay in place."
          end
        rescue IOError, SystemCallError, ArgumentError => e
          @message = e.message
        end
        redraw
      elsif (Input.trigger?(:LEFT) || Input.trigger?(:RIGHT)) && @focus == :settings && @settings_page == :display
        begin
          change_display(Input.trigger?(:LEFT) ? -1 : 1)
        rescue IOError, SystemCallError, ArgumentError => e
          @message = e.message
        end
        redraw
      elsif Input.trigger?(:X) && @focus == :settings && @settings_page == :rtp
        refresh_rtp_status
        @message = "RTP status refreshed."
        redraw
      elsif Input.trigger?(:B) || Input.trigger?(:LEFT)
        back
      elsif Input.trigger?(:RIGHT) && @focus == :menu && @menu == 0
        @focus = :games
        redraw
      elsif Input.trigger?(:X) && @menu == 0 && @library
        @library.refresh
        reset_selection
        redraw
      end
    end

    def run
      loop { Graphics.update; Input.update; update }
    ensure
      HardRPGArchive.close if defined?(HardRPGArchive)
      [@background, @content].each { |s| s.bitmap.dispose; s.dispose }
    end
  end
end

HardRPG::Picker.new.run unless defined?(HARDRPG_PREVIEW_DRIVER)
