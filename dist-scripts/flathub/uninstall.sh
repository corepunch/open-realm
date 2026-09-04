#!/usr/bin/env bash
set -euo pipefail

APP_ID="io.github.corepunch.OpenRealm"
DELETE_DATA=0
KEEP_STEAM_SHORTCUT=0

usage() {
    cat <<USAGE
Usage: $0 [--delete-data] [--keep-steam-shortcut]

Uninstall the OpenRealm Flatpak for the current user.

By default the script removes an OpenRealm Steam shortcut/artwork created by
OpenRealm, then uninstalls the Flatpak while preserving saved/config data.

Options:
  --delete-data          Also delete Flatpak-owned OpenRealm user data.
  --keep-steam-shortcut  Leave any OpenRealm non-Steam shortcut in Steam.
  -h, --help             Show this help.
USAGE
}

while (($#)); do
    case "$1" in
        --delete-data)
            DELETE_DATA=1
            ;;
        --keep-steam-shortcut)
            KEEP_STEAM_SHORTCUT=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if flatpak info --user "$APP_ID" >/dev/null 2>&1; then
    if [[ "$KEEP_STEAM_SHORTCUT" == "0" ]]; then
        if ! flatpak run --command=python3 "$APP_ID" \
            /app/bin/steam_shortcut.py \
            --all-steamids \
            --remove-flatpak-app-id "$APP_ID" \
            >/dev/null 2>&1; then
            echo "Warning: could not remove the OpenRealm Steam shortcut automatically." >&2
            echo "Steam may need to be restarted after removing it manually." >&2
        else
            echo "Removed OpenRealm Steam shortcut data where present."
        fi
    fi

    uninstall_args=(--user -y)
    if [[ "$DELETE_DATA" == "1" ]]; then
        uninstall_args+=(--delete-data)
    fi
    flatpak uninstall "${uninstall_args[@]}" "$APP_ID"
else
    echo "$APP_ID is not installed for the current user."
    if [[ "$DELETE_DATA" == "1" ]]; then
        data_dir="${HOME}/.var/app/${APP_ID}"
        if [[ -d "$data_dir" ]]; then
            rm -rf -- "$data_dir"
            echo "Removed preserved OpenRealm Flatpak data: $data_dir"
        fi
    fi
fi

if [[ "$DELETE_DATA" == "0" ]]; then
    printf 'Uninstalled %s\nUser data was preserved. Re-run with --delete-data for a full purge.\n' "$APP_ID"
else
    printf 'Uninstalled %s and removed its Flatpak-owned user data.\n' "$APP_ID"
fi
