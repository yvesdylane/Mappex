#!/usr/bin/env bash
#
# Mappex installer — fetches the source from its git repo, builds it, installs it
# to /opt/Mappex and creates a desktop launcher that opens the GUI.
#
# Usage:
#   ./install.sh                                    # installs from the official repo
#   REPO_URL=https://github.com/you/Mappex ./install.sh   # override the source
#   BRANCH=main ./install.sh                        # override the source branch
#
# Only part 1 (system packages) and the /opt copy need root; the script uses
# `sudo` only for those and installs the launcher into your own session.

set -euo pipefail

# ---------------------------------------------------------------- config ----

REPO_URL="${REPO_URL:-https://github.com/yvesdylane/Mappex}"
BRANCH="${BRANCH:-main}"
APP_NAME="Mappex"
INSTALL_DIR="${INSTALL_DIR:-/opt/Mappex}"
ICON_PATH="$INSTALL_DIR/assets/mappex.svg"
SKIP_DEPS="${SKIP_DEPS:-0}"   # set to 1 to skip system-package installation (staging/CI)

# ----------------------------------------------------------------- helper ----

say()  { printf '\033[1;34m[mappex]\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[  OK  ]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[ WARN ]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[ FAIL ]\033[0m %s\n' "$*" >&2; exit 1; }

need_sudo() { sudo -v 2>/dev/null || die "sudo is required for the system parts of the install."; }

# Install build/runtime dependencies. Supports Fedora/RHEL (dnf) and Debian/Ubuntu (apt).
install_deps() {
    if command -v dnf >/dev/null 2>&1; then
        say "Installing build dependencies via dnf..."
        sudo dnf install -y gcc-c++ cmake pkgconf libevdev-devel readline-devel \
            systemd-devel nlohmann-json-devel glfw-devel mesa-libGL-devel git
    elif command -v apt-get >/dev/null 2>&1; then
        say "Installing build dependencies via apt..."
        sudo apt-get update
        sudo apt-get install -y g++ cmake pkg-config libevdev-dev libreadline-dev \
            libudev-dev nlohmann-json3-dev libglfw3-dev libgl1-mesa-dev git
    else
        die "No supported package manager found (need dnf or apt-get)."
    fi
}

# ------------------------------------------------------------------- main ----

main() {
    if [[ "$(id -u)" -eq 0 ]]; then
        warn "Running as root — the launcher will be created for the current root user."
    fi

    need_sudo
    if [[ "$SKIP_DEPS" != "1" ]]; then
        install_deps
    else
        warn "SKIP_DEPS=1 — skipping system package installation."
    fi

    # --- fetch --------------------------------------------------------------
    WORK_DIR="$(mktemp -d)"
    trap 'rm -rf "${WORK_DIR:-}"' EXIT
    local work="$WORK_DIR"
    say "Cloning $REPO_URL (branch $BRANCH)..."
    git clone --depth 1 --branch "$BRANCH" --recursive "$REPO_URL" "$work/mappex" \
        || die "Failed to clone the repository. Check the URL and your git access."
    ok "Source fetched."

    # --- build --------------------------------------------------------------
    say "Building $APP_NAME (this can take a minute)..."
    (
        cd "$work/mappex"
        cmake -B build -DCMAKE_BUILD_TYPE=Release
        cmake --build build -j"$(nproc)"
    ) || die "Build failed."
    [[ -x "$work/mappex/build/$APP_NAME" ]] || die "Build finished but $APP_NAME binary is missing."
    ok "Build complete."

    # --- install tree -------------------------------------------------------
    say "Installing to $INSTALL_DIR..."
    sudo mkdir -p "$INSTALL_DIR"
    sudo install -m 0755 "$work/mappex/build/$APP_NAME" "$INSTALL_DIR/$APP_NAME"
    if [[ -d "$work/mappex/mappings" ]]; then
        sudo rm -rf "$INSTALL_DIR/mappings"
        sudo cp -r "$work/mappex/mappings" "$INSTALL_DIR/mappings"
    fi
    if [[ -d "$work/mappex/devices" ]]; then
        sudo mkdir -p "$INSTALL_DIR/devices"
        sudo cp -n "$work/mappex"/devices/* "$INSTALL_DIR/devices/" 2>/dev/null || true
    fi
    sudo mkdir -p "$INSTALL_DIR/assets"
    sudo install -m 0644 "$work/mappex/assets/mappex.svg" "$ICON_PATH" 2>/dev/null \
        || warn "Icon not bundled; the launcher will use the fallback icon."
    sudo install -m 0644 "$work/mappex/README.md" "$INSTALL_DIR/README.md" 2>/dev/null || true
    ok "Installed to $INSTALL_DIR."

    # --- kernel + permissions -----------------------------------------------
    if [[ ! -e /dev/uinput ]]; then
        say "Loading the uinput kernel module..."
        sudo modprobe uinput 2>/dev/null || true
        echo uinput | sudo tee /etc/modules-load.d/mappex-uinput.conf >/dev/null
    fi

    local user="${SUDO_USER:-$(id -un)}"
    if ! grep -qE "^(input:)(.*)$" <(getent group input) 2>/dev/null; then
        sudo groupadd -f input
    fi
    if id -nG "$user" 2>/dev/null | grep -q '\binput\b'; then
        ok "User '$user' is already in the 'input' group."
    else
        sudo usermod -aG input "$user"
        warn "Added '$user' to the 'input' group — log out and back in for device access."
    fi

    # --- launcher -----------------------------------------------------------
    say "Creating the desktop launcher..."
    local apps_dir="$HOME/.local/share/applications"
    mkdir -p "$apps_dir"
    cat > "$apps_dir/${APP_NAME,,}.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$APP_NAME
GenericName=Controller Remapper
Comment=Turn any gamepad into a virtual Xbox 360 pad
Exec=$INSTALL_DIR/$APP_NAME --gui
Icon=$ICON_PATH
Terminal=false
Categories=Game;Utility;
Keywords=gamepad;controller;remap;xbox;
EOF
    command -v update-desktop-database >/dev/null 2>&1 \
        && update-desktop-database "$apps_dir" >/dev/null 2>&1 || true
    ok "Launcher created: $apps_dir/${APP_NAME,,}.desktop"

    # CLI convenience symlink (terminal mode)
    mkdir -p "$HOME/.local/bin"
    ln -sfn "$INSTALL_DIR/$APP_NAME" "$HOME/.local/bin/${APP_NAME,,}"

    say "Done! Find Mappex in your app menu, or run:"
    say "    ${APP_NAME,,} --gui        # desktop app"
    say "    ${APP_NAME,,} --terminal   # terminal mode"
}

main "$@"