#!/usr/bin/env bash
# Sets up KDE Plasma (Wayland) so desktop icons and widgets render above
# desktop-canvas wallpapers.
#
# What it does:
#  1. Installs the org.desktopcanvas.transparent wallpaper plugin to
#     ~/.local/share/plasma/wallpapers (a fully transparent Plasma
#     wallpaper that also clears the desktop window's opaque fill).
#  2. Switches every desktop containment to that wallpaper plugin.
#  3. Installs and enables the org.desktopcanvas.raisedesktop KWin script,
#     which re-raises plasmashell's desktop containment whenever a window
#     maps. Wallpaper layer surfaces and the containment share KWin's
#     DesktopLayer and are ordered by map order, so without this the last
#     surface to map wins and the icons disappear whenever the wallpaper
#     daemon restarts. See README.md for why a window rule cannot do this.
#  4. Removes the obsolete "desktop-canvas-icons" KWin window rule if an
#     earlier version of this script installed it. That rule never had any
#     effect: Window::belongsToLayer() returns DesktopLayer for desktop-type
#     windows before it ever looks at keepAbove().
#
# Both the containment switch and the KWin script take effect immediately;
# no plasmashell or session restart is needed.
#
# Revert: re-select your wallpaper in the desktop settings, then
#   kwriteconfig6 --file kwinrc --group Plugins \
#       --key org.desktopcanvas.raisedesktopEnabled false
#   qdbus6 org.kde.KWin /KWin org.kde.KWin.reconfigure
#   rm -rf ~/.local/share/kwin/scripts/org.desktopcanvas.raisedesktop \
#          ~/.local/share/plasma/wallpapers/org.desktopcanvas.transparent
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_ID="org.desktopcanvas.transparent"
KWIN_SCRIPT_ID="org.desktopcanvas.raisedesktop"
RULE_ID="desktop-canvas-icons"
WALLPAPER_DIR="$HOME/.local/share/plasma/wallpapers"
KWIN_SCRIPT_DIR="$HOME/.local/share/kwin/scripts"

script_loaded() {
    [[ "$(qdbus6 org.kde.KWin /Scripting \
        org.kde.kwin.Scripting.isScriptLoaded "$KWIN_SCRIPT_ID" 2>/dev/null)" == "true" ]]
}

echo "Installing transparent wallpaper plugin..."
mkdir -p "$WALLPAPER_DIR"
rm -rf "${WALLPAPER_DIR:?}/$PLUGIN_ID"
cp -r "$SCRIPT_DIR/$PLUGIN_ID" "$WALLPAPER_DIR/"

echo "Switching desktop containments to the transparent wallpaper..."
qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript \
    "desktops().forEach(d => { d.wallpaperPlugin = '$PLUGIN_ID'; });"

echo "Installing KWin script..."
mkdir -p "$KWIN_SCRIPT_DIR"
rm -rf "${KWIN_SCRIPT_DIR:?}/$KWIN_SCRIPT_ID"
cp -r "$SCRIPT_DIR/kwin-script/$KWIN_SCRIPT_ID" "$KWIN_SCRIPT_DIR/"
kwriteconfig6 --file kwinrc --group Plugins \
    --key "${KWIN_SCRIPT_ID}Enabled" true

echo "Removing obsolete '$RULE_ID' window rule..."
existing_rules="$(kreadconfig6 --file kwinrulesrc --group General --key rules || true)"
if [[ ",$existing_rules," == *",$RULE_ID,"* ]]; then
    # grep exits 1 when the rule was the only entry; an empty list is the
    # correct result there, so do not let it trip set -e / pipefail.
    new_rules="$(printf '%s' "$existing_rules" | tr ',' '\n' \
        | { grep -vx "$RULE_ID" || true; } | paste -sd, -)"
    count="$(kreadconfig6 --file kwinrulesrc --group General --key count || true)"
    count="${count:-1}"
    kwriteconfig6 --file kwinrulesrc --group General --key rules "$new_rules"
    kwriteconfig6 --file kwinrulesrc --group General --key count \
        "$(( count > 0 ? count - 1 : 0 ))"
    for key in Description above aboverule types wmclass wmclassmatch; do
        kwriteconfig6 --file kwinrulesrc --group "$RULE_ID" --key "$key" --delete
    done
fi

echo "Reloading KWin configuration..."
# Drop any previously loaded copy so a re-run picks up edited script sources.
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript \
    "$KWIN_SCRIPT_ID" >/dev/null 2>&1 || true
qdbus6 org.kde.KWin /KWin org.kde.KWin.reconfigure

# reconfigure normally loads newly enabled scripts; load it by hand if not.
if ! script_loaded; then
    echo "Loading KWin script explicitly..."
    qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript \
        "$KWIN_SCRIPT_DIR/$KWIN_SCRIPT_ID/contents/code/main.js" \
        "$KWIN_SCRIPT_ID" >/dev/null
    qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start
fi

if ! script_loaded; then
    echo "Warning: KWin script $KWIN_SCRIPT_ID did not load." >&2
    echo "Check 'journalctl --user -b -o cat | grep js:' for errors." >&2
    exit 1
fi

echo "Done. Desktop icons now render above desktop-canvas wallpapers."
echo "Note: mouse interaction with wallpapers is intercepted by the"
echo "desktop containment; scene parallax will not receive pointer input."
