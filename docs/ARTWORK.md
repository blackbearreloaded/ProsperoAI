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

`launch-background-source.png` and `pic1.dds` deliberately contain only the native
UI's dark vertical gradient. The system keeps this launch frame visible during
renderer initialization; the application then presents its animated doorway.
This removes the separate illustrated splash before the animation on fresh
process launches, including reopening after closing the application.

DDS conversion used AMD Compressonator 4.5.52, `-fd BC7 -nomipmap`; icon and
background resizing used Pillow Lanczos.
