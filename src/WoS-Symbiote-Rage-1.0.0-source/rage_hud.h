// HUD inspired by the game's dark, angular red-and-black panels.
static IDirect3DTexture9* hudText;
static int lastSecond=-1;
static bool makeHudText(IDirect3DDevice9*d,int seconds){
 if(!hudText && FAILED(d->CreateTexture(256,32,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&hudText,nullptr)))return false;
 if(lastSecond==seconds)return true;
 BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=256;bi.bmiHeader.biHeight=-32;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
 void*bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bmp=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
 if(!dc||!bmp||!bits){if(bmp)DeleteObject(bmp);if(dc)DeleteDC(dc);return false;}
 auto old=SelectObject(dc,bmp);memset(bits,0,256*32*4);
 HFONT font=CreateFontW(-24,0,0,0,FW_HEAVY,TRUE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");auto oldFont=SelectObject(dc,font);SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);
 TextOutW(dc,4,2,L"RAGE",4);wchar_t text[12];swprintf_s(text,L"%02d",seconds);TextOutW(dc,217,2,text,(int)wcslen(text));GdiFlush();
 D3DLOCKED_RECT lock{};bool ok=SUCCEEDED(hudText->LockRect(0,&lock,nullptr,0));
 if(ok){for(int y=0;y<32;y++){auto src=(DWORD*)bits+y*256;auto dst=(DWORD*)((BYTE*)lock.pBits+y*lock.Pitch);for(int x=0;x<256;x++){DWORD v=src[x];unsigned a=(v&255);if(((v>>8)&255)>a)a=(v>>8)&255;if(((v>>16)&255)>a)a=(v>>16)&255;dst[x]=(a<<24)|0xffffff;}}hudText->UnlockRect(0);lastSecond=seconds;}
 SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);return ok;
}
struct HudV{float x,y,z,rhw;DWORD color;float u,v;};
static void hudQuad(IDirect3DDevice9*d,float x,float y,float w,float h,float skew,DWORD color){HudV v[]={{x+skew,y,0,1,color,0,0},{x+w+skew,y,0,1,color,1,0},{x,y+h,0,1,color,0,1},{x+w,y+h,0,1,color,1,1}};d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(HudV));}
static void drawHud(IDirect3DDevice9*d){
 if(!visible || reveal<=0 || failed)return;
 D3DVIEWPORT9 vp{};if(FAILED(d->GetViewport(&vp)))return;
 IDirect3DStateBlock9* state=nullptr;if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&state)))return;if(FAILED(state->Capture())){state->Release();return;}
 d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);
 d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_FOGENABLE,FALSE);
 d->SetTexture(0,nullptr);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
 float s=vp.Height/1080.f,w=224*s,h=48*s,x=vp.X+vp.Width-330*s,y=vp.Y+vp.Height-310*s;
 auto col=[](DWORD rgb,float alpha){return (DWORD(255*fminf(1.f,reveal*3)*alpha)<<24)|rgb;};
 hudQuad(d,x-3*s,y-3*s,w+6*s,h+6*s,8*s,col(0x040405,.95f));hudQuad(d,x,y,w,h,8*s,col(0x341519,.95f));hudQuad(d,x+3*s,y+2*s,w-6*s,h-4*s,7*s,col(0x101014,.96f));
 // Faint honeycomb detail behind the label.
 for(int row=0;row<2;row++)for(int k=0;k<15;k++){float cx=x+(10+k*15+row*7)*s,cy=y+(10+row*13)*s;HudV v[12];for(int j=0;j<6;j++){float a=j*3.14159265f/3,b=(j+1)*3.14159265f/3;v[j*2]={cx+7*s*cosf(a),cy+7*s*sinf(a),0,1,col(0x6c4449,.28f),0,0};v[j*2+1]={cx+7*s*cosf(b),cy+7*s*sinf(b),0,1,col(0x6c4449,.28f),0,0};}d->DrawPrimitiveUP(D3DPT_LINELIST,6,v,sizeof(HudV));}
 hudQuad(d,x+7*s,y+34*s,w-16*s,7*s,2*s,col(0x2a080d,1));float pulse=hudSeconds<=3?.72f+.28f*sinf(float(GetTickCount64()%10000)*.016f):1.f;
 if(hudFraction>0)hudQuad(d,x+7*s,y+34*s,(w-16*s)*hudFraction,7*s,2*s,col(0xbc2437,pulse));
 if(makeHudText(d,(int)hudSeconds)){d->SetTexture(0,hudText);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);hudQuad(d,x+10*s,y+2*s,w-18*s,29*s,0,col(0xf3e6e7,1));}
 state->Apply();state->Release();
}
