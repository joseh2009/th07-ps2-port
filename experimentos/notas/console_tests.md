# PS2 Touhou 7 - performance items A-G: what to test on the console

Build: `make -f Makefile.ps2 -j8` (PS2DEV/PS2SDK/GSKIT set as before). Compare each item with the previous one,
same stage, same save, same character, with the R3 panel open.

Panel fields used below: FPS, GS (ms in SwapBuffers), EE~ (estimate), RENDER, CALC, DRAW, untimed,
TEX binds / upload (new, last frame), callback table.

## A. No console printf (Makefile.ps2)
- Build has no -DTH07_PS2_VERBOSE. Boot and title screen: the on-screen log still shows.
- Check: the start-up takes less time than with the verbose build (time from the logo to the title).
- Nothing else should change visually.

## B. GS overlap (opt-in: -DTH07_PS2_GS_PIPELINE, default OFF)
- Default build: identical to before (nothing changes unless the define is set).
- With the define: the picture is one frame behind the input. Check for tearing or flicker at stage 1.
- Check texture changes: leave and re-enter a stage several times (texture free and upload path), and exit to the
  desktop from the pause menu. No crash, no garbage sprites.
- The GS value on the panel means "wait for the previous frame + flip + kick" in this build, not GS work.

## C. Screen-space sprites use the UV form
- HUD, title menu, text, bullet/item icons, the boss bar: textures must be in the right place (no shifted or
  stretched graphics). This is the one item whose units were inferred from the headers, not from gsKit's source.
- Fall back check: if sprites look wrong, the UV unit (texel * 16) is the first suspect.

## D. Integer floor for the pixel snap
- Expect no visible change. Look for seams between sprites (light lines) on the stage background and HUD.
- Expected gain is small. Not measured: read EE~ and RENDER on the panel over a 30 s dense stage.

## E. Render state cache (on by default; -DTH07_PS2_NO_STATE_CACHE turns it off)
- Compare the same stage with and without the define. Look for: wrong alpha cut-outs (edges of sprites, the
  player's shot, the bomb), depth errors, sprites clipped at the edges of the screen, and the pause menu after a
  Clear (the Clear path is where the cache is reset).
- If anything differs, build with -DTH07_PS2_NO_STATE_CACHE and report which stage shows it.

## F. Texture measurement and opt-in CT16 for formats 1 and 5
- Panel line "TEX binds N upload K per frame": check it changes between title, a stage and a boss.
- Default build: no visual change at all.
- -DTH07_PS2_TEX16_FMT15 (opt-in): check semi-transparent sprites (bomb glow, option orbs, bullet fades) for
  cut-out edges. If the cut-outs are acceptable, that is a decision for the owner; otherwise leave it off.

## G. Bullet flicker counts only the visible bullets
- Dense stage with many bullets, especially bullets that leave the screen: the flicker should start later than
  before (only when the on-screen count exceeds 256).
- Check the bullets that come back on screen are drawn (no permanent gap).
- Read BulletDraw time on the callback table before and after; the limit (256) is unchanged and still needs a
  decision from console measurements.
