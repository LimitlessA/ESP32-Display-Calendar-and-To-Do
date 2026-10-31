#!/usr/bin/env bash
# Regenerates src/fonts/*.c: Montserrat with English letters, common
# punctuation and the Romanian letters (for calendar and task text; the UI
# itself is English), plus the Font Awesome icons the UI uses.
# Needs Node.js. Run from the firmware folder: tools/gen_fonts.sh
set -euo pipefail
WORK=$(mktemp -d)
(cd "$WORK" && npm init -y >/dev/null && npm i --silent lv_font_conv@1.5.2 @fontsource/montserrat@5.1.0)
git clone -q --depth 1 -b v8.3.11 https://github.com/lvgl/lvgl.git "$WORK/lvgl"
MONTSERRAT="$WORK/node_modules/@fontsource/montserrat/files"
FA="$WORK/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"
CONV="$WORK/node_modules/.bin/lv_font_conv"

TEXT="0x20-0x7E,0xA0,0xB0,0xB7,0xD7,0x2013-0x2014,0x2018-0x2019,0x201C-0x201D,0x2022,0x2026,0x20AC"
# Romanian: Â â Î î (Latin-1), Ă ă, Ș ș Ț ț, and the cedilla forms Ş ş Ţ ţ that
# some calendars still use. All but Â â Î î are in Montserrat's latin-ext file.
RO_LATIN="0xC2,0xCE,0xE2,0xEE"
RO_EXT="0x102-0x103,0x15E-0x15F,0x162-0x163,0x218-0x21B"
# Keep in sync with src/icons.h
ICONS="0xF015,0xF133,0xF14A,0xF1DE,0xF1EB,0xF00D,0xF00C,0xF0F3,0xF053,0xF054,0xF021,0xF1F8,0xF06E,0xF0C0,0xF3C5,0xF017,0xF023,0xF240-0xF244,0xF0E7,0xF7C2,0xF093,0xF55A,0xF062,0xF029,0xF067,0xF186,0xF028,0xF53F,0xF0C2,0xF071,0xF005,0xF1B9,0xF06B,0xF11C,0xF2F2,0xF024,0xF04C,0xF04B,0xF04D,0xF0EB,0xF554,0xF5FD,0xF3CD,0xF522,0xF338,0xF0D7,0xF0D8,0xF05B"

gen() {  # name size weight [icons]
  local extra=()
  [ "${4:-}" = icons ] && extra=(--font "$FA" -r "$ICONS")
  "$CONV" --no-compress --no-prefilter --bpp 4 --size "$2" --format lvgl --lv-include lvgl.h \
    --font "$MONTSERRAT/montserrat-latin-$3-normal.woff" -r "$TEXT,$RO_LATIN" \
    --font "$MONTSERRAT/montserrat-latin-ext-$3-normal.woff" -r "$RO_EXT" \
    "${extra[@]}" -o "src/fonts/$1.c"
}

gen font_12 12 500 icons
gen font_14 14 500 icons
gen font_14b 14 700 icons
gen font_16 16 500 icons
gen font_16s 16 600 icons
gen font_20b 20 700 icons
gen font_26b 26 700
gen font_34b 34 700
# Digits for the big clocks, timers and the meeting cost counter
"$CONV" --no-compress --no-prefilter --bpp 4 --size 72 --format lvgl --lv-include lvgl.h \
  --font "$MONTSERRAT/montserrat-latin-700-normal.woff" -r "0x20,0x2C,0x2D,0x30-0x3A,0x41,0x4D,0x50,0x68,0x6D,0x20AC" -o src/fonts/font_72.c
"$CONV" --no-compress --no-prefilter --bpp 4 --size 150 --format lvgl --lv-include lvgl.h \
  --font "$MONTSERRAT/montserrat-latin-500-normal.woff" -r "0x20,0x2D,0x30-0x3A" -o src/fonts/font_150.c
rm -rf "$WORK"
