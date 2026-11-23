#!/bin/sh
set -eu

uuid=sniplingo-pointer@wangzhichong2003.github.io
case "$(LC_ALL=C gnome-shell --version)" in
    'GNOME Shell 42.'*|'GNOME Shell 43.'*|'GNOME Shell 44.'*) ;;
    *) printf '%s\n' 'SnipLingo Pointer supports GNOME Shell 42 through 44.' >&2; exit 1 ;;
esac

source=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
destination=${XDG_DATA_HOME:-"$HOME/.local/share"}/gnome-shell/extensions/$uuid
install -d "$destination"
install -m 644 "$source/metadata.json" "$source/extension.js" "$destination"

# GNOME 42 discovers newly installed local extensions at login. Enable this UUID now so the user
# can sign back in without a second setup step. Preserve the other extension preferences.
gjs -c '
const { Gio } = imports.gi;
const uuid = ARGV[0];
const settings = new Gio.Settings({ schema_id: "org.gnome.shell" });
const enabled = settings.get_strv("enabled-extensions");
const disabled = settings.get_strv("disabled-extensions");
if (disabled.includes(uuid))
    settings.set_strv("disabled-extensions", disabled.filter(id => id !== uuid));
if (!enabled.includes(uuid))
    settings.set_strv("enabled-extensions", [...enabled, uuid]);
Gio.Settings.sync();
' "$uuid"

if [ "$(gsettings get org.gnome.shell disable-user-extensions)" = true ]; then
    printf '%s\n' 'SnipLingo Pointer is installed. Turn on user extensions in GNOME Extensions.'
elif gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
    --method org.freedesktop.DBus.NameHasOwner io.github.WangZhiChong2003.SnipLingo.Pointer \
    | grep -Fq true; then
    printf '%s\n' 'SnipLingo Pointer is enabled.'
else
    printf '%s\n' 'SnipLingo Pointer is installed. Sign out and back in to load it.'
fi
