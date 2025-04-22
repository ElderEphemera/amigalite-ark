with import <nixpkgs> {};
stdenv.mkDerivation {
  name = "env";
  buildInputs = [
    cmake
    libGL xorg.libX11 xorg.libXcursor xorg.libXi xorg.libXinerama xorg.libXrandr
    emscripten
  ];
}
