# Art direction

The Veldane Grand Harbourfront at night: a calm, expensive front of house over a plain, hard-working back of house. The player should feel the change in materials the moment they pass a staff door.

## The building
- One walkable building, built in code by `AKcHotel` (Source/Keycard/Private/World/KcHotel.cpp): B1 back of house (LP office, CCTV room, loading dock, corridors), ground floor (street doors, lobby, front desk, lounge and bar), floor 14 (guest corridor and rooms, including 1412), stairs A and B and two lifts joining them. Floors 1-13 are shown in the lift panel but are not fitted yet.
- Front of house: marble floor, walnut and dark stone counters, brass, warm lamps (about 2700 K), one chandelier, harbour prints, patterned corridor carpet and wallpaper upstairs.
- Back of house: sealed concrete floors, painted walls, fluorescent panels (about 4000 K), steel, safety yellow, fire red, signage.
- Outside: wet-looking asphalt, promenade paving, harbour water, a drawn night dome (city glow at the horizon) and lit-window skylines on four distant panels (`T_SkylineHarbour`, `T_SkylineCity`), so every floor from G to 14 sees the city at its own height. The night HDRI only lights the scene.

## Palette
| Role | Colour | Where |
|---|---|---|
| Accent (harbour blue) | #4B9CC4 | UI focus and primary buttons, hotel signage glow, interaction highlight |
| Text | #ECEEF0 on #0E1116 panels | All UI |
| Muted text | #9AA3AD | Labels, secondary lines |
| Good / warning / bad | #5CC48A / #E3A33B / #D9534F | Results and status (colour-blind safe set: #56B4E9 / #F0E442 / #E69F00) |
| Warm light | lamp amber, brass | Front of house |
| Back-of-house light | neutral white | LP office, corridors, CCTV room |

Never use a real hotel chain's colours, logo shapes or wording. Veldane marks are plain type only ("Veldane Grand").

## UI rules
- One style module: `Source/Keycard/Public/KcUIStyle.h`. Screens never set their own colours or fonts.
- Inter (SIL OFL) for all UI text. Body text is 14 pt Slate (about 19 px at 96 DPI); nothing under 12 pt.
- Text over the world sits on a panel (#0E1116 at about 92% opacity). Every screen has a keyboard path and a visible focus ring in the accent colour.
- Status is never shown by colour alone: each state also has a word or an icon.
- Rubrics are hidden until the player files the report; questions never hint the score (no reaction to the player's answer before the debrief).
- Reduced motion: camera moves become cuts, fades stay.

## In-world screens and print
All drawn by `Scripts/GenerateTextures.py`: CCTV wall (camera ids such as 14-03 and L-07, clock 00:41:07 to match M001), shift board, Port Avener room rack, incident report, clip export, fire panel, whiteboard, evacuation plan, exit sign, Do Not Disturb tag, harbour prints, corridor carpet and wallpaper. Screen text must stay legible at the distance the player stands: these textures never stream out their mips.

## Performance budget (GTX 1650 4 GB, 1080p, 60 fps at High)
- DX11 / SM5, no Nanite, no virtual shadow maps, no 8K textures (1k Poly Haven maps; 2k HDRI).
- Repeated geometry is instanced (one component per mesh and material); hidden floors are switched off as the player changes floor.
