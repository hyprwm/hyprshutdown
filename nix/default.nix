{
  lib,
  stdenv,
  cmake,
  pkg-config,
  aquamarine,
  cairo,
  glaze,
  hyprgraphics,
  hyprtoolkit,
  hyprutils,
  libdrm,
  pixman,
  version ? "git",
}:
stdenv.mkDerivation {
  pname = "hyprshutdown";
  inherit version;

  src = ../.;

  nativeBuildInputs = [
    cmake
    pkg-config
  ];

  buildInputs = [
    aquamarine
    cairo
    (glaze.override { enableSSL = false; })
    hyprgraphics
    hyprtoolkit
    hyprutils
    libdrm
    pixman
  ];

  meta = {
    homepage = "https://github.com/hyprwm/hyprshutdown";
    description = "A graceful shutdown utility for Hyprland";
    license = lib.licenses.bsd3;
    platforms = lib.platforms.linux;
    mainProgram = "hyprshutdown";
  };
}
