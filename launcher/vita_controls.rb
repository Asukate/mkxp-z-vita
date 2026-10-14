# Optional touch controls and pointer feedback. Bitmaps are allocated once;
# game input remains owned by the native RGSS input layer.
if Input.respond_to?(:vita_controls) && Input.vita_controls[0]
  module HardRPGControlOverlay
    PROFILE = Input.vita_controls.freeze
    def self.box(x, y, w, h, label)
      sprite = Sprite.new
      sprite.bitmap = Bitmap.new(w, h)
      @bitmaps << sprite.bitmap
      sprite.bitmap.fill_rect(0, 0, w, h, Color.new(24, 8, 12, 150))
      sprite.bitmap.fill_rect(0, 0, w, 2, Color.new(255, 210, 210, 200))
      sprite.bitmap.font.size = 14
      sprite.bitmap.draw_text(2, 0, w - 4, h, label, 1)
      sprite.x, sprite.y, sprite.z = x, y, 999_999
      sprite
    end
    def self.update
      profile = PROFILE
      w, h = Graphics.width, Graphics.height
      if @width != w || @height != h || (@sprites && @sprites.any? { |s| s.disposed? })
        (@bitmaps || []).each { |b| b.dispose unless b.disposed? }
        (@sprites || []).each { |s| s.dispose unless s.disposed? }
        @sprites = []
        @bitmaps = []
        @pointer = nil
        @width, @height = w, h
        bw, bh = (w * 0.25).to_i, (h * 0.2).to_i
        if profile[1] == 1
          @sprites << box(0, h - bh, bw, bh, profile[4])
          @sprites << box(w - bw, h - bh, bw, bh, profile[5])
        elsif profile[1] == 3
          dw, dh = (w * 0.36).to_i, (h * 0.45).to_i
          @sprites << box(0, h - dh, dw, dh, 'D-pad')
          @sprites << box(w - bw, h - bh, bw, bh, profile[5])
        end
        if profile[3]
          @pointer = Sprite.new
          @pointer.bitmap = Bitmap.new(13, 13)
          @bitmaps << @pointer.bitmap
          @pointer.bitmap.fill_rect(0, 6, 13, 1, Color.new(255, 255, 255))
          @pointer.bitmap.fill_rect(6, 0, 1, 13, Color.new(255, 255, 255))
          @pointer.z = 1_000_000
          @sprites << @pointer
        end
      end
      if @pointer
        @pointer.x = [[Input.mouse_x, 0].max, w - 1].min - 6
        @pointer.y = [[Input.mouse_y, 0].max, h - 1].min - 6
      end
    end
  end
  class << Graphics
    alias hardrpg_controls_update update
    def update
      HardRPGControlOverlay.update
      hardrpg_controls_update
    end
  end
end
