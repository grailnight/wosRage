#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <cstdarg>
#include <iterator>
#include "native_signatures.h"
struct Vec {float x,y,z;};
struct Signal {DWORD magic,pid,active,heartbeat,remaining,total,growMs;};
static Signal* signal;
static constexpr int Tendrils=10;
static constexpr int Pieces=14;
static constexpr int Pairs=Tendrils/2;
static constexpr float Reach=.45f;   // strand length relative to V10
static constexpr float Girth=.55f;   // strand thickness relative to V10
static unsigned handles[Tendrils*Pieces];
static Vec positions[Tendrils][21],velocities[Tendrils][21];
static ULONGLONG previousTick,fadeTick;
static float reveal=0;
static float hudFraction=0;
static unsigned hudSeconds=0;
static unsigned owner;
static bool hooked=false, failed=false, visible=false;
static unsigned phase=0;
static void releaseDrops(unsigned manager);
static void releaseFx(unsigned manager);
static void releaseDrips(unsigned manager);
static bool createFx(unsigned manager);
static ULONGLONG lastClick=0;
static wchar_t logPath[MAX_PATH];
static void log(const char* text) {FILE*f=nullptr;_wfopen_s(&f,logPath,L"a");if(f){fprintf(f,"%llu phase=%u %s\n",GetTickCount64(),phase,text);fclose(f);}}
template<class T> T& field(unsigned p,unsigned o=0){return *reinterpret_cast<T*>(p+o);}
static unsigned resolve(unsigned h){if(!h)return 0;unsigned table=field<unsigned>(0xFFEA24);if(!table)return 0;unsigned slot=table+(h&0x7FFF)*8;return field<unsigned>(slot,4)==h?field<unsigned>(slot):0;}
static void show(unsigned p,bool on){auto f=reinterpret_cast<void(__thiscall*)(void*,int,int)>(field<unsigned>(field<unsigned>(p),0x10c));f((void*)p,on?1:0,0);}
static void cleanup(unsigned manager){for(auto &h:handles){unsigned p=resolve(h);if(p){show(p,false);reinterpret_cast<void(__thiscall*)(void*,void*)>(0x7fbf20)((void*)(manager+0x88),(void*)p);}h=0;}visible=false;owner=0;reveal=0;releaseDrops(manager);releaseFx(manager);releaseDrips(manager);log("Effects removed");}
static Vec plus(Vec a,Vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
static Vec minus(Vec a,Vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static Vec mul(Vec a,float s){return {a.x*s,a.y*s,a.z*s};}
static float length(Vec a){return sqrtf(a.x*a.x+a.y*a.y+a.z*a.z);}
static float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static Vec transform(Vec p,const float*m){return {m[12]+m[0]*p.x+m[4]*p.y+m[8]*p.z,m[13]+m[1]*p.x+m[5]*p.y+m[9]*p.z,m[14]+m[2]*p.x+m[6]*p.y+m[10]*p.z};}
// WOSTweaks jointPosing: hero+0xc points directly to the pose block.
// Offsets confirmed from dinput8.dll joint table (Chest, Neck, L/R Shoulder).
static bool torsoFrame(unsigned hero,const float*actor,float*out){
 unsigned holder=field<unsigned>(hero,0xc);if(!holder)return false;
 unsigned pose=holder;
 Vec chest=field<Vec>(pose,0x450),neck=field<Vec>(pose,0x490);
 Vec left=field<Vec>(pose,0x590),right=field<Vec>(pose,0xd10);
 for(Vec p:{chest,neck,left,right})if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||length(p)>5)return false;
 Vec up=minus(neck,chest);float ul=length(up);if(ul<.05f||ul>.6f)return false;up=mul(up,1/ul);
 Vec side=minus(right,left);side=minus(side,mul(up,dot(side,up)));float sl=length(side);if(sl<.04f||sl>.8f)return false;side=mul(side,1/sl);
 Vec forward=cross(side,up);Vec origin=transform(chest,actor);
 Vec axes[]={side,up,forward};memset(out,0,64);
 for(int i=0;i<3;i++){Vec a=axes[i];out[i*4]=actor[0]*a.x+actor[4]*a.y+actor[8]*a.z;out[i*4+1]=actor[1]*a.x+actor[5]*a.y+actor[9]*a.z;out[i*4+2]=actor[2]*a.x+actor[6]*a.y+actor[10]*a.z;}
 out[12]=origin.x;out[13]=origin.y;out[14]=origin.z;out[15]=1;return true;
}
static Vec previousActor={},filteredSpeed={},localSpeed={};
static ULONGLONG motionTick=0;
static void updateMotion(Vec actor,const float*torso,ULONGLONG tick,bool reset){
 float dt=motionTick?float(tick-motionTick)*.001f:0;motionTick=tick;
 Vec delta=minus(actor,previousActor);previousActor=actor;
 if(reset||dt<=0||dt>.2f||length(delta)>4){filteredSpeed={};localSpeed={};return;}
 Vec speed=mul(delta,1/dt);float magnitude=length(speed);if(magnitude>25)speed=mul(speed,25/magnitude);
 filteredSpeed=plus(filteredSpeed,mul(minus(speed,filteredSpeed),1-expf(-dt/.16f)));
 localSpeed={dot(filteredSpeed,{torso[0],torso[1],torso[2]}),dot(filteredSpeed,{torso[4],torso[5],torso[6]}),dot(filteredSpeed,{torso[8],torso[9],torso[10]})};
}
static Vec bendForSpeed(Vec point,Vec root,float u){
 // The first 15% remains rigidly attached; drag increases toward the tip.
 float w=fmaxf(0.f,(u-.15f)/.85f);w=w*w*(3-2*w);
 Vec relative=minus(point,root);float rest=length(relative);
 float fold=fminf(1.f,length(localSpeed)/9.f)*.32f*w;
 Vec bent={relative.x*(1-fold),relative.y*(1-fold),relative.z};
 bent=minus(bent,mul(localSpeed,.085f*w));float size=length(bent);
 if(size>.0001f)bent=mul(bent,rest/size);
 return plus(root,bent);
}
static float strike=0;     // 1 at the click, decays to 0
static int strikePair=0;    // which pair lashes
static float bloom=0;       // thickening overshoot when the tendrils finish emerging
static void updateStrike(bool on,float dt){
 static bool held=false;
 bool down=on && (GetAsyncKeyState(VK_LBUTTON)&0x8000) && GetForegroundWindow() && reveal>.9f;
 DWORD owner=0;GetWindowThreadProcessId(GetForegroundWindow(),&owner);
 if(owner!=GetCurrentProcessId())down=false;
 if(down && !held){strike=1;strikePair=(strikePair+2)%Pairs;lastClick=GetTickCount64();}
 held=down;
 strike=fmaxf(0.f,strike-dt/.38f);
}
// Lash envelope: fast snap out (first 30%), slower recoil.
static float lash(int n){
 if(n/2!=strikePair && n/2!=(strikePair+1)%Pairs)return 0;
 float k=1-strike;float e=k<.3f?sinf(k/.3f*1.5708f):cosf((k-.3f)/.7f*1.5708f);
 return strike>0?e*(n/2==strikePair?1.f:.55f):0;
}
// Fixed per-strand variation in [-1,1], so the layout is irregular but stable.
static float jitter(int n,int k){unsigned h=unsigned(n)*2654435761u^unsigned(k)*2246822519u;h^=h>>15;h*=2246822519u;h^=h>>13;return float(h&0xFFFF)/32767.5f-1;}
static Vec target(int n,float u,float t,const float*m){
 float side=(n&1)?1.f:-1.f;
 // Pairs are spread evenly over the original three-pair height range.
 float pair=float(n/2)*2.f/float(Pairs-1);
 // Roots cluster on the upper back: the top pair sits under the shoulders,
 // lower pairs step down between the shoulder blades.
 float root=.24f-.06f*pair+.03f*jitter(n,1),rootX=.16f-.035f*pair+.025f*jitter(n,2),rootZ=-.07f+.02f*jitter(n,3);
 float reach=Reach*(1+.22f*jitter(n,4)),spread=1+.3f*jitter(n,5),lift=1+.35f*jitter(n,6);
 float phase=t*(1.3f+.13f*n)+n*2.17f;
 // Targets are relative to the animated chest, not the actor origin.
 float x=side*(rootX+.76f*reach*spread*sinf(u*1.75f))+.20f*reach*u*sinf(phase-u*5.4f);
 float y=root+(.88f-.49f*pair)*reach*lift*sinf(u*2.4f)-.30f*reach*u*u+.24f*reach*u*u*sinf(phase*1.37f-u*6.1f);
 float z=rootZ-.85f*reach*u+.30f*reach*u*u*cosf(phase*.73f-u*4.6f);
 // Strike: the strand arcs over the shoulder and snaps forward.
 float l=lash(n)*u*u;
 x=x*(1-.45f*l);y+=.55f*l*sinf(u*3.1416f)-.25f*l*u;z+=2.1f*l;
 return transform(bendForSpeed({x,y,z},{side*rootX,root,rootZ},u),m);
}
static void simulate(float t,const float*m,bool reset){
 auto tick=GetTickCount64();float dt=previousTick?float(tick-previousTick)*.001f:1.f/60;previousTick=tick;
 if(dt>.2f)reset=true;if(dt>.04f)dt=.04f;if(dt<.001f)dt=.001f;
 for(int n=0;n<Tendrils;n++){
  Vec root=target(n,0,t,m);if(length(minus(root,positions[n][0]))>4)reset=true;
  for(int j=0;j<=20;j++){
   Vec goal=target(n,j/20.f,t,m);
   if(reset||j<=3){positions[n][j]=goal;velocities[n][j]={};continue;}
   // Independently damped points lag behind turns and acceleration.
   float stiffness=110.f-65.f*j/20.f;
   Vec force=minus(mul(minus(goal,positions[n][j]),stiffness),mul(velocities[n][j],8.f));
   velocities[n][j]=plus(velocities[n][j],mul(force,dt));
   positions[n][j]=plus(positions[n][j],mul(velocities[n][j],dt));
  }
  // Enforce segment lengths so movement bends the strand rather than stretching it.
  for(int pass=0;pass<5;pass++)for(int j=4;j<=20;j++){
   Vec delta=minus(positions[n][j],positions[n][j-1]);float d=length(delta);
   float rest=length(minus(target(n,j/20.f,t,m),target(n,(j-1)/20.f,t,m)));
   if(d>.0001f){Vec correction=mul(delta,(d-rest)/d*.65f);positions[n][j]=minus(positions[n][j],correction);if(j>4)positions[n][j-1]=plus(positions[n][j-1],mul(correction,.35f));}
  }
 }
}
static Vec curve(int piece,int j,float,const float*m){
 int n=piece/Pieces;
 float u=((piece%Pieces)+(j/15.f)*1.04f-.02f)/Pieces;
 u=fmaxf(0.f,fminf(1.f,u));float x=u*20;int a=(int)x;if(a>=20)a=19;
 float t=x-a,t2=t*t,t3=t2*t;
 Vec p0=positions[n][a>0?a-1:0],p1=positions[n][a],p2=positions[n][a+1],p3=positions[n][a<18?a+2:20];
 Vec result=mul(plus(plus(mul(p1,2),mul(minus(p2,p0),t)),plus(mul(plus(minus(mul(p0,2),mul(p1,5)),minus(mul(p2,4),p3)),t2),mul(plus(minus(mul(p1,3),p0),minus(p3,mul(p2,3))),t3))),.5f);
 float eased=reveal*reveal*(3-2*reveal);
 return transform(plus(positions[n][0],mul(minus(result,positions[n][0]),eased)),m);
}
static float width(int piece,float t){
 int n=piece/Pieces;float u=(piece%Pieces+.5f)/Pieces;
 float base=(.006f+.061f*powf(1.f-u,1.35f))*Girth;
 float pulse=1+.13f*sinf(t*3.4f+n*1.9f-u*7.f)*(.4f+.6f*u);
 float hit=1+.35f*lash(n)*sinf(u*3.1416f);
 return base*pulse*hit*(1+bloom*(1-u*.5f));
}

// ---- Symbiote splash: short black strands thrown from the striking fist ----
// The game's own hit effects are keyed to animation events that the swapped
// wall clips do not provide, so the splash is driven by fist motion instead.
static constexpr int Drops=12;
static unsigned dropHandles[Drops];
static Vec dropOrigin[Drops],dropDir[Drops];
static double dropStart[Drops];
static bool dropLive[Drops];
static Vec prevHand[2];static float handPeak[2];static Vec peakDir[2];static double handCooldown[2];
static bool handInit=false;
static const unsigned HandOffset[2]={0x690,0xe10}; // L Hand, R Hand in the pose block
static unsigned createDrop(unsigned manager){
 unsigned p=reinterpret_cast<unsigned(__thiscall*)(void*,int)>(0x804090)((void*)(manager+0x88),0x2000);
 if(!p)return 0;
 unsigned h=field<unsigned>(p,4);
 reinterpret_cast<void(__thiscall*)(void*)>(0x654a00)((void*)p);
 float*mat=field<float*>(p,0x10);memset(mat,0,64);mat[0]=mat[5]=mat[10]=mat[15]=1;
 field<float>(p,0x100)=.001f;field<unsigned>(p,0x108)=8;field<float>(p,0x110)=30.f;
 reinterpret_cast<void(__thiscall*)(void*,const char*)>(0x615760)((void*)p,"tentacleblackspidey");
 Vec zero={0,-1000,0};for(int j=0;j<16;j++)reinterpret_cast<void(__thiscall*)(void*,const Vec*)>(0x661b20)((void*)p,&zero);
 reinterpret_cast<void(__thiscall*)(void*,int,int)>(0x96f1b0)((void*)(p+0xbc),8,1);
 show(p,false);return h;
}
static void releaseDrops(unsigned manager){
 for(int i=0;i<Drops;i++){unsigned p=resolve(dropHandles[i]);if(p){show(p,false);reinterpret_cast<void(__thiscall*)(void*,void*)>(0x7fbf20)((void*)(manager+0x88),(void*)p);}dropHandles[i]=0;dropLive[i]=false;}
 handInit=false;
}
static float rnd(unsigned&seed){seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;}
static void spawnSplash(Vec origin,Vec dir,double now){
 static unsigned seed=12345;int spawned=0;
 float dl=length(dir);dir=dl>.0001f?mul(dir,1/dl):Vec{0,1,0};
 for(int i=0;i<Drops && spawned<6;i++){
  if(dropLive[i]||!dropHandles[i])continue;
  Vec r={rnd(seed)*2-1,rnd(seed)*2-1,rnd(seed)*2-1};
  Vec d=plus(dir,mul(r,.75f));d.y+=.25f;float l=length(d);d=mul(d,1/l);
  dropOrigin[i]=origin;dropDir[i]=mul(d,.25f+.35f*rnd(seed));dropStart[i]=now;dropLive[i]=true;spawned++;
  unsigned p=resolve(dropHandles[i]);if(p)show(p,true);
 }
}

// ---- Super-hit effects: tendril burst, ground shockwave, flash, fist trail ----
// All are native polytubes like the shoulder tendrils; only the material differs.
enum FxKind{FxBurst,FxRing,FxFlash,FxTrail,FxKinds};
static constexpr int FxCount[FxKinds]={10,12,6,2};
static const char*FxMaterial[FxKinds]={"tentacleblackspidey","efx_wave_a","efx_flash_fb","efx_bsuit_blur"};
static constexpr int FxMax=12;
struct Fx{unsigned handle;bool live;double start;Vec origin,dir;float seed,scale;};
static bool activationPending=false; // set when the Rage timer starts, consumed where the pose is known
static Fx fx[FxKinds][FxMax];
static double prewarmUntil=0; // effect materials are drawn hair-thin once so the first hit is not lost to loading
static Vec trailPoints[2][16];static int trailHead[2];static float trailAlpha[2];
static unsigned createTube(unsigned manager,const char*material){
 unsigned p=reinterpret_cast<unsigned(__thiscall*)(void*,int)>(0x804090)((void*)(manager+0x88),0x2000);
 if(!p)return 0;
 unsigned h=field<unsigned>(p,4);
 reinterpret_cast<void(__thiscall*)(void*)>(0x654a00)((void*)p);
 float*mat=field<float*>(p,0x10);memset(mat,0,64);mat[0]=mat[5]=mat[10]=mat[15]=1;
 field<float>(p,0x100)=.001f;field<unsigned>(p,0x108)=8;field<float>(p,0x110)=30.f;
 reinterpret_cast<void(__thiscall*)(void*,const char*)>(0x615760)((void*)p,material);
 Vec zero={0,-1000,0};for(int j=0;j<16;j++)reinterpret_cast<void(__thiscall*)(void*,const Vec*)>(0x661b20)((void*)p,&zero);
 reinterpret_cast<void(__thiscall*)(void*,int,int)>(0x96f1b0)((void*)(p+0xbc),8,1);
 show(p,false);return h;
}
static bool createFx(unsigned manager){
 for(int k=0;k<FxKinds;k++)for(int i=0;i<FxCount[k];i++){fx[k][i]={};fx[k][i].handle=createTube(manager,FxMaterial[k]);if(!fx[k][i].handle)return false;}
 trailAlpha[0]=trailAlpha[1]=0;prewarmUntil=GetTickCount64()*.001+.6;return true;
}
static void releaseFx(unsigned manager){
 for(int k=0;k<FxKinds;k++)for(int i=0;i<FxCount[k];i++){unsigned p=resolve(fx[k][i].handle);if(p){show(p,false);reinterpret_cast<void(__thiscall*)(void*,void*)>(0x7fbf20)((void*)(manager+0x88),(void*)p);}fx[k][i]={};}
}
static void setPoint(unsigned p,int j,Vec q){reinterpret_cast<void(__thiscall*)(void*,int,const Vec*)>(0x492a10)((void*)(p+0xbc),j,&q);}
static void startFx(int k,Vec origin,Vec dir,double now,int count,float scale=1){
 static unsigned seed=777;
 for(int i=0;i<FxCount[k] && count>0;i++){
  Fx&f=fx[k][i];if(!f.handle)continue;
  if(f.live){
   if(k!=FxRing)continue;
   int oldest=i;for(int o=0;o<FxCount[k];o++)if(fx[k][o].start<fx[k][oldest].start)oldest=o;
   if(oldest!=i || now-f.start<.12)continue;
  }
  Vec r={rnd(seed)*2-1,rnd(seed)*2-1,rnd(seed)*2-1};float rl=length(r);if(rl<.0001f)r={0,1,0};else r=mul(r,1/rl);
  f.live=true;f.start=now;f.origin=origin;f.seed=rnd(seed);f.scale=scale;
  f.dir=k==FxBurst?Vec{r.x,fabsf(r.y)*.8f+.1f,r.z}:k==FxFlash?r:dir;
  unsigned p=resolve(f.handle);if(p)show(p,true);count--;
 }
}
// Activation: the same burst and ring, larger and slower, from under the feet.
static void spawnActivation(Vec chest,float ground,double now){
 startFx(FxBurst,chest,{0,1,0},now,FxCount[FxBurst],1.7f);
 for(int i=0;i<FxCount[FxRing];i++){Vec d={cosf(i*6.2832f/FxCount[FxRing]),0,sinf(i*6.2832f/FxCount[FxRing])};startFx(FxRing,{chest.x,ground,chest.z},d,now,1,1.7f);}
}
static void spawnSuperHit(Vec hit,Vec chest,float ground,double now){
 startFx(FxBurst,chest,{0,1,0},now,FxCount[FxBurst]);
 for(int i=0;i<FxCount[FxRing];i++){Vec d={cosf(i*6.2832f/FxCount[FxRing]),0,sinf(i*6.2832f/FxCount[FxRing])};startFx(FxRing,{chest.x,ground,chest.z},d,now,1);}
 startFx(FxFlash,hit,{0,1,0},now,FxCount[FxFlash]);
}
static void updateFx(unsigned hero,double now,bool trailOn[2],Vec hands[2]){
 if(now<prewarmUntil){
  const float*actor=field<float*>(hero,0x10);
  if(actor)for(int k=0;k<FxKinds;k++)for(int i=0;i<FxCount[k];i++){
   Fx&f=fx[k][i];if(f.live)continue;unsigned p=resolve(f.handle);if(!p)continue;
   show(p,true);for(int j=0;j<16;j++)setPoint(p,j,{actor[12],actor[13]+1+j*.01f,actor[14]});
   field<float>(p,0x100)=.0005f;field<unsigned char>(p,0xb0)=0;
  }
 }else if(prewarmUntil>0){
  for(int k=0;k<FxKinds;k++)for(int i=0;i<FxCount[k];i++){Fx&f=fx[k][i];if(!f.live){unsigned p=resolve(f.handle);if(p)show(p,false);}}
  prewarmUntil=0;
 }
 for(int k=0;k<FxKinds-1;k++)for(int i=0;i<FxCount[k];i++){
  Fx&f=fx[k][i];if(!f.live)continue;unsigned p=resolve(f.handle);if(!p){f.live=false;continue;}
  float age=float(now-f.start);
  float grow=sqrtf(fmaxf(1.f,f.scale));
  float life=(k==FxBurst?.38f:k==FxRing?.45f:.18f)*(k==FxFlash?1.f:grow);
  if(age>life){show(p,false);f.live=false;continue;}
  float a=age/life,width=0;
  for(int j=0;j<16;j++){
   float s=j/15.f;Vec q;
   if(k==FxBurst){
    // Shoots out in 0.1 s, then pulls back into the chest.
    float len=1.1f*f.scale*(a<.26f?sinf(a/.26f*1.5708f):cosf((a-.26f)/.74f*1.5708f));
    Vec side=cross(f.dir,{0,1,0});float sl=length(side);side=sl>.001f?mul(side,1/sl):Vec{1,0,0};
    q=plus(f.origin,mul(f.dir,len*s));q=plus(q,mul(side,.08f*sinf(s*9+f.seed*20+age*30)*s));
    width=.05f*grow*(1-a*.6f);
    field<float>(p,0x100)=width*(1-.85f*s*s);
   }else if(k==FxRing){
    // Arc of the expanding ground ring; each tube covers one sector.
    float radius=.3f+2.7f*f.scale*(1-(1-a)*(1-a));
    float ang0=atan2f(f.dir.z,f.dir.x),ang=ang0+(s-.5f)*6.2832f/FxCount[FxRing]*1.08f;
    q={f.origin.x+cosf(ang)*radius,f.origin.y+.06f,f.origin.z+sinf(ang)*radius};
    width=.2f*grow*(1-a);field<float>(p,0x100)=width;
   }else{
    float len=.55f*(1-a);
    q=plus(f.origin,mul(f.dir,(s-.5f)*2*len));
    width=.24f*(1-a);field<float>(p,0x100)=width*(1-fabsf(s-.5f));
   }
   setPoint(p,j,q);
  }
  field<unsigned char>(p,0xb0)=0;
 }
 // Trail: the last 16 fist positions, fading in while the fist moves fast.
 for(int h=0;h<2;h++){
  Fx&f=fx[FxTrail][h];unsigned p=resolve(f.handle);if(!p)continue;
  trailHead[h]=(trailHead[h]+1)%16;trailPoints[h][trailHead[h]]=hands[h];
  trailAlpha[h]=fmaxf(0.f,fminf(1.f,trailAlpha[h]+(trailOn[h]?.25f:-.12f)));
  if(trailAlpha[h]<=0){if(f.live){show(p,false);f.live=false;}continue;}
  if(!f.live){show(p,true);f.live=true;}
  for(int j=0;j<16;j++)setPoint(p,j,trailPoints[h][(trailHead[h]+1+j)%16]);
  field<float>(p,0x100)=.05f*trailAlpha[h];field<unsigned char>(p,0xb0)=0;
 }
}

// ---- Ichor drips: drops fall from tendril tips and leave fading stains ----
static constexpr int Drips=16;
struct Drip{unsigned handle;int state;double start;Vec pos,vel;float ground,size;}; // state 0 idle, 1 falling, 2 stain
static Drip drips[Drips];
static double nextDrip=0;
static bool createDrips(unsigned manager){
 for(auto&d:drips){d={};d.handle=createTube(manager,"tentacleblackspidey");if(!d.handle)return false;}
 nextDrip=0;return true;
}
static void releaseDrips(unsigned manager){
 for(auto&d:drips){unsigned p=resolve(d.handle);if(p){show(p,false);reinterpret_cast<void(__thiscall*)(void*,void*)>(0x7fbf20)((void*)(manager+0x88),(void*)p);}d={};}
}
static void updateDrips(const float*torso,float ground,bool on,double now,float dt){
 static unsigned seed=4242;
 if(on&&reveal>.95f&&now>=nextDrip){
  if(nextDrip>0)for(auto&d:drips){
   if(d.state||!d.handle)continue;
   int n=int(rnd(seed)*Tendrils)%Tendrils;
   d.pos=transform(positions[n][20],torso);d.vel={(rnd(seed)-.5f)*.4f,-.2f,(rnd(seed)-.5f)*.4f};
   d.ground=ground+.02f;d.size=.7f+.6f*rnd(seed);d.start=now;d.state=1;
   unsigned p=resolve(d.handle);if(p)show(p,true);
   break;
  }
  nextDrip=now+.2+.4*rnd(seed);
 }
 if(!on)nextDrip=0;
 for(auto&d:drips){
  if(!d.state)continue;
  unsigned p=resolve(d.handle);if(!p){d.state=0;continue;}
  if(d.state==1){
   d.vel.y-=9.8f*dt;d.pos=plus(d.pos,mul(d.vel,dt));
   if(d.pos.y<=d.ground||now-d.start>3){d.pos.y=d.ground;d.state=2;d.start=now;}
   // A short streak stretched along the fall speed.
   float streak=.025f+fminf(.12f,length(d.vel)*.012f);
   Vec back=mul(d.vel,-1/fmaxf(.001f,length(d.vel)));
   for(int j=0;j<16;j++)setPoint(p,j,plus(d.pos,mul(back,streak*j/15.f)));
   field<float>(p,0x100)=.011f*d.size;
  }else{
   float age=float(now-d.start),life=1.5f;
   if(age>life){show(p,false);d.state=0;continue;}
   // Flat stain: a short tube lying on the ground, widening then fading.
   float a=age/life,r=.05f*d.size*(1+.6f*fminf(1.f,age/.15f));
   for(int j=0;j<16;j++){float s=j/15.f-.5f;setPoint(p,j,{d.pos.x+s*r,d.ground,d.pos.z+s*r*.35f});}
   field<float>(p,0x100)=.018f*d.size*(1-a*a);
  }
  field<unsigned char>(p,0xb0)=0;
 }
}
// World height of the ground under the hero: the lowest foot joint. The actor
// origin sits at the pelvis, about a metre above the feet.
static float groundHeight(unsigned hero,const float*actor){
 unsigned pose=field<unsigned>(hero,0xc);float best=actor[13]-1.f;bool found=false;
 static const unsigned feet[]={0x15d0,0x1610,0x1710,0x1750};
 if(pose)for(unsigned o:feet){Vec f=field<Vec>(pose,o);if(!std::isfinite(f.y)||length(f)>3)continue;float y=transform(f,actor).y;if(!found||y<best){best=y;found=true;}}
 return best;
}
static void updateSplash(unsigned hero,bool on,double now,float dt){
 unsigned pose=field<unsigned>(hero,0xc);const float*actor=field<float*>(hero,0x10);
 if(activationPending&&pose&&actor&&fx[FxRing][0].handle){activationPending=false;spawnActivation(transform(field<Vec>(pose,0x450),actor),groundHeight(hero,actor),now);}
 if(pose&&actor&&on&&reveal>.9f&&dt>0){
  for(int k=0;k<2;k++){
   Vec hand=field<Vec>(pose,HandOffset[k]);
   if(!std::isfinite(hand.x)||length(hand)>3)continue;
   if(!handInit){prevHand[k]=hand;continue;}
   Vec v=mul(minus(hand,prevHand[k]),1/dt);prevHand[k]=hand;float sp=length(v);
   bool recentClick=GetTickCount64()-lastClick<1500;
   if(sp>handPeak[k]){handPeak[k]=sp;peakDir[k]=v;}
   else if(handPeak[k]>3.2f && sp<handPeak[k]*.6f){
    if(recentClick && now>handCooldown[k]){
     Vec world=transform(hand,actor);
     Vec wdir={actor[0]*peakDir[k].x+actor[4]*peakDir[k].y+actor[8]*peakDir[k].z,actor[1]*peakDir[k].x+actor[5]*peakDir[k].y+actor[9]*peakDir[k].z,actor[2]*peakDir[k].x+actor[6]*peakDir[k].y+actor[10]*peakDir[k].z};
     spawnSplash(world,wdir,now);handCooldown[k]=now+.22;
     spawnSuperHit(world,transform(field<Vec>(pose,0x450),actor),groundHeight(hero,actor),now);
    }
    handPeak[k]=0;
   }
  }
  handInit=true;
 }
 {bool trailOn[2]={false,false};Vec hands[2]={{0,-1000,0},{0,-1000,0}};
  if(pose&&actor)for(int k=0;k<2;k++){Vec hl=field<Vec>(pose,HandOffset[k]);if(std::isfinite(hl.x)&&length(hl)<3){hands[k]=transform(hl,actor);trailOn[k]=on&&reveal>.9f&&handPeak[k]>2.f&&GetTickCount64()-lastClick<900;}}
  updateFx(hero,now,trailOn,hands);}
 for(int i=0;i<Drops;i++){
  if(!dropLive[i])continue;
  unsigned p=resolve(dropHandles[i]);if(!p){dropLive[i]=false;continue;}
  float age=float(now-dropStart[i]);
  if(age>.45f){show(p,false);dropLive[i]=false;continue;}
  float grow=fminf(1.f,age/.12f);
  for(int j=0;j<16;j++){
   float s=j/15.f,t=age*(.35f+.65f*s);
   Vec q=plus(dropOrigin[i],mul(dropDir[i],(.4f+3.2f*t)*s*grow));q.y-=4.5f*t*t*s;
   reinterpret_cast<void(__thiscall*)(void*,int,const Vec*)>(0x492a10)((void*)(p+0xbc),j,&q);
  }
  field<float>(p,0x100)=.03f*(1-age/.45f);field<unsigned char>(p,0xb0)=0;
 }
}
static unsigned create(int n,float t,const float*m,unsigned manager){
 phase=1;
 unsigned p=reinterpret_cast<unsigned(__thiscall*)(void*,int)>(0x804090)((void*)(manager+0x88),0x2000);
 if(!p)return 0;
 unsigned h=field<unsigned>(p,4);handles[n]=h;
 phase=2;reinterpret_cast<void(__thiscall*)(void*)>(0x654a00)((void*)p);
 // Control points below are world-space; the effect's own transform is identity.
 float*mat=field<float*>(p,0x10);memset(mat,0,64);mat[0]=mat[5]=mat[10]=mat[15]=1;
 float u=(n%Pieces+.5f)/Pieces;field<float>(p,0x100)=(.006f+.061f*powf(1.f-u,1.35f))*Girth;field<unsigned>(p,0x108)=8;field<float>(p,0x110)=30.f;
 phase=3;reinterpret_cast<void(__thiscall*)(void*,const char*)>(0x615760)((void*)p,"tentacleblackspidey");
 phase=4;for(int j=0;j<16;j++){Vec v=curve(n,j,t,m);reinterpret_cast<void(__thiscall*)(void*,const Vec*)>(0x661b20)((void*)p,&v);}
 phase=5;reinterpret_cast<void(__thiscall*)(void*,int,int)>(0x96f1b0)((void*)(p+0xbc),8,1);
 phase=6;show(p,true);log("Created native shoulder polytube");return h;
}
static void update(){
 bool on=signal && signal->magic==0x5241474B && signal->pid==GetCurrentProcessId() && signal->active==1 && DWORD(GetTickCount64()-signal->heartbeat)<1500;
 auto now=GetTickCount64();float fadeDt=fadeTick?fminf(.05f,float(now-fadeTick)*.001f):.016f;fadeTick=now;
 {static bool running=false;bool now_running=on&&signal->total&&signal->remaining+150<signal->total;if(now_running&&!running)activationPending=true;if(!on)activationPending=false;running=now_running;}
 hudFraction=on?fminf(1.f,float(signal->remaining)/fmaxf(1.f,float(signal->total))):0;hudSeconds=on?(signal->remaining+999)/1000:0;
 {float before=reveal;
 reveal=fmaxf(0.f,fminf(1.f,reveal+(on?fadeDt/fmaxf(.1f,signal->growMs*.001f):-fadeDt/.45f)));
 if(before<1 && reveal>=1)bloom=.45f;
 bloom=fmaxf(0.f,bloom-fadeDt/.5f);
 updateStrike(on,fadeDt);}
 unsigned manager=field<unsigned>(0x10fc54c);if(!manager)return;
 unsigned hero=field<unsigned>(manager,0x120);
 if((!on && reveal<=.001f) || !hero){if(visible)cleanup(manager);return;}
 unsigned heroHandle=field<unsigned>(hero,4);if(resolve(heroHandle)!=hero)return;
 if(owner && owner!=heroHandle){cleanup(manager);return;}
 const float*m=field<float*>(hero,0x10);if(!m || !std::isfinite(m[12])||!std::isfinite(m[13])||!std::isfinite(m[14]))return;
 float t=float(GetTickCount64()%1000000)*.001f;
 float torso[16];if(!torsoFrame(hero,m,torso)){if(visible)cleanup(manager);return;}
 updateMotion({m[12],m[13],m[14]},torso,now,!visible);
 m=torso;
 // Simulate in the torso frame, then transform all points with the CURRENT pose.
 // Thus translation/roll cannot leave the roots or the body of a strand behind.
 static const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
 simulate(t,identity,!visible);
 if(!visible){owner=heroHandle;visible=true;for(int n=0;n<Tendrils*Pieces;n++){if(!create(n,t,m,manager)){cleanup(manager);failed=true;log("Allocation failed");return;}}for(int i=0;i<Drops;i++){dropHandles[i]=createDrop(manager);if(!dropHandles[i]){cleanup(manager);failed=true;log("Splash allocation failed");return;}}if(!createFx(manager)){cleanup(manager);failed=true;log("Super-hit allocation failed");return;}if(!createDrips(manager)){cleanup(manager);failed=true;log("Drip allocation failed");return;}log("Rage shoulders ON");}
 phase=7;
 for(int n=0;n<Tendrils*Pieces;n++){unsigned p=resolve(handles[n]);if(!p){cleanup(manager);return;}for(int j=0;j<16;j++){Vec v=curve(n,j,t,m);reinterpret_cast<void(__thiscall*)(void*,int,const Vec*)>(0x492a10)((void*)(p+0xbc),j,&v);}field<float>(p,0x100)=width(n,t);field<unsigned char>(p,0xb0)=0;}
 phase=8;updateSplash(hero,on,now*.001,fadeDt);
 {const float*actor=field<float*>(hero,0x10);if(actor){phase=9;updateDrips(m,groundHeight(hero,actor),on,now*.001,fadeDt);}}
 phase=0;
}
static void frame(){
 if(failed)return;
 __try{update();}__except(EXCEPTION_EXECUTE_HANDLER){failed=true;log("Native call fault; effect disabled. Restart game before retry.");}
}


#include "rage_hud.h"

// Optional live test module. Does not replace any import DLL or change startup.
using EndSceneFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
static EndSceneFn originalEndScene;
static HRESULT STDMETHODCALLTYPE liveEndScene(IDirect3DDevice9* device){frame();drawHud(device);return originalEndScene(device);}
#include "rage_inproc.h"
// Loaded at game start by Play - WoS.exe: wait for the game's D3D device, chain
// EndScene for the effects, then run the Rage controller on this thread.
static DWORD WINAPI attach(LPVOID){
 if((unsigned)GetModuleHandleW(nullptr)!=0x400000){log("Unsupported base");return 1;}
 for(;;){
  bool ready=false;
  __try{
   bool signaturesOk=true;
   for(const auto&sig:signatures)if(memcmp((void*)sig.address,sig.bytes,8)){signaturesOk=false;break;}
   if(!signaturesOk){log("Signature mismatch; Rage disabled");return 2;}
   ready=*reinterpret_cast<IDirect3DDevice9**>(0x11170bc)!=nullptr;
  }__except(EXCEPTION_EXECUTE_HANDLER){}
  if(ready)break;
  Sleep(500);
 }
 Sleep(2000);
 __try {
  auto device=*reinterpret_cast<IDirect3DDevice9**>(0x11170bc);
  void**vtable=*reinterpret_cast<void***>(device);
  MEMORY_BASIC_INFORMATION info{};
  if(!VirtualQuery(vtable[42],&info,sizeof(info))||info.State!=MEM_COMMIT||!(info.Protect&0xf0)){log("Invalid EndScene pointer");return 4;}
  auto prior=vtable[42];originalEndScene=(EndSceneFn)prior;
  DWORD protection=0;
  if(!VirtualProtect(vtable+42,sizeof(void*),PAGE_EXECUTE_READWRITE,&protection)){log("Cannot access device vtable");return 5;}
  auto found=InterlockedCompareExchangePointer(vtable+42,(void*)liveEndScene,prior);
  DWORD ignored;VirtualProtect(vtable+42,sizeof(void*),protection,&ignored);
  if(found!=prior){log("Another hook changed EndScene; no change applied");return 6;}
  log("WoS_Rage READY: EndScene chained");
 }__except(EXCEPTION_EXECUTE_HANDLER){log("Attach failed; effect not enabled");return 7;}
 signal=&localSignal;
 controllerLoop();
 return 0;
}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){
  DisableThreadLibraryCalls(module);GetModuleFileNameW(module,logPath,MAX_PATH);
  wcscpy_s(iniPath,logPath);
  wchar_t*slash=wcsrchr(logPath,L'\\');if(slash)wcscpy_s(slash+1,MAX_PATH-(slash+1-logPath),L"WoS_Rage.log");
  slash=wcsrchr(iniPath,L'\\');if(slash)wcscpy_s(slash+1,MAX_PATH-(slash+1-iniPath),L"WoS_Rage.ini");
  HANDLE worker=CreateThread(nullptr,0,attach,nullptr,0,nullptr);if(worker)CloseHandle(worker);else return FALSE;
 }
 return TRUE;
}
