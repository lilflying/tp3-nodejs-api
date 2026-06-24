/*
 * GTA VI - Vice City 3D  —  NEXTGEN EDITION v4
 * Vraie application 3D (fenetre OpenGL via raylib + shaders GLSL)
 * Developpe par Claude.
 *
 * ─── NOUVEAUTES v4 ───────────────────────────────────────────────────────────
 *  RENDU
 *   + Cycle jour/nuit dynamique (~10 min) : soleil/lune, ciel interpole,
 *     golden hour rose/cyan, etoiles, lumieres de ville la nuit
 *   + Pipeline post-process : sceneRT HDR -> bloom (bright+blur) -> ecran
 *   + Fog de distance (dans le shader de scene)
 *   + Shadow mapping directionnel du soleil (depth pass + PCF)
 *  MONDE VIVANT
 *   + Trafic IA sur graphe de voies (feux, espacement, virages)
 *   + Pietons sur trottoirs avec navigation + panique
 *   + Streaming spawn/despawn par distance (perfs bornees)
 *  GAMEPLAY
 *   + Police (etoiles) : poursuite, barrages, helico aux etoiles elevees
 *   + Physique vehicule : accel/frein/marche AR, grip + derapage (frein a main),
 *     dégats visuels (fumee/teinte), traces de pneus
 *   + Combat : raycast + hitmarker, damage numbers, screen shake, recul, reload
 *
 * ─── CONTROLES ──────────────────────────────────────────────────────────────
 *   ZQSD / fleches : marcher / conduire     SOURIS : viser / camera
 *   CLIC GAUCHE : tirer    R : recharger    1/2/3 : armes
 *   F : voiture   MAJ : sprint   ESPACE : saut (a pied) / frein a main (voiture)
 *   T : se debloquer (reset position)        ECHAP : quitter
 */

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
// ── reseau (co-op TCP, cross-platform) ──
#include <cstring>
#include <cstdint>
#include <csignal>
#include <cerrno>
#include <chrono>
#include <thread>
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET socket_t;
  #define CLOSESOCK closesocket
  #define SOCK_INVALID INVALID_SOCKET
  #define RX_FLAGS 0
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  typedef int socket_t;
  #define CLOSESOCK close
  #define SOCK_INVALID (-1)
  #define RX_FLAGS MSG_DONTWAIT
#endif

// ─── CONFIG ──────────────────────────────────────────────────────────────────
static const int   SCREEN_W = 1280, SCREEN_H = 720;
static const float WORLD  = 200.0f;
static const int   GRID   = 8;
static const float CELL   = (WORLD*2.0f)/GRID;
static const float ROADW  = 12.0f;
static const int   SHADOWRES = 2048;
static const float DAYLEN = 600.0f;     // 10 min = 24 h
static const float STREAM_R = 165.0f;   // rayon de streaming

// ─── STRUCTURES ──────────────────────────────────────────────────────────────
struct Box { Vector3 c, h; };
struct Building { Vector3 pos, size; Color color, topColor; bool enterable; int doorSide; };
struct Tree { Vector3 pos; float height, radius; };
struct Lamp { Vector3 pos; };

struct Car {
    Vector3 pos, vel;          // vel = vitesse monde (pour derapage)
    float   angle, speed;
    Color   color;
    bool    occupied, isPolice, isTraffic;
    int     type;
    float   health; bool alive;
    float   sirenPhase, wreckTimer, smokeCd;
    // trafic IA
    int     node, next, prevNode; float t; float laneSide;
};

struct Pedestrian {
    Vector3 pos, vel; float facing;
    Color   shirt, pants, skin, hair;
    float   bob; bool alive;
    float   health, deadTimer, fall, fallDir;
    bool    isPolice; float shootCd, fleeTimer;
    int     navNode; float navCd;
};

struct Bullet { Vector3 a, b; float life; Color col; };
struct Particle { Vector3 pos, vel; Color col; float life, size; bool grav; };
struct Mark { Vector3 pos; float life, ang; };
struct DamageNum { Vector3 pos; float life; int amount; Color col; };
struct Heli { Vector3 pos; float rotor, angle, shootCd; bool active; };

// ─── ETAT GLOBAL ─────────────────────────────────────────────────────────────
static std::vector<Building>   gBuildings;
static std::vector<Box>        gColliders;
static std::vector<Tree>       gTrees;
static std::vector<Lamp>       gLamps;
static std::vector<Car>        gCars;
static std::vector<Pedestrian> gPeds;
static std::vector<Bullet>     gBullets;
static std::vector<Particle>   gParticles;
static std::vector<Mark>       gMarks;
static std::vector<DamageNum>  gDmgNums;
static Heli gHeli = {0};

static Vector3 gPlayerPos = {0,1,0};
static float gPlayerYaw=0, gPlayerPitch=-0.1f, gPlayerVelY=0; static bool gOnGround=true;
static bool gThirdPerson=false;      // vue 3e personne a pied (touche V)
static const float EYE_H=0.75f;      // hauteur des yeux au-dessus de la base du corps

// ── multijoueur ──
static int gNetMode=0;               // 0 solo, 1 host, 2 client
static socket_t gListenFd=SOCK_INVALID, gConnFd=SOCK_INVALID; // sockets
static bool gPeerConnected=false;
// avatar de l'autre joueur (host->client = host player, client->host = client player)
struct Remote { Vector3 pos; float yaw,pitch; int inCar; float health; int weapon; bool alive, shoot; Vector3 shotA,shotB; float shotTimer; };
static Remote gRemote = { {0,1,0},0,0,-1,100,0,true,false,{0,0,0},{0,0,0},0 };
static bool gShotFired=false; static Vector3 gShotA={0,0,0}, gShotB={0,0,0};   // dernier tir local (envoye au host)
static int gInCar=-1, gStars=0, gMoney=500, gKills=0;
static float gHealth=100, gArmor=0, gWantedTimer=0;
static bool gDead=false; static float gDeadTimer=0, gMuzzle=0, gHurtFx=0, gPoliceSpawnCd=0;
static float gShake=0, gHitmark=0, gReload=0, gRoadblockCd=0;
static float gTime=9.0f;             // heure du jour [0,24)
static float gLight=0;               // timer feux de circulation

static int gWeapon=0;
struct Weapon { const char* name; int dmg; float fireRate; int ammo, maxAmmo; float range, spread, recoil; };
static Weapon gWeapons[3] = {
    { "PISTOLET", 34, 0.28f, 60,  60,  120, 0.010f, 0.020f },
    { "UZI",      18, 0.07f, 200, 200, 90,  0.045f, 0.012f },
    { "FUSIL",    75, 0.85f, 30,  30,  160, 0.004f, 0.045f },
};
static float gFireCd=0;

static Sound gSndGun, gSndThud, gSndHurt, gSndBoom; static bool gAudioOK=false;

// ── rendu ──
static RenderTexture2D gSceneRT, gBrightRT, gBlurRT[2], gShadowRT;
static Shader gScene, gDepth, gBright, gBlur;
static int slcLightVP, slcSun, slcSunCol, slcAmb, slcView, slcFog, slcFogCol, slcShadow, slcShadowRes;
static int blcDir, blcSize;
// ambiance courante (calculee chaque frame)
static Vector3 gSunDir, gSunCol, gAmbCol, gFogCol; static float gSunHeight, gNight;
static Vector3 gStars3D[160];

// ─── UTILS ───────────────────────────────────────────────────────────────────
static float frand(float a,float b){ return a+(float)rand()/(float)0x7fffffff*(b-a); }
static Color randCity(){ Color p[]={{90,110,150,255},{120,90,140,255},{200,120,90,255},{80,140,160,255},
    {150,150,170,255},{180,100,120,255},{100,160,140,255},{160,140,90,255}}; return p[rand()%8]; }
static Color randSkin(){ Color p[]={{245,210,175,255},{225,180,140,255},{190,140,100,255},{150,110,80,255},{110,80,55,255}}; return p[rand()%5]; }
static Color randHair(){ Color p[]={{30,25,20,255},{90,60,30,255},{160,120,60,255},{200,180,140,255},{180,180,180,255},{150,40,40,255}}; return p[rand()%6]; }
static Vector3 lerp3(Vector3 a,Vector3 b,float t){ return { a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t }; }

// ─── SHADERS (GLSL 330) ──────────────────────────────────────────────────────
static const char* VS_SCENE = R"(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor;
uniform mat4 mvp; uniform mat4 matModel; uniform mat4 matNormal;
out vec2 fragTexCoord; out vec4 fragColor; out vec3 fragPosition; out vec3 fragNormal;
void main(){
  fragTexCoord=vertexTexCoord; fragColor=vertexColor;
  fragPosition=vec3(matModel*vec4(vertexPosition,1.0));
  fragNormal=normalize(vec3(matNormal*vec4(vertexNormal,0.0)));
  gl_Position=mvp*vec4(vertexPosition,1.0);
})";
static const char* FS_SCENE = R"(#version 330
in vec2 fragTexCoord; in vec4 fragColor; in vec3 fragPosition; in vec3 fragNormal;
uniform sampler2D texture0; uniform vec4 colDiffuse;
uniform vec3 sunDir, sunColor, ambient, viewPos, fogColor; uniform float fogDensity;
uniform mat4 lightVP; uniform sampler2D shadowMap; uniform int shadowRes;
out vec4 finalColor;
void main(){
  vec4 base = texture(texture0,fragTexCoord)*colDiffuse*fragColor;
  vec3 n=normalize(fragNormal); vec3 l=normalize(-sunDir);
  float ndl=max(dot(n,l),0.0);
  // shadow PCF
  vec4 lp=lightVP*vec4(fragPosition,1.0); vec3 pc=lp.xyz/lp.w; pc=pc*0.5+0.5;
  float sh=1.0;
  if(pc.z<=1.0 && pc.x>=0.0&&pc.x<=1.0&&pc.y>=0.0&&pc.y<=1.0){
    float bias=max(0.0016*(1.0-ndl),0.0004); float s=0.0; float tx=1.0/float(shadowRes);
    for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++){
      float d=texture(shadowMap,pc.xy+vec2(x,y)*tx).r; s+=(pc.z-bias>d)?0.0:1.0; }
    sh=s/9.0;
  }
  vec3 lit=base.rgb*(ambient + sunColor*ndl*sh);
  float dist=length(viewPos-fragPosition);
  float f=clamp(1.0-exp(-fogDensity*dist),0.0,1.0);
  finalColor=vec4(mix(lit,fogColor,f), base.a);
})";
static const char* VS_DEPTH = R"(#version 330
in vec3 vertexPosition; uniform mat4 mvp;
void main(){ gl_Position=mvp*vec4(vertexPosition,1.0); })";
static const char* FS_DEPTH = R"(#version 330
out vec4 c; void main(){ c=vec4(1.0); })";
static const char* FS_BRIGHT = R"(#version 330
in vec2 fragTexCoord; uniform sampler2D texture0; out vec4 finalColor;
void main(){ vec3 c=texture(texture0,fragTexCoord).rgb;
  float b=dot(c,vec3(0.2126,0.7152,0.0722)); float t=0.72;
  finalColor=vec4((b>t)?c*((b-t)/(1.0-t)):vec3(0.0),1.0); })";
static const char* FS_BLUR = R"(#version 330
in vec2 fragTexCoord; uniform sampler2D texture0; uniform vec2 dir; uniform vec2 sz; out vec4 finalColor;
void main(){ vec2 tx=1.0/sz; float w[5]=float[](0.227,0.194,0.121,0.054,0.016);
  vec3 c=texture(texture0,fragTexCoord).rgb*w[0];
  for(int i=1;i<5;i++){ vec2 o=dir*tx*float(i);
    c+=texture(texture0,fragTexCoord+o).rgb*w[i]; c+=texture(texture0,fragTexCoord-o).rgb*w[i]; }
  finalColor=vec4(c,1.0); })";

// ─── AUDIO ───────────────────────────────────────────────────────────────────
static Sound makeNoise(float dur,float decay,float vol){ int sr=22050,n=(int)(sr*dur);
    Wave w={0}; w.frameCount=(unsigned)n; w.sampleRate=sr; w.sampleSize=16; w.channels=1;
    short* d=(short*)malloc(sizeof(short)*n);
    for(int i=0;i<n;i++){ float t=(float)i/n,e=expf(-t*decay); int v=(int)(frand(-1,1)*e*vol*32767);
        if(v>32767)v=32767; if(v<-32768)v=-32768; d[i]=(short)v; }
    w.data=d; Sound s=LoadSoundFromWave(w); UnloadWave(w); return s; }
static void playSnd(Sound s){ if(gAudioOK)PlaySound(s); }

// ─── GRAPHE DE VOIES ─────────────────────────────────────────────────────────
static Vector3 nodePos(int i,int j){ return { -WORLD+CELL*i, 0, -WORLD+CELL*j }; }
static int nodeId(int i,int j){ return j*(GRID+1)+i; }
static void nodeIJ(int id,int& i,int& j){ i=id%(GRID+1); j=id/(GRID+1); }
static int nodeNeighbor(int id,int dir){ // 0=+x 1=-x 2=+z 3=-z
    int i,j; nodeIJ(id,i,j);
    if(dir==0)i++; else if(dir==1)i--; else if(dir==2)j++; else j--;
    if(i<0||i>GRID||j<0||j>GRID)return -1; return nodeId(i,j); }

