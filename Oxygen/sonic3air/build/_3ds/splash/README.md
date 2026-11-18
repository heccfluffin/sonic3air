# Loading screen

The picture shown on the top screen from the moment the game opens until its first frame
(the bottom screen shows "Loading..." meanwhile).

- `make_splash.py` generates `s3air_splash.bin` (400 x 240 RGBA, zlib-compressed, embedded into the game by CMakeLists.txt)
  and `splash_preview.png`
- Default design: text and simple shapes only (no game artwork)
- Your own picture: `python3 make_splash.py --image my_loading_screen.png` (exactly 400 x 240 pixels), then commit
  `s3air_splash.bin` - the next build uses it
