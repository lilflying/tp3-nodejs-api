#!/usr/bin/env bash
# Build & lance GTA VI 3D (raylib)
set -e

RAYLIB=$(brew --prefix raylib)

echo "Compilation de GTA VI 3D..."
clang++ -std=c++17 -O2 gta6_3d.cpp -o gta6_3d \
  -I"$RAYLIB/include" -L"$RAYLIB/lib" -lraylib \
  -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL

echo "Compilation terminee."
echo "  Solo   : ./gta6_3d"
echo "  Hote   : ./gta6_3d --host"
echo "  Rejoindre : ./gta6_3d --connect <IP> --port 7777   (voir MULTIJOUEUR.md)"
echo "Lancement (solo)..."
./gta6_3d