// ─── MURS BATIMENT ───────────────────────────────────────────────────────────
static void buildingWalls(const Building& b, std::vector<Box>& out){
    if(!b.enterable){ out.push_back({ b.pos, {b.size.x/2,b.size.y/2,b.size.z/2} }); return; }
    float sx=b.size.x/2, sz=b.size.z/2, h=b.size.y, t=0.6f, g=3.8f, cy=h/2, cx=b.pos.x, cz=b.pos.z;
    for(int side=0;side<4;side++){ bool door=(side==b.doorSide);
        if(side<2){ float zc=cz+(side==0?sz:-sz);
            if(!door)out.push_back({{cx,cy,zc},{sx,h/2,t/2}});
            else if(sx>g/2){ float hw=(sx-g/2)/2;
                out.push_back({{cx-(sx+g/2)/2,cy,zc},{hw,h/2,t/2}});
                out.push_back({{cx+(sx+g/2)/2,cy,zc},{hw,h/2,t/2}}); }
        } else { float xc=cx+(side==2?sx:-sx);
            if(!door)out.push_back({{xc,cy,cz},{t/2,h/2,sz}});
            else if(sz>g/2){ float hz=(sz-g/2)/2;
                out.push_back({{xc,cy,cz-(sz+g/2)/2},{t/2,h/2,hz}});
                out.push_back({{xc,cy,cz+(sz+g/2)/2},{t/2,h/2,hz}}); } }
    }
}

// ─── COLLISIONS ──────────────────────────────────────────────────────────────
static bool collides(Vector3 p,float r){
    for(auto& b:gColliders) if(fabsf(p.x-b.c.x)<b.h.x+r && fabsf(p.z-b.c.z)<b.h.z+r) return true;
    for(auto& t:gTrees){ float dx=p.x-t.pos.x,dz=p.z-t.pos.z; if(dx*dx+dz*dz<(t.radius+r)*(t.radius+r))return true; }
    if(fabsf(p.x)>WORLD-2||fabsf(p.z)>WORLD-2) return true; return false;
}

// ─── PARTICULES ──────────────────────────────────────────────────────────────
static void spawnBlood(Vector3 a,int n){ for(int i=0;i<n;i++){ Particle p;p.pos=a;
    p.vel={frand(-3,3),frand(2,6),frand(-3,3)}; p.col={(unsigned char)frand(140,200),0,0,255};
    p.life=frand(0.5f,1.2f); p.size=frand(0.1f,0.3f); p.grav=true; gParticles.push_back(p);} }
static void spawnSpark(Vector3 a){ for(int i=0;i<6;i++){ Particle p;p.pos=a;
    p.vel={frand(-2,2),frand(0,3),frand(-2,2)}; p.col={255,(unsigned char)frand(180,240),80,255};
    p.life=frand(0.15f,0.4f); p.size=frand(0.05f,0.15f); p.grav=true; gParticles.push_back(p);} }
static void spawnSmoke(Vector3 a){ Particle p;p.pos=a; p.vel={frand(-.5f,.5f),frand(1.5f,3),frand(-.5f,.5f)};
    unsigned char g=(unsigned char)frand(40,90); p.col={g,g,g,200}; p.life=frand(.8f,1.6f); p.size=frand(.4f,.9f); p.grav=false; gParticles.push_back(p); }
static void spawnExplosion(Vector3 a){ for(int i=0;i<30;i++){ Particle p;p.pos=a;
    p.vel={frand(-6,6),frand(2,9),frand(-6,6)}; p.col={255,(unsigned char)frand(80,200),30,255};
    p.life=frand(.4f,1.0f); p.size=frand(.2f,.6f); p.grav=true; gParticles.push_back(p);} for(int i=0;i<12;i++)spawnSmoke(a); gShake=fmaxf(gShake,0.8f); }

// ─── MORT PNJ ────────────────────────────────────────────────────────────────
static void killPed(Pedestrian& p,bool byPlayer){ if(!p.alive)return;
    p.alive=false; p.deadTimer=8; p.fall=0; spawnBlood({p.pos.x,1,p.pos.z},18);
    if(byPlayer){ gKills++; gMoney+=p.isPolice?50:10; int add=p.isPolice?2:1; gStars=(gStars+add>5)?5:gStars+add; gWantedTimer=22; }
    for(auto& o:gPeds) if(o.alive&&!o.isPolice&&Vector3Distance(o.pos,p.pos)<25) o.fleeTimer=6;
}

// ─── TIR ─────────────────────────────────────────────────────────────────────
static Vector3 camDir(){ return { cosf(gPlayerYaw)*cosf(gPlayerPitch), sinf(gPlayerPitch), sinf(gPlayerYaw)*cosf(gPlayerPitch) }; }
static void addDmgNum(Vector3 at,int amt,Color c){ if(gDmgNums.size()<40) gDmgNums.push_back({at,1.0f,amt,c}); }
static void playerShoot(){ Weapon& w=gWeapons[gWeapon];
    if(gFireCd>0||gReload>0) return;
    if(w.ammo<=0){ gReload=1.0f; return; }
    w.ammo--; gFireCd=w.fireRate; gMuzzle=0.05f; playSnd(gSndGun);
    gPlayerPitch+=w.recoil; gShake=fmaxf(gShake,0.18f);
    Vector3 o=gPlayerPos, d=camDir();
    d.x+=frand(-w.spread,w.spread); d.y+=frand(-w.spread,w.spread); d.z+=frand(-w.spread,w.spread); d=Vector3Normalize(d);
    float best=w.range; int hp=-1,hc=-1;
    for(size_t i=0;i<gPeds.size();++i){ if(!gPeds[i].alive)continue; Vector3 c={gPeds[i].pos.x,1.3f,gPeds[i].pos.z},oc=Vector3Subtract(c,o);
        float t=Vector3DotProduct(oc,d); if(t<0||t>best)continue;
        if(Vector3Distance(Vector3Add(o,Vector3Scale(d,t)),c)<0.8f){best=t;hp=(int)i;hc=-1;} }
    for(size_t i=0;i<gCars.size();++i){ if(!gCars[i].alive||(int)i==gInCar)continue; Vector3 c=gCars[i].pos,oc=Vector3Subtract(c,o);
        float t=Vector3DotProduct(oc,d); if(t<0||t>best)continue;
        if(Vector3Distance(Vector3Add(o,Vector3Scale(d,t)),c)<2.2f){best=t;hc=(int)i;hp=-1;} }
    Vector3 m=Vector3Add(o,Vector3Scale(d,0.6f)); m.y-=0.15f; Vector3 e=Vector3Add(o,Vector3Scale(d,best));
    gBullets.push_back({m,e,0.05f,{255,230,120,255}});
    gShotA=m; gShotB=e; gShotFired=true;                 // pour la synchro co-op
    if(hp>=0){ gPeds[hp].health-=w.dmg; spawnBlood(e,8); gHitmark=0.12f; addDmgNum(e,w.dmg,{255,80,80,255});
        if(gPeds[hp].health<=0)killPed(gPeds[hp],true); else gPeds[hp].fleeTimer=6; }
    else if(hc>=0){ gCars[hc].health-=w.dmg; spawnSpark(e); gHitmark=0.12f; addDmgNum(e,w.dmg,{255,180,80,255});
        if(gCars[hc].health<=0&&gCars[hc].alive){ gCars[hc].alive=false; gCars[hc].wreckTimer=8; spawnExplosion(gCars[hc].pos); playSnd(gSndBoom);} }
}
static void hurtPlayer(float dmg){ if(gDead)return; gHurtFx=.3f; gShake=fmaxf(gShake,0.3f); playSnd(gSndHurt);
    if(gArmor>0){ float a=(dmg<gArmor)?dmg:gArmor; gArmor-=a; dmg-=a; } gHealth-=dmg;
    if(gHealth<=0){ gHealth=0; gDead=true; gDeadTimer=3; if(gInCar>=0){gCars[gInCar].occupied=false;gInCar=-1;} } }

// ─── SPAWN ───────────────────────────────────────────────────────────────────
static Vector3 edgeSpawn(){ float e=WORLD-8; int s=rand()%4;
    if(s==0)return{frand(-e,e),0,-e}; if(s==1)return{frand(-e,e),0,e}; if(s==2)return{-e,0,frand(-e,e)}; return{e,0,frand(-e,e)}; }
static void spawnPoliceCar(){ Car c={0}; Vector3 p=edgeSpawn(); c.pos={p.x,0.65f,p.z};
    c.angle=atan2f(gPlayerPos.z-c.pos.z,gPlayerPos.x-c.pos.x); c.color={245,245,245,255};
    c.occupied=true; c.isPolice=true; c.isTraffic=false; c.type=0; c.health=110; c.alive=true; gCars.push_back(c); }
static void spawnCop(){ Pedestrian p={0}; Vector3 d={cosf(frand(0,2*PI)),0,sinf(frand(0,2*PI))};
    p.pos={gPlayerPos.x+d.x*38,0,gPlayerPos.z+d.z*38}; p.shirt={30,40,120,255}; p.pants={20,25,60,255};
    p.skin=randSkin(); p.hair={40,40,40,255}; p.alive=true; p.health=100; p.fall=0; p.fallDir=frand(-.4f,.4f);
    p.isPolice=true; p.shootCd=frand(.6f,1.6f); p.navNode=-1; gPeds.push_back(p); }
static void spawnRoadblock(){ Vector3 d=camDir(); d.y=0; d=Vector3Normalize(d);
    Vector3 perp={-d.z,0,d.x}; Vector3 ahead={gPlayerPos.x+d.x*45,0.65f,gPlayerPos.z+d.z*45};
    for(int k=-1;k<=1;k++){ Car c={0}; c.pos={ahead.x+perp.x*k*3.2f,0.65f,ahead.z+perp.z*k*3.2f};
        if(collides(c.pos,2.5f))continue; c.angle=atan2f(perp.z,perp.x); c.color={245,245,245,255};
        c.occupied=true; c.isPolice=true; c.type=0; c.health=110; c.alive=true; c.speed=0; gCars.push_back(c); } }

// ─── RESPAWN ─────────────────────────────────────────────────────────────────
static void respawn(){ gDead=false; gHealth=100; gArmor=0; gStars=0; gWantedTimer=0;
    gPlayerPos={0,1,0}; gPlayerVelY=0; gInCar=-1; gMoney=(gMoney>100)?gMoney-100:0; gHeli.active=false;
    for(auto& c:gCars) if(c.isPolice){c.alive=false;c.wreckTimer=0;}
    for(auto& p:gPeds) if(p.isPolice){p.alive=false;p.deadTimer=0;} }

// ─── CONSTRUCTION VILLE ──────────────────────────────────────────────────────
static Vector3 validSpawn(float r){ for(int i=0;i<60;i++){ Vector3 p={frand(-WORLD+14,WORLD-14),0,frand(-WORLD+14,WORLD-14)};
    if(!collides(p,r))return p; } return {0,0,0}; }

static Car makeTrafficCar(Vector3 near, bool nearPlayer){
    Car c={0}; c.isTraffic=true; c.alive=true; c.health=100; c.type=rand()%3;
    Color cols[]={RED,BLUE,YELLOW,ORANGE,LIME,SKYBLUE,MAGENTA,WHITE,BLACK,MAROON,DARKGREEN,PURPLE};
    c.color=cols[rand()%12];
    // place sur une arete de voie proche
    int i=rand()%(GRID+1), j=rand()%(GRID+1); c.node=nodeId(i,j);
    int dir=rand()%4, nb=nodeNeighbor(c.node,dir); int tries=0;
    while(nb<0&&tries<8){ dir=rand()%4; nb=nodeNeighbor(c.node,dir); tries++; }
    if(nb<0)nb=c.node; c.next=nb; c.prevNode=c.node; c.t=frand(0,1); c.laneSide=1.0f;
    Vector3 A=nodePos(i,j), B=nodePos(nb%(GRID+1),nb/(GRID+1));
    Vector3 dv=Vector3Normalize(Vector3Subtract(B,A)); Vector3 perp={-dv.z,0,dv.x};
    Vector3 pp=lerp3(A,B,c.t); c.pos={pp.x+perp.x*ROADW*0.25f,0.65f,pp.z+perp.z*ROADW*0.25f};
    c.angle=atan2f(dv.z,dv.x); c.speed=14; return c;
}

