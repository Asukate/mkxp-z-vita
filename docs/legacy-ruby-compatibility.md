# Legacy Ruby and Windows compatibility

RPG Maker XP and VX shipped Ruby 1.8, VX Ace Ruby 1.9; HardRPG runs every
game on Ruby 3.1. `launcher/legacy_ruby_compat.rb` (a preload) restores what
shipped games rely on, and `binding/miniffi-binding.cpp` answers the Windows
DLL calls games make through `Win32API`. Game files are never edited.

## Ruby 1.8 behaviour (RGSS1/2)

- `Object#to_a` returns `[self]`, `Object#id` is `object_id` (`nil.id` is 4,
  as on 1.8). Finding Paradise reads `$game_party.actors[0].id` with an empty
  starting party.
- `name (x).width` meant `name(x).width` on 1.8 and `name((x).width)` since
  1.9. Ruby's parser finds calls whose single argument starts with that
  parenthesis, closed before a `.` or `[`, and the space is removed.
- Syntax 1.8 accepted and Ruby 3.1 rejects is repaired after the SyntaxError,
  before the section runs, keeping line numbers: `{'key', 'value'}` hashes,
  `when x:`, spaced multi-argument calls, `else` without `rescue`.

## Windows calls

- `steam_api.dll`: there is no Steam client on the Vita. `SteamAPI_Init`
  reports the API as started and the other calls do nothing; achievements stay
  off because their helper DLLs answer 0.
- `Utils!AVSetEnv` points `AV_APPDATA` at `ux0:/data/hardrpg/saves`; games add
  their own folder.
- `user32` `GetCursorPos`/`ScreenToClient`: the cursor is outside the window.
- Games that read the keyboard themselves (`GetAsyncKeyState`) and never call
  `Input.update`: once the engine's input has gone unread for 250 ms while keys
  are polled, the engine reads the pad once per frame and answers the keys RPG
  Maker's default layout gives each button (Enter/Space/C, Esc/X, Shift/Z,
  Page Up/Down, arrows).
- Ruby `p`/`print` and `MessageBox` text is kept for the error report, since
  the Vita cannot show desktop message boxes.
