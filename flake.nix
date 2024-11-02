{
  inputs = { systems.url = "github:nix-systems/default"; };

  outputs = { self, nixpkgs, systems }:
    let
      eachSystem = nixpkgs.lib.genAttrs (import systems);
      compress-mpq-drv = pkgs:
        pkgs.stdenv.mkDerivation {
          name = "compress-mpq";
          src = self;
          buildInputs = [ pkgs.gnumake ];
          buildPhase = "make compress-mpq";
          installPhase = "install -Dt $out/bin compress-mpq";
        };
    in rec {
      packages = eachSystem (system: rec {
        compress-mpq = compress-mpq-drv (import nixpkgs { inherit system; });
        default = compress-mpq;
      });

      apps = eachSystem (system: rec {
        compress-mpq = {
          program = "${packages.${system}.compress-mpq}/bin/compress-mpq";
          type = "app";
        };

        default = compress-mpq;
      });
    };
}

