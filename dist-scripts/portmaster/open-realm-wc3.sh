#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt # We source the control.txt file contents here
# The $ESUDO, $directory, $param_device and necessary sdl configuration controller configurations will be sourced from the control.txt file shown [here]

# We source custom mod files from the portmaster folder example mod_jelos.txt which containts pipewire fixes
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

# We pull the controller configs like the correct SDL2 Gamecontrollerdb GUID from the get_controls function from the control.txt file here
get_controls

# We switch to the port's directory location below & set the variable for the gamedir and a configuration dir  easier handling below
GAMEDIR=/$directory/ports/open-realm-wc3/
CONFDIR="$GAMEDIR/conf/"

# Ensure the conf directory exists
mkdir -p "$GAMEDIR/conf"

# Switch to the game directory
cd $GAMEDIR

# Log the execution of the script, the script overwrites itself on each launch
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# Some ports like to create save files or settings files in the user's home folder or other locations. We map these config folders so we can either preconfigure games and or have the savefiles in one place. I
# You can either use XDG variables to redirect the Ports to our gamefolder if the port supports it:

# Set the XDG environment variables for config & savefiles
export XDG_DATA_HOME="$CONFDIR"

# OR

# Use bind_directories to reroute that to a location within the ports folder.
#bind_directories ~/.portfolder $GAMEDIR/conf/.portfolder

# Port specific additional libraries should be included within the port's directory in a separate subfolder named libs.aarch64, libs.armhf or libs.x64
export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"

# Provide appropriate controller configuration if it recognizes SDL controller input
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# If a port uses GL4ES (libgl.so.1) a folder named gl4es.aarch64 etc. needs to be created with the libgl.so.1 file in it. This makes sure that each cfw and device get the correct GL4ES export.
#if [ -f "${controlfolder}/libgl_${CFW_NAME}.txt" ]; then
#  source "${controlfolder}/libgl_${CFW_NAME}.txt"
#else
#  source "${controlfolder}/libgl_default.txt"
#fi

