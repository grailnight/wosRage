// In-process Rage controller: the former rage_controller.py, running on a worker
// thread inside the game. It publishes its state into localSignal, which the
// effect code reads exactly as it read the shared mapping before.
#pragma once
#include "rage_data.h"

static Signal localSignal{};

// ---------------------------------------------------------------- memory ----
static bool readMem(unsigned addr,void*out,unsigned size){
 if(!addr)return false;
 __try{memcpy(out,(void*)addr,size);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static unsigned rdU32(unsigned addr){unsigned v=0;return readMem(addr,&v,4)?v:0;}
static bool copyMem(unsigned addr,const BYTE*src,unsigned size){
 __try{memcpy((void*)addr,src,size);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Writes only when the current bytes are exactly `expected`, then reads back.
static bool writeMem(unsigned addr,const BYTE*expected,const BYTE*replacement,unsigned size){
 BYTE current[32];if(size>sizeof(current)||!readMem(addr,current,size)||memcmp(current,expected,size))return false;
 DWORD old=0;bool changed=false;
 MEMORY_BASIC_INFORMATION info{};
 if(VirtualQuery((void*)addr,&info,sizeof(info))&&!(info.Protect&(PAGE_READWRITE|PAGE_EXECUTE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_WRITECOPY))){
  if(!VirtualProtect((void*)addr,size,PAGE_READWRITE,&old))return false;changed=true;
 }
 bool ok=copyMem(addr,replacement,size);
 if(changed){DWORD ignored;VirtualProtect((void*)addr,size,old,&ignored);}
 return ok&&readMem(addr,current,size)&&!memcmp(current,replacement,size);
}

// ---------------------------------------------------------------- config ----
struct RageConfig{int key;float introSeconds,introGrow,rageSeconds,stillSpeed,whipHold,whipRepeat,suitSettle;bool autoSuit;char introClip[32];};
static RageConfig cfg;
static wchar_t iniPath[MAX_PATH];
static float iniFloat(const wchar_t*name,float fallback){
 wchar_t def[32],buf[32];swprintf_s(def,L"%g",fallback);
 if(GetPrivateProfileStringW(L"Rage",name,L"",buf,32,iniPath)==0)WritePrivateProfileStringW(L"Rage",name,def,iniPath);
 GetPrivateProfileStringW(L"Rage",name,def,buf,32,iniPath);return float(_wtof(buf));
}
static void loadConfig(){
 wchar_t key[8];
 if(GetPrivateProfileStringW(L"Rage",L"ActivationKey",L"",key,8,iniPath)==0)WritePrivateProfileStringW(L"Rage",L"ActivationKey",L"R",iniPath);
 GetPrivateProfileStringW(L"Rage",L"ActivationKey",L"R",key,8,iniPath);
 cfg.key=(key[0]==L'F'||key[0]==L'f')&&key[1]?0x6F+_wtoi(key+1):towupper(key[0]);
 cfg.introSeconds=iniFloat(L"IntroSeconds",3.5f);cfg.introGrow=iniFloat(L"IntroGrowSeconds",2.5f);
 cfg.rageSeconds=iniFloat(L"RageSeconds",20);cfg.stillSpeed=iniFloat(L"StillSpeed",.6f);
 cfg.whipHold=iniFloat(L"WhipHoldSeconds",.25f);cfg.whipRepeat=iniFloat(L"WhipRepeatSeconds",.45f);
 cfg.suitSettle=iniFloat(L"SuitSettleSeconds",.6f);cfg.autoSuit=iniFloat(L"AutoBlackSuit",1)!=0;
 wchar_t clip[32];
 if(GetPrivateProfileStringW(L"Rage",L"IntroClip",L"",clip,32,iniPath)==0)WritePrivateProfileStringW(L"Rage",L"IntroClip",L"bs3fidgit_2",iniPath);
 GetPrivateProfileStringW(L"Rage",L"IntroClip",L"bs3fidgit_2",clip,32,iniPath);
 memset(cfg.introClip,0,32);WideCharToMultiByte(CP_ACP,0,clip,-1,cfg.introClip,31,nullptr,nullptr);
}

// ----------------------------------------------------------------- state ----
struct Edit{unsigned addr;BYTE old[4],neu[4];};
static constexpr unsigned IdleNodes[3]={0x57F2F486,0xA4ED3147,0xF1E76E08};
static constexpr unsigned ClipType=0xC39F70A0;
static struct{
 bool scanned;unsigned bases[4];int baseCount;
 Edit swaps[32];int swapCount;Edit whips[128];int whipCount;
 unsigned idleSlots[16];int idleCount;BYTE idleOld[16][32];bool introWritten[16];
 bool active,whipMode;double deadline,introDeadline;float introOrigin[3];
 double nextScan;
}rs;
static double nowSeconds(){return GetTickCount64()*.001;}
static void clog(const char*fmt,...){char text[256];va_list args;va_start(args,fmt);vsnprintf(text,sizeof(text),fmt,args);va_end(args);log(text);}

static bool baseValid(unsigned base){
 BYTE got[20];
 unsigned want=WhipRAnimHash;if(!readMem(base+WhipRNameOffset-4,got,18)||memcmp(got,&want,4)||memcmp(got+4,"smbichorwhipr",14))return false;
 want=WhipLAnimHash;if(!readMem(base+WhipLNameOffset-4,got,18)||memcmp(got,&want,4)||memcmp(got+4,"smbichorwhipl",14))return false;
 return true;
}
static bool allValid(){if(!rs.baseCount)return false;for(int i=0;i<rs.baseCount;i++)if(!baseValid(rs.bases[i]))return false;return true;}

// One 4-byte-aligned pass over readable memory (skipping this module) collects
// ALS copies, loaded-animation table entries and idle clip slots.
static bool scanGame(){
 memset(&rs,0,offsetof(decltype(rs),active));
 HMODULE self=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)&scanGame,&self);
 MEMORY_BASIC_INFORMATION selfInfo{};VirtualQuery(self,&selfInfo,sizeof(selfInfo));
 unsigned selfStart=(unsigned)selfInfo.AllocationBase,selfEnd=selfStart;
 for(unsigned a=selfStart;;){MEMORY_BASIC_INFORMATION i{};if(!VirtualQuery((void*)a,&i,sizeof(i))||(unsigned)i.AllocationBase!=selfStart)break;a+=(unsigned)i.RegionSize;selfEnd=a;}
 unsigned entries[32]={};  // animation table entries, parallel to hashes below
 unsigned hashes[32];int hashCount=0;
 for(const auto&s:clipSwaps){hashes[hashCount++]=s.groundHash;hashes[hashCount++]=s.wallHash;}
 unsigned address=0x10000;
 while(address<0x7FFE0000){
  MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery((void*)address,&info,sizeof(info)))break;
  unsigned start=(unsigned)info.BaseAddress,size=(unsigned)info.RegionSize;address=start+size;
  if(info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!(info.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE)))continue;
  if(start>=selfStart&&start<selfEnd)continue;
  __try{
   const unsigned*p=(const unsigned*)start;unsigned n=size/4;
   for(unsigned i=1;i+8<n;i++){
    unsigned v=p[i];
    if(v==WhipRAnimHash&&!memcmp(&p[i+1],"smbichorwhipr",14)&&rs.baseCount<4){
     unsigned base=start+i*4+4-WhipRNameOffset;bool seen=false;for(int b=0;b<rs.baseCount;b++)seen|=rs.bases[b]==base;
     if(!seen)rs.bases[rs.baseCount++]=base;
    }else if(v==ClipType&&rs.idleCount<16&&(p[i-1]==IdleNodes[0]||p[i-1]==IdleNodes[1]||p[i-1]==IdleNodes[2])){
     rs.idleSlots[rs.idleCount++]=start+(i-1)*4+124;
    }else for(int h=0;h<hashCount;h++)if(v==hashes[h]&&!entries[h]){
     // Entry: name pointer, hash, resource pointer. The name must match.
     const ClipSwap&s=clipSwaps[h/2];const char*name=h&1?s.wall:s.ground;char buf[32];
     if(readMem(p[i-1],buf,(unsigned)strlen(name)+1)&&!memcmp(buf,name,strlen(name)+1)&&p[i+1])entries[h]=start+(i-1)*4;
    }
   }
  }__except(EXCEPTION_EXECUTE_HANDLER){}
 }
 int kept=0;for(int b=0;b<rs.baseCount;b++)if(baseValid(rs.bases[b]))rs.bases[kept++]=rs.bases[b];rs.baseCount=kept;
 if(!rs.baseCount){clog("Scan: player ALS not found");return false;}
 for(int b=0;b<rs.baseCount;b++)for(const auto&g:groundRefs){
  unsigned addr=rs.bases[b]+g.offset,tag[2];
  if(!readMem(addr-8,tag,8)||tag[0]!=13||tag[1]!=0||rdU32(addr)!=g.nodeHash||rs.whipCount>=128)continue;
  Edit&e=rs.whips[rs.whipCount++];e.addr=addr;memcpy(e.old,&g.nodeHash,4);memcpy(e.neu,&g.whipHash,4);
 }
 for(int s=0;s<int(std::size(clipSwaps));s++){
  unsigned ground=entries[s*2],wall=entries[s*2+1];
  if(!ground||!wall){clog("Scan: animation %s or %s not loaded",clipSwaps[s].ground,clipSwaps[s].wall);continue;}
  Edit&e=rs.swaps[rs.swapCount++];e.addr=ground+8;unsigned o=rdU32(ground+8),n=rdU32(wall+8);memcpy(e.old,&o,4);memcpy(e.neu,&n,4);
 }
 rs.scanned=true;
 clog("Scan: ALS copies=%d whip refs=%d clip swaps=%d idle slots=%d",rs.baseCount,rs.whipCount,rs.swapCount,rs.idleCount);
 return true;
}