static void buildCity(){
    gBuildings.clear(); gColliders.clear(); gTrees.clear(); gLamps.clear(); gCars.clear(); gPeds.clear();
    for(int gx=0;gx<GRID;gx++)for(int gz=0;gz<GRID;gz++){
        float cx=-WORLD+CELL*(gx+0.5f), cz=-WORLD+CELL*(gz+0.5f);
        if(fabsf(cx)<CELL&&fabsf(cz)<CELL)continue;
        float lot=(CELL-ROADW)*0.5f-1.5f;
        if(rand()%100<30){ Building b; float w=frand(10,lot*1.6f),d=frand(10,lot*1.6f),h=frand(6,9);
            b.pos={cx,h/2,cz}; b.size={w,h,d}; b.color=randCity(); b.topColor=ColorBrightness(b.color,-.35f);
            b.enterable=true; b.doorSide=rand()%4; gBuildings.push_back(b);
        } else { int n=1+rand()%3; for(int k=0;k<n;k++){ Building b; float w=frand(7,lot),d=frand(7,lot),h=frand(10,58);
            float ox=frand(-lot*.4f,lot*.4f),oz=frand(-lot*.4f,lot*.4f); b.pos={cx+ox,h/2,cz+oz}; b.size={w,h,d};
            b.color=randCity(); b.topColor=ColorBrightness(b.color,-.3f); b.enterable=false; b.doorSide=0; gBuildings.push_back(b);} }
        if(rand()%2){ Tree t; t.pos={cx+frand(-lot,lot),0,cz+frand(-lot,lot)}; t.height=frand(5,8); t.radius=0.6f; gTrees.push_back(t); }
    }
    for(auto& b:gBuildings) buildingWalls(b,gColliders);
    for(int i=1;i<GRID;i++)for(int j=1;j<GRID;j++) gLamps.push_back({{-WORLD+CELL*i+ROADW*0.5f,0,-WORLD+CELL*j+ROADW*0.5f}});
    // trafic + pietons initiaux
    for(int i=0;i<14;i++) gCars.push_back(makeTrafficCar(gPlayerPos,false));
    for(int i=0;i<32;i++){ Pedestrian p={0}; Vector3 sp=validSpawn(0.6f); p.pos={sp.x,0,sp.z};
        float a=frand(0,2*PI); p.vel={cosf(a)*.022f,0,sinf(a)*.022f}; p.facing=a;
        p.shirt=randCity(); p.pants=ColorBrightness(DARKGRAY,frand(-.2f,.2f)); p.skin=randSkin(); p.hair=randHair();
        p.bob=frand(0,2*PI); p.alive=true; p.health=100; p.fall=0; p.fallDir=frand(-.4f,.4f); p.navNode=-1; gPeds.push_back(p); }
    for(int i=0;i<160;i++){ float a=frand(0,2*PI),e=frand(0.1f,1.2f); gStars3D[i]={cosf(a)*cosf(e),sinf(e),sinf(a)*cosf(e)}; }
}

// ─── CYCLE JOUR/NUIT ─────────────────────────────────────────────────────────
static void updateSky(){
    float dayAng=(gTime-6.0f)/12.0f*PI;           // 6h->0, 18h->PI
    Vector3 sunPos=Vector3Normalize((Vector3){ -cosf(dayAng), sinf(dayAng), 0.35f });
    gSunDir=Vector3Negate(sunPos); gSunHeight=sunPos.y;
    gNight=Clamp(-gSunHeight*2.5f+0.15f,0,1);
    float day=Clamp(gSunHeight*1.6f,0,1);
    float golden=Clamp(1.0f-fabsf(gSunHeight)*4.0f,0,1)*Clamp(gSunHeight+0.2f,0,1);
    // couleurs lumiere
    Vector3 dayS={1.0f,0.97f,0.90f}, goldS={1.0f,0.62f,0.40f}, nightS={0.10f,0.13f,0.25f};
    Vector3 s=lerp3(nightS,dayS,day); s=lerp3(s,goldS,golden); gSunCol=Vector3Scale(s, day*0.9f+0.1f);
    Vector3 dayA={0.30f,0.34f,0.44f}, nightA={0.07f,0.09f,0.18f}, goldA={0.34f,0.26f,0.28f};
    gAmbCol=lerp3(nightA,dayA,day); gAmbCol=lerp3(gAmbCol,goldA,golden*0.6f);
    // horizon / fog
    Vector3 dayF={0.62f,0.72f,0.85f}, goldF={1.0f,0.55f,0.55f}, nightF={0.05f,0.06f,0.14f};
    gFogCol=lerp3(nightF,dayF,day); gFogCol=lerp3(gFogCol,goldF,golden);
}
// couleurs de ciel (0..255) pour le degrade 2D
static Color skyTopC(){ Vector3 d={70,120,200},g={150,70,150},n={8,10,30};
    float day=Clamp(gSunHeight*1.6f,0,1), gold=Clamp(1-fabsf(gSunHeight)*4,0,1)*Clamp(gSunHeight+0.2f,0,1);
    Vector3 c=lerp3(n,d,day); c=lerp3(c,g,gold); return {(unsigned char)c.x,(unsigned char)c.y,(unsigned char)c.z,255}; }
static Color skyBotC(){ Vector3 d={150,200,235},g={255,150,140},n={20,22,55};
    float day=Clamp(gSunHeight*1.6f,0,1), gold=Clamp(1-fabsf(gSunHeight)*4,0,1)*Clamp(gSunHeight+0.2f,0,1);
    Vector3 c=lerp3(n,d,day); c=lerp3(c,g,gold); return {(unsigned char)c.x,(unsigned char)c.y,(unsigned char)c.z,255}; }

// ═══════════════════════════════════════════════════════════════════════════
//   UPDATE
// ═══════════════════════════════════════════════════════════════════════════
static void updateTraffic(float dt){
    gLight+=dt; bool nsGreen=fmodf(gLight,16.0f)<8.0f;   // feux N-S vs E-O
    for(auto& c:gCars){
        if(!c.isTraffic||!c.alive)continue;
        int ai,aj; nodeIJ(c.prevNode,ai,aj); int bi,bj; nodeIJ(c.next,bi,bj);
        Vector3 A=nodePos(ai,aj), B=nodePos(bi,bj);
        Vector3 dv=Vector3Subtract(B,A); float len=Vector3Length(dv); if(len<0.1f){len=1;} dv=Vector3Scale(dv,1.0f/len);
        Vector3 perp={-dv.z,0,dv.x};
        bool movingX=fabsf(dv.x)>fabsf(dv.z);
        // feu : stop avant le noeud si rouge
        float desired=14.0f;
        bool red=(movingX&&nsGreen)||(!movingX&&!nsGreen);
        if(red && c.t>0.82f) desired=0.0f;
        // espacement : ralentir si voiture devant
        Vector3 selfPos=c.pos;
        for(auto& o:gCars){ if(&o==&c||!o.alive)continue;
            Vector3 to=Vector3Subtract(o.pos,selfPos); float fd=to.x*dv.x+to.z*dv.z;
            if(fd>0&&fd<7 && fabsf(to.x*perp.x+to.z*perp.z)<2.0f){ desired=0.0f; break; } }
        // joueur obstacle
        { Vector3 to=Vector3Subtract(gPlayerPos,selfPos); float fd=to.x*dv.x+to.z*dv.z;
          if(fd>0&&fd<8&&fabsf(to.x*perp.x+to.z*perp.z)<2.5f) desired=0.0f; }
        c.speed+=(desired-c.speed)*fmin(1.0f,dt*3.0f);
        c.t+=c.speed*dt/len;
        if(c.t>=1.0f){ c.t-=1.0f; // arrive au noeud : choisir suivant (pas demi-tour)
            int from=-1; for(int d=0;d<4;d++) if(nodeNeighbor(c.next,d)==c.prevNode) from=d;
            int pick=-1; for(int tries=0;tries<8;tries++){ int d=rand()%4; if(d==from)continue; int nb=nodeNeighbor(c.next,d); if(nb>=0){pick=nb;break;} }
            if(pick<0)pick=nodeNeighbor(c.next,from); if(pick<0)pick=c.next;
            c.prevNode=c.next; c.next=pick; c.t=0;
            nodeIJ(c.prevNode,ai,aj); nodeIJ(c.next,bi,bj); A=nodePos(ai,aj); B=nodePos(bi,bj);
            dv=Vector3Normalize(Vector3Subtract(B,A)); perp=(Vector3){-dv.z,0,dv.x};
        }
        Vector3 pp=lerp3(A,B,c.t); c.pos={pp.x+perp.x*ROADW*0.25f,0.65f,pp.z+perp.z*ROADW*0.25f};
        c.angle=atan2f(dv.z,dv.x);
    }
}

static void updateVehiclePhysics(float dt){
    Car& c=gCars[gInCar]; float thr=0,steer=0; bool hand=false;
    if(!gDead){ if(IsKeyDown(KEY_W)||IsKeyDown(KEY_UP))thr+=1; if(IsKeyDown(KEY_S)||IsKeyDown(KEY_DOWN))thr-=1;
        if(IsKeyDown(KEY_A)||IsKeyDown(KEY_LEFT))steer-=1; if(IsKeyDown(KEY_D)||IsKeyDown(KEY_RIGHT))steer+=1;
        hand=IsKeyDown(KEY_SPACE); }
    // accel / frein / marche arriere
    if(thr>0) c.speed+=46*dt; else if(thr<0){ if(c.speed>1) c.speed-=70*dt; else c.speed-=24*dt; }
    c.speed*= (thr==0)?0.985f:0.995f; c.speed=Clamp(c.speed,-30.0f,72.0f);
    float grip=hand?0.86f:0.985f;                     // frein a main = derapage
    c.angle+=steer*1.9f*dt*Clamp(c.speed/26.0f,-1.2f,1.2f);
    Vector3 fwd={cosf(c.angle),0,sinf(c.angle)};
    Vector3 desiredVel={fwd.x*c.speed,0,fwd.z*c.speed};
    c.vel=lerp3(c.vel,desiredVel,1.0f-grip);          // glisse vers l'avant selon grip
    Vector3 np=c.pos; np.x+=c.vel.x*dt; np.z+=c.vel.z*dt;
    if(!collides(np,2.4f)) c.pos=np; else { c.speed*=-0.25f; c.vel=Vector3Scale(c.vel,-0.25f); if(fabsf(c.speed)>18){spawnSpark(c.pos);gShake=fmaxf(gShake,0.3f);} }
    c.pos.y=0.65f;
    // traces de pneus si derapage/freinage
    float lateral=fabsf(c.vel.x*(-fwd.z)+c.vel.z*fwd.x);
    if((hand&&fabsf(c.speed)>10)||lateral>6){ if(gMarks.size()<400){
        gMarks.push_back({{c.pos.x-fwd.x*1.3f+0.0f,0.04f,c.pos.z-fwd.z*1.3f},6.0f,c.angle}); } }
    // ecraser pietons
    if(fabsf(c.speed)>6) for(auto& p:gPeds) if(p.alive&&Vector3Distance({c.pos.x,0,c.pos.z},{p.pos.x,0,p.pos.z})<2.6f){ killPed(p,true); playSnd(gSndThud); c.speed*=0.85f; }
    // percuter voitures de police
    for(auto& o:gCars) if(o.isPolice&&o.alive&&&o!=&c){ if(Vector3Distance(c.pos,o.pos)<4&&fabsf(c.speed)>14){
        o.health-=fabsf(c.speed)*1.2f; spawnSpark(o.pos); c.speed*=0.6f;
        if(o.health<=0){o.alive=false;o.wreckTimer=8;spawnExplosion(o.pos);playSnd(gSndBoom);} } }
    if(fabsf(c.speed)>45)gMoney+=1;
    gPlayerPos=c.pos;
}

