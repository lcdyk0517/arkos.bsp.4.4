#!/bin/bash

DTB_DIR="arch/arm64/boot/dts/rockchip"
CONSOLE_DIR="/mnt/d/Tools/arkos4clone/consoles/"

success=0
fail=0

while IFS='|' read -r dtb folder; do
    src="$DTB_DIR/${dtb}.dtb"
    dst="$CONSOLE_DIR/$folder/"

    if [ ! -f "$src" ]; then
        echo "[SKIP] $src not found"
        ((fail++))
        continue
    fi

    if [ ! -d "$dst" ]; then
        echo "[SKIP] folder not found: $dst"
        ((fail++))
        continue
    fi

    if cp "$src" "$dst"; then
        echo "[OK] $src -> $dst"
        ((success++))
    else
        echo "[FAIL] $src -> $dst"
        ((fail++))
    fi
done << 'EOF'
rk3326-a10mini-linux|a10mini
rk3326-a10mini-v4-linux|a10miniv4
rk3326-d007-linux|d007
rk3326-dc35v-linux|dc35v
rk3326-dc40v-linux|dc40v
rk3326-dc45v-linux|dc45v
rk3326-dr28s-linux|dr28s
rk3326-g350-linux|g350
rk3326-go2-linux|go2
rk3326-h7-linux|h7
rk3326-hg36-linux|hg36
rk3326-k36-linux|k36
rk3326-k36s-linux|k36s
rk3326-mini40-linux|mini40
rk3326-mymini-linux|mymini
rk3326-o30s-linux|o30s
rk3326-r33s-linux|r33s
rk3326-r36h-linux|r36h
rk3326-r36hpromax-linux|r36hpromax
rk3326-r36max-type1-linux|r36max type1
rk3326-r36max-type2-linux|r36max type2
rk3326-r36max-type3-linux|r36max type3
rk3326-r36max2-linux|r36max2
rk3326-r36pro-linux|r36pro
rk3326-r36s-panel0-linux|origin panel0
rk3326-r36s-panel1-linux|origin panel1
rk3326-r36s-panel2-linux|origin panel2
rk3326-r36s-panel3-linux|origin panel3
rk3326-r36s-panel4-type1-linux|origin panel4 type1
rk3326-r36s-panel4-type2-linux|origin panel4 type2
rk3326-r36s-sauce-panel1-linux|sauce panel1
rk3326-r36s-sauce-panel2-linux|sauce panel2
rk3326-r36s-sauce-panel3-linux|sauce panel3
rk3326-r36s-sauce-panel4-linux|sauce panel4
rk3326-r36s-sauce-panel5-linux|sauce panel5
rk3326-r36s-type1-linux|clone type1
rk3326-r36s-type1-with-amp-linux|clone type1 amp
rk3326-r36s-type1-invert-linux|clone type1 invert
rk3326-r36s-type2-linux|clone type2
rk3326-r36s-type2-with-amp-linux|clone type2 amp
rk3326-r36s-type3-panel1-linux|clone type3 panel1
rk3326-r36s-type3-panel2-linux|clone type3 panel2
rk3326-r36s-type3-panel3-linux|clone type3 panel3
rk3326-r36s-type5-linux|clone type5
rk3326-r36s-v21-linux|v21 panel4 rumble test
rk3326-r36splus-linux|r36splus
rk3326-r36t-linux|r36t
rk3326-r36tmax-linux|r36tmax
rk3326-r36ultra-linux|r36ultra
rk3326-r36ultrax-linux|r36ultrax
rk3326-r36xx-linux|r36xx
rk3326-r40s-linux|r40s
rk3326-r40xx-linux|r40xx
rk3326-r40xxpromax-linux|r40xxpromax
rk3326-r45h-linux|r45h
rk3326-r46h-linux|r46h
rk3326-r50h-linux|r50h
rk3326-r50s-linux|r50s
rk3326-rf35h-linux|rf35h
rk3326-rf40h-linux|rf40h
rk3326-rf45h-linux|rf45h
rk3326-rf45v-linux|rf45v
rk3326-rf55h-linux|rf55h
rk3326-rg351mp-linux|rg351mp
rk3326-rg351p-linux|rg351p
rk3326-rg351v-panel1-linux|rg351v panel1
rk3326-rg351v-panel2-linux|rg351v panel2
rk3326-rg36-linux|rg36
rk3326-rg36pro-linux|rg36pro
rk3326-rgb10-linux|rgb10
rk3326-rgb10max1-linux|rgb10max1
rk3326-rgb10max2-linux|rgb10max2
rk3326-rgb10x-linux|rgb10x
rk3326-rgb20s-linux|rgb20s
rk3326-rgbv10-linux|rgbv10
rk3326-rp1-linux|rp1
rk3326-rx6h-linux|rx6h
rk3326-t16max-linux|t16max
rk3326-u8-panel1-linux|u8 panel1
rk3326-u8-panel2-linux|u8 panel2
rk3326-xf28-linux|xf28
rk3326-xf35h-linux|xf35h
rk3326-xf40h-linux|xf40h
rk3326-xf40v-linux|xf40v
rk3326-xf45v-linux|xf45v
rk3326-xgb36-linux|xgb36
rk3326-xu10-linux|xu10
EOF

echo ""
echo "Done: $success success, $fail fail"
