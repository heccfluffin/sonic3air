# Your own bottom screen themes

Put a background picture here and add a line for it to `THEMES` in `../make_bottom_screen.py`.

**The picture**
- Exactly **320 x 240 pixels** (the 3DS bottom screen), PNG.
- It's shown 1:1, so pixel art stays crisp.
- The glass panels, buttons and text are drawn on top of it by the generator, so most of the picture is covered by
  translucent panels: patterns, gradients and textures work best; details in the middle won't be very visible.
- The 3DS shows the bottom screen with 16-bit colors; very subtle gradients get slightly dithered.

**The theme line** (example)

    { 'id': 'mytheme', 'name': 'My Theme', 'style': 'image', 'image': 'mytheme.png',
      'save': (50, 175, 70), 'load': (30, 165, 235), 'outline': (20, 70, 140) },

- `save` / `load`: colors of the SAVE STATE / LOAD STATE buttons (red, green, blue, 0-255)
- `outline`: color of the outline around the white texts
