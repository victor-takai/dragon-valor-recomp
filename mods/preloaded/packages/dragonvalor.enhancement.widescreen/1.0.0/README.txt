Dragon Valor Widescreen (dragonvalor.enhancement.widescreen 1.0.0)
==================================================================

Mods > Display > Widescreen. Off by default; tick it, pick a View and Play.
Your choice is saved in mods/state.toml and applied at the next start.

What it does
------------
Stages render wider than 4:3. The extra columns are real rendering, not a
stretch: the game draws the forest, rocks and enemies that sit beyond the
4:3 edges. The HUD (HP/MP bars, enemy counter, tutorial text) keeps its
usual positions inside the 4:3 area.

View
  16:9           The default. The window opens at 16:9 and the picture keeps
                 that aspect whatever the window shape.
  21:9, 32:9     Wider fixed ratios. Checked only in a screenshot; the far
                 edges may show less.
  Fit to Window  Opens at 16:9, then follows the window as you resize it,
                 with no upper limit (4:3 is the floor).

What stays 4:3
--------------
The Namco logo, the intro movie, the title screen and its menus, the status
screen's panel (the stage behind it is wide) and the story movies keep 4:3
with side bars, because that art is drawn for 4:3. The pause screen stays
wide.

Notes
-----
- Experimental: tested on chapter 1 (forest, town, castle). Known blemish:
  a small block of texture noise can sit just right of the 4:3 area in some
  stages (a parked part of the hero model that the console's 4:3 picture
  clipped); report any other oddity with the stage name.
- OpenGL renderer (the default). Not checked on the software renderer.
- Changing it needs no rebuild; the wide path is in every build and does
  nothing while this package is disabled.

Credits
-------
psxrecomp's native-wide renderer; package pattern after TombaRecomp's and
RidgeRacerType4Recomp's widescreen packages.
