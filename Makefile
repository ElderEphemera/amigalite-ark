.PHONY: linux run-linux web host-web

####################
## Linux          ##
####################

build-linux: CMakeLists.txt shell.nix
	nix-shell --run 'cmake -B build-linux'

build-linux/ark: build-linux main.c FastNoiseLite.h
	nix-shell --run 'cmake --build build-linux'

linux: build-linux/ark

run-linux: build-linux/ark
	nix-shell --run 'build-linux/ark'

####################
## Web            ##
####################

build-web: CMakeLists.txt shell.nix
	nix-shell --run 'emcmake cmake -B build-web -DPLATFORM=Web -DCMAKE_BUILD_TYPE=Release'

build-web/ark.html: build-web assets main.c FastNoiseLite.h
	nix-shell --run 'emmake make -C build-web'

web: build-web/ark.html

host-web: build-web/ark.html
	nix-shell -p busybox --run 'httpd -f -v -p 8080 -h build-web'
