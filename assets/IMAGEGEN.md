# LORA Tunnel radio logo

The current asset is `lora-tunnel-radio-logo.png`, generated with the built-in
image-generation tool on 2026-11-01. The user requested a green radio identity
and explicitly removed BWM from the new branding. The current splash and source
archive use this radio-only asset. Earlier BWM/reference images remain local
historical files and are excluded from the source package.

The artwork depicts a bright green handheld radio with two radio-wave arcs
above a white `LORA TUNNEL` wordmark, on an opaque dark background. This clean
generation replaces the earlier radio asset, whose transparency had visible
speckling. No original project logo or upstream project's branding is included.

Condensed generation brief, not a verbatim prompt: professional flat geometric
green handheld radio on opaque `#000408`, crisp antialiased edges, clean solid
fills, bold white uppercase LORA / TUNNEL on two lines, central 80% composition
and legibility at 180 pixels; no BWM, arrows, grunge, texture, speckles, shadow,
glow or 3D treatment.

- Project asset: `assets/lora-tunnel-radio-logo.png`.
- Format: 1,254 x 1,254 RGB PNG, opaque.
- File size: 1,075,724 bytes.
- SHA-256: `aa0c01fb449e94c32a28888532e7ce644632cf1254316de7ab320d8d0b92c749`.

The generated output and project copy were checked for matching hashes. The asset was
visually inspected on the host for the green radio motif and wordmark. The UI
embeds it and renders it within a 180-pixel square on its 240 x 320 splash.
The user confirmed that this clean radio logo looks right on the installed
0.2.0 device. That scoped appearance confirmation is separate from the earlier
0.1.2 flicker fix; visibility under every lighting condition and complete UI
input acceptance have not been established. The same asset is retained in
0.3.0-beta.1. Public UI previews are separately documented in [ui/README.md](ui/README.md); those renders use actual UI drawing code and synthetic fixtures, not image generation.

## Supplied VTS mark

`vts-official-logo.png` is the original VTS artwork supplied by Valleytech for
the device header. It is embedded unchanged, scaled by the display renderer
onto a small light backing so its black shapes remain visible. It was not
generated or redrawn. SHA-256:
`704dc0daf32a201379dd2c18c7756b44fc3cf215ebcd9839cb7d6abfb7167748`.
The VTS mark identifies Valleytech; software licensing does not imply trademark
rights or endorsement of third-party products.
