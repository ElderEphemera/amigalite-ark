with import <nixpkgs> {};
rec {
  version = "0.4";
  src = nix-gitignore.gitignoreSource ["*.nix"] ./.;

  raylib-5_5 = raylib.overrideAttrs (final: prev: {
    version = "5.5";

    src = fetchFromGitHub {
      owner = "raysan5";
      repo = "raylib";
      rev = "5.5";
      hash = "sha256-J99i4z4JF7d6mJNuJIB0rHNDhXJ5AEkG0eBvvuBLHrY=";
    };

    patches = [];

    cmakeFlags = prev.cmakeFlags ++ [
      "-DSUPPORT_FILEFORMAT_JPG=ON"
      "-DCMAKE_BUILD_TYPE=Release"
    ];

    postInstall = ''
      cp $src/src/rcamera.h $out/include
    '';
  });

  raylib-5_5-web = raylib-5_5.overrideAttrs (prev: {
    buildInputs = prev.buildInputs ++ [ emscripten ];
    cmakeFlags = prev.cmakeFlags ++ [ "-DPLATFORM=Web" ];

    configurePhase = ''
      HOME=$TMPDIR
      emcmake cmake . -B build $cmakeFlags -DCMAKE_INSTALL_PREFIX=$out
    '';

    buildPhase = ''
      emmake make -C build
    '';

    installPhase = ''
      mkdir -p $out
      emmake cmake --install build/raylib
      runHook postInstall
    '';

    dontStrip = true;
  });

  desktop = stdenv.mkDerivation {
    pname = "amigalite-ark";
    inherit version src;
    nativeBuildInputs = [ cmake ];
    buildInputs = [ raylib-5_5 ];
    cmakeFlags = [ "-DPLATFORM=Desktop" "-DCMAKE_BUILD_TYPE=Release" ];
  };

  web-exe = desktop.overrideAttrs (prev: {
    nativeBuildInputs = prev.nativeBuildInputs ++ [ emscripten ];
    buildInputs = [ raylib-5_5-web ];
    cmakeFlags = [ "-DPLATFORM=Web" "-DCMAKE_BUILD_TYPE=Release" ];

    configurePhase = ''
      HOME=$TEMPDIR
      emcmake cmake . -B build $cmakeFlags -DCMAKE_FIND_ROOT_PATH="${raylib-5_5-web}"
    '';

    buildPhase = ''
      emmake make -C build
    '';

    installPhase = ''
      mkdir -p $out
      cp build/amigalite-ark.js build/amigalite-ark.wasm $out/
    '';
  });

  web = stdenv.mkDerivation {
    pname = "amigalite-ark-web";
    inherit version src;
    nativeBuildInputs = [ python3 ];

    buildPhase = ''
      mkdir -p $out

      cp ${web-exe}/* $out

      find assets -type f -exec bash -c '${emscripten}/share/emscripten/tools/file_packager \
        "$out/''${0//\//.}.data" \
        --preload "$0" \
        --js-output="$out/''${0//\//.}.js" \
      ' {} \;

      cp index.html $out
    '';

    dontInstall = true;
  };
}
