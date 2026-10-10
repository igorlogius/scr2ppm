{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  name = "scr2ppm-flatpak";

  packages = with pkgs; [
    appstream  # provides appstreamcli, used by flatpak-builder
    cmake
    desktop-file-utils
    flatpak
    flatpak-builder
    gcc
    git        # needed for the git sources (glm, slop)
    glew
    glm
    gnumake
    icu
    libx11
    libxext
    libxrender
    mesa
    pkg-config
  ];

  shellHook = ''
    # use a per-user flatpak installation, no root needed
    flatpak remote-add --user --if-not-exists flathub \
      https://dl.flathub.org/repo/flathub.flatpakrepo
    echo "flatpak shell ready - run ./build-flatpak.sh"
  '';
}