static void update(float dt, bool simWorld=true){
    if(simWorld){ gTime+=dt*(24.0f/DAYLEN); if(gTime>=24)gTime-=24; }
    updateSky();
    if(gDead){ gDeadTimer-=dt; if(gDeadTimer<=0)respawn(); }

    Vector2 md=GetMouseDelta(); gPlayerYaw+=md.x*0.003f; gPlayerPitch-=md.y*0.003f; gPlayerPitch=Clamp(gPlayerPitch,-1.4f,1.4f);
    Vector3 fwd={cosf(gPlayerYaw),0,sinf(gPlayerYaw)}, rgt={-sinf(gPlayerYaw),0,cosf(gPlayerYaw)};
    if(gMuzzle>0)gMuzzle-=dt; if(gHurtFx>0)gHurtFx-=dt; if(gFireCd>0)gFireCd-=dt; if(gShake>0)gShake-=dt*1.5f;
    if(gHitmark>0)gHitmark-=dt;
    if(gReload>0){ gReload-=dt; if(gReload<=0)gWeapons[gWeapon].ammo=gWeapons[gWeapon].maxAmmo; }

    if(!gDead){
        if(IsKeyPressed(KEY_ONE))gWeapon=0; if(IsKeyPressed(KEY_TWO))gWeapon=1; if(IsKeyPressed(KEY_THREE))gWeapon=2;
        if(IsKeyPressed(KEY_R)&&gReload<=0&&gWeapons[gWeapon].ammo<gWeapons[gWeapon].maxAmmo)gReload=1.0f;
        bool shoot=(gWeapon==0)?IsMouseButtonPressed(MOUSE_BUTTON_LEFT):IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        if(shoot&&gInCar<0)playerShoot();
        if(gNetMode==0 && IsKeyPressed(KEY_F)){   // conduite desactivee en co-op v1 (synchro voitures = v2)
            if(gInCar>=0){ gCars[gInCar].occupied=false; gPlayerPos=gCars[gInCar].pos; gPlayerPos.x+=rgt.x*3; gPlayerPos.z+=rgt.z*3; gPlayerPos.y=1; gInCar=-1; }
            else for(size_t i=0;i<gCars.size();++i) if(gCars[i].alive&&Vector3Distance(gPlayerPos,gCars[i].pos)<5){
                gInCar=(int)i; gCars[i].occupied=true; gCars[i].isTraffic=false; gCars[i].vel={0,0,0};
                gStars=(gStars<5)?gStars+1:5; gWantedTimer=20; break; }
        }
        if(IsKeyPressed(KEY_T)){ gPlayerPos={0,1,0}; if(gInCar>=0){gCars[gInCar].occupied=false;gInCar=-1;} }
        if(IsKeyPressed(KEY_V)) gThirdPerson=!gThirdPerson;
    }

    if(gInCar>=0) updateVehiclePhysics(dt);
    else if(!gDead){
        float spd=12*dt; if(IsKeyDown(KEY_LEFT_SHIFT))spd*=1.9f; Vector3 np=gPlayerPos;
        if(IsKeyDown(KEY_W)||IsKeyDown(KEY_UP)){np.x+=fwd.x*spd;np.z+=fwd.z*spd;}
        if(IsKeyDown(KEY_S)||IsKeyDown(KEY_DOWN)){np.x-=fwd.x*spd;np.z-=fwd.z*spd;}
        if(IsKeyDown(KEY_A)||IsKeyDown(KEY_LEFT)){np.x-=rgt.x*spd;np.z-=rgt.z*spd;}
        if(IsKeyDown(KEY_D)||IsKeyDown(KEY_RIGHT)){np.x+=rgt.x*spd;np.z+=rgt.z*spd;}
        if(!collides({np.x,gPlayerPos.y,gPlayerPos.z},0.5f))gPlayerPos.x=np.x;
        if(!collides({gPlayerPos.x,gPlayerPos.y,np.z},0.5f))gPlayerPos.z=np.z;
        if(IsKeyPressed(KEY_SPACE)&&gOnGround){gPlayerVelY=8;gOnGround=false;}
        gPlayerVelY-=20*dt; gPlayerPos.y+=gPlayerVelY*dt; if(gPlayerPos.y<=1){gPlayerPos.y=1;gPlayerVelY=0;gOnGround=true;}
    }

    if(simWorld){
    updateTraffic(dt);

    // recherche
    if(gWantedTimer>0){ gWantedTimer-=dt; if(gWantedTimer<=0&&gStars>0){gStars--; if(gStars>0)gWantedTimer=14;} }

    // spawn police
    gPoliceSpawnCd-=dt; gRoadblockCd-=dt;
    if(gStars>0&&!gDead&&gPoliceSpawnCd<=0){ int cc=0,cp=0; for(auto&c:gCars)if(c.isPolice&&c.alive)cc++; for(auto&p:gPeds)if(p.isPolice&&p.alive)cp++;
        if(cc<gStars)spawnPoliceCar(); if(gStars>=2&&cp<gStars*2&&gInCar<0)spawnCop(); gPoliceSpawnCd=2.2f; }
    if(gStars>=3&&!gDead&&gRoadblockCd<=0&&gInCar>=0){ spawnRoadblock(); gRoadblockCd=12.0f; }
    // helico aux etoiles elevees
    if(gStars>=4&&!gDead){ if(!gHeli.active){ gHeli.active=true; gHeli.pos={gPlayerPos.x,40,gPlayerPos.z}; gHeli.shootCd=2; } }
    else gHeli.active=false;
    if(gHeli.active){ gHeli.rotor+=dt*30; Vector3 tgt={gPlayerPos.x,38,gPlayerPos.z};
        gHeli.pos=lerp3(gHeli.pos,tgt,fmin(1.0f,dt*0.6f)); gHeli.angle+=dt*0.5f; gHeli.shootCd-=dt;
        if(gHeli.shootCd<=0&&!gDead){ gHeli.shootCd=1.2f; gBullets.push_back({{gHeli.pos.x,gHeli.pos.y,gHeli.pos.z},gPlayerPos,0.06f,{255,120,80,255}});
            if(frand(0,1)<0.35f)hurtPlayer(frand(3,7)); } }

    // police cars chase
    for(auto& c:gCars){
        if(c.isPolice&&c.alive){ c.sirenPhase+=dt*8;
            float want=atan2f(gPlayerPos.z-c.pos.z,gPlayerPos.x-c.pos.x),diff=want-c.angle;
            while(diff>PI)diff-=2*PI; while(diff<-PI)diff+=2*PI; c.angle+=Clamp(diff,-2.0f*dt,2.0f*dt);
            float dist=Vector3Distance(c.pos,gPlayerPos); c.speed+=((dist>10)?26:-22)*dt; c.speed=Clamp(c.speed,-8.0f,42.0f);
            Vector3 dir={cosf(c.angle),0,sinf(c.angle)},np=c.pos; np.x+=dir.x*c.speed*dt; np.z+=dir.z*c.speed*dt;
            if(!collides(np,2.4f))c.pos=np; else{c.speed*=-0.3f;c.health-=4;spawnSpark(c.pos);} c.pos.y=0.65f;
            if(dist<3.8f&&fabsf(c.speed)>10&&!gDead)hurtPlayer(40*dt);
            if(c.health<45){c.smokeCd-=dt; if(c.smokeCd<=0){spawnSmoke({c.pos.x,1.2f,c.pos.z});c.smokeCd=0.12f;}}
            if(c.health<=0){c.alive=false;c.wreckTimer=8;spawnExplosion(c.pos);playSnd(gSndBoom);}
        } else if(!c.alive&&c.wreckTimer>0){ c.wreckTimer-=dt; c.smokeCd-=dt; if(c.smokeCd<=0){spawnSmoke({c.pos.x,1.0f,c.pos.z});c.smokeCd=0.25f;} }
    }

    // pietons & flics
    for(auto& p:gPeds){
        if(!p.alive){ if(p.fall<1)p.fall+=dt*2.2f; if(p.deadTimer>0)p.deadTimer-=dt; continue; }
        if(p.isPolice){
            Vector3 to=Vector3Subtract(gPlayerPos,p.pos); float dist=Vector3Length(to);
            if(dist>0.1f){to=Vector3Scale(to,1.0f/dist); p.facing=atan2f(to.z,to.x);}
            float spd=(dist>12)?0.085f:0.0f; Vector3 np=p.pos; np.x+=to.x*spd; np.z+=to.z*spd;
            if(!collides({np.x,0,p.pos.z},0.4f))p.pos.x=np.x; if(!collides({p.pos.x,0,np.z},0.4f))p.pos.z=np.z;
            p.bob+=dt*8; p.shootCd-=dt;
            if(dist<28&&p.shootCd<=0&&!gDead){ p.shootCd=frand(.8f,1.5f); gBullets.push_back({{p.pos.x,1.3f,p.pos.z},gPlayerPos,0.05f,{255,120,80,255}}); if(frand(0,1)<.4f)hurtPlayer(frand(4,8)); }
        } else {
            if(p.fleeTimer>0){ p.fleeTimer-=dt; Vector3 a=Vector3Subtract(p.pos,gPlayerPos); float l=Vector3Length(a);
                if(l>0.1f){a=Vector3Scale(a,1.0f/l);p.facing=atan2f(a.z,a.x);} p.vel={a.x*.11f,0,a.z*.11f};
            } else {
                // navigation trottoir : viser un noeud, avancer le long
                p.navCd-=dt;
                if(p.navNode<0||p.navCd<=0){ int i=rand()%(GRID+1),j=rand()%(GRID+1); p.navNode=nodeId(i,j); p.navCd=frand(4,9); }
                int ni,nj; nodeIJ(p.navNode,ni,nj); Vector3 tgt=nodePos(ni,nj);
                tgt.x+=ROADW*0.55f; tgt.z+=ROADW*0.55f;          // trottoir
                Vector3 d=Vector3Subtract(tgt,p.pos); float l=Vector3Length(d);
                if(l<2.0f){ p.navNode=-1; } else { d=Vector3Scale(d,1.0f/l); p.facing=atan2f(d.z,d.x); p.vel={d.x*.03f,0,d.z*.03f}; }
            }
            Vector3 np=p.pos; np.x+=p.vel.x; np.z+=p.vel.z;
            if(!collides({np.x,0,p.pos.z},0.4f))p.pos.x=np.x; else p.vel.x*=-1;
            if(!collides({p.pos.x,0,np.z},0.4f))p.pos.z=np.z; else p.vel.z*=-1;
            p.bob+=dt*6;
        }
    }

    // streaming : despawn lointain, respawn proche
    for(int i=(int)gCars.size()-1;i>=0;--i){ Car& c=gCars[i];
        if(c.isTraffic&&i!=gInCar&&Vector3Distance(c.pos,gPlayerPos)>STREAM_R+30){ gCars.erase(gCars.begin()+i); if(gInCar>i)gInCar--; } }
    for(int i=(int)gPeds.size()-1;i>=0;--i){ Pedestrian& p=gPeds[i];
        if(p.alive&&!p.isPolice&&p.fleeTimer<=0&&Vector3Distance(p.pos,gPlayerPos)>STREAM_R+30) gPeds.erase(gPeds.begin()+i); }
    { int tc=0; for(auto&c:gCars)if(c.isTraffic&&c.alive)tc++;
      if(tc<14){ Car c=makeTrafficCar(gPlayerPos,true); if(Vector3Distance(c.pos,gPlayerPos)<STREAM_R) gCars.push_back(c); } }
    { int pc=0; for(auto&p:gPeds)if(p.alive&&!p.isPolice)pc++;
      if(pc<30){ Vector3 sp=validSpawn(0.6f); if(Vector3Distance(sp,gPlayerPos)<STREAM_R){ Pedestrian p={0}; p.pos={sp.x,0,sp.z};
        float a=frand(0,2*PI); p.vel={cosf(a)*.022f,0,sinf(a)*.022f}; p.facing=a; p.shirt=randCity();
        p.pants=ColorBrightness(DARKGRAY,frand(-.2f,.2f)); p.skin=randSkin(); p.hair=randHair(); p.bob=frand(0,2*PI);
        p.alive=true; p.health=100; p.fall=0; p.fallDir=frand(-.4f,.4f); p.navNode=-1; gPeds.push_back(p);} } }
    } // fin simWorld

    // entites diverses (cosmetique : tourne aussi chez le client)
    for(auto& b:gBullets)b.life-=dt;
    gBullets.erase(std::remove_if(gBullets.begin(),gBullets.end(),[](const Bullet&b){return b.life<=0;}),gBullets.end());
    for(auto& pt:gParticles){ pt.life-=dt; if(pt.grav)pt.vel.y-=12*dt; pt.pos=Vector3Add(pt.pos,Vector3Scale(pt.vel,dt)); if(pt.grav&&pt.pos.y<0.05f){pt.pos.y=0.05f;pt.vel={0,0,0};} }
    gParticles.erase(std::remove_if(gParticles.begin(),gParticles.end(),[](const Particle&p){return p.life<=0;}),gParticles.end());
    for(auto& m:gMarks)m.life-=dt;
    gMarks.erase(std::remove_if(gMarks.begin(),gMarks.end(),[](const Mark&m){return m.life<=0;}),gMarks.end());
    for(auto& d:gDmgNums){ d.life-=dt; d.pos.y+=dt*1.2f; }
    gDmgNums.erase(std::remove_if(gDmgNums.begin(),gDmgNums.end(),[](const DamageNum&d){return d.life<=0;}),gDmgNums.end());
    if(simWorld){
        gPeds.erase(std::remove_if(gPeds.begin(),gPeds.end(),[](const Pedestrian&p){return !p.alive&&p.deadTimer<=0;}),gPeds.end());
        for(int i=(int)gCars.size()-1;i>=0;--i) if(!gCars[i].alive&&gCars[i].isPolice&&gCars[i].wreckTimer<=0&&i!=gInCar){ gCars.erase(gCars.begin()+i); if(gInCar>i)gInCar--; }
    }

    static float t1=0; t1+=dt; if(t1>1){ gMoney+=5; if(gHealth>0&&gHealth<100&&gStars==0)gHealth=fmin(100,gHealth+1); t1=0; }
}

// ═══════════════════════════════════════════════════════════════════════════
//   RENDU
// ═══════════════════════════════════════════════════════════════════════════
static bool isNightLights(){ return gNight>0.35f; }

