{
    inputs = {
        nixpkgs.url = "github:NixOS/nixpkgs";
        flake-utils.url = "github:numtide/flake-utils";
    };

    outputs = { self, nixpkgs, flake-utils }:
        flake-utils.lib.eachDefaultSystem (system:
            let pkgs = import nixpkgs { inherit system; };
                packageName = "compress-mpq";
                buildInputs = [
                    pkgs.gnumake
                ];

                drv-args = {
                        name = packageName;
                        src = self;
                        inherit buildInputs;

                        buildPhase = "make compress-mpq";
                        installPhase = ''
                            mkdir -p $out/bin
                            install -t $out/bin compress-mpq
                        '';
                    };
		drv = pkgs.stdenv.mkDerivation drv-args;
            in rec {
		apps.default = {
		    type = "app";
		    program = "${drv}/bin/compress-mpq";
		};
		apps.compressmpq = apps.default;
                packages = {
		    default = drv;
                    ${packageName} = drv;
                };

                defaultPackage = packages.${packageName};
            }
        );
}
    
