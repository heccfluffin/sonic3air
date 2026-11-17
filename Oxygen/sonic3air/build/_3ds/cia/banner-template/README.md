# HOME Menu banner template

The banner is the picture the 3DS HOME Menu shows on the top screen when the app is selected.
The build makes it from `cia/banner.png` (picture) and `cia/banner.wav` (sound, see `make_banner_sound.py`).

## Files

| File | What it's for |
|---|---|
| `banner_current.png` | The current banner, to start editing from |
| `banner_template_guides.png` | Empty 256 x 128 canvas with guides |
| `banner_guide_layer.png` | Only the guides on a transparent background: put it as the top layer in your image editor, hide it before saving |
| `banner_current_with_guides.png`, `preview_3x.png` | The current banner with the guides, to see how it's laid out |

Guides: blue = image edge, yellow = recommended margin (8 px; keep text and important details inside),
red = center lines, white = 16 px grid.

## Rules for your banner

- Exactly **256 x 128 pixels**, PNG
- Fully opaque: no transparent pixels (they don't show up as you'd expect)
- The 3DS shows it smaller than on your PC monitor: use big, bold text and simple shapes
- The HOME Menu draws it slightly curved / in perspective, so details right at the edges may be harder to see

## Using it

Save your picture as `cia/banner.png` (replacing the current one), commit and push: the next build's CIA has the new banner.
Install that CIA to see it on the HOME Menu.
