#!/usr/bin/env bash
# Sets up KDE Plasma (Wayland) so desktop icons and widgets render above
# desktop-canvas wallpapers.
#
# What it does:
#  1. Installs the org.desktopcanvas.transparent wallpaper plugin to
#     ~/.local/share/plasma/wallpapers (a fully transparent Plasma
#     wallpaper that also clears the desktop window's opaque fill).
#  2. Switches every desktop containment to that wallpaper plugin.
#  3. Adds a KWin window rule forcing "keep above" on plasmashell's desktop
#     windows. Within KWin's desktop layer this pins the containment (icons
#     and widgets) above wallpaper layer surfaces, deterministically, even
#     when wallpaper surfaces are recreated.
#  4. Restarts plasmashell so the rule and plugin apply.
#
# Revert: re-select your wallpaper in the desktop settings, remove the
# "desktop-canvas-icons" rule from ~/.config/kwinrulesrc (or via System
# Settings > Window Rules), and delete
# ~/.local/share/plasma/wallpapers/org.desktopcanvas.transparent.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_ID="org.desktopcanvas.transparent"
RULE_ID="desktop-canvas-icons"
WALLPAPER_DIR="$HOME/.local/share/plasma/wallpapers"

echo "Installing transparent wallpaper plugin..."
mkdir -p "$WALLPAPER_DIR"
cp -r "$SCRIPT_DIR/$PLUGIN_ID" "$WALLPAPER_DIR/"

echo "Switching desktop containments to the transparent wallpaper..."
qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript \
    "desktops().forEach(d => { d.wallpaperPlugin = '$PLUGIN_ID'; });"

echo "Adding KWin window rule..."
existing_rules="$(kreadconfig6 --file kwinrulesrc --group General --key rules || true)"
if [[ ",$existing_rules," != *",$RULE_ID,"* ]]; then
    if [[ -n "$existing_rules" ]]; then
        new_rules="$existing_rules,$RULE_ID"
    else
        new_rules="$RULE_ID"
    fi
    count="$(kreadconfig6 --file kwinrulesrc --group General --key count || true)"
    kwriteconfig6 --file kwinrulesrc --group General --key rules "$new_rules"
    kwriteconfig6 --file kwinrulesrc --group General --key count "$(( ${count:-0} + 1 ))"
    kwriteconfig6 --file kwinrulesrc --group "$RULE_ID" --key Description \
        "desktop-canvas: keep desktop containment above wallpaper layer"
    kwriteconfig6 --file kwinrulesrc --group "$RULE_ID" --key above true
    kwriteconfig6 --file kwinrulesrc --group "$RULE_ID" --key aboverule 2
    kwriteconfig6 --file kwinrulesrc --group "$RULE_ID" --key types 2
    kwriteconfig6 --file kwinrulesrc --group "$RULE_ID" --key wmclass plasmashell
    kwriteconfig6 --file kwinrulesrc --group "$RULE_ID" --key wmclassmatch 1
fi
qdbus6 org.kde.KWin /KWin org.kde.KWin.reconfigure

echo "Restarting plasmashell..."
systemctl --user restart plasma-plasmashell.service

echo "Done. Desktop icons now render above desktop-canvas wallpapers."
echo "Note: mouse interaction with wallpapers is intercepted by the"
echo "desktop containment; scene parallax will not receive pointer input."
