# GTA VI — Multijoueur co-op (v1)

Co-op à **2 joueurs** : vous vous voyez bouger et tirer en temps réel dans la
**même ville vivante**. Le **monde** (trafic, PNJ, police, cycle jour/nuit) est
**host-autoritaire** → identique sur les deux écrans.

> ⚠️ v1 : co-op **à pied** (la conduite synchronisée arrive en v2). Pas encore
> de dégâts entre joueurs (PvP) — vous coopérez contre les PNJ/police.

## Modes de lancement

| Commande | Rôle |
|----------|------|
| `./gta6_3d` | Solo (inchangé) |
| `./gta6_3d --host` | Tu **joues ET héberges** (port 7777) |
| `./gta6_3d --connect <IP> --port 7777` | Ton ami **rejoint** ton partie |
| `GTA6_SERVER=<IP>:7777 ./gta6_3d` | idem (variable d'env) |
| `./gta6_3d --server --port 7777` | Serveur **dédié headless** (sans fenêtre, conteneurisable) |

## Méthode recommandée : Tailscale (le plus simple, gratuit, UDP/TCP)

1. Toi **et** ton ami installez [Tailscale](https://tailscale.com) et connectez-vous au **même compte** (ou partagez le réseau).
2. Récupère ton IP Tailscale : `tailscale ip -4` → ex. `100.101.102.103`.
3. **Toi (hôte)** : `./gta6_3d --host`
4. **Ton ami** : `./gta6_3d --connect 100.101.102.103 --port 7777`

C'est tout — pas de redirection de port, ça marche à distance.

## Alternative : ngrok (tunnel TCP)

1. **Toi** : `./gta6_3d --host` puis dans un autre terminal `ngrok tcp 7777`.
2. ngrok affiche `tcp://5.tcp.eu.ngrok.io:18923` (exemple).
3. **Ton ami** : `./gta6_3d --connect 5.tcp.eu.ngrok.io --port 18923`

> ngrok gratuit autorise 1 tunnel TCP — suffisant ici.

## Serveur dédié en conteneur (optionnel, jouer à distance sans laisser ton PC hôte)

```bash
docker build -f Dockerfile.server -t gta6-server .
docker run --rm -p 7777:7777 gta6-server
```
Puis chacun se connecte avec `--connect <IP-du-serveur> --port 7777`
(via Tailscale sur la machine du serveur, ou un VPS à IP publique).

## Compiler le client sur l'autre machine

Le binaire `gta6_3d` est compilé pour **macOS**. Ton ami doit recompiler pour son OS :

**macOS** :
```bash
clang++ -std=c++17 -O2 gta6_3d.cpp -o gta6_3d \
  -I"$(brew --prefix raylib)/include" -L"$(brew --prefix raylib)/lib" -lraylib \
  -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL
```

**Linux** (raylib installé) :
```bash
g++ -std=c++17 -O2 gta6_3d.cpp -o gta6_3d -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
```

### 🪟 Ton ami est sur Windows

Le code est **cross-platform** (sockets Winsock gérés automatiquement). Deux options :

**Option A — exe natif via MSYS2/MinGW (recommandé)**
1. Installer [MSYS2](https://www.msys2.org), ouvrir le terminal **« MSYS2 MINGW64 »**.
2. Installer les dépendances :
   ```bash
   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-raylib
   ```
3. Compiler (depuis le dossier contenant `gta6_3d.cpp`) :
   ```bash
   g++ -std=c++17 -O2 gta6_3d.cpp -o gta6_3d.exe \
       -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32
   ```
4. Lancer / rejoindre :
   ```bash
   ./gta6_3d.exe --connect <IP-de-l-hote> --port 7777
   ```
   (double-clic sur `gta6_3d.exe` = solo)

**Option B — WSL2 + WSLg (Windows 11, sans MSYS2)**
1. Dans Windows : `wsl --install` (installe Ubuntu + WSLg pour l'affichage GUI).
2. Dans le terminal Ubuntu (WSL) :
   ```bash
   sudo apt update && sudo apt install -y build-essential git cmake \
     libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
   git clone --depth 1 -b 5.5 https://github.com/raysan5/raylib && \
     cmake -S raylib -B raylib/build -DBUILD_EXAMPLES=OFF && \
     sudo cmake --build raylib/build -j --target install && sudo ldconfig
   g++ -std=c++17 -O2 gta6_3d.cpp -o gta6_3d -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
   ./gta6_3d --connect <IP-de-l-hote> --port 7777
   ```
   La fenêtre du jeu s'affiche via WSLg.

> ⚠️ Sur Windows, pense à **autoriser `gta6_3d.exe` dans le pare-feu** s'il héberge,
> et — si vous passez par ngrok/Tailscale — c'est ton ami qui se **connecte**, donc
> aucune config pare-feu côté Windows n'est nécessaire pour rejoindre.

## Détails techniques

- Transport : **TCP** length-prefixé (compatible ngrok-free **et** Tailscale).
- Le **host** envoie un *snapshot* du monde (temps, voitures, PNJ) ~60 Hz ; le
  **client** envoie sa position/visée/tir et **n'exécute pas** la simulation du monde.
- Tirs du client : appliqués **côté host** (autoritaire) sur PNJ/voitures ;
  tracer visuel des deux côtés.
- Géométrie statique (immeubles/routes) générée du même seed → aucune donnée de
  carte transmise.

## Feuille de route v2
- Conduite synchronisée (voitures comme entités réseau).
- PvP (dégâts entre joueurs) + respawn synchronisé.
- Serveur dédié à **2 clients** (les deux humains distants).