# ------------------------------------------------------------
# First-launch ISO installation (native C tools, no Wine/Node.js)
# ------------------------------------------------------------
install() {
    local data="$GAMEDIR/gamefiles/warcraft-3"
    local isoextract="$GAMEDIR/isoextract.${DEVICE_ARCH}"
    local setupreader="$GAMEDIR/wc3setup.${DEVICE_ARCH}"
    local staging="$GAMEDIR/gamefiles/.wc3import-staging"
    local marker="$data/.portmaster-iso-import-v1"
    local disc source name candidate rc index=0 keep_roc=0
    local disc_recognized signature old_count marker_matches=1
    local -a images=() signatures=() missing_before_setup=()
    local -a archives=(War3.mpq War3Local.mpq War3x.mpq War3xLocal.mpq War3Patch.mpq)

    mkdir -p "$data" || return 1
    # The only ISO input location: the same gamefiles/ folder shipped in the port.
    # Bash's nocaseglob accepts .ISO as well as .iso; filenames may have spaces.
    shopt -s nullglob nocaseglob
    images=("$GAMEDIR"/gamefiles/*.iso)
    shopt -u nullglob nocaseglob

    if [ "${#images[@]}" -eq 0 ]; then
        if [ -s "$data/War3.mpq" ]; then
            return 0  # Traditional manual MPQ installation is still supported.
        fi
        pm_message "Warcraft III game data missing"
        pm_message "Copy your original ISO files to gamefiles/"
        return 1
    fi
    if [ ! -x "$isoextract" ] || [ ! -x "$setupreader" ]; then
        pm_message "ISO importer binaries not found for ${DEVICE_ARCH}"
        return 1
    fi

    for disc in "${images[@]}"; do
        signatures+=("$disc:$(stat -c '%s:%Y' "$disc" 2>/dev/null || echo missing)")
    done

    # Upgrade / interrupted-cleanup path: a previous successful import may
    # have left one or both ISOs behind. Only delete unchanged, recorded inputs
    # when the installed game archives still validate; never delete new inputs
    # merely because War3.mpq already happens to exist.
    if [ -f "$marker" ]; then
        for signature in "${signatures[@]}"; do
            if ! grep -Fxq -- "$signature" "$marker"; then
                marker_matches=0
                break
            fi
        done
        old_count=$(grep -c '^' "$marker" || true)
        if [ "$marker_matches" -eq 1 ] && [ -s "$data/War3.mpq" ] &&
           { [ "$old_count" -le 1 ] || [ -s "$data/War3x.mpq" ]; }; then
            for name in "${archives[@]}" Setup.mpq; do
                if [ -e "$data/$name" ] &&
                   { [ ! -s "$data/$name" ] || ! "$setupreader" verify "$data/$name"; }; then
                    marker_matches=0
                    break
                fi
            done
            if [ "$marker_matches" -eq 1 ]; then
                pm_message "Warcraft III data already imported; removing ISO files..."
                for disc in "${images[@]}"; do
                    if ! rm -- "$disc"; then
                        pm_message "Could not delete ${disc##*/}; remove it manually"
                    fi
                done
                return 0
            fi
        fi
    fi

    # Installer-owned staging folder, on the same filesystem as the final MPQs.
    # No source ISO is removed until ALL discs are processed and installed data
    # is verified. A failed import leaves the source ISOs untouched for retry.
    rm -rf -- "$staging"
    mkdir -p "$staging/archives" || return 1
    pm_message "Importing Warcraft III game data from ISO..."
    for disc in "${images[@]}"; do
        index=$((index + 1))
        source="$staging/disc-$index"
        mkdir -p "$source" || return 1
        disc_recognized=0
        echo "WC3 ISO: $disc"
        for name in "${archives[@]}"; do
            # Never replace an archive already installed or recovered.
            if [ -s "$data/$name" ] || [ -s "$staging/archives/$name" ]; then
                continue
            fi
            rc=0
            "$isoextract" extract-file "$disc" "$name" "$staging/archives/$name" || rc=$?
            if [ "$rc" -eq 0 ]; then
                disc_recognized=1
            elif [ "$rc" -ne 2 ]; then
                pm_message "Failed reading $name from ISO"
                rm -rf -- "$staging"
                return 1
            fi
        done
        rc=0
        "$isoextract" extract-file "$disc" Setup.mpq "$source/Setup.mpq" || rc=$?
        if [ "$rc" -eq 0 ]; then
            # Keep ONLY the RoC installer for the engine's built-in map loader.
            keep_roc=0
            if "$setupreader" maps "$source/Setup.mpq"; then
                disc_recognized=1
                if [ ! -s "$data/Setup.mpq" ] && [ ! -s "$staging/Setup.mpq" ]; then
                    keep_roc=1
                fi
            fi
            # Record which game archives were missing before parsing Setup.mpq;
            # a supported, newly recovered nested archive identifies the disc.
            missing_before_setup=()
            for name in "${archives[@]}"; do
                if [ ! -s "$staging/archives/$name" ] && [ ! -s "$data/$name" ]; then
                    missing_before_setup+=("$name")
                fi
            done
            if ! "$setupreader" extract "$source/Setup.mpq" "$staging/archives"; then
                pm_message "Cannot read embedded MPQs from Setup.mpq"
                rm -rf -- "$staging"
                return 1
            fi
            for name in "${missing_before_setup[@]}"; do
                if [ -s "$staging/archives/$name" ]; then
                    disc_recognized=1
                fi
            done
            if [ "$keep_roc" -eq 1 ]; then
                # Reuse this large copy rather than doubling temporary storage.
                mv -- "$source/Setup.mpq" "$staging/Setup.mpq" || return 1
            else
                rm -f -- "$source/Setup.mpq"
            fi
        elif [ "$rc" -ne 2 ]; then
            pm_message "Failed reading Setup.mpq from ISO"
            rm -rf -- "$staging"
            return 1
        fi
        if [ "$disc_recognized" -ne 1 ]; then
            pm_message "No new Warcraft III data recognized in ${disc##*/}"
            pm_message "Keeping all ISOs; check that the correct discs were supplied"
            rm -rf -- "$staging"
            return 1
        fi
    done

    # Base assets are essential; the expansion remains optional for RoC-only
    # installations. Check staged files before moving anything into place.
    if [ ! -s "$data/War3.mpq" ] && [ ! -s "$staging/archives/War3.mpq" ]; then
        pm_message "War3.mpq missing. Supply the Reign of Chaos ISO."
        rm -rf -- "$staging"
        return 1
    fi
    if [ "${#images[@]}" -gt 1 ] && [ ! -s "$data/War3x.mpq" ] &&
       [ ! -s "$staging/archives/War3x.mpq" ]; then
        pm_message "War3x.mpq not recovered from the supplied ISOs"
        pm_message "Check your Frozen Throne disc and log.txt"
        rm -rf -- "$staging"
        return 1
    fi
    for candidate in "$staging/archives"/*.mpq; do
        [ -f "$candidate" ] || continue
        if ! "$setupreader" verify "$candidate"; then
            pm_message "Invalid extracted archive: ${candidate##*/}"
            rm -rf -- "$staging"
            return 1
        fi
    done

    for name in "${archives[@]}" Setup.mpq; do
        candidate="$staging/archives/$name"
        [ "$name" = Setup.mpq ] && candidate="$staging/Setup.mpq"
        [ -s "$candidate" ] || continue
        if [ -e "$data/$name" ]; then
            echo "WC3 ISO: preserving existing $name"
        else
            mv -n -- "$candidate" "$data/$name" || {
                pm_message "Cannot install $name"
                rm -rf -- "$staging"
                return 1
            }
        fi
    done

    # Verify the final files, including pre-existing MPQs. Do not delete disc
    # images if a destination archive is absent, truncated or unreadable.
    if [ ! -s "$data/War3.mpq" ] || ! "$setupreader" verify "$data/War3.mpq"; then
        pm_message "Installed War3.mpq failed verification"
        rm -rf -- "$staging"
        return 1
    fi
    for name in "${archives[@]}" Setup.mpq; do
        if [ -e "$data/$name" ] &&
           { [ ! -s "$data/$name" ] || ! "$setupreader" verify "$data/$name"; }; then
            pm_message "Installed $name failed verification"
            rm -rf -- "$staging"
            return 1
        fi
    done
    if [ "${#images[@]}" -gt 1 ] && [ ! -s "$data/War3x.mpq" ]; then
        pm_message "Frozen Throne data incomplete; preserving ISO files"
        rm -rf -- "$staging"
        return 1
    fi

    # Record successfully imported images BEFORE removal; if removal fails or
    # the device is interrupted mid-cleanup, the next launch can finish safely.
    if ! printf '%s\n' "${signatures[@]}" > "$marker.part" ||
       ! mv -f -- "$marker.part" "$marker"; then
        pm_message "Cannot record successful ISO import; preserving ISO files"
        rm -rf -- "$staging"
        return 1
    fi
    rm -rf -- "$staging"
    pm_message "Warcraft III ISO import complete; deleting original ISO files..."
    for disc in "${images[@]}"; do
        if ! rm -- "$disc"; then
            pm_message "Could not delete ${disc##*/}; remove it manually"
        fi
    done
    return 0
}

