{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    systems.url = "github:nix-systems/default";
  };

  outputs = { self, nixpkgs, systems }:
    let eachSystem = nixpkgs.lib.genAttrs (import systems);
    in {
      devShells = eachSystem (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default =
            pkgs.mkShell { packages = [ pkgs.gnumake ]; };
        });

      packages = eachSystem (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.callPackage ./default.nix { };
          mingw = pkgs.pkgsCross.mingw32.callPackage ./default.nix { };
        });
    };
}

