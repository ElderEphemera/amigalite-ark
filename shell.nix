with import <nixpkgs> {};
stdenv.mkDerivation {
  name = "env";
  nativeBuildInputs = [ cmake emscripten ];
  buildInputs = raylib.buildInputs ++ raylib.propagatedBuildInputs;
  LD_LIBRARY_PATH = lib.makeLibraryPath [ alsa-lib libpulseaudio ];
}
