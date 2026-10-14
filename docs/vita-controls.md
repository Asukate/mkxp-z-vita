# Vita controls

In Games, highlight a game and press Triangle -> Controls. Edits save
immediately and apply on the next launch of that game. Circle goes back one
level; from the controls list it returns to the game settings menu.

Profiles start disabled, so a game keeps its original controls until Custom
controls is turned on. Mapping any button or touch zone turns it on. Reset
everything turns the profile off and restores the default mappings.

Profiles belong to each game's library identity. Renaming a game in the
launcher keeps its profile, config and saves; moving it changes its identity.
Game folders and archives are never edited. Profiles are stored in
`ux0:/data/hardrpg/controls/<game-key>.json`.

## The controls list

| Row | What it does |
| --- | --- |
| Custom controls | Turns the profile on or off. |
| Cross ... R button | One row per Vita button. Cross opens the picker. |
| Tap to walk | Tap the map to walk there (see `docs/tap-to-walk.md`). |
| Front touch screen | The front screen's mode and zones, on a diagram. |
| Rear touch pad | The rear pad's mode and zones, on a diagram. |
| Sticks and pointer | Stick deadzone, right-stick pointer, pointer speed, Square toggle. |
| Test inputs | Live buttons, sticks, touches and the actions they produce. |
| Reset everything | Back to defaults, profile off. |

## The picker

Every button and touch zone can send either an RPG Maker action or a keyboard
key. The top row holds the RPG Maker actions; X=A, Y=S, Z=D, L=Q and R=W name
the PC key that sends each of RPG Maker's letter buttons, which are not the
keyboard letters. Below it is a full keyboard, for games that read keys
themselves (LISA's W/A/S/D combos, F-key menus). Keys reach RGSS input, the
raw key state and the Win32 key functions games call through Win32API.
Entries already in use elsewhere are tinted.

## Touch modes

| Surface | Mode | Behaviour |
| --- | --- | --- |
| Front | Whole screen | The whole physical screen is one button. Nothing is drawn. |
| Front | Left / right halves | Two buttons. Nothing is drawn. |
| Front | Four corners | Four buttons, several at once with several fingers. Nothing is drawn. |
| Front | Two drawn corner buttons | Two boxes drawn in the lower corners of the game image. |
| Front | D-pad + drawn button | A drawn D-pad lower left and a button lower right. |
| Front | Mouse pointer | A touch inside the game image moves the cursor and holds mouse left. |
| Rear | Whole pad, halves, four corners | As on the front, read from the rear pad. |
| Rear | Two upper zones | Two buttons along the top edge, clear of the grip. |
| Rear | Trackpad | Hold L and slide a finger to move the cursor. |

The invisible zones cover the whole screen or pad edge to edge, including the
black borders around a game image. A finger between two zones takes the nearer
one and keeps its zone until it is clearly past the line, so it cannot flicker.
Fingers already on the screen or pad when the game takes input are ignored
until lifted, so a resting grip on the rear pad presses nothing. Tap to walk
needs the front screen as a pointer, so it and the front zone modes turn each
other off.

Pointer modes use mkxp-z's mouse input; ordinary RGSS games need mouse-aware
scripts to use them. The left stick moves in four or eight directions using the
deadzone. RGSS A / Run also sets SHIFT. The Square toggle turns Square's action
into an on/off switch; it resets on launch, resume, or an input gap longer than
250 ms. A button held through launch or resume must be released before the
profile takes input.