static void drawCarBody(const Car& c){
    rlPushMatrix(); rlTranslatef(c.pos.x,c.pos.y,c.pos.z); rlRotatef(-c.angle*RAD2DEG,0,1,0);
    float L=4.4f,W=2.0f,H=0.9f,roofY=1.0f,lift=0;
    if(c.type==1){H=0.75f;L=4.7f;roofY=0.9f;} if(c.type==2){H=1.2f;W=2.2f;L=4.9f;lift=0.18f;roofY=1.2f;}
    Color body=c.alive?c.color:Color{45,40,35,255}; Color dark=ColorBrightness(body,-.25f);
    Color glass={110,160,205,210}, blk={20,20,24,255};
    DrawCube({0,0.12f+lift,0},L*0.96f,H,W,body);
    DrawCube({0,-0.05f+lift,0},L*0.9f,H*0.5f,W*1.04f,dark);
    DrawCube({L*0.34f,0.05f+lift,0},L*0.36f,H*0.65f,W*0.9f,body);
    DrawCube({L*0.50f,-0.02f+lift,0},L*0.10f,H*0.5f,W*0.85f,body);
    DrawCube({-L*0.36f,0.06f+lift,0},L*0.30f,H*0.65f,W*0.9f,body);
    float cabL=L*0.42f;
    DrawCube({-L*0.02f,roofY+lift,0},cabL,H*0.8f,W*0.82f,dark);
    DrawCube({-L*0.02f,roofY+H*0.45f+lift,0},cabL*0.95f,0.08f,W*0.8f,dark);
    DrawCube({cabL*0.52f,roofY+lift,0},0.1f,H*0.62f,W*0.74f,glass);
    DrawCube({-cabL*0.52f,roofY+lift,0},0.1f,H*0.62f,W*0.74f,glass);
    DrawCube({-L*0.02f,roofY+lift,W*0.41f},cabL*0.85f,H*0.55f,0.05f,glass);
    DrawCube({-L*0.02f,roofY+lift,-W*0.41f},cabL*0.85f,H*0.55f,0.05f,glass);
    DrawCube({-L*0.02f,roofY+lift,W*0.42f},0.12f,H*0.6f,0.06f,blk);
    DrawCube({-L*0.02f,roofY+lift,-W*0.42f},0.12f,H*0.6f,0.06f,blk);
    DrawCube({cabL*0.4f,roofY-0.1f+lift,W*0.5f},0.18f,0.12f,0.12f,dark);
    DrawCube({cabL*0.4f,roofY-0.1f+lift,-W*0.5f},0.18f,0.12f,0.12f,dark);
    for(int sx=-1;sx<=1;sx+=2){ DrawCube({sx*L*0.32f,-0.05f+lift,W*0.5f},1.1f,0.5f,0.08f,blk); DrawCube({sx*L*0.32f,-0.05f+lift,-W*0.5f},1.1f,0.5f,0.08f,blk); }
    float wy=-H*0.45f+lift,wr=0.5f+lift*0.3f,wx=L*0.32f,wz=W*0.5f;
    for(int sx=-1;sx<=1;sx+=2)for(int sz=-1;sz<=1;sz+=2){ Vector3 a={sx*wx,wy,sz*wz-0.12f},b={sx*wx,wy,sz*wz+0.12f};
        DrawCylinderEx(a,b,wr,wr,12,blk); DrawCylinderEx(a,b,wr*0.45f,wr*0.45f,8,LIGHTGRAY); }
    DrawCube({L*0.53f,-0.1f+lift,0},0.1f,0.28f,W*0.95f,dark); DrawCube({-L*0.53f,-0.1f+lift,0},0.1f,0.28f,W*0.95f,dark);
    DrawCube({L*0.55f,-0.1f+lift,0},0.03f,0.16f,0.5f,{235,235,235,255}); DrawCube({-L*0.55f,-0.1f+lift,0},0.03f,0.16f,0.5f,{235,235,235,255});
    DrawCylinderEx({-L*0.5f,-0.25f+lift,W*0.28f},{-L*0.58f,-0.25f+lift,W*0.28f},0.07f,0.07f,6,LIGHTGRAY);
    DrawCube({-L*0.3f,roofY+0.4f+lift,W*0.3f},0.02f,0.5f,0.02f,blk);
    if(c.type==1){ DrawCube({-L*0.46f,roofY*0.6f+lift,0},0.1f,0.35f,W*0.9f,dark); DrawCube({-L*0.52f,roofY*0.8f+lift,0},0.5f,0.07f,W*0.95f,dark); }
    rlPopMatrix();
}
// elements lumineux d'une voiture (passe emissive)
static void drawCarLights(const Car& c){
    rlPushMatrix(); rlTranslatef(c.pos.x,c.pos.y,c.pos.z); rlRotatef(-c.angle*RAD2DEG,0,1,0);
    float L=4.4f,W=2.0f,lift=0; if(c.type==2)lift=0.18f;
    bool on = c.alive && (c.health>40);
    if(on){ DrawCube({L*0.55f,0.1f+lift,W*0.32f},0.06f,0.2f,0.32f,{255,250,210,255}); DrawCube({L*0.55f,0.1f+lift,-W*0.32f},0.06f,0.2f,0.32f,{255,250,210,255}); }
    DrawCube({-L*0.55f,0.1f+lift,W*0.32f},0.05f,0.18f,0.3f,{255,40,40,255}); DrawCube({-L*0.55f,0.1f+lift,-W*0.32f},0.05f,0.18f,0.3f,{255,40,40,255});
    if(c.isPolice&&c.alive){ bool blue=((int)(c.sirenPhase)%2)==0; float roofY=(c.type==2)?1.2f:1.0f; float H=(c.type==2)?1.2f:0.9f;
        DrawCube({0,roofY+H*0.6f+lift,0.25f},0.5f,0.16f,0.34f, blue?(Color){80,140,255,255}:(Color){30,30,60,255});
        DrawCube({0,roofY+H*0.6f+lift,-0.25f},0.5f,0.16f,0.34f, blue?(Color){60,30,30,255}:(Color){255,60,60,255}); }
    rlPopMatrix();
}

static void drawPersonLocal(const Pedestrian& p,bool anim){
    float step=anim?sinf(p.bob)*0.25f:0;
    DrawCube({0,0.4f,0.13f},0.32f,0.8f+step*0.3f,0.28f,p.pants); DrawCube({0,0.4f,-0.13f},0.32f,0.8f-step*0.3f,0.28f,p.pants);
    DrawCube({0,1.2f,0},0.5f,0.85f,0.42f,p.shirt); DrawCube({0,1.2f,0.34f},0.18f,0.7f,0.18f,p.shirt); DrawCube({0,1.2f,-0.34f},0.18f,0.7f,0.18f,p.shirt);
    DrawSphere({0,0.85f,0.34f},0.1f,p.skin); DrawSphere({0,0.85f,-0.34f},0.1f,p.skin);
    DrawSphere({0,1.95f,0},0.28f,p.skin); DrawSphere({-0.05f,2.05f,0},0.27f,p.hair);
    if(p.isPolice)DrawCube({0,2.18f,0},0.5f,0.16f,0.5f,{20,30,90,255});
    DrawCube({0.24f,2.0f,0.1f},0.05f,0.08f,0.08f,BLACK); DrawCube({0.24f,2.0f,-0.1f},0.05f,0.08f,0.08f,BLACK);
    DrawCube({0.26f,2.04f,0.1f},0.03f,0.03f,0.06f,WHITE); DrawCube({0.26f,2.04f,-0.1f},0.03f,0.03f,0.06f,WHITE);
    DrawCube({0.25f,1.85f,0},0.04f,0.04f,0.16f,{120,60,60,255}); DrawCube({0.27f,1.95f,0},0.06f,0.06f,0.06f,p.skin);
    if(p.isPolice)DrawCube({0.27f,1.3f,0.1f},0.04f,0.1f,0.1f,GOLD);
}
static void drawPed(const Pedestrian& p){
    if(!p.alive){ float fall=p.fall>1?1:p.fall; float pr=0.3f+fall*0.9f;
        DrawCylinder({p.pos.x,0.03f,p.pos.z},pr,pr,0.03f,10,{110,0,0,200});
        rlPushMatrix(); rlTranslatef(p.pos.x,0,p.pos.z); rlRotatef(-p.facing*RAD2DEG,0,1,0);
        rlRotatef(p.fallDir*40*fall,1,0,0); rlRotatef(fall*90,0,0,1); drawPersonLocal(p,false); rlPopMatrix(); return; }
    float by=sinf(p.bob)*0.08f; rlPushMatrix(); rlTranslatef(p.pos.x,by,p.pos.z); rlRotatef(-p.facing*RAD2DEG,0,1,0); drawPersonLocal(p,true); rlPopMatrix();
}
static void drawTree(const Tree& t){
    DrawCylinder({t.pos.x,0,t.pos.z},t.radius*0.7f,t.radius*0.5f,t.height,8,{120,85,55,255});
    Vector3 top={t.pos.x,t.height,t.pos.z};
    for(int i=0;i<6;i++){ float a=i/6.0f*2*PI; DrawSphereEx({top.x+cosf(a)*1.6f,top.y-0.2f,top.z+sinf(a)*1.6f},0.9f,6,6,{35,135,60,255}); }
    DrawSphere(top,0.7f,{45,150,70,255}); DrawSphere({top.x,top.y-0.4f,top.z},0.18f,{90,60,40,255});
}
static void drawLampPole(const Lamp& l){ DrawCylinder({l.pos.x,0,l.pos.z},0.18f,0.14f,5.0f,8,{60,60,68,255});
    DrawCube({l.pos.x,5.0f,l.pos.z},1.2f,0.18f,0.18f,{60,60,68,255}); }

static void drawShop(const Building& b){
    std::vector<Box> walls; buildingWalls(b,walls); Color wc=b.color,wd=ColorBrightness(b.color,-.4f);
    for(auto& w:walls){ DrawCubeV(w.c,{w.h.x*2,w.h.y*2,w.h.z*2},wc); }
    float sx=b.size.x/2,sz=b.size.z/2,h=b.size.y,cx=b.pos.x,cz=b.pos.z;
    DrawCube({cx,0.04f,cz},b.size.x-0.6f,0.08f,b.size.z-0.6f,{110,95,80,255});
    DrawCube({cx,h,cz},b.size.x,0.3f,b.size.z,wd);
    DrawCube({cx,0.55f,cz-sz*0.55f},b.size.x*0.55f,1.0f,0.6f,{120,80,50,255});
    DrawCube({cx+b.size.x*0.18f,1.15f,cz-sz*0.55f},0.4f,0.25f,0.4f,{40,40,40,255});
    DrawCube({cx-sx*0.7f,1.0f,cz},0.4f,2.0f,b.size.z*0.6f,{150,150,160,255});
    DrawCube({cx+sx*0.5f,0.45f,cz+sz*0.3f},1.0f,0.1f,1.0f,{120,80,50,255});
}
static void drawSolidBuilding(const Building& b){
    DrawCubeV(b.pos,b.size,b.color);
    DrawCube({b.pos.x,b.pos.y+b.size.y/2+0.1f,b.pos.z},b.size.x*0.9f,0.4f,b.size.z*0.9f,b.topColor);
}
static void drawRoads(){
    DrawPlane({0,0,0},{WORLD*2,WORLD*2},{92,112,72,255});
    Color asph={48,48,55,255},side={135,135,145,255},dash={235,235,235,255},ctr={235,215,70,255};
    for(int i=0;i<=GRID;i++){ float p=-WORLD+CELL*i;
        DrawCube({p,0.02f,0},ROADW,0.04f,WORLD*2,asph); DrawCube({0,0.02f,p},WORLD*2,0.04f,ROADW,asph);
        DrawCube({p+ROADW/2+0.6f,0.08f,0},1.2f,0.16f,WORLD*2,side); DrawCube({p-ROADW/2-0.6f,0.08f,0},1.2f,0.16f,WORLD*2,side);
        DrawCube({0,0.08f,p+ROADW/2+0.6f},WORLD*2,0.16f,1.2f,side); DrawCube({0,0.08f,p-ROADW/2-0.6f},WORLD*2,0.16f,1.2f,side);
        DrawCube({p,0.05f,0},0.25f,0.02f,WORLD*2,ctr); DrawCube({0,0.05f,p},WORLD*2,0.02f,0.25f,ctr);
    }
}
// repere joueur a pied (petit corps visible projetant une ombre)
static void drawPlayerAvatar(){ if(gInCar>=0)return;
    Pedestrian me={0}; me.pos={gPlayerPos.x,0,gPlayerPos.z}; me.facing=gPlayerYaw;
    me.shirt={210,210,225,255}; me.pants={40,40,55,255}; me.skin=(Color){235,200,170,255}; me.hair=(Color){45,30,20,255};
    me.alive=true; me.bob=(float)GetTime()*7.0f;
    rlPushMatrix(); rlTranslatef(me.pos.x,gPlayerPos.y-1.0f,me.pos.z); rlRotatef(-me.facing*RAD2DEG,0,1,0);
    drawPersonLocal(me,true); rlPopMatrix(); }

// avatar du joueur distant (co-op)
static void drawRemoteAvatar(){ if(gNetMode==0||!gPeerConnected||!gRemote.alive)return;
    Pedestrian r={0}; r.pos={gRemote.pos.x,0,gRemote.pos.z}; r.facing=gRemote.yaw;
    r.shirt={230,90,60,255}; r.pants={40,40,55,255}; r.skin=(Color){235,200,170,255}; r.hair=(Color){30,20,15,255};
    r.alive=true; r.bob=(float)GetTime()*7.0f;
    rlPushMatrix(); rlTranslatef(r.pos.x,gRemote.pos.y-1.0f,r.pos.z); rlRotatef(-r.facing*RAD2DEG,0,1,0);
    drawPersonLocal(r,true);
    // petit marqueur flottant pour reperer son ami
    rlPopMatrix();
    DrawCube({gRemote.pos.x,gRemote.pos.y+1.6f,gRemote.pos.z},0.3f,0.3f,0.3f,(Color){80,200,255,255}); }

