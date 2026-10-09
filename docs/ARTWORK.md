# Launcher artwork

Regenerated with the built-in imagegen tool on 2026-10-09, using the previous
`launch-background-source.png` as a doorway reference.

Icon prompt: square PS5 ProsperoAI icon; midnight charcoal background; centered
nested champagne-gold arches and slightly open dark double doors revealing soft
warm light; restrained glow and floor reflection; readable at small sizes;
no text, planets, landscape, sun orb or watermark.

Selection background prompt: widescreen PS5 home background; matching midnight
charcoal and champagne-gold arched double doors on a subtly reflective floor;
portal right of center; quiet dark left area for system text and icons;
minimal architecture, soft light; no planets, stars, mountains, desert, text or
watermark.

Sources: `sce_sys/icon0.png` and `sce_sys/background-source.png`. PS5 selection
art is `sce_sys/pic0.dds` (3840×2160 BC7).

Launch picture: the same doorway seen straight on from standing height, its two
black marble leaves almost closed on a narrow line of warm light; the whole arched
frame is visible and takes about three quarters of the height, with dark space on
both sides. No text, people or logos. The system shows it from the moment the app is
started until the app has drawn its first frame, so nothing is black in between.

Sources: `sce_sys/launch-background-source.png` (3840×2160); the console's copy is
`sce_sys/pic1.dds`. The source pictures stay in the repository and are not packaged.

DDS conversion: `pic0.dds` with AMD Compressonator 4.5.52, `-fd BC7 -nomipmap`;
`pic1.dds` with DirectXTex texconv, `-f BC7_UNORM -m 1`; icon and
background resizing used Pillow Lanczos.
