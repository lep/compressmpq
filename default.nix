{ pkgs, lib, stdenv, ... }:
stdenv.mkDerivation {
  name = "compress-mpq";
  src = ./.;
  makeFlags = [ "PREFIX=$(out)" ]
    ++ lib.optionals stdenv.hostPlatform.isWindows [ "release" ];
  buildInputs =
    lib.optional stdenv.hostPlatform.isWindows [ pkgs.windows.pthreads ];
  meta.mainProgram = "compress-mpq";
}