// blob shadow de contact (passe scene, multiplie l'ombre du shadowmap)
static void drawBlob(Vector3 at,float r){ DrawCylinder({at.x,0.03f,at.z},r,r,0.02f,10,{0,0,0,90}); }

// toute la geometrie qui projette/recoit des ombres
static void drawWorldGeometry(bool forShadow){
    drawRoads();
    for(auto& b:gBuildings){ if(b.enterable)drawShop(b); else drawSolidBuilding(b); }
    for(auto& t:gTrees)drawTree(t);
    for(auto& l:gLamps)drawLampPole(l);
    for(auto& c:gCars)drawCarBody(c);
    for(auto& p:gPeds)drawPed(p);
    drawRemoteAvatar();                                                // l'autre joueur (co-op)
    if(forShadow || (gThirdPerson && gInCar<0)) drawPlayerAvatar();   // visible en 3e pers., sinon juste pour l'ombre
    // traces de pneus
    for(auto& m:gMarks){ float a=Clamp(m.life/6.0f,0,1); rlPushMatrix(); rlTranslatef(m.pos.x,0.04f,m.pos.z); rlRotatef(-m.ang*RAD2DEG,0,1,0);
        DrawCube({0,0,0.6f},2.0f,0.02f,0.25f,(Color){20,20,20,(unsigned char)(120*a)}); DrawCube({0,0,-0.6f},2.0f,0.02f,0.25f,(Color){20,20,20,(unsigned char)(120*a)}); rlPopMatrix(); }
    // blobs + fumee/sang (recoivent l'ambiance)
    for(auto& c:gCars) if(c.alive) drawBlob(c.pos,2.4f);
    for(auto& p:gPeds) if(p.alive) drawBlob(p.pos,0.6f);
    for(auto& pt:gParticles) if(pt.col.r<120&&pt.col.g<120&&pt.col.b<120) DrawCube(pt.pos,pt.size,pt.size,pt.size,pt.col); // fumee
}

// elements emissifs (full bright -> bloom), passe sans ombres
static void drawEmissive(){
    // soleil / lune
    Vector3 sunWorld=Vector3Add(gPlayerPos,Vector3Scale(Vector3Negate(gSunDir),300));
    if(gSunHeight>-0.1f) DrawSphere(sunWorld,18,{255,225,160,255});
    else { Vector3 moon=Vector3Add(gPlayerPos,Vector3Scale(gSunDir,300)); DrawSphere(moon,12,{220,225,240,255}); }
    // lampadaires + neons + fenetres la nuit
    if(isNightLights()){
        for(auto& l:gLamps){ DrawCube({l.pos.x+0.55f,4.85f,l.pos.z},0.4f,0.2f,0.3f,{255,235,170,255}); DrawSphere({l.pos.x+0.55f,4.7f,l.pos.z},0.35f,(Color){255,235,170,120}); }
        for(auto& b:gBuildings){ if(b.enterable){ Vector3 sign={b.pos.x,b.size.y+0.4f,b.pos.z}; float sz=b.size.z/2,sx=b.size.x/2,off;
                off=(b.doorSide<2?sz:sx)+0.2f; if(b.doorSide==0)sign.z=b.pos.z+off; else if(b.doorSide==1)sign.z=b.pos.z-off; else if(b.doorSide==2)sign.x=b.pos.x+sx+0.2f; else sign.x=b.pos.x-sx-0.2f;
                Color neon=((int)(GetTime()*3)%2)?(Color){255,80,200,255}:(Color){80,220,255,255};
                DrawCube(sign,(b.doorSide<2)?3.0f:0.2f,0.7f,(b.doorSide<2)?0.2f:3.0f,neon);
            } else { int fl=(int)(b.size.y/4.0f); for(int f=1;f<fl;f++){ if((f*7+(int)b.pos.x)%3)continue; float wy=b.pos.y-b.size.y/2+f*4.0f;
                DrawCube({b.pos.x,wy,b.pos.z+b.size.z/2+0.05f},b.size.x*0.7f,0.18f,0.05f,{255,230,150,255});
                DrawCube({b.pos.x,wy,b.pos.z-b.size.z/2-0.05f},b.size.x*0.7f,0.18f,0.05f,{255,230,150,255}); } }
        }
    }
    for(auto& c:gCars)drawCarLights(c);
    // helico + projecteur
    if(gHeli.active){ rlPushMatrix(); rlTranslatef(gHeli.pos.x,gHeli.pos.y,gHeli.pos.z); rlRotatef(-gHeli.angle*RAD2DEG,0,1,0);
        DrawCube({0,0,0},3.0f,1.2f,1.2f,{30,30,40,255}); DrawCube({-2.0f,0.2f,0},2.0f,0.3f,0.3f,{30,30,40,255});
        DrawCube({0,0.9f,0},0.15f,0.3f,0.15f,{40,40,50,255});
        float rl=4.5f; DrawCube({cosf(gHeli.rotor)*rl,1.1f,sinf(gHeli.rotor)*rl},0.3f,0.05f,9.0f,(Color){80,80,90,200});
        DrawSphere({0,-0.6f,1.4f},0.3f,{255,255,210,255}); rlPopMatrix();
        DrawCylinder({gPlayerPos.x,0.06f,gPlayerPos.z},5.0f,1.0f,0.02f,16,(Color){255,255,210,40}); }
    // flash bouche
    if(gMuzzle>0&&gInCar<0){ Vector3 m=Vector3Add(gPlayerPos,Vector3Scale(camDir(),0.8f)); m.y-=0.15f; DrawSphere(m,0.18f,{255,230,120,255}); }
    // balles + etincelles/explosions
    for(auto& b:gBullets){ DrawLine3D(b.a,b.b,b.col); DrawSphere(b.b,0.08f,b.col); }
    for(auto& pt:gParticles) if(!(pt.col.r<120&&pt.col.g<120&&pt.col.b<120)) DrawCube(pt.pos,pt.size,pt.size,pt.size,pt.col);
    // etoiles
    if(gNight>0.3f){ float a=Clamp((gNight-0.3f)/0.4f,0,1); for(int i=0;i<160;i++){ Vector3 s=Vector3Add(gPlayerPos,Vector3Scale(gStars3D[i],400));
        if(s.y>gPlayerPos.y){ float tw=0.6f+0.4f*sinf(GetTime()*3+i); DrawSphere(s,1.2f,(Color){255,255,255,(unsigned char)(220*a*tw)}); } } }
}

// ─── CAMERA ──────────────────────────────────────────────────────────────────
static Camera3D makeCamera(){ Camera3D cam={0}; Vector3 d=camDir();
    if(gInCar>=0){ Car& c=gCars[gInCar]; Vector3 b={cosf(c.angle),0,sinf(c.angle)};
        cam.position={c.pos.x-b.x*12,c.pos.y+6,c.pos.z-b.z*12}; cam.target={c.pos.x+b.x*4,c.pos.y+1.5f,c.pos.z+b.z*4}; gPlayerYaw=c.angle;
    } else {
        Vector3 eye={gPlayerPos.x, gPlayerPos.y+EYE_H, gPlayerPos.z};   // hauteur des yeux corrigee
        if(gThirdPerson){   // camera derriere/au-dessus du joueur, regarde devant
            cam.position={ eye.x-d.x*5.0f, eye.y-d.y*5.0f+1.2f, eye.z-d.z*5.0f };
            cam.target=Vector3Add(eye,Vector3Scale(d,2.0f));
        } else { cam.position=eye; cam.target=Vector3Add(eye,d); }
    }
    if(gShake>0){ float s=gShake*0.5f; Vector3 j={frand(-s,s),frand(-s,s),frand(-s,s)}; cam.position=Vector3Add(cam.position,j); cam.target=Vector3Add(cam.target,j); }
    cam.up={0,1,0}; cam.fovy=70; cam.projection=CAMERA_PERSPECTIVE; return cam;
}

// ─── SHADOW MAP (FBO depth-only) ─────────────────────────────────────────────
static RenderTexture2D loadShadowMap(int w,int h){ RenderTexture2D t={0};
    t.id=rlLoadFramebuffer(); t.texture.width=w; t.texture.height=h;
    if(t.id>0){ rlEnableFramebuffer(t.id);
        t.depth.id=rlLoadTextureDepth(w,h,false); t.depth.width=w; t.depth.height=h; t.depth.format=19; t.depth.mipmaps=1;
        rlFramebufferAttach(t.id,t.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_TEXTURE2D,0);
        rlDisableFramebuffer(); }
    return t; }

static Matrix gLightVP;
static void shadowPass(){
    Camera3D lc={0}; lc.position=Vector3Add(gPlayerPos,Vector3Scale(Vector3Negate(gSunDir),70));
    lc.target=gPlayerPos; lc.up={0,1,0}; lc.fovy=170; lc.projection=CAMERA_ORTHOGRAPHIC;
    rlEnableFramebuffer(gShadowRT.id); rlViewport(0,0,SHADOWRES,SHADOWRES);
    rlClearScreenBuffers();
    rlMatrixMode(RL_PROJECTION); rlPushMatrix(); rlLoadIdentity();
    rlMatrixMode(RL_MODELVIEW); rlLoadIdentity();
    BeginMode3D(lc);
        Matrix mv=rlGetMatrixModelview(), pr=rlGetMatrixProjection(); gLightVP=MatrixMultiply(mv,pr);
        BeginShaderMode(gDepth); drawWorldGeometry(true); EndShaderMode();
    EndMode3D();
    rlMatrixMode(RL_PROJECTION); rlPopMatrix(); rlMatrixMode(RL_MODELVIEW); rlLoadIdentity();
    rlDisableFramebuffer();
    rlViewport(0,0,GetScreenWidth(),GetScreenHeight());
}

static void renderFrame(){
    shadowPass();
    Camera3D cam=makeCamera();
    // uniforms scene
    float sd[3]={gSunDir.x,gSunDir.y,gSunDir.z}; SetShaderValue(gScene,slcSun,sd,SHADER_UNIFORM_VEC3);
    float sc[3]={gSunCol.x,gSunCol.y,gSunCol.z}; SetShaderValue(gScene,slcSunCol,sc,SHADER_UNIFORM_VEC3);
    float am[3]={gAmbCol.x,gAmbCol.y,gAmbCol.z}; SetShaderValue(gScene,slcAmb,am,SHADER_UNIFORM_VEC3);
    float vp[3]={cam.position.x,cam.position.y,cam.position.z}; SetShaderValue(gScene,slcView,vp,SHADER_UNIFORM_VEC3);
    float fc[3]={gFogCol.x,gFogCol.y,gFogCol.z}; SetShaderValue(gScene,slcFogCol,fc,SHADER_UNIFORM_VEC3);
    float fd=0.0042f; SetShaderValue(gScene,slcFog,&fd,SHADER_UNIFORM_FLOAT);
    int sres=SHADOWRES; SetShaderValue(gScene,slcShadowRes,&sres,SHADER_UNIFORM_INT);
    SetShaderValueMatrix(gScene,slcLightVP,gLightVP);

    // ── scene -> sceneRT ──
    BeginTextureMode(gSceneRT);
        ClearBackground(BLACK);
        rlDisableDepthTest();
        DrawRectangleGradientV(0,0,SCREEN_W,SCREEN_H,skyTopC(),skyBotC());
        rlDrawRenderBatchActive(); rlEnableDepthTest();
        BeginMode3D(cam);
            BeginShaderMode(gScene);
                // enregistre le shadow map aupres du batch raylib (lie au bon slot au flush)
                Texture2D st={0}; st.id=gShadowRT.depth.id; st.width=SHADOWRES; st.height=SHADOWRES; st.mipmaps=1; st.format=19;
                SetShaderValueTexture(gScene,slcShadow,st);
                drawWorldGeometry(false);
            EndShaderMode();
            drawEmissive();
        EndMode3D();
    EndTextureMode();

    // ── bloom : bright -> blur H -> blur V ──
    BeginTextureMode(gBrightRT); ClearBackground(BLACK); BeginShaderMode(gBright);
        DrawTextureRec(gSceneRT.texture,{0,0,(float)SCREEN_W,-(float)SCREEN_H},{0,0},WHITE); EndShaderMode(); EndTextureMode();
    float bw=(float)gBrightRT.texture.width, bh=(float)gBrightRT.texture.height;
    BeginTextureMode(gBlurRT[0]); ClearBackground(BLACK); BeginShaderMode(gBlur);
        { float dir[2]={1,0},sz[2]={bw,bh}; SetShaderValue(gBlur,blcDir,dir,SHADER_UNIFORM_VEC2); SetShaderValue(gBlur,blcSize,sz,SHADER_UNIFORM_VEC2); }
        DrawTextureRec(gBrightRT.texture,{0,0,bw,-bh},{0,0},WHITE); EndShaderMode(); EndTextureMode();
    BeginTextureMode(gBlurRT[1]); ClearBackground(BLACK); BeginShaderMode(gBlur);
        { float dir[2]={0,1},sz[2]={bw,bh}; SetShaderValue(gBlur,blcDir,dir,SHADER_UNIFORM_VEC2); SetShaderValue(gBlur,blcSize,sz,SHADER_UNIFORM_VEC2); }
        DrawTextureRec(gBlurRT[0].texture,{0,0,bw,-bh},{0,0},WHITE); EndShaderMode(); EndTextureMode();

    // ── composite a l'ecran ──
    BeginDrawing();
        ClearBackground(BLACK);
        DrawTextureRec(gSceneRT.texture,{0,0,(float)SCREEN_W,-(float)SCREEN_H},{0,0},WHITE);
        BeginBlendMode(BLEND_ADDITIVE);
            DrawTexturePro(gBlurRT[1].texture,{0,0,bw,-bh},{0,0,(float)SCREEN_W,(float)SCREEN_H},{0,0},0,WHITE);
        EndBlendMode();
        // HUD dessine apres (dans main)
}

