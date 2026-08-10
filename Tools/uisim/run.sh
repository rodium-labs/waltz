#!/bin/sh
# Build the real gfx/UI sources for the host and render the player screen to
# docs/ui-preview.png. Only st7789_* and HAL_GetTick/HAL_Delay are faked, so
# what you see here is what the panel gets.
#
#   ./Tools/uisim/run.sh
set -e

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=$(mktemp -d)

cc -O1 -Wall -Wextra -std=gnu11 \
  -I"$here" \
  -I"$root/Libs/gfx/Inc" -I"$root/Libs/st7789/Inc" \
  -I"$root/Assets/Icons/Inc" -I"$root/Core/Inc/Ui" -I"$root/Core/Inc" \
  "$here/sim.c" \
  "$root/Libs/gfx/Src/gfx.c" "$root/Libs/gfx/Src/gfx_fonts.c" \
  "$root/Assets/Icons/Src/icons.c" \
  "$root/Core/Src/player.c" \
  "$root/Core/Src/Ui/player_ui.c" "$root/Core/Src/Ui/theme.c" \
  -o "$out/uisim"

"$out/uisim" "$out"
python3 "$here/sheet.py" "$out"/0[1-6]*.ppm "$root/docs/ui-preview.png"
python3 "$here/sheet.py" "$out"/0[789]*.ppm "$out"/1*.ppm "$root/docs/ui-screens.png"
python3 "$here/sheet.py" "$out"/T0[0-4]*.ppm "$root/docs/ui-themes-a.png"
python3 "$here/sheet.py" "$out"/T0[5-9]*.ppm "$root/docs/ui-themes-b.png"
python3 "$here/sheet.py" "$out"/S0*.ppm "$root/docs/ui-splash.png"
rm -rf "$out"