// ------------------------------------------------------------ game state ----
static unsigned heroPtr(){unsigned manager=rdU32(0x10fc54c);return manager?rdU32(manager+0x120):0;}
static bool heroPosition(float out[3]){unsigned hero=heroPtr();unsigned m=hero?rdU32(hero+0x10):0;return m&&readMem(m+48,out,12);}
static int blackSuit(){unsigned hero=heroPtr();BYTE b=0;if(!hero||!readMem(hero+0xB70,&b,1))return -1;return b==1;}
static bool standing(){
 float a[3],b[3];if(!heroPosition(a))return false;Sleep(120);if(!heroPosition(b))return false;
 float d=sqrtf((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
 return d/.12f<cfg.stillSpeed;
}
static bool gameForeground(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}

// ------------------------------------------------------------------ input ----
static unsigned suitScanCode(){
 wchar_t path[MAX_PATH];if(!GetEnvironmentVariableW(L"LOCALAPPDATA",path,MAX_PATH))return 0x3A;
 wcscat_s(path,L"\\Activision\\Spider-Man Web of Shadows\\Config.xml");
 FILE*f=nullptr;_wfopen_s(&f,path,L"rb");if(!f)return 0x3A;
 char text[8192]={};fread(text,1,sizeof(text)-1,f);fclose(f);
 const char*at=strstr(text,"id=\"ActionSuit\">");if(!at)return 0x3A;
 int code=atoi(at+16);return code>256&&code<512?unsigned(code-256):0x3A;
}
static void pressScan(unsigned scan){
 INPUT in{};in.type=INPUT_KEYBOARD;in.ki.wScan=WORD(scan);in.ki.dwFlags=KEYEVENTF_SCANCODE;SendInput(1,&in,sizeof(in));
 Sleep(150);in.ki.dwFlags=KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;SendInput(1,&in,sizeof(in));
}
static void clickMouse(){
 INPUT in{};in.type=INPUT_MOUSE;in.mi.dwFlags=MOUSEEVENTF_LEFTUP;SendInput(1,&in,sizeof(in));Sleep(35);
 in.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;SendInput(1,&in,sizeof(in));
}

// ------------------------------------------------------------------ rage ----
static void publish(){
 localSignal.magic=0x5241474B;localSignal.pid=GetCurrentProcessId();
 localSignal.active=(rs.active||rs.introDeadline)?1:0;localSignal.heartbeat=DWORD(GetTickCount64());
 localSignal.total=DWORD(cfg.rageSeconds*1000);
 localSignal.remaining=rs.active?DWORD(fmaxf(0,float(rs.deadline-nowSeconds()))*1000):(rs.introDeadline?localSignal.total:0);
 localSignal.growMs=rs.introDeadline?DWORD(cfg.introGrow*1000):650;
}
static void setWhip(bool on){
 if(on==rs.whipMode||(on&&!rs.active))return;
 for(int i=0;i<rs.whipCount;i++){Edit&e=rs.whips[i];writeMem(e.addr,on?e.old:e.neu,on?e.neu:e.old,4);}
 rs.whipMode=on;clog("Tendril Whip hold: %s",on?"ON":"OFF");
}
static void endIntro(){
 BYTE name[32]={};memcpy(name,cfg.introClip,31);
 for(int i=0;i<rs.idleCount;i++)if(rs.introWritten[i]){writeMem(rs.idleSlots[i],name,rs.idleOld[i],32);rs.introWritten[i]=false;}
 rs.introDeadline=0;
}
static void startIntro(){
 BYTE name[32]={};memcpy(name,cfg.introClip,31);
 for(int i=0;i<rs.idleCount;i++){
  if(!readMem(rs.idleSlots[i],rs.idleOld[i],32)||!memcmp(rs.idleOld[i],name,32))continue;
  rs.introWritten[i]=writeMem(rs.idleSlots[i],rs.idleOld[i],name,32);
 }
 heroPosition(rs.introOrigin);rs.introDeadline=nowSeconds()+cfg.introSeconds;
 clog("RAGE INTRO: %s",cfg.introClip);
}
static void enableRage(){
 if(rs.active)return;
 for(int i=0;i<rs.swapCount;i++)if(!writeMem(rs.swaps[i].addr,rs.swaps[i].old,rs.swaps[i].neu,4)){
  for(int j=i-1;j>=0;j--)writeMem(rs.swaps[j].addr,rs.swaps[j].neu,rs.swaps[j].old,4);
  clog("RAGE failed at clip swap %d; nothing applied",i);return;
 }
 rs.active=true;rs.deadline=nowSeconds()+cfg.rageSeconds;
 clog("RAGE ON for %.0f s",cfg.rageSeconds);
}
static void restoreRage(){
 setWhip(false);
 if(rs.active)for(int i=rs.swapCount-1;i>=0;i--)writeMem(rs.swaps[i].addr,rs.swaps[i].neu,rs.swaps[i].old,4);
 if(rs.active)clog("RAGE OFF: restored");
 rs.active=false;
}
// The level or save changed under us: the old addresses are no longer the
// player's, so nothing is written back; the next scan starts from scratch.
static void forgetState(const char*why){
 if(rs.scanned)clog("Player data gone (%s); rescanning when a level is loaded",why);
 rs.scanned=false;rs.active=false;rs.whipMode=false;rs.introDeadline=0;rs.nextScan=nowSeconds()+3;
}
static bool introMoved(){
 float p[3];if(!heroPosition(p))return false;
 float d=sqrtf((p[0]-rs.introOrigin[0])*(p[0]-rs.introOrigin[0])+(p[1]-rs.introOrigin[1])*(p[1]-rs.introOrigin[1])+(p[2]-rs.introOrigin[2])*(p[2]-rs.introOrigin[2]));
 return d>.35f;
}

static void controllerLoop(){
 HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\WoS_Rage_Controller_v1");
 if(GetLastError()==ERROR_ALREADY_EXISTS){clog("Another Rage controller is already running in this game; this copy stays idle until it exits");}
 WaitForSingleObject(mutex,INFINITE);
 loadConfig();unsigned suitScan=suitScanCode();
 clog("Controller ready: key=%d suit scan=0x%X",cfg.key,suitScan);
 bool previousKey=false;double heldSince=0,nextClick=0;int upSamples=0;
 for(;;){
  Sleep(15);
  double now=nowSeconds();
  if(!heroPtr()){if(rs.scanned)forgetState("no hero");publish();continue;}
  if(!rs.scanned){if(now>=rs.nextScan&&!scanGame())rs.nextScan=now+5;publish();continue;}
  if(!allValid()){forgetState("ALS unloaded");publish();continue;}
  if(rs.introDeadline&&(now>=rs.introDeadline||introMoved())){endIntro();enableRage();}
  if(rs.active&&now>=rs.deadline)restoreRage();
  bool foreground=gameForeground();
  // Holding LMB turns ground attacks into Tendril Whip and re-clicks for each whip.
  bool mouse=foreground&&(GetAsyncKeyState(VK_LBUTTON)&0x8000);
  upSamples=mouse?0:upSamples+1;
  if(upSamples>=4)heldSince=0;else if(mouse&&!heldSince)heldSince=now;
  bool holding=heldSince&&rs.active&&cfg.whipHold>0&&now-heldSince>=cfg.whipHold;
  if(cfg.whipHold>0)setWhip(holding);
  if(holding&&now>=nextClick){clickMouse();nextClick=now+cfg.whipRepeat;}else if(!holding)nextClick=0;
  bool key=foreground&&(GetAsyncKeyState(cfg.key)&0x8000);
  if(key&&!previousKey&&!rs.active&&!rs.introDeadline){
   clog("R pressed");
   bool ready=true;
   if(cfg.autoSuit){
    int suit=blackSuit();
    if(suit==0){
     clog("Red suit: pressing suit key");pressScan(suitScan);
     double until=nowSeconds()+3;bool retried=false;ready=false;
     while(nowSeconds()<until){
      if(blackSuit()==1){Sleep(DWORD(cfg.suitSettle*1000));ready=true;clog("Black suit on");break;}
      if(!retried&&nowSeconds()>until-2){retried=true;clog("Suit unchanged after 1 s; pressing again");pressScan(suitScan);}
      Sleep(30);
     }
     if(!ready)clog("Suit did not change; Rage not started");
    }else if(suit<0){ready=false;clog("Suit state unreadable; Rage not started");}
   }
   if(ready){if(rs.idleCount&&standing())startIntro();else enableRage();}
  }
  previousKey=key;
  publish();
 }
}