// ─── HUD ─────────────────────────────────────────────────────────────────────
static void drawBar(int x,int y,int w,int h,float v,float max,Color c,const char* label){
    DrawRectangle(x-1,y-1,w+2,h+2,Fade(BLACK,0.5f)); DrawRectangle(x,y,(int)(w*(v/max)),h,c);
    DrawRectangleLines(x,y,w,h,Fade(WHITE,0.5f)); DrawText(label,x,y-16,14,RAYWHITE); }
static void drawHUD(){ int W=SCREEN_W,H=SCREEN_H; Camera3D cam=makeCamera();
    DrawText(TextFormat("$%d",gMoney),W-190,18,36,GREEN);
    for(int i=0;i<5;i++){ Color col=(i<gStars)?GOLD:Fade(GRAY,0.4f); DrawText("*",W-190+i*32,64,42,col); }
    if(gStars>0&&((int)(GetTime()*2)%2)==0)DrawText("RECHERCHE",W-200,104,22,RED);
    drawBar(20,30,220,18,gHealth,100,RED,"VIE"); if(gArmor>0)drawBar(20,66,220,14,gArmor,100,SKYBLUE,"ARMURE");
    DrawText((gInCar>=0)?"AU VOLANT":"A PIED",20,92,22,RAYWHITE);
    if(gInCar>=0)DrawText(TextFormat("%.0f km/h",fabsf(gCars[gInCar].speed)*3.6f),20,118,26,YELLOW);
    DrawText(TextFormat("%02dh%02d",(int)gTime,(int)((gTime-(int)gTime)*60)),W/2-44,46,22, isNightLights()?(Color){150,170,255,255}:(Color){255,230,160,255});
    Weapon& w=gWeapons[gWeapon]; DrawRectangle(W-230,H-78,210,56,Fade(BLACK,0.55f));
    DrawText(w.name,W-220,H-72,24,RAYWHITE);
    if(gReload>0)DrawText("RELOAD...",W-220,H-46,24,ORANGE); else DrawText(TextFormat("%d",w.ammo),W-220,H-46,28,(w.ammo>0)?LIME:RED);
    DrawText("1 PISTOL  2 UZI  3 FUSIL",W-220,H-94,14,Fade(RAYWHITE,0.7f));
    DrawText(TextFormat("KILLS: %d",gKills),20,150,20,ORANGE);
    if(gNetMode!=0){ const char* role=(gNetMode==1)?"HOTE":"CLIENT";
        if(gPeerConnected) DrawText(TextFormat("CO-OP %s : connecte",role),20,176,18,LIME);
        else DrawText(TextFormat("CO-OP %s : en attente...",role),20,176,18,ORANGE); }
    DrawText("GTA VI",W/2-60,15,30,Fade(MAGENTA,0.7f)); DrawText("VICE CITY",20,H-118,26,Fade(PINK,0.8f));
    DrawRectangle(0,H-26,W,26,Fade(BLACK,0.5f));
    DrawText("ZQSD  SOURIS  CLIC tirer  R reload  F voiture  1/2/3 arme  V vue 1e/3e pers  ESPACE saut/frein  T debloquer  ECHAP",14,H-21,15,RAYWHITE);
    DrawFPS(W-95,6);
    // damage numbers projetes
    for(auto& d:gDmgNums){ Vector2 sp=GetWorldToScreen(d.pos,cam); if(sp.x>0&&sp.x<W&&sp.y>0&&sp.y<H){ int a=(int)(255*Clamp(d.life,0,1));
        DrawText(TextFormat("-%d",d.amount),(int)sp.x,(int)sp.y,22,(Color){d.col.r,d.col.g,d.col.b,(unsigned char)a}); } }
    // reticule + hitmarker
    if(gInCar<0&&!gDead){ Color cc=Fade(WHITE,0.85f);
        DrawLine(W/2-12,H/2,W/2-4,H/2,cc); DrawLine(W/2+4,H/2,W/2+12,H/2,cc);
        DrawLine(W/2,H/2-12,W/2,H/2-4,cc); DrawLine(W/2,H/2+4,W/2,H/2+12,cc); DrawCircleLines(W/2,H/2,2,cc);
        if(gHitmark>0){ Color hm=Fade(RED,Clamp(gHitmark/0.12f,0,1));
            DrawLine(W/2-10,H/2-10,W/2-4,H/2-4,hm); DrawLine(W/2+10,H/2-10,W/2+4,H/2-4,hm);
            DrawLine(W/2-10,H/2+10,W/2-4,H/2+4,hm); DrawLine(W/2+10,H/2+10,W/2+4,H/2+4,hm); } }
    if(gMuzzle>0)DrawRectangle(0,0,W,H,Fade(YELLOW,0.06f));
    if(gHurtFx>0)DrawRectangle(0,0,W,H,Fade(RED,gHurtFx*0.5f));
    if(gNight>0.4f)DrawRectangle(0,0,W,H,Fade((Color){10,10,40,255},gNight*0.12f));
    if(gDead){ DrawRectangle(0,0,W,H,Fade(MAROON,0.55f)); const char* t="WASTED"; int fs=90,tw=MeasureText(t,fs);
        DrawText(t,W/2-tw/2+3,H/2-45+3,fs,BLACK); DrawText(t,W/2-tw/2,H/2-45,fs,RED);
        const char* s="Vous reapparaissez a l'hopital..."; int sw=MeasureText(s,24); DrawText(s,W/2-sw/2,H/2+50,24,RAYWHITE); }
}
static void drawMinimap(){ int H=SCREEN_H; int mx=20,my=H-300,ms=150;
    DrawRectangle(mx-2,my-2,ms+4,ms+4,Fade(BLACK,0.6f)); DrawRectangleLines(mx-2,my-2,ms+4,ms+4,Fade(WHITE,0.5f));
    auto m=[&](float wx,float wz){ return Vector2{ mx+(wx+WORLD)/(WORLD*2)*ms, my+(wz+WORLD)/(WORLD*2)*ms }; };
    for(auto& b:gBuildings){ Vector2 p=m(b.pos.x,b.pos.z); DrawRectangle((int)p.x-1,(int)p.y-1,3,3, b.enterable?Fade(YELLOW,0.7f):Fade(LIGHTGRAY,0.6f)); }
    for(auto& c:gCars){ if(!c.alive)continue; Vector2 p=m(c.pos.x,c.pos.z); DrawCircle((int)p.x,(int)p.y,2, c.isPolice?BLUE:(c.occupied?GREEN:RED)); }
    for(auto& pd:gPeds){ if(!pd.alive)continue; Vector2 p=m(pd.pos.x,pd.pos.z); DrawCircle((int)p.x,(int)p.y,1.5f, pd.isPolice?SKYBLUE:Fade(WHITE,0.7f)); }
    if(gHeli.active){ Vector2 p=m(gHeli.pos.x,gHeli.pos.z); DrawCircle((int)p.x,(int)p.y,3,VIOLET); }
    Vector2 pp=m(gPlayerPos.x,gPlayerPos.z); DrawCircle((int)pp.x,(int)pp.y,4,YELLOW);
    DrawLine((int)pp.x,(int)pp.y,(int)(pp.x+cosf(gPlayerYaw)*10),(int)(pp.y+sinf(gPlayerYaw)*10),YELLOW); }

// ═══════════════════════════════════════════════════════════════════════════
//   RESEAU (co-op TCP) — monde host-autoritaire, framing length-prefixe
// ═══════════════════════════════════════════════════════════════════════════
static bool gServerHeadless=false;        // mode --server (pas de joueur local)
struct NBuf { uint8_t d[65536]; int n; NBuf():n(0){} };
static void W8(NBuf&b,uint8_t v){ b.d[b.n++]=v; }
static void Wf(NBuf&b,float v){ memcpy(b.d+b.n,&v,4); b.n+=4; }
static void W16(NBuf&b,uint16_t v){ memcpy(b.d+b.n,&v,2); b.n+=2; }
static uint8_t  R8 (const uint8_t*d,int&o){ return d[o++]; }
static float    Rf (const uint8_t*d,int&o){ float v; memcpy(&v,d+o,4); o+=4; return v; }
static uint16_t R16(const uint8_t*d,int&o){ uint16_t v; memcpy(&v,d+o,2); o+=2; return v; }

static std::vector<uint8_t> gRx;
static uint8_t gNetBuf[65536];

static void sendraw(socket_t fd,const uint8_t*p,int n){ int s=0; while(s<n){ int r=(int)send(fd,(const char*)p+s,n-s,0);
    if(r<=0){
#ifdef _WIN32
        if(r<0 && WSAGetLastError()==WSAEWOULDBLOCK) continue;
#else
        if(r<0 && (errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK)) continue;
#endif
        gPeerConnected=false; return; } s+=r; } }
static void sendFrame(socket_t fd,const NBuf&b){ uint32_t len=b.n; uint8_t h[4]; memcpy(h,&len,4); sendraw(fd,h,4); sendraw(fd,b.d,b.n); }
static bool recvLatest(socket_t fd,uint8_t type,uint8_t*out,int&outLen){ uint8_t tmp[16384]; int r;
    while((r=(int)recv(fd,(char*)tmp,sizeof(tmp),RX_FLAGS))>0) gRx.insert(gRx.end(),tmp,tmp+r);
    if(r==0) gPeerConnected=false;
    bool got=false;
    while(gRx.size()>=4){ uint32_t len; memcpy(&len,gRx.data(),4); if(len>60000){gRx.clear();break;} if(gRx.size()<4+(size_t)len)break;
        if(len>=1 && gRx[4]==type){ memcpy(out,gRx.data()+4,len); outLen=(int)len; got=true; }
        gRx.erase(gRx.begin(),gRx.begin()+4+len); }
    return got; }

// ── host -> client : snapshot du monde ──
static void buildSnapshot(NBuf&b){ b.n=0; W8(b,'S'); Wf(b,gTime);
    Wf(b,gPlayerPos.x);Wf(b,gPlayerPos.y);Wf(b,gPlayerPos.z); Wf(b,gPlayerYaw);
    W8(b,(uint8_t)Clamp(gHealth,0,255)); W8(b,(gServerHeadless||gDead)?0:1); W8(b,(uint8_t)gWeapon);
    uint16_t nc=0; int pc=b.n; W16(b,0);
    for(auto&c:gCars){ if(nc>=80)break; Wf(b,c.pos.x);Wf(b,c.pos.z);Wf(b,c.angle); W8(b,(uint8_t)c.type);
        W8(b,c.color.r);W8(b,c.color.g);W8(b,c.color.b);
        W8(b,(uint8_t)((c.isPolice?1:0)|(c.alive?2:0)|(c.occupied?4:0))); W8(b,(uint8_t)Clamp(c.health,0,255)); nc++; }
    memcpy(b.d+pc,&nc,2);
    uint16_t np=0; int pp=b.n; W16(b,0);
    for(auto&p:gPeds){ if(np>=120)break; Wf(b,p.pos.x);Wf(b,p.pos.z);Wf(b,p.facing);
        W8(b,p.shirt.r);W8(b,p.shirt.g);W8(b,p.shirt.b); W8(b,p.skin.r);W8(b,p.skin.g);W8(b,p.skin.b); W8(b,p.hair.r);W8(b,p.hair.g);W8(b,p.hair.b);
        W8(b,(uint8_t)((p.alive?1:0)|(p.isPolice?2:0))); W8(b,(uint8_t)Clamp(p.fall*255,0,255)); np++; }
    memcpy(b.d+pp,&np,2);
}
static void applySnapshot(const uint8_t*d,int len){ int o=1; gTime=Rf(d,o);
    gRemote.pos.x=Rf(d,o);gRemote.pos.y=Rf(d,o);gRemote.pos.z=Rf(d,o); gRemote.yaw=Rf(d,o);
    gRemote.health=R8(d,o); gRemote.alive=R8(d,o)!=0; gRemote.weapon=R8(d,o);
    uint16_t nc=R16(d,o); gCars.clear();
    for(int i=0;i<nc;i++){ Car c={0}; c.pos.x=Rf(d,o);c.pos.z=Rf(d,o);c.pos.y=0.65f; c.angle=Rf(d,o); c.type=R8(d,o);
        c.color.r=R8(d,o);c.color.g=R8(d,o);c.color.b=R8(d,o);c.color.a=255; uint8_t fl=R8(d,o); c.health=R8(d,o);
        c.isPolice=fl&1; c.alive=fl&2; c.occupied=fl&4; c.sirenPhase=(float)GetTime()*8; if(!c.alive)c.wreckTimer=1; gCars.push_back(c); }
    uint16_t np=R16(d,o); gPeds.clear();
    for(int i=0;i<np;i++){ Pedestrian p={0}; p.pos.x=Rf(d,o);p.pos.z=Rf(d,o); p.facing=Rf(d,o);
        p.shirt={R8(d,o),R8(d,o),R8(d,o),255}; p.skin={R8(d,o),R8(d,o),R8(d,o),255}; p.hair={R8(d,o),R8(d,o),R8(d,o),255};
        p.pants={40,40,50,255}; uint8_t fl=R8(d,o); p.fall=R8(d,o)/255.0f; p.alive=fl&1; p.isPolice=fl&2;
        p.bob=(float)GetTime()*6; if(!p.alive)p.deadTimer=1; gPeds.push_back(p); }
}
// ── client -> host : input/avatar ──
static void buildInput(NBuf&b){ b.n=0; W8(b,'I');
    Wf(b,gPlayerPos.x);Wf(b,gPlayerPos.y);Wf(b,gPlayerPos.z); Wf(b,gPlayerYaw); Wf(b,gPlayerPitch);
    W8(b,gDead?0:1); W8(b,(uint8_t)gWeapon); W8(b,gShotFired?1:0);
    Wf(b,gShotA.x);Wf(b,gShotA.y);Wf(b,gShotA.z); Wf(b,gShotB.x);Wf(b,gShotB.y);Wf(b,gShotB.z);
    gShotFired=false; }