if ! install; then
    pm_message "See log.txt for ISO import details"
    exit 1
fi

# We launch gptokeyb using this $GPTOKEYB variable as it will take care of sourcing the executable from the central location,
# assign the appropriate exit hotkey dependent on the device (ex. select + start for most devices and minus + start for the
# rgb10) and assign the appropriate method for killing an executable dependent on the OS the port is run from.
# With -c we assign a custom mapping file else gptokeyb will only run as a tool to kill the process.
# For $ANALOG_STICKS we have the ability to supply multiple gptk files to support 1 and 2 analogue stick devices in different ways.
# For a proper documentation how gptokeyb works: [Link](https://github.com/PortsMaster/gptokeyb)
# $GPTOKEYB "openwarcraft3.${DEVICE_ARCH}" -c "$GAMEDIR/open-realm.gptk" &
$GPTOKEYB2 -1 "openwarcraft3.${DEVICE_ARCH}" -c "$GAMEDIR/openrealm-wc3.gptk2.ini" >/dev/null &
#$GPTOKEYB2 -1 "openwow.${DEVICE_ARCH}" -c "$GAMEDIR/openrealm.gptk2.ini" >/dev/null &

# Do some platform specific stuff right before the port is launched but after GPTOKEYB is run.
pm_platform_helper "$GAMEDIR/openwarcraft3.${DEVICE_ARCH}"

# Now we launch the port's executable with multiarch support. Make sure to rename your file according to the architecture you built for. E.g. open-realm.aarch64
#export LIBGL_ES=2
#export LIBGL_LOGSHADERERROR=1
#export LIBGL_SILENTSTUB=0
# try shrink textures to see if worth shrinking them - doesn't seem to improve FPS but astc might still help who knows
# export LIBGL_FORCE16BITS=1
# export LIBGL_SHRINK=1

#./openwarcraft3.${DEVICE_ARCH} -data "$GAMEDIR/gamefiles/warcraft-3"  +menu_single_player_campaign +vid_mode 0
#/opt/muos/bin/strace
./openwarcraft3.${DEVICE_ARCH} -data "$GAMEDIR/gamefiles/warcraft-3" +set vid_native 1 +set vid_fullscreen 1 +set r_profile 0 +r_cursor 1  +r_stats 1
# +set sv_debug_layout 1 +set wc3_harvest_path_debug 2  +set wc3_hero_skill_debug 3

#  +r_entities 1 \
#  +r_msaa 0 \
#  +r_vsync 0 \
#  +r_fogofwar 1 \
#  +r_particles 1 \
#  +r_shadows 0 \


# +set r_cursor 2 \
#  +set g_debug_destructables 1 \
#  +set r_debug_destructables 1 \


#/opt/muos/bin/strace
#./openwow.${DEVICE_ARCH} -data "$GAMEDIR/gamefiles/world-of-warcraft" +vid_mode 0 +set r_profile 0 +set r_cursor 1  \
#   +map 1



# Cleanup any running gptokeyb instances, and any platform specific stuff.
pm_finish