static void remoteShoot(Vector3 a,Vector3 bb){ Vector3 dr=Vector3Subtract(bb,a); float range=Vector3Length(dr); if(range<0.1f)return; dr=Vector3Scale(dr,1.0f/range);
    int wi=gRemote.weapon; if(wi<0)wi=0; if(wi>2)wi=2; int dmg=gWeapons[wi].dmg; float best=range; int hp=-1,hc=-1;
    for(size_t i=0;i<gPeds.size();++i){ if(!gPeds[i].alive)continue; Vector3 c={gPeds[i].pos.x,1.3f,gPeds[i].pos.z},oc=Vector3Subtract(c,a); float t=Vector3DotProduct(oc,dr); if(t<0||t>best)continue; if(Vector3Distance(Vector3Add(a,Vector3Scale(dr,t)),c)<0.8f){best=t;hp=(int)i;hc=-1;} }
    for(size_t i=0;i<gCars.size();++i){ if(!gCars[i].alive)continue; Vector3 c=gCars[i].pos,oc=Vector3Subtract(c,a); float t=Vector3DotProduct(oc,dr); if(t<0||t>best)continue; if(Vector3Distance(Vector3Add(a,Vector3Scale(dr,t)),c)<2.2f){best=t;hc=(int)i;hp=-1;} }
    Vector3 e=Vector3Add(a,Vector3Scale(dr,best)); gBullets.push_back({a,e,0.05f,{255,230,120,255}});
    if(hp>=0){ gPeds[hp].health-=dmg; spawnBlood(e,8); if(gPeds[hp].health<=0)killPed(gPeds[hp],true); else gPeds[hp].fleeTimer=6; }
    else if(hc>=0){ gCars[hc].health-=dmg; spawnSpark(e); if(gCars[hc].health<=0&&gCars[hc].alive){gCars[hc].alive=false;gCars[hc].wreckTimer=8;spawnExplosion(gCars[hc].pos);} } }
static void applyInput(const uint8_t*d,int len){ int o=1;
    gRemote.pos.x=Rf(d,o);gRemote.pos.y=Rf(d,o);gRemote.pos.z=Rf(d,o); gRemote.yaw=Rf(d,o); gRemote.pitch=Rf(d,o);
    gRemote.alive=R8(d,o)!=0; gRemote.weapon=R8(d,o); uint8_t shoot=R8(d,o);
    Vector3 a={Rf(d,o),Rf(d,o),Rf(d,o)}, bb={Rf(d,o),Rf(d,o),Rf(d,o)};
    if(shoot && gRemote.shotTimer<=0){ remoteShoot(a,bb); gRemote.shotTimer=0.07f; } }

// ── sockets ──
static void setNB(socket_t fd){
#ifdef _WIN32
    u_long m=1; ioctlsocket(fd,FIONBIO,&m);
#else
    int f=fcntl(fd,F_GETFL,0); fcntl(fd,F_SETFL,f|O_NONBLOCK);
#endif
}
static bool hostStart(int port){ gListenFd=socket(AF_INET,SOCK_STREAM,0); if(gListenFd==SOCK_INVALID)return false;
    int yes=1; setsockopt(gListenFd,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof(yes));
    sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(port);
    if(bind(gListenFd,(sockaddr*)&a,sizeof(a))<0){ perror("bind"); return false; }
    listen(gListenFd,1); setNB(gListenFd); printf("[NET] host en ecoute sur le port %d\n",port); return true; }
static void hostAccept(){ if(gPeerConnected)return; socket_t fd=accept(gListenFd,0,0); if(fd==SOCK_INVALID)return;
    int yes=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,(const char*)&yes,sizeof(yes));
#ifdef _WIN32
    setNB(fd);
#endif
    gConnFd=fd; gPeerConnected=true; gRx.clear(); printf("[NET] joueur connecte !\n"); }
static bool clientConnect(const char*host,int port){ gConnFd=socket(AF_INET,SOCK_STREAM,0); if(gConnFd==SOCK_INVALID)return false;
    sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons(port);
    if(inet_pton(AF_INET,host,&a.sin_addr)<=0){ struct hostent* he=gethostbyname(host); if(!he){printf("[NET] host introuvable: %s\n",host);return false;} memcpy(&a.sin_addr,he->h_addr,he->h_length); }
    if(connect(gConnFd,(sockaddr*)&a,sizeof(a))<0){ perror("[NET] connect"); return false; }
    int yes=1; setsockopt(gConnFd,IPPROTO_TCP,TCP_NODELAY,(const char*)&yes,sizeof(yes));
#ifdef _WIN32
    setNB(gConnFd);
#endif
    gPeerConnected=true; gRx.clear(); printf("[NET] connecte au host %s:%d\n",host,port); return true; }
static void hostPump(float dt){ hostAccept(); if(!gPeerConnected)return;
    if(gRemote.shotTimer>0)gRemote.shotTimer-=dt;
    int len; if(recvLatest(gConnFd,'I',gNetBuf,len)) applyInput(gNetBuf,len);
    NBuf b; buildSnapshot(b); sendFrame(gConnFd,b);
    if(!gPeerConnected){ CLOSESOCK(gConnFd); gConnFd=SOCK_INVALID; printf("[NET] joueur deconnecte\n"); } }
static void clientPump(float){ if(!gPeerConnected)return;
    int len; if(recvLatest(gConnFd,'S',gNetBuf,len)) applySnapshot(gNetBuf,len);
    NBuf b; buildInput(b); sendFrame(gConnFd,b); }

// boucle serveur dediee (headless, sans fenetre) — conteneurisable
static int runServer(int port){ gNetMode=1; gServerHeadless=true;
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2),&wsa);
#endif
    srand(1234); buildCity();
    if(!hostStart(port)) return 1;
    printf("[NET] serveur dedie demarre. En attente de joueur...\n");
    auto last=std::chrono::steady_clock::now();
    while(true){ auto now=std::chrono::steady_clock::now();
        float dt=std::chrono::duration<float>(now-last).count(); last=now; if(dt>0.05f)dt=0.05f;
        update(dt,true); hostPump(dt);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return 0; }

// ─── MAIN ────────────────────────────────────────────────────────────────────
int main(int argc,char**argv){
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2),&wsa);
#else
    signal(SIGPIPE,SIG_IGN);
#endif
    int port=7777; const char* connectHost=nullptr;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--server")){ gNetMode=1; }
        else if(!strcmp(argv[i],"--host")){ gNetMode=1; }
        else if(!strcmp(argv[i],"--connect")&&i+1<argc){ gNetMode=2; connectHost=argv[++i]; }
        else if(!strcmp(argv[i],"--port")&&i+1<argc){ port=atoi(argv[++i]); }
    }
    if(getenv("GTA6_SERVER")){ static std::string hp=getenv("GTA6_SERVER"); size_t c=hp.find(':');
        gNetMode=2; if(c!=std::string::npos){ port=atoi(hp.c_str()+c+1); hp=hp.substr(0,c); } static std::string h2=hp; connectHost=h2.c_str(); }
    if(getenv("GTA6_PORT")) port=atoi(getenv("GTA6_PORT"));

    // mode serveur dedie = headless (pas de fenetre)
    bool headless=false; for(int i=1;i<argc;i++) if(!strcmp(argv[i],"--server")) headless=true;
    if(headless) return runServer(port);

    InitWindow(SCREEN_W,SCREEN_H,"GTA VI - Vice City 3D NEXTGEN v4 (par Claude)");
    SetTargetFPS(60); DisableCursor();
    InitAudioDevice(); gAudioOK=IsAudioDeviceReady();
    if(gAudioOK){ gSndGun=makeNoise(.18f,22,.6f); gSndThud=makeNoise(.25f,10,.8f); gSndHurt=makeNoise(.12f,28,.4f); gSndBoom=makeNoise(.5f,6,.9f); }

    // RT + shaders
    gSceneRT=LoadRenderTexture(SCREEN_W,SCREEN_H);
    gBrightRT=LoadRenderTexture(SCREEN_W/2,SCREEN_H/2);
    gBlurRT[0]=LoadRenderTexture(SCREEN_W/2,SCREEN_H/2); gBlurRT[1]=LoadRenderTexture(SCREEN_W/2,SCREEN_H/2);
    gShadowRT=loadShadowMap(SHADOWRES,SHADOWRES);
    gScene=LoadShaderFromMemory(VS_SCENE,FS_SCENE);
    gDepth=LoadShaderFromMemory(VS_DEPTH,FS_DEPTH);
    gBright=LoadShaderFromMemory(0,FS_BRIGHT);
    gBlur=LoadShaderFromMemory(0,FS_BLUR);
    gScene.locs[SHADER_LOC_MATRIX_MODEL]=GetShaderLocation(gScene,"matModel");
    slcLightVP=GetShaderLocation(gScene,"lightVP"); slcSun=GetShaderLocation(gScene,"sunDir");
    slcSunCol=GetShaderLocation(gScene,"sunColor"); slcAmb=GetShaderLocation(gScene,"ambient");
    slcView=GetShaderLocation(gScene,"viewPos"); slcFog=GetShaderLocation(gScene,"fogDensity");
    slcFogCol=GetShaderLocation(gScene,"fogColor"); slcShadow=GetShaderLocation(gScene,"shadowMap");
    slcShadowRes=GetShaderLocation(gScene,"shadowRes");
    blcDir=GetShaderLocation(gBlur,"dir"); blcSize=GetShaderLocation(gBlur,"sz");

    srand(1234); buildCity(); updateSky();

    // init reseau (host GUI ou client)
    if(gNetMode==1){ if(!hostStart(port)){ printf("[NET] echec hote -> solo\n"); gNetMode=0; } }
    else if(gNetMode==2){ if(!clientConnect(connectHost,port)){ printf("[NET] echec connexion -> solo\n"); gNetMode=0; } }

    const char* shot=getenv("GTA6_SCREENSHOT"); int cd=shot?40:-1;
    const char* th=getenv("GTA6_TIME"); if(th)gTime=(float)atof(th);
    if(getenv("GTA6_TPS")) gThirdPerson=true;
    if(shot){ gStars=4; gWantedTimer=20; gWeapon=1; spawnPoliceCar(); spawnCop(); spawnCop();
        if(!gPeds.empty())killPed(gPeds[0],true); spawnExplosion({10,1,6}); }

    while(!WindowShouldClose()){
        float dt=GetFrameTime(); if(dt>0.05f)dt=0.05f;
        bool client=(gNetMode==2);
        update(dt, !client);              // le client ne simule pas le monde (recu du host)
        if(gNetMode==1) hostPump(dt);
        else if(gNetMode==2) clientPump(dt);
        renderFrame();     // ouvre BeginDrawing, dessine scene+bloom
        drawMinimap(); drawHUD();
        EndDrawing();
        if(cd>0&&--cd==0){ TakeScreenshot(shot); break; }
    }
    if(gConnFd!=SOCK_INVALID)CLOSESOCK(gConnFd); if(gListenFd!=SOCK_INVALID)CLOSESOCK(gListenFd);

    UnloadShader(gScene); UnloadShader(gDepth); UnloadShader(gBright); UnloadShader(gBlur);
    UnloadRenderTexture(gSceneRT); UnloadRenderTexture(gBrightRT); UnloadRenderTexture(gBlurRT[0]); UnloadRenderTexture(gBlurRT[1]);
    if(gAudioOK){ UnloadSound(gSndGun);UnloadSound(gSndThud);UnloadSound(gSndHurt);UnloadSound(gSndBoom);CloseAudioDevice(); }
    CloseWindow();
    printf("\n=== GTA VI NEXTGEN v4 ===\nArgent $%d  Kills %d  Etoiles %d/5\n",gMoney,gKills,gStars);
    return 0;
}
