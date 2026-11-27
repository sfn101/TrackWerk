#include "ui/studio.h"
#include "studio/sing_text.h"
#include "ui/demo_font_style.h"
#include "ui/scene_layout.h"
#include "ui/scene_animation.h"
#include "ui/product_brand.h"
#include "ui/studio_help.h"
#include "ui/live_resample.h"
#include "ui/premium.h"
#include "ui/brand_graphics.h"
#include "ui/punch_graphics.h"
#include "ui/studio_navigation.h"
#include "ui/studio_synth_picker.h"
#include "ui/studio_workspace.h"
#include "studio/synth.h"
#include "studio/rack.h"
#include "studio/punch.h"
#include "ui/modules/theme_editor.h"
#include "ui/modules/oscilloscope.h"
#include <cmath>
#include <cstdio>
#include <iterator>
#include <type_traits>
namespace pt::ui {
namespace {
using namespace premium;
void panel_texture(Canvas& c,int x,int y,int w,int h,const Theme& t){
 if(t.sceneChrome||t.surfaceStyle==7||(t.rowEvery4th&0x00ffffffu)==0)return;
 Canvas::ClipScope clip(c,x+5,y+3,w-10,h-6);
 const auto light=with_alpha(t.textValue,.025f),dark=0x16000000u;
 if(t.surfaceStyle==1||t.surfaceStyle==3){
  for(int py=y+5;py<y+h-4;py+=5)c.fill_rect(x+5,py,w-10,1,((py-y)/5)%2?light:dark);
 }else if(t.surfaceStyle==2||t.surfaceStyle==6){
  for(int px=x-h;px<x+w;px+=14){c.line(px,y+h-3,px+h,y+3,light);if(t.surfaceStyle==2)c.line(px+5,y+h-3,px+h+5,y+3,dark);}
 }else if(t.surfaceStyle==4||t.surfaceStyle==5){
  for(int px=x+12;px<x+w-7;px+=28)c.fill_rect(px,y+4,1,h-8,light);
  for(int py=y+11;py<y+h-4;py+=20)c.fill_rect(x+5,py,w-10,1,light);
  if(t.surfaceStyle==4)for(int px=x+12;px<x+w-10;px+=56)c.fill_rect(px-1,y+10,3,3,with_alpha(t.rowCursor,.11f));
 }else if(t.surfaceStyle>=12){
  // New materials have an optional engraved treatment on cards as well as the cached background.
  // Keep it near the edges so knobs, captions and waveform values retain a quiet ground.
  const auto ink=with_alpha(t.rowCursor,.045f);
  for(int px=x+7;px<x+w-7;px+=12){
   c.line(px,y+4,px+8,y+12,ink);
   if(t.surfaceStyle==12)c.line(px+8,y+4,px,y+12,ink);
  }
  if(w>180)brand::mark(c,x+w-38,y+h-35,24,with_alpha(t.rowCursor,.055f),ink);
 }else{
  for(int py=y+6;py<y+h-5;py+=11)for(int px=x+8;px<x+w-6;px+=13){
   const unsigned hash=static_cast<unsigned>(px*31+py*17);c.fill_rect(px+(hash%5),py,1,1,hash&8?light:dark);
  }
 }
}
void studio_card(Canvas& c,int x,int y,int w,int h,const Theme& t,bool focus=false){
 premium::card(c,x,y,w,h,t,focus);panel_texture(c,x,y,w,h,t);
 c.fill_rect(x+8,y+1,w-16,1,with_alpha(t.textValue,focus?.13f:.06f));
 c.fill_rect(x+8,y+h-2,w-16,1,0x34000000u);
 if(focus)c.fill_rect(x+10,y+3,w-20,1,with_alpha(t.rowCursor,.19f));
}
int synth_family(int model){return std::max(0,studio_synth_model_group(model)-1);}
Theme synth_theme(const AppState& s){
 Theme t=s.theme;
 // One tint per family after Sample Granular (synth_family). Spectral and Voice were missing before
 // 1.1.0-dev.2 patch 0062: a Spectral model read past the end of this table.
 static constexpr Argb colors[]={0xFF5CD8D0,0xFFB699F3,0xFF78B7EE,0xFFE4BD7A,0xFF8FDEA6,0xFFE89BBE,0xFFE8A37A,0xFF9FC4F8,0xFFF0A0A8,0xFFB8E07A,0xFFFF9A5E};
 static_assert(std::size(colors)==std::tuple_size_v<std::remove_reference_t<decltype(studio_synth_model_groups())>>-1);
 if(t.name!="MONO"&&t.name.find("OLED")==std::string::npos)
  t.rowCursor=mix(t.rowCursor,colors[synth_family(s.studioModel)],t.name=="STUDIO"||t.name=="GRAPHITE"||t.name=="CARBON"?.52f:.20f);
 return t;
}
// Static circuit emblems identify engine families; these are diagrams, never output meters.
void engine_diagram(Canvas& c,int family,int x,int y,Argb color){
 const auto faint=with_alpha(color,.32f);
 if(family==0||family==5){
  for(int i=0;i<3;++i){const int px=x+i*29;c.panel_rect(px,y+2,20,16,0x17000000u,faint,3);
   if(family==0){c.line(px+5,y+13,px+10,y+6,color);c.line(px+10,y+6,px+15,y+13,color);}
   else{c.line(px+5,y+6,px+15,y+14,color);c.line(px+5,y+14,px+15,y+6,color);}
   if(i<2)c.line(px+21,y+10,px+28,y+10,faint);
  }
 }else if(family==1){
  for(int i=0;i<3;++i){c.line(x,y+4+i*5,x+75,y+16-i*5,color);c.line(x,y+16-i*5,x+75,y+4+i*5,faint);}
 }else if(family==2){
  c.line(x+1,y+2,x+1,y+18,faint);c.line(x+76,y+2,x+76,y+18,faint);
  for(int i=0;i<4;++i){const int py=y+4+i*4;c.line(x+3,py,x+38,py+(i%2?3:-2),color);c.line(x+38,py+(i%2?3:-2),x+74,py,faint);}
 }else if(family==3){
  c.line(x+5,y+10,x+38,y+1,color);c.line(x+38,y+1,x+74,y+10,color);c.line(x+74,y+10,x+38,y+19,faint);c.line(x+38,y+19,x+5,y+10,faint);
  c.line(x+22,y+6,x+55,y+14,faint);c.line(x+22,y+14,x+55,y+6,faint);c.fill_rect(x+36,y+8,4,4,color);
 }else if(family==6){
  c.line(x,y+18,x+76,y+18,faint);
  for(int bounce=0;bounce<3;++bounce){
   const int left=x+25*bounce, height=14-4*bounce;
   int previous=y+17;
   for(int step=1;step<=24;++step){
    const int py=y+17-height*step*(24-step)/144;
    c.line(left+step-1,previous,left+step,py,color);
    previous=py;
   }
  }
 }else if(family==8){
  // Voice: three formant humps over a baseline.
  c.line(x,y+18,x+76,y+18,faint);
  static constexpr int centres[]={14,40,62},heights[]={15,10,6},widths[]={9,7,6};
  for(int f=0;f<3;++f){
   int previous=y+18;
   for(int dx=-widths[f]*2;dx<=widths[f]*2;++dx){
    const int py=y+18-heights[f]*widths[f]*widths[f]/(widths[f]*widths[f]+dx*dx);
    if(dx>-widths[f]*2)c.line(x+centres[f]+dx-1,previous,x+centres[f]+dx,py,f==0?color:faint);
    previous=py;
   }
  }
 }else if(family==10){
  // Low End and Sirens: a fat sub sine under a falling saw, the shape of a bass note landing.
  c.line(x,y+19,x+76,y+19,faint);
  int previous=y+10;
  for(int px=1;px<=76;++px){const int py=y+10-int(std::lround(8.f*std::sin(float(px)*.17f)));c.line(x+px-1,previous,x+px,py,faint);previous=py;}
  for(int tooth=0;tooth<4;++tooth){const int left=x+tooth*19;c.line(left,y+2+tooth*2,left+18,y+17,color);c.line(left+18,y+17,left+18,y+2+(tooth+1)*2,color);}
 }else if(family==9){
  // Vintage: a square wave stepping through an arpeggio (SID CHIP; DCO POLY draws its own).
  static constexpr int levels[]={14,10,6,14,10,6};
  int previous=y+18;
  for(int i=0;i<6;++i){
   const int left=x+i*13,top=y+18-levels[i];
   c.line(left,previous,left,top,color);c.line(left,top,left+6,top,color);
   c.line(left+6,top,left+6,y+18,faint);c.line(left+6,y+18,left+13,y+18,faint);
   previous=y+18;
  }
}else{
  for(int i=0;i<18;++i){const int px=x+(i*37)%76,py=y+2+(i*11)%16;c.fill_rect(px,py,i%3+2,2,i%2?color:faint);}
 }
}
// One line on what a model is for, shown under its name and in the model list. nullptr: no line of its own.
enum class SynthSheet { Blocks, Knob, Lcd, Bars };
// The control sheet of a family (the index of its tab).
SynthSheet synth_sheet(int group){
 switch(group){
  case 2:case 10:return SynthSheet::Lcd;     // Braids (digital), Vintage Chips
  case 3:case 4:case 11:return SynthSheet::Knob;   // Rings, Elements, Low End and Sirens
  case 7:case 8:return SynthSheet::Bars;     // Gravity, Spectral
  default:return SynthSheet::Blocks;
 }
}
const char* model_blurb(int model){
 if(model==studio::kFormantChoirModel)return "A choir of vowels: ah, oh, ee";
 if(model==studio::kSidChipModel)return "8-bit chip: pulse, noise, filter, arp";
 if(model==studio::kDcoPolyModel)return "Warm digital oscillators and a filter";
 if(model==studio::kFmSysexModel)return "FM with DX7 voice banks you can load";
 if(model==studio::kDubSirenModel)return "Wailing siren: LFO, sweep and ping";
 if(model==studio::kFrogVoiceModel)return "A croaking frog: ri-bbit, throat size";
 if(model==studio::kSingFrogModel)return "Sings the words you type";
 if(model==studio::kAcidBoxModel)return "Squelchy acid bass with accent";
 if(model==studio::kReeseBassModel)return "Wide detuned bass that beats and moves";
 return nullptr;
}
const char* gravity_description(int model){
 switch(model-studio::kGravityFirstModel){
 case 0:return "Each rebound strikes a tuned resonator";
 case 1:return "Crossing orbit sectors strikes bells";
 default:return "Independent drops trigger tuned impacts";
 }
}
// The frog stage (Studio > Synth, first and last page of CROAKER and FROGGY): a neon side view in the manner of an
// anatomy drawing, alive when scene motion is on. The dot of the pad is a firefly with a glowing trail; the frog's
// mouth, throat sac and sound rings follow the REAL level of what is playing (the master waveform), a new note makes
// it hop and the pond ripples where it lands, the tongue flicks out for the firefly now and then, the eye blinks and
// follows it, and the body breathes. A reflection hangs in the water. With scene motion off it is a still picture
// of the settings. Readouts: the three formants the Mouth and Throat settings give an "ah", and the jaw, lips and
// tongue. Everything is drawn from lines and fills; the cost is a few hundred of them per frame.
void frog_stage(Canvas& c,const AppState& s,int x,int y,int w,int h){
 const Theme& t=s.theme;
 const bool sing=s.studioModel==studio::kSingFrogModel;
 const bool moving=s.settings.sceneMotion;
 const float T=moving?float(s.animationMs%600000)/1000.f:0.f;
 const float mouth=std::clamp(s.studioHarmonics,0,255)/255.f,second=std::clamp(s.studioTimbre,0,255)/255.f,third=std::clamp(s.studioMorph,0,255)/255.f;
 const Argb line=t.rowCursor,dim=with_alpha(t.rowCursor,.42f);
 const Argb green=0xFF67E58Cu,orange=0xFFFF9B5Eu,pink=0xFFFF7FC6u,gold=0xFFFFE27Au,water=0xFF58B8E8u;
 // The level of the sound, smoothed (rises quickly, falls slowly); a rising edge is a new note.
 static float level=0.f,lastRaw=0.f,hopStart=-10.f;
 static uint32_t lastMs=0;
 float raw=0.f;
 if(s.waveform){float sum=0.f;for(int i=0;i<WAVEFORM_SIZE;i+=4)sum+=std::fabs(s.waveform[i]);raw=std::clamp(sum/float(WAVEFORM_SIZE/4)*4.f,0.f,1.f);}
 if(moving){
  const float dt=std::clamp(float(s.animationMs-lastMs)/1000.f,0.f,.1f);
  level+=(raw-level)*(1.f-std::exp(-dt*(raw>level?30.f:7.f)));
  if(raw>.05f&&lastRaw<.02f)hopStart=T;
  lastRaw=raw;lastMs=s.animationMs;
 }else{level=.4f;hopStart=-10.f;}
 const float hopU=(T-hopStart)/.5f;
 const float hop=hopU>=0.f&&hopU<1.f?std::sin(hopU*3.14159f):0.f;   // 0..1..0 over half a second
 c.panel_rect(x,y,w,h,mix(t.rowEvery4th,t.background,.55f),with_alpha(t.rowCursor,.55f),5);
 const int wy=y+h-17;   // the water line
 const auto ellipse=[&](float cx,float cy,float rx,float ry,Argb col,float from=0.f,float to=6.2831853f){
  const int n=26;float px=cx+rx*std::cos(from),py=cy+ry*std::sin(from);
  for(int i=1;i<=n;++i){const float a=from+(to-from)*float(i)/n;const float nx=cx+rx*std::cos(a),ny=cy+ry*std::sin(a);
   c.line(int(std::lround(px)),int(std::lround(py)),int(std::lround(nx)),int(std::lround(ny)),col);px=nx;py=ny;}
 };
 const auto fill=[&](float cx,float cy,float rx,float ry,Argb col){
  for(int dy=-int(ry);dy<=int(ry);++dy){const float f=1.f-float(dy*dy)/(ry*ry);if(f<0)continue;const int half=int(std::lround(rx*std::sqrt(f)));c.fill_rect(int(cx)-half,int(cy)+dy,2*half+1,1,col);}
 };
 // the sky: a few dim stars and far fireflies drifting slowly
 for(int i=0;i<9;++i){
  const float fx=x+10+float((i*53)%(w-20)),fy=y+22+float((i*29)%(wy-y-34));
  const float tw=moving?.5f+.5f*std::sin(T*(.8f+.13f*i)+i*1.7f):.6f;
  const float dx=moving?4.f*std::sin(T*.3f+i):0.f,dy=moving?3.f*std::cos(T*.4f+i*2.f):0.f;
  c.fill_rect(int(fx+dx),int(fy+dy),i%3==0?2:1,i%3==0?2:1,with_alpha(i%2?gold:t.textParam,.10f+.28f*tw));
 }
 // the pond: three layers of waves at different speeds, and ripples from the lily pad and from each landing
 for(int px=x+4;px<x+w-4;px+=2){
  const int wob=int(std::lround(1.2f*std::sin(float(px)*.11f+T*2.f)));
  c.fill_rect(px,wy+3+wob,2,1,with_alpha(water,.55f));
  if(px%4==0)c.fill_rect(px,wy+8+int(std::lround(std::sin(float(px)*.17f-T*1.6f))),2,1,with_alpha(water,.28f));
  if(px%8==0)c.fill_rect(px,wy+12+int(std::lround(.8f*std::sin(float(px)*.07f+T*1.1f))),3,1,with_alpha(water,.16f));
 }
 const float padX=x+w*(w<360?.25f:.30f);
 for(int ring=0;ring<2;++ring){
  const float ph=moving?std::fmod(T*.45f+ring*.5f,1.f):.35f+ring*.3f;
  ellipse(padX+10,wy+7,26+ph*70,2+ph*5,with_alpha(water,.5f*(1.f-ph)),0.f,6.2831853f);
 }
 if(hopU>=1.f&&hopU<2.2f){   // the landing: a splash ring after the hop
  const float ph=(hopU-1.f)/1.2f;
  ellipse(padX+4,wy+5,20+ph*46,2+ph*4,with_alpha(water,.8f*(1.f-ph)));
  ellipse(padX+4,wy+5,10+ph*30,1+ph*3,with_alpha(water,.5f*(1.f-ph)));
 }
 // lily pad: sways a little with the water
 const float sway=moving?1.6f*std::sin(T*1.3f):0.f;
 fill(padX+4,wy+1+sway*.3f,54,4,with_alpha(green,.20f));ellipse(padX+4,wy+1+sway*.3f,54,4,with_alpha(green,.85f));
 c.line(int(padX+4),int(wy+1+sway*.3f),int(padX+58),int(wy-1+sway*.3f),with_alpha(green,.85f));   // the notch
 c.line(int(padX-30),int(wy+3),int(padX-34),int(wy+6),with_alpha(green,.5f));
 // the frog, facing right (everything scaled by S)
 const bool narrow=w<360;
 const float S=narrow?1.05f:1.4f;
 const float breathe=moving?.045f*std::sin(T*2.3f):0.f;
 const float bx=padX,by=wy-15*S-hop*13.f,hx=bx+43*S,hy=by-5*S;
 const float beat=moving?(.5f-.5f*std::cos(6.2831853f*T*(sing?3.f:.5f*std::exp2(5.f*mouth)))):.45f;
 const float activity=moving?std::clamp(.18f+level*1.6f+.12f*std::sin(T*2.1f),0.f,1.f):.45f;
 // reflection first, under the waterline: a dim, wobbling copy of the head and eye
 if(moving||true){
  const float wob=moving?2.2f*std::sin(T*2.6f):0.f;
  const float ry=wy+8.f+(wy-by)*.18f;
  ellipse(hx+wob,ry+9.f,23*S,5.f,with_alpha(line,.10f));
  ellipse(hx+3*S+wob,ry+3.f,6*S,3.f,with_alpha(green,.16f));
 }
 fill(bx,by,45*S,(19+breathe*19)*S,with_alpha(line,.10f));ellipse(bx,by,45*S,(19+breathe*19)*S,line);
 for(int dot=0;dot<7;++dot)c.fill_rect(int(bx+(-30+dot*9)*S),int(by+(-12+((dot*5)%7))*S),2,2,dim);   // spots on the back
 c.line(int(bx-28*S),int(by+12*S),int(bx+22*S),int(by+14*S),with_alpha(line,.22f));                    // the belly line
 // hind leg: folded; it kicks out behind while the frog is in the air
 const float footX=bx-50*S-14*S*hop,footY=wy-1-hop*6.f;
 c.line(int(bx-28*S),int(by+8*S),int(footX),int(footY),line);c.line(int(footX),int(footY),int(footX+28*S),int(wy),line);c.line(int(footX+28*S),int(wy),int(bx-10*S),int(by+16*S),dim);
 c.line(int(footX),int(footY),int(footX-5*S),int(wy+1),dim);c.line(int(footX),int(footY),int(footX-2*S),int(wy+2),dim);   // toes
 // front arm
 c.line(int(hx-14*S),int(by+8*S),int(hx-8*S),int(wy-hop*6.f),line);c.line(int(hx-8*S),int(wy-hop*6.f),int(hx+2*S),int(wy-hop*6.f),dim);
 fill(hx,hy,23*S,13*S,with_alpha(line,.10f));ellipse(hx,hy,23*S,13*S,line);
 // the eye on top: a bulge that pulses with the sac, a lid that droops when it is quiet, a blink, a pupil on the firefly
 const float ex=hx+3*S,ey=hy-13*S;
 const float drift=moving?1.2f:0.f;
 const float dotX=x+10+mouth*(w-20)+(moving?5.f*std::sin(T*1.7f)+drift*std::sin(T*4.f):0.f);
 const float dotY=y+8+(1.f-second)*float(wy-y-26)+(moving?3.5f*std::cos(T*2.1f):0.f);
 const float bulge=1.f+(moving?.08f*activity*std::sin(T*9.f):0.f);
 ellipse(ex,ey,7*S*bulge,7*S*bulge,line);fill(ex,ey,5*S,5*S,with_alpha(green,.30f));
 const float lookX=std::clamp((dotX-ex)/60.f,-1.f,1.f),lookY=std::clamp((dotY-ey)/40.f,-1.f,1.f);
 const float blinkPhase=moving?std::fmod(T,3.7f):0.f;
 const bool blink=moving&&blinkPhase>3.5f;
 const float lid=blink?1.f:moving?std::clamp(.45f-level*2.4f,0.f,.45f):0.f;   // sleepy when quiet, wide open when it sings
 fill(ex+lookX*3,ey+lookY*3,3,3,0xFF0A0F14u);c.fill_rect(int(ex+lookX*3)-1,int(ey+lookY*3)-1,1,1,0xFFFFFFFFu);
 if(lid>0.f){const int cover=int(lid*10.f*S);for(int row=0;row<cover;++row){const float f=1.f-float((ey-7*S+row-ey)*(ey-7*S+row-ey))/(49.f*S*S);if(f>0)c.fill_rect(int(ex-7*S*std::sqrt(f)),int(ey-7*S+row),int(14*S*std::sqrt(f))+1,1,with_alpha(t.background,.88f));}
  c.line(int(ex-7*S),int(ey-7*S+cover),int(ex+7*S),int(ey-7*S+cover),line);}
 // the tongue flicks out for the firefly every few seconds; the mouth opens wide for it
 const float flickU=moving?std::fmod(T,3.4f)/3.4f:0.f;
 const float flick=flickU<.2f?std::sin(flickU/.2f*3.14159f):0.f;
 // the mouth: the upper jaw is fixed, the lower jaw follows X and the real level; lips in pink
 const float rest=moving?.12f+mouth*.5f*std::clamp(level*3.5f,0.f,1.f):mouth;
 const float open=(1.f+std::max(rest,flick*.8f)*(sing?13.f:9.f))*S;
 const float cornerX=hx-8*S,cornerY=hy+5*S,noseX=hx+22*S,noseY=hy+1*S;
 c.line(int(cornerX),int(cornerY),int(noseX),int(noseY),pink);c.line(int(cornerX),int(cornerY)+1,int(noseX),int(noseY)+1,with_alpha(pink,.6f));
 c.line(int(cornerX),int(cornerY),int(noseX-2*S),int(noseY+4*S+open),orange);
 c.line(int(noseX-2*S),int(noseY+4*S+open),int(noseX-9*S),int(noseY+4*S+open*.6f),orange);
 c.fill_rect(int(noseX)-4,int(noseY)-3,2,2,dim);   // nostril
 if(open>3.f*S||flick>.01f){
  const float reach=(moving&&flick>0.f)?flick:std::clamp(open/(14.f*S),0.f,1.f)*.6f;
  const float aimX=moving&&flick>0.f?std::clamp(dotX,hx,hx+70.f)-hx:(noseX-hx+14*S),aimY=moving&&flick>0.f?std::clamp(dotY-hy,-26.f,12.f):open*.45f;
  const float tipX=hx+6*S+aimX*reach,tipY=hy+4*S+aimY*reach+(moving?1.2f*std::sin(T*9.f):0.f);
  float lx=cornerX+2,ly=cornerY+2;
  for(int i=1;i<=9;++i){const float u=float(i)/9.f;const float nx=cornerX+2+(tipX-cornerX-2)*u,ny=cornerY+2+(tipY-cornerY-2)*u+std::sin(u*3.14159f)*2.f*(1.f-flick);
   c.line(int(lx),int(ly),int(nx),int(ny),green);c.line(int(lx),int(ly)+1,int(nx),int(ny)+1,with_alpha(green,.7f));c.line(int(lx),int(ly)+2,int(nx),int(ny)+2,with_alpha(green,.45f));lx=nx;ly=ny;}
  c.fill_rect(int(tipX)-1,int(tipY)-1,4,4,pink);   // the sticky tip
 }
 // the throat sac: skin with veins, rippling when it sings
 const float sacR=(3.f+activity*(sing?10.f:12.f))*S;
 const float sacY=hy+10*S+open*.5f;
 const float sacCx=hx-1,sacCy=sacY+sacR*.5f;
 fill(sacCx,sacCy,sacR*1.3f,sacR,with_alpha(pink,.20f+.25f*activity));ellipse(sacCx,sacCy,sacR*1.3f,sacR,with_alpha(pink,.55f+.35f*activity));
 for(int vein=1;vein<=3;++vein){const float f=float(vein)/4.f;ellipse(sacCx,sacCy,sacR*1.3f*f,sacR*f,with_alpha(pink,.16f+.18f*activity));}
 if(moving&&level>.04f){const float ph=std::fmod(T*5.f,1.f);ellipse(sacCx,sacCy,sacR*1.3f*ph,sacR*ph,with_alpha(pink,.6f*(1.f-ph)));}
 // sound rings spread from the mouth with the real level
 if(moving&&level>.03f){
  for(int k=0;k<3;++k){
   const float r=std::fmod(T*46.f+k*15.f,45.f),fade=(1.f-r/45.f)*std::min(1.f,level*4.f);
   ellipse(noseX,noseY+4*S+open*.4f,r,r,with_alpha(gold,.55f*fade),-.9f,.9f);
  }
 }
 // bubbles rise from the mouth with the breath; a hop shakes a few more loose from the pond
 if(second>.12f||hop>0.f){
  const int n=1+int(second*6.f)+(hop>0.f?3:0);
  for(int i=0;i<n;++i){
   const float ph=moving?std::fmod(T*.7f+float(i)*.37f,1.f):float(i)/float(n);
   const float bxp=noseX+4+ph*(narrow?20.f:34.f)+3.f*std::sin(T*3.f+float(i)),byp=noseY+6*S-ph*(narrow?30.f:36.f);
   if(byp<y+6)continue;
   const int r=1+i%3;c.stroke_rect(int(bxp)-r,int(byp)-r,2*r+1,2*r+1,with_alpha(water,.9f*(1.f-ph)));
  }
 }
 // the firefly (the pad's dot): halo, glowing body, beating wings and a fading trail
 const float flicker=moving?.5f+.5f*std::sin(T*7.f):.8f;
 const float wing=moving?std::sin(T*38.f):0.f;
 if(moving)for(int tr=5;tr>=1;--tr){
  const float tt=T-tr*.06f;
  const float tx=x+10+mouth*(w-20)+5.f*std::sin(tt*1.7f)+drift*std::sin(tt*4.f),ty=y+8+(1.f-second)*float(wy-y-26)+3.5f*std::cos(tt*2.1f);
  c.fill_rect(int(tx)-1,int(ty)-1,2,2,with_alpha(gold,.07f*(6-tr)));
 }
 c.stroke_rect(int(dotX)-5,int(dotY)-5,11,11,with_alpha(gold,.18f+.25f*flicker));
 c.stroke_rect(int(dotX)-8,int(dotY)-8,17,17,with_alpha(gold,.07f+.10f*flicker));
 c.fill_rect(int(dotX)-2,int(dotY)-2,5,5,with_alpha(gold,.9f));
 c.line(int(dotX)-1,int(dotY)-3,int(dotX)-5,int(dotY)-6-int((.5f+.5f*wing)*3),with_alpha(gold,.7f));
 c.line(int(dotX)+1,int(dotY)-3,int(dotX)+5,int(dotY)-6-int((.5f-.5f*wing)*3),with_alpha(gold,.7f));
 // readouts: the three formants the sound gives an ah, and the jaw, lips and tongue
 const float throat=.7f*std::exp2(third)*(1.f/.95f);
 const float jaw=.8f+.5f*mouth;
 const int f1=int(std::lround(700.f*jaw*throat)),f2=int(std::lround(1300.f*throat)),f3=int(std::lround(2500.f*throat));
 char text[40];
 std::snprintf(text,sizeof(text),"F1 %d",f1);label(c,text,x+8,y+5,orange,56,11);
 std::snprintf(text,sizeof(text),"F2 %d",f2);label(c,text,x+68,y+5,orange,56,11);
 std::snprintf(text,sizeof(text),"F3 %d",f3);label(c,text,x+128,y+5,orange,56,11);
 // three small resonance bars under the numbers: they jump with the level
 for(int bar=0;bar<3;++bar){const int bh=1+int((moving?level:.4f)*6.f*(1.f-.2f*bar)*(.8f+.2f*std::sin(T*6.f+bar*2.f)));c.fill_rect(x+8+bar*60,y+21-bh,40,bh,with_alpha(orange,.35f));}
 std::snprintf(text,sizeof(text),"JAW %.1f cm",.2f+open*.12f);label(c,text,x+w-84,y+22,orange,80,11);
 std::snprintf(text,sizeof(text),"LIPS %.1f cm",.3f+mouth*1.1f);label(c,text,x+w-84,y+36,pink,80,11);
 if(!sing){std::snprintf(text,sizeof(text),"TONGUE %.1f",std::clamp(flick>0.f?flick:open/(14.f*S),0.f,1.f));label(c,text,x+w-84,y+50,green,80,11);}   // FROGGY shows the karaoke here
 if(sing){
  // Karaoke: the word being sung in gold with one dot per syllable (the sung one filled), the next word beneath it.
  const std::string defaultText="ribbit ribbit quack la la";
  const std::string& text=s.studioDraft.lyric.empty()?defaultText:s.studioDraft.lyric;
  studio::sing::WordSpan spans[64];
  const int wordCount=studio::sing::lyric_words(text.c_str(),spans,64);
  const int total=wordCount>0?spans[wordCount-1].firstSyllable+spans[wordCount-1].syllables:0;
  const int pitchParam=s.studioDraft.studioSynthParams[studio::kSingSyllableParam];
  const int fixed=pitchParam>=5?pitchParam/5:0;
  const int sung=total<=0?-1:fixed>0?(fixed-1)%total:s.lyricNotes>0?int((s.lyricNotes-1)%uint32_t(total)):-1;
  int current=0;
  for(int i=0;i<wordCount;++i)if(sung>=spans[i].firstSyllable&&sung<spans[i].firstSyllable+spans[i].syllables)current=i;
  if(wordCount>0){
   const auto wordText=[&](int i){return text.substr(size_t(spans[i].start),size_t(spans[i].length));};
   const bool active=sung>=0;
   label(c,wordText(current),x+w-84,y+h-40,active?gold:with_alpha(gold,.5f),80,14);
   for(int k=0;k<spans[current].syllables&&k<8;++k){
    const bool on=active&&sung==spans[current].firstSyllable+k;
    c.fill_rect(x+w-84+k*8,y+h-22,on?6:4,on?6:4,on?gold:with_alpha(gold,.4f));
   }
   if(wordCount>1)label(c,wordText((current+1)%wordCount),x+w-84,y+h-14,with_alpha(t.textParam,.8f),80,10);
  }
 }else{
  std::snprintf(text,sizeof(text),"CROAK %.1f/s",.5f*std::exp2(5.f*mouth));label(c,text,x+w-84,y+h-26,gold,80,11);
 }
 label(c,"HOLD L + D-PAD",x+8,y+h-14,with_alpha(t.textParam,.8f),110,10);
}
// `live`: the AppState for diagrams that draw the sound's own settings and move (DUB SIREN); null elsewhere.
void synth_diagram(Canvas& c,int model,int x,int y,Argb color,const AppState* live=nullptr){
 if(model==studio::kFmSysexModel){
  // FM SYSEX: six operators, a stack of three modulating a carrier beside a second stack.
  const auto dim=with_alpha(color,.38f);
  static constexpr int ox[]={8,8,8,44,44,44},oy[]={0,7,14,0,7,14};
  for(int i=0;i<6;++i){c.panel_rect(x+ox[i],y+oy[i],14,5,0x17000000u,i==2||i==5?color:dim,1);if(oy[i]<14)c.line(x+ox[i]+7,y+oy[i]+5,x+ox[i]+7,y+oy[i]+7,dim);}
  c.line(x+15,y+19,x+51,y+19,color);
  return;
 }
 if(model==studio::kDcoPolyModel){
  // DCO POLY: the filter's response, from the sound's own macros when live: the cutoff sets where the peak
  // sits, the resonance how tall it stands, then the four-pole fall.
  const auto dim=with_alpha(color,.38f);
  const float cutoff=live?live->studioHarmonics/255.f:.55f,resonance=live?live->studioTimbre/255.f:.3f;
  const int fc=8+int(cutoff*58.f),peakHeight=2+int(resonance*9.f);
  c.line(x,y+18,x+76,y+18,dim);
  int previous=y+8;
  for(int px=1;px<=76;++px){
   const float t=(px-fc)/5.f;
   const int bump=int(float(peakHeight)/(1.f+t*t));
   const int fall=px>fc?(px-fc)*(px-fc)/60:0;
   const int py=std::min(y+8-bump+fall,y+18);
   c.line(x+px-1,previous,x+px,py,px<=fc?color:dim);
   previous=py;
  }
  return;
 }
 if(model==studio::kFrogVoiceModel||model==studio::kSingFrogModel){
  // A frog: two eyes on top, a wide mouth, and a throat sac under it that swells. CROAKER swells it at the
  // croak rate (macro A); FROGGY opens the mouth with macro A (Mouth) and breathes the sac with the
  // sound. With motion off it is a still frog.
  const auto dim=with_alpha(color,.38f);
  const auto disc=[&](int cx,int cy,int r,Argb col){for(int dy=-r;dy<=r;++dy){const int half=int(std::lround(std::sqrt(float(r*r-dy*dy))));c.fill_rect(cx-half,cy+dy,2*half+1,1,col);}};
  const bool sing=model==studio::kSingFrogModel;
  const float macroA=live?live->studioHarmonics/255.f:.5f;
  const bool moving=live&&live->settings.sceneMotion;
  const float rateHz=sing?4.f:.5f*std::exp2(5.f*macroA);
  const float beat=moving?.5f-.5f*std::cos(6.2831853f*float(live->animationMs%100000)/1000.f*rateHz):.45f;
  // Drawn 1.7 times the size of the diagram slot, centred on it and rising above it: the card has the room.
  const auto sc=[](float v){return int(std::lround(v*1.7f));};
  const int cx=x+14,oy=y-12;
  // head: a wide dome, filled softly, with the outline on top
  for(int row=sc(8);row<=sc(13);++row){const int half=sc(22)+(row-sc(8))*sc(5)/std::max(1,sc(13)-sc(8));c.fill_rect(cx-half,oy+row,2*half+1,1,with_alpha(color,.14f));}
  c.line(cx-sc(27),oy+sc(13),cx-sc(22),oy+sc(8),dim);c.line(cx-sc(22),oy+sc(8),cx+sc(22),oy+sc(8),dim);c.line(cx+sc(22),oy+sc(8),cx+sc(27),oy+sc(13),dim);
  c.fill_rect(cx-3,oy+sc(9),2,2,dim);c.fill_rect(cx+2,oy+sc(9),2,2,dim);   // nostrils
  // eyes
  for(int side=-1;side<=1;side+=2){
   disc(cx+side*sc(15),oy+sc(5),sc(5),dim);disc(cx+side*sc(15),oy+sc(5),sc(5)-1,0x17000000u);
   c.fill_rect(cx+side*sc(15)-2,oy+sc(5)-2,5,5,color);c.fill_rect(cx+side*sc(15)-1,oy+sc(5)-1,2,2,0xFF000000u);
  }
  // mouth: a line, or an opening that follows the Mouth macro
  const int gap=sing?2+int(macroA*7.f):0;
  c.line(cx-sc(26),oy+sc(13),cx+sc(26),oy+sc(13),color);
  c.line(cx-sc(26),oy+sc(13),cx-sc(28),oy+sc(11),color);c.line(cx+sc(26),oy+sc(13),cx+sc(28),oy+sc(11),color);
  if(gap>0){c.fill_rect(cx-sc(20),oy+sc(13)+1,sc(41),gap,with_alpha(color,.5f));c.line(cx-sc(26),oy+sc(13)+gap,cx+sc(26),oy+sc(13)+gap,color);}
  // throat sac
  const int sac=3+int(beat*(sing?8.f:11.f));
  const int top=oy+sc(13)+gap+1;
  for(int row=0;row<sac;++row){
   const int half=int(std::lround(float(sc(18))*std::sqrt(1.f-float(row*row)/float(sac*sac))));
   c.fill_rect(cx-half,top+row,2*half+1,1,with_alpha(color,.30f+.35f*beat));
  }
  return;
 }
 if(model==studio::kDubSirenModel){
  // DUB SIREN: the pitch curve the note will draw - the LFO's swing (rate: how many waves, depth: how tall)
  // riding on the sweep (a slope up or down). With a live state it is the sound's own macros, and with
  // motion on a bright point travels along it and leaves a short trail, like a sonar sweep.
  const auto dim=with_alpha(color,.38f);
  const float rate=live?live->studioHarmonics/255.f:.45f,depth=live?live->studioTimbre/255.f:.6f,sweep=live?live->studioMorph/255.f:.5f;
  const float waves=1.f+rate*4.f,amp=1.5f+depth*6.5f,slope=(std::fabs(sweep-.5f)<.012f?0.f:(sweep-.5f)*2.f)*7.f;
  c.line(x,y+19,x+76,y+19,dim);   // the note's own pitch
  const auto at=[&](int px){const float t=float(px)/76.f;return std::clamp(y+10-int(std::lround(amp*std::sin(t*6.2831853f*waves)+slope*t)),y,y+19);};
  int previous=at(0);
  for(int px=1;px<=76;++px){const int py=at(px);c.line(x+px-1,previous,x+px,py,dim);previous=py;}
  const bool moving=live&&live->settings.sceneMotion;
  const int head=moving?int((live->animationMs/38)%77):38;
  for(int trail=7;trail>=0;--trail){
   const int px=head-trail;if(px<1||px>76)continue;
   c.line(x+px-1,at(px-1),x+px,at(px),with_alpha(color,trail==0?1.f:.9f-trail*.1f));
  }
  c.fill_rect(x+head-1,at(head)-1,3,3,color);
  return;
 }
 const int kind=model-studio::kGravityFirstModel;
 if(kind==1){
  const auto dim=with_alpha(color,.36f);
  static constexpr int dx[]={0,9,21,29,32,29,21,9,0,-9,-21,-29,-32,-29,-21,-9};
  static constexpr int dy[]={-8,-8,-6,-3,0,3,6,8,8,8,6,3,0,-3,-6,-8};
  for(int i=0;i<16;++i){const int j=(i+1)%16;c.line(x+38+dx[i],y+10+dy[i],x+38+dx[j],y+10+dy[j],i<8?color:dim);}
  c.fill_rect(x+36,y+8,4,4,color);
  c.fill_rect(x+65,y+5,4,4,color);
 }else if(kind==2){
  const auto dim=with_alpha(color,.38f);
  for(int i=0;i<7;++i){const int xx=x+4+i*11,yy=y+2+(i*7)%10;c.line(xx,yy,xx-3,yy+6,dim);c.fill_rect(xx-4,yy+7,3,2,color);}
  c.line(x,y+18,x+76,y+18,dim);
 }else engine_diagram(c,synth_family(model),x,y,color);
}
int studio_play_step(const AppState& s){
 for(const auto& head:s.playheads)if(head.step>=0)return head.step;
 return -1;
}

void scene_heading(Canvas& c, const AppState& s, const char* transport, bool active) {
 const Theme& t=s.theme;
 const auto scene=scene_layout(t);
 const auto tabs=[&](int x, int y, int columns, int stride, int width) {
  for(size_t i=0;i<kStudioMusicTabs.size();++i){
   const int column=static_cast<int>(i)%columns,row=static_cast<int>(i)/columns;
   const auto& tab=kStudioMusicTabs[i];
   chip(c,tab.label,x+column*stride,y+row*30,width,t,tab.id==s.studioTab);
   if(tab.id==s.studioTab)c.mark_selector(2,x+column*stride,y+row*30,width,23);   // glides (ui/layout.cpp)
  }
 };
 const auto identity=[&](int x,int y){
  brand::mark(c,x,y,22,t.rowCursor,with_alpha(t.textTitle,.44f));
  c.draw_label(kProductName,x+30,y-2,t.textTitle,18);
  c.draw_label("Kid Bunka",x+30,y+20,t.textParam,12);
 };

 if(scene.railLeft){
  const int board=scene.scopeBottom?223:16;
  const int mast=scene.scopeBottom?16:430;
  c.panel_rect(board,9,401,76,t.rowEvery4th,with_alpha(t.textParam,.45f),4);
  tabs(board+8,17,4,98,92);
  identity(mast,15);
  label(c,"B + SELECT  SECTIONS",mast,51,t.textParam,188,12);
  chip(c,transport,mast,68,94,t,active);
  if(s.isPlaying&&s.project&&s.settings.sceneMotion)scene_animation::family_strip(c,mast+108,70,t,s.animationMs,songcore::tempo_in_force(*s.project),studio_play_step(s));
 }else{
  const int mastY=scene.scopeBottom?42:9;
  const int tabsY=scene.scopeBottom?9:63;
  c.panel_rect(16,mastY,608,42,t.rowEvery4th,with_alpha(t.textParam,.42f),4);
  identity(24,mastY+9);
  label(c,"SELECT  HELP",231,mastY+13,t.textParam,114,12);
  label(c,"B + SELECT  SECTIONS",355,mastY+13,t.textParam,160,12);
  chip(c,transport,520,mastY+9,94,t,active);
  tabs(16,tabsY,8,77,69);
  // Between the TrackWerk name and SELECT HELP: the Scene's own playback animation.
  if(s.isPlaying&&s.project&&s.settings.sceneMotion)scene_animation::family_strip(c,158,mastY+11,t,s.animationMs,songcore::tempo_in_force(*s.project),studio_play_step(s));
 }
}

void heading(Canvas& c,const AppState& s){
 const auto& t=s.theme;
 c.surface_rect(-c.origin_x(),0,DESIGN_W,DESIGN_H,t.background);   // the whole canvas, centred or not
 const bool audition=s.studioTab==0&&(s.studioAuditionHeld||s.studioSynthPicker.previewSounding);
 const bool recording=s.resampleView.active||granular_recording_busy(s.sampleGranular.recording);
 const char* transport=recording?"RECORDING":s.studioAppearanceSettings?"SETTINGS":s.studioTab==3&&s.studioAUsed?"FX HELD":audition?"AUDITION":s.isPlaying?"PLAYING":"STOPPED";
 const bool active=recording||(!s.studioAppearanceSettings&&(s.isPlaying||audition||s.studioAUsed));
 if(t.sceneChrome&&!s.studioAppearanceSettings){scene_heading(c,s,transport,active);return;}
 brand::mark(c,18,12,27,t.rowCursor,with_alpha(t.textTitle,.42f));
 c.draw_label(kProductName,57,13,t.textTitle,18);
 c.draw_label("Kid Bunka",194,18,t.textParam,12);
 label(c,"SELECT  HELP",272,18,t.rowCursor,90,12);
 if(!s.studioAppearanceSettings) {
  c.draw_label("B + Select   Sections",365,18,t.textParam,12);
  if(brand::expressive_material(t))brand::registration(c,365,35,123,t,s.blinkPhaseMs,s.isPlaying||s.studioAuditionHeld||s.studioAUsed);
 }
 chip(c,transport,530,14,94,t,active);
 c.fill_rect(18,47,606,1,with_alpha(t.textParam,.2f));
 if(s.studioAppearanceSettings){
  c.draw_label("SETTINGS / APPEARANCE",18,62,t.textValue,18);
  label(c,"B: Back to Settings",466,67,t.rowCursor,158);
  return;
 }
 for(size_t i=0;i<kStudioMusicTabs.size();++i){
  chip(c,kStudioMusicTabs[i].label,16+static_cast<int>(i)*77,60,70,t,kStudioMusicTabs[i].id==s.studioTab);
  if(kStudioMusicTabs[i].id==s.studioTab)c.mark_selector(2,16+static_cast<int>(i)*77,60,70,23);   // glides (ui/layout.cpp)
 }
}
void preset(Canvas& c,const AppState& s,const std::string& title){
 const auto& t=s.theme;studio_card(c,16,102,608,66,t,s.studioRow==0);
 label(c,title,30,109,t.textParam,570);
 label(c,studio_value(s,0),30,129,t.textValue,570,18);
 label(c,s.studioTab==1?studio::effect_preset_description(s.studioEffectPreset):studio::master_preset_description(s.studioMasterPreset),30,152,t.textEmpty,570);
}
void dynamics(Canvas& c,const AppState& s){
 const auto& t=s.theme;auto p=s.project->studioMaster;
 // The transfer curve morphs to new settings: its five shaping values ease (Canvas::tween).
 for(const int k:{studio::THRESHOLD,studio::RATIO,studio::OUTPUT,studio::CEILING,studio::DRIVE})
  p[static_cast<size_t>(k)]=static_cast<std::decay_t<decltype(p[0])>>(c.tween(0x4000000u+static_cast<uint32_t>(k),p[static_cast<size_t>(k)]));
 studio_card(c,416,180,208,226,t);
 c.draw_label(p[studio::ENABLE]?"Drive / comp / output":"Dynamics bypassed",430,191,t.textParam);
 const int x=436,y=219,w=165,h=106;
 c.fill_rect(x,y,w,h,recess(t.background,t.vizBackground,.75f));
 for(int k=0;k<=4;++k){c.fill_rect(x+k*w/4,y,1,h,with_alpha(t.textParam,.13f));c.fill_rect(x,y+k*h/4,w,1,with_alpha(t.textParam,.13f));}
 c.line(x,y+h-1,x+w-1,y,with_alpha(t.textParam,.25f));
 const float threshold=-48+48*p[studio::THRESHOLD]/255.f,ratio=1+19*p[studio::RATIO]/255.f;
 const float gain=20*(p[studio::OUTPUT]-128)/128.f,ceiling=-12+12*p[studio::CEILING]/255.f;
 int last=y+h-1;
 for(int k=0;k<w;++k){const float in=-48+48*k/float(w-1);float out=in;
  if(p[studio::ENABLE]){
   if(p[studio::DRIVE]){const float drive=1+7*p[studio::DRIVE]/255.f;out=20*std::log10(std::max(1e-6f,std::tanh(std::pow(10.f,in/20)*drive)/std::tanh(drive)));}
   if(out>threshold)out=threshold+(out-threshold)/ratio;out=std::min(ceiling,out+gain);
  }
  const int cy=y+h-1-static_cast<int>(std::clamp((out+48)/48,0.f,1.f)*(h-1));if(k)c.line(x+k-1,last,x+k,cy,t.rowCursor);last=cy;}
 c.draw_text("-48",x,y+h+5,t.textEmpty,1,2);c.draw_text("0 dB",x+w-43,y+h+5,t.textEmpty,1,2);
 c.draw_label("Output",430,355,t.textParam);label(c,studio_value(s,8),515,354,t.textValue,96);
 c.draw_label("Peak ceiling",430,379,t.textParam);label(c,studio_value(s,9),515,378,t.rowCursor,96);
}
void list_page(Canvas& c,const AppState& s){
 const auto& t=s.theme;preset(c,s,std::string(s.studioTab==1?"FX selection / ":"MASTER selection / ")+(s.studioTab==1?studio::effect_preset_category(s.studioEffectPreset):studio::master_preset_category(s.studioMasterPreset))+" / A to apply");
 const int first=std::max(1,s.studioRow>6?s.studioRow-6:1);
 for(int i=first;i<std::min(first+7,studio_rows(s.studioTab));++i)
  row(c,16,180+(i-first)*32,390,studio_label(s,i),studio_value(s,i),t,s.studioRow==i);
 c.mark_scroll(10+s.studioTab,16,180,390,7*32,first*32);   // the rows scroll smoothly (shell/app.cpp)
 if(s.studioTab==2){dynamics(c,s);return;}
 const auto& p=s.project->studioMaster;studio_card(c,416,180,208,226,t);
 c.draw_label("Wet / Dry",430,192,t.textParam);knob(c,520,247,p[studio::FX_MIX],t,s.studioRow==2);
 percent(c,p[studio::FX_MIX],520,291,t,s.studioRow==2);
 c.draw_label("Signal path",430,329,t.textParam);
 chip(c,"IN",430,353,38,t);c.line(470,364,483,364,t.textEmpty);
 chip(c,p[studio::FX]?"FX":"DRY",487,353,54,t,p[studio::FX]&&p[studio::ENABLE]);
 c.line(544,364,557,364,t.textEmpty);chip(c,"OUT",561,353,48,t);
 label(c,studio::effect_name(p[studio::FX]),430,384,t.textValue,180);
}
void synth_page(Canvas& c,const AppState& s){
 const auto t=synth_theme(s);
 const bool live=studio_synth_live(s);
 const auto target=songcore::hex2(s.currentInstrument);
 const auto& group=studio_synth_model_groups()[studio_synth_model_group(s.studioModel)];
 const bool hasTarget=s.project&&s.currentInstrument>=0&&s.currentInstrument<int(s.project->instruments.size());
 studio_card(c,16,100,154,70,t,s.studioRow==0);
 label(c,"TARGET SLOT",28,108,t.textParam,130);
 c.draw_label(target,28,128,t.textValue,20);
 chip(c,live?"LIVE":"DRAFT",77,126,79,t,live);
 if(hasTarget)label(c,s.project->instruments[s.currentInstrument].name,28,153,t.textParam,130);

 studio_card(c,180,100,444,70,t,s.studioRow==1);
 label(c,"SYNTH MODEL",194,108,t.textParam,132);
 label(c,"A: Choose",527,108,s.studioRow==1?t.rowCursor:t.textParam,86);
 label(c,studio::model_name(s.studioModel),194,128,t.textValue,322,20);
 label(c,studio::is_gravity_model(s.studioModel)?gravity_description(s.studioModel):model_blurb(s.studioModel)?model_blurb(s.studioModel):group.name,194,153,t.textParam,316);
 synth_diagram(c,s.studioModel,526,134,t.rowCursor,&s);

 studio_card(c,16,178,608,42,t,s.studioRow==2);
 label(c,"PRESET",28,191,t.textParam,75);
 const auto variation=s.studioSynthCustom?"Custom macros":studio_synth_preset_label(s.studioModel,s.studioVariation);
 label(c,variation,111,188,t.textValue,340,18);
 if(!s.studioSynthCustom)label(c,std::to_string(s.studioVariation+1)+" / "+std::to_string(studio::variation_count()),462,191,t.textParam,56,12);
 label(c,"A: Choose",527,191,s.studioRow==2?t.rowCursor:t.textParam,86);

 const std::string page=studio_synth_page_name(s)+"   "+std::to_string(s.studioSynthPage+1)+" / "+std::to_string(studio_synth_page_count(s));
 label(c,page,22,229,t.rowCursor,370);
 {// Page pips, a tracker's position strip: one per page, the current one lit.
  const int pages=std::clamp(studio_synth_page_count(s),1,16),x0=22+std::min(Canvas::label_width(page,12),370)+14;
  for(int i=0;i<pages;++i){const int x=x0+i*11;
   if(i==s.studioSynthPage)c.fill_rect(x,232,7,7,t.rowCursor);
   else c.stroke_rect(x,232,7,7,with_alpha(t.textParam,.55f));}}
 label(c,"R + Left / Right: Pages",455,229,t.textParam,168);
 const int controls=std::clamp(studio_synth_control_count(s),1,4);
 const bool xyPage=studio_synth_xy_page(s);
 const bool frogPage=xyPage&&studio_synth_frog_model(s);
 const bool frogFirst=frogPage&&s.studioSynthPage==0;   // three macro cards beside the stage
 const int cellWidth=frogFirst?94:frogPage?100:xyPage?190:608/controls;   // the XY page keeps two slim cards and gives the rest to the pad
 studio_card(c,16,248,608,105,t);
 // Each family has its own control sheet: knobs for the resonators and the low end, an LCD for the digital and chip
 // engines, a bar graph for the spectral ones, the plain blocks for the rest. The numbers and the cursor stay the same.
 const SynthSheet sheet=frogFirst?SynthSheet::Blocks:synth_sheet(studio_synth_model_group(s.studioModel));
 for(int i=0;i<controls;++i){
  const int x=16+i*cellWidth;
  const bool selected=s.studioRow==i+3;
  const Argb ink=selected?t.rowCursor:t.textValue;
  if(i)c.fill_rect(x,258,1,84,with_alpha(t.textParam,.23f));
  if(selected){
   c.panel_rect(x+5,254,cellWidth-10,92,mix(t.rowEvery4th,t.rowCursor,.12f),t.rowCursor,5);
   c.mark_cursor(x+5,254,cellWidth-10,92);   // the highlight glides between controls (ui/layout.cpp)
   c.fill_rect(x+6,265,2,70,t.rowCursor);
  }
  const int level=c.tween(0x2000000u+static_cast<uint32_t>(i),std::clamp(studio_synth_control_level(s,i),0,255));   // the numbers fill smoothly
  label(c,studio_synth_control_label(s,i),x+16,265,selected?ink:t.textParam,cellWidth-32,14);
  if(sheet==SynthSheet::Knob){
   // A dial: ticks on a 270 degree sweep, the lit part up to the value, and a pointer.
   label(c,studio_synth_control_value(s,i),x+16,290,ink,cellWidth-32-(controls>2?40:52),20);
   const float cx=float(x+cellWidth-(controls>2?32:42)),cy=311.f,r=controls>2?17.f:22.f;
   const int ticks=16;
   for(int k=0;k<=ticks;++k){
    const float a=(225.f-270.f*float(k)/ticks)*3.14159265f/180.f;
    const bool lit=k*255/ticks<=level;
    const float r0=r-(k%4==0?6.f:4.f);
    c.line(int(std::lround(cx+r0*std::cos(a))),int(std::lround(cy-r0*std::sin(a))),int(std::lround(cx+r*std::cos(a))),int(std::lround(cy-r*std::sin(a))),with_alpha(t.rowCursor,lit?(selected?.95f:.6f):.2f));
   }
   const float pa=(225.f-270.f*float(level)/255.f)*3.14159265f/180.f;
   c.line(int(cx),int(cy),int(std::lround(cx+(r-8.f)*std::cos(pa))),int(std::lround(cy-(r-8.f)*std::sin(pa))),selected?t.rowCursor:t.textValue);
   c.fill_rect(int(cx)-1,int(cy)-1,3,3,selected?t.rowCursor:t.textValue);
   c.fill_rect(x+16,330,cellWidth-32,1,with_alpha(t.rowCursor,.15f));
  }else if(sheet==SynthSheet::Lcd){
   // A small LCD: the value in phosphor on a dark inset with scanlines, and a strip of LED cells for the level.
   const Argb phosphor=0xFF9BE08Au;
   c.panel_rect(x+14,286,cellWidth-28,28,0xFF081008u,with_alpha(phosphor,selected?.7f:.35f),3);
   for(int row=288;row<312;row+=2)c.fill_rect(x+16,row,cellWidth-32,1,with_alpha(phosphor,.06f));
   label(c,studio_synth_control_value(s,i),x+20,290,with_alpha(phosphor,selected?1.f:.8f),cellWidth-40,20);
   const int cells=24,step=(cellWidth-32)/cells;
   for(int j=0;j<cells;++j){const bool lit=j*255/cells<level;c.fill_rect(x+16+j*step,322,std::max(1,step-1),11,with_alpha(phosphor,lit?(selected?.9f:.55f):.10f));}
  }else if(sheet==SynthSheet::Bars){
   // A bar graph that rises like a spectrum: every bar a little taller than the one before it.
   label(c,studio_synth_control_value(s,i),x+16,290,ink,cellWidth-32,20);
   const int bars=16,step=(cellWidth-32)/bars;
   for(int j=0;j<bars;++j){const bool lit=j*255/bars<level;const int bh=3+j*13/bars;c.fill_rect(x+16+j*step,334-bh,std::max(1,step-2),bh,with_alpha(t.rowCursor,lit?(selected?.92f:.57f):.14f));}
  }else{
   label(c,studio_synth_control_value(s,i),x+16,290,ink,cellWidth-32,20);
   const int blocks=16,step=(cellWidth-32)/blocks;
   for(int j=0;j<blocks;++j){
    const bool lit=j*255/blocks<level;
    c.fill_rect(x+16+j*step,326,step-2,7,with_alpha(t.rowCursor,lit?(selected?.92f:.57f):.15f));
   }
  }
 }
 if(xyPage){
  // The pad: X is the first macro, Y the second (up is more). The dot glides to each new position; with
  // scene motion on a ring breathes around it. Hold L and press the D-pad to move it.
  if(frogFirst)frog_stage(c,s,304,254,312,92);
  else if(frogPage)frog_stage(c,s,224,254,392,92);
  else{
  const int px=398,py=254,pw=218,ph=92,inner=8;
  c.panel_rect(px,py,pw,ph,mix(t.rowEvery4th,t.background,.5f),with_alpha(t.rowCursor,.55f),5);
  for(int g=1;g<4;++g){c.fill_rect(px+inner+g*(pw-2*inner)/4,py+inner,1,ph-2*inner,with_alpha(t.textParam,.18f));c.fill_rect(px+inner,py+inner+g*(ph-2*inner)/4,pw-2*inner,1,with_alpha(t.textParam,.18f));}
  const int gx=c.tween(0x2100000u,std::clamp(s.studioHarmonics,0,255)),gy=c.tween(0x2100001u,std::clamp(s.studioTimbre,0,255));
  const int dx=px+inner+gx*(pw-2*inner-1)/255,dy=py+ph-inner-1-gy*(ph-2*inner-1)/255;
  c.fill_rect(px+inner,dy,pw-2*inner,1,with_alpha(t.rowCursor,.35f));   // the crosshair
  c.fill_rect(dx,py+inner,1,ph-2*inner,with_alpha(t.rowCursor,.35f));
  if(s.settings.sceneMotion){const int phase=int((s.animationMs/90)%6);c.stroke_rect(dx-4-phase,dy-4-phase,9+2*phase,9+2*phase,with_alpha(t.rowCursor,.5f-.08f*phase));}
  c.fill_rect(dx-2,dy-2,5,5,t.rowCursor);
  label(c,"HOLD L + D-PAD",px+10,py+ph-20,with_alpha(t.textParam,.8f),pw-20,10);
  }
 }

 const bool focus=s.studioRow==studio_synth_apply_row(s);
 const auto& compare=s.studioSynthCompare;
 const bool applyFocused=focus&&!compare.focused;
 studio_card(c,16,361,192,53,t,applyFocused);
 const std::string action=s.studioConfirm?"A AGAIN: REPLACE":live?"LIVE EDIT":"A: APPLY TO "+target;
 label(c,action,30,371,applyFocused?t.rowCursor:t.textValue,163,16);
 label(c,live?"Linked instrument":s.studioConfirm?"Replace slot "+target:"Hold L: Audition",30,394,t.textParam,163);
 const bool compareFocused=focus&&compare.focused;
 studio_card(c,216,361,132,53,t,compareFocused);
 label(c,"A / B",228,371,compareFocused?t.rowCursor:t.textValue,105,16);
 label(c,!compare.alternate?"A: Store A":compare.listeningToA?"A active   > B":"B active   > A",228,394,t.textParam,105);
 studio_card(c,358,361,266,53,t);
 const auto& take=s.resampleView;
 std::string status;
 if(take.active){
  char text[32];std::snprintf(text,sizeof(text),"REC %.1fs",take.rate>0?double(take.frames)/take.rate:0.0);
  status=text;
 }else if(take.saving)status="SAVING";
 else if(s.studioAuditionHeld)status="AUDITION  "+studio_synth_note_name(s);
 else if(take.frames>0&&!take.failed&&!take.path.empty())status="B + Up: Use take";
 else status=s.isPlaying?"PLAYBACK":"L: "+studio_synth_note_name(s);
 c.draw_label("OUT",371,370,t.textParam,12);
 label(c,status,403,369,take.active?0xffffaaaa:(take.saving||s.studioAuditionHeld)?t.rowCursor:t.textValue,200,13);
 scope(c,371,386,240,20,s.waveform,WAVEFORM_SIZE,t);
}

// The sound browser's rows for `tab`, read-only: FAV / RECENT from settings, a category as scanned.
// (A category is not copied: ALL holds 2048 entries.)
const std::vector<SoundEntry>& browser_rows(const AppState& s,SoundTab tab){
 static std::vector<SoundEntry> listed;
 listed.clear();
 if(tab==SoundTab::FAV||tab==SoundTab::RECENT){
  for(const auto& key:tab==SoundTab::FAV?s.settings.soundFavorites:s.settings.soundRecent)
   if(sound_key_valid(key))listed.push_back(sound_entry_from_key(key));
 }else if(sound_tab_is_category(tab)&&s.soundLibrary.scanned[static_cast<size_t>(tab)])
  return s.soundLibrary.entries[static_cast<size_t>(tab)];
 return listed;
}

void sound_tabs(Canvas& c,const AppState& s,const Theme& t){
 // Each tab as wide as its name (the Studio font is proportional); the free width spreads evenly,
 // so all twelve read in full across the 608 px card.
 const auto current=s.studioSynthPicker.tab;
 constexpr int x0=16,span=608,y=150,h=21,size=11;
 int widths[kSoundTabCount],used=0;
 for(int i=0;i<kSoundTabCount;++i){widths[i]=Canvas::label_width(sound_tab_name(static_cast<SoundTab>(i)),size)+10;used+=widths[i];}
 const int extra=std::max(0,(span-used)/kSoundTabCount);
 int x=x0;
 for(int i=0;i<kSoundTabCount;++i){
  const auto tab=static_cast<SoundTab>(i);
  const bool focus=tab==current;
  const int w=widths[i]+extra;
  if(focus)c.panel_rect(x+1,y,w-2,h,mix(t.rowEvery4th,t.rowCursor,.18f),t.rowCursor,4);
  c.draw_label(sound_tab_name(tab),x+(w-widths[i])/2+5,y+5,focus?t.rowCursor:(i==0?t.textValue:t.textParam),size);
  x+=w;
  // MODEL | FAV RECENT | ALL + categories: a hairline after MODEL and after RECENT.
  if(i==0||i==2)c.fill_rect(x-1,y+3,1,h-6,with_alpha(t.textParam,.35f));
 }
}

void synth_picker_page(Canvas& c,const AppState& s){
 const auto t=synth_theme(s);
 const auto& picker=s.studioSynthPicker;
 const bool model=picker.mode==StudioSynthPickerMode::Model;
 const bool live=studio_synth_live(s);
 // A message (a use, a star, the preview level) takes the hint line, as on the Studio's pages.
 const bool focusHint=s.studioMessage.empty();
 const bool factoryTab=!model&&picker.tab!=SoundTab::MODEL;
 studio_card(c,16,100,608,44,t);
 label(c,model?"SYNTH MODEL":factoryTab?"SOUNDS":"PRESET",29,112,t.textValue,180,18);
 if(!model)label(c,factoryTab?"Factory instrument presets":studio::model_name(s.studioModel),165,115,t.textParam,285,14);
 const std::string target="Slot "+songcore::hex2(s.currentInstrument)+(live?" / LIVE":" / DRAFT");
 label(c,target,481,116,t.rowCursor,128,12);
 if(model){
  const auto& groups=studio_synth_model_groups();
  const bool railRight=scene_layout(t).railLeft;
  const int familyX=railRight?446:16;
  const int modelX=railRight?16:202;
  studio_card(c,familyX,153,178,259,t);
  // Eight families fit the card at 31 px; a ninth (Spectral) tightens the rows to 27 px, a tenth (Voice)
  // to 25 px, an eleventh (Vintage) to 23 px.
  const int families=studio_synth_model_group_count();
  const int familyStep=families>11?21:families>10?23:families>9?25:families>8?27:31;
  const int familyTop=families>10?158:161;   // eleven rows end 3 px above the card's lower edge
  for(int i=0;i<families;++i){
   const bool focus=i==picker.group;
   const int y=familyTop+i*familyStep;
   if(focus)c.panel_rect(familyX+6,y,166,familyStep-2,mix(t.rowEvery4th,t.rowCursor,.16f),t.rowCursor,4);
   label(c,groups[i].name,familyX+14,y+7,focus?t.rowCursor:t.textValue,150,14);
  }
  studio_card(c,modelX,153,422,259,t);
  const auto& group=groups[picker.group];
  const int highlighted=studio_synth_group_model(group,picker.item);
  constexpr int visible=8;
  const int first=std::clamp(picker.item-visible/2,0,std::max(0,group.count-visible));
  const int active=sample_granular_studio(s)?kSampleGranularModel:s.studioModel;
  for(int i=first;i<std::min(group.count,first+visible);++i){
   const int y=161+(i-first)*27;
   const int id=studio_synth_group_model(group,i);
   const bool focus=i==picker.item;
   if(focus){c.panel_rect(modelX+6,y,410,25,mix(t.rowEvery4th,t.rowCursor,.15f),t.rowCursor,4);c.mark_cursor(modelX+6,y,410,25);}
   label(c,id==kSampleGranularModel?"Sample Granular":studio::model_name(id),modelX+20,y+5,focus?t.rowCursor:t.textValue,307,14);
   if(id==active)label(c,"CURRENT",modelX+338,y+6,t.textParam,65,12);
  }
  c.mark_scroll(20+16*picker.group,modelX+6,161,410,visible*27,first*27);   // one family's models slide
  if(group.firstModel==kSampleGranularModel){
   label(c,"Granulate a loaded sample.",modelX+20,199,t.textParam,383,14);
   label(c,"Load or crop the source in the Sample Granular editor.",modelX+20,225,t.textParam,383,12);
  }else{
   const std::string position=std::to_string(picker.item+1)+" / "+std::to_string(group.count);
   if(studio::is_gravity_model(highlighted))label(c,gravity_description(highlighted),modelX+20,364,t.textParam,375,14);
   else if(model_blurb(highlighted))label(c,model_blurb(highlighted),modelX+20,364,t.textParam,300,14);
   label(c,position,modelX+20,390,t.textParam,111);
   synth_diagram(c,highlighted,modelX+324,384,t.rowCursor);
  }
  if(focusHint)label(c,"Left / Right: Family    Up / Down: Model    R: Sample Granular",22,435,t.textParam,596);
 }else{
  sound_tabs(c,s,t);
  if(picker.tab==SoundTab::MODEL){
   // The current model's sixteen presets, as before; Up / Down walk them in order.
   const int count=studio::variation_count();
   const int columnSize=(count+1)/2;
   for(int i=0;i<count;++i){
    const int x=16+(i/columnSize)*309,y=178+(i%columnSize)*29;
    const bool focus=i==picker.item;
    studio_card(c,x,y,299,26,t,focus);
    label(c,studio_synth_preset_label(s.studioModel,i),x+15,y+5,focus?t.rowCursor:t.textValue,206,14);
    if(!s.studioSynthCustom&&i==s.studioVariation)label(c,"CURRENT",x+225,y+7,t.textParam,64,12);
   }
   if(focusHint)label(c,"Left / Right: Tab    Up / Down: Preset    Right: Factory sounds",22,435,t.textParam,596);
  }else{
   const auto& rows=browser_rows(s,picker.tab);
   studio_card(c,16,176,608,240,t);
   constexpr int visible=8,rowH=27,top=182;
   const int count=static_cast<int>(rows.size());
   const int row=std::clamp(picker.rows[static_cast<size_t>(picker.tab)],0,std::max(0,count-1));
   if(count==0){
    const char* empty=picker.tab==SoundTab::FAV?"No stars yet. R on a sound stars it.":
                      picker.tab==SoundTab::RECENT?"Nothing used yet. A on a sound uses it.":
                      !s.soundLibrary.found?"Factory sounds not found. INSTRUMENT > LOAD opens .pti files.":
                      "No sounds in this tab.";
    label(c,empty,34,top+8,t.textParam,570,14);
   }
   const int first=std::clamp(row-visible/2,0,std::max(0,count-visible));
   for(int i=first;i<std::min(count,first+visible);++i){
    const auto& e=rows[static_cast<size_t>(i)];
    const int y=top+(i-first)*rowH;
    const bool focus=i==row;
    const bool used=e.key==picker.usedKey;
    if(focus){c.panel_rect(22,y,596,rowH-2,mix(t.rowEvery4th,t.rowCursor,.15f),t.rowCursor,4);c.mark_cursor(22,y,596,rowH-2);}
    if(sound_is_favorite(s.settings.soundFavorites,e.key))label(c,"*",32,y+5,t.meterHigh,12,14);
    label(c,e.family,48,y+5,focus?t.rowCursor:t.textValue,230,14);
    label(c,e.variant,288,y+5,focus?t.rowCursor:t.textValue,190,14);
    label(c,used?"IN SLOT":e.group,488,y+7,used?t.rowCursor:t.textParam,124,12);
   }
   c.mark_scroll(200+static_cast<int>(picker.tab),22,top,596,visible*rowH,first*rowH);
   const std::string position=count?std::to_string(row+1)+" / "+std::to_string(count):"0 / 0";
   label(c,position,30,top+visible*rowH+4,t.textParam,120,12);
   const int ceiling=picker.previewCeiling;
   const bool capped=ceiling>0&&s.settings.soundPreviewLevel>ceiling;
   label(c,"Preview "+std::to_string(s.settings.soundPreviewLevel)+" %"+(capped?" (max "+std::to_string(ceiling)+" %)":""),
         capped?400:470,top+visible*rowH+4,t.textParam,capped?210:140,12);
   if(focusHint)label(c,"Up / Down: Sound   A + Up / Down: Page   R: Star   A + Left / Right: Preview level",22,435,t.textParam,596);
  }
 }
 c.fill_rect(18,424,606,1,with_alpha(t.textParam,.2f));
 if(!focusHint)label(c,s.studioMessage,22,435,t.rowCursor,596);
 const bool granularChoice=model&&picker.group==0;
 const char* keys=granularChoice?"A: Open editor    B: Back    Select: Help":
                  model?"Hold L: Audition    A: Presets    B: Back":
                  picker.confirmReplace?"A: Replace    B: Keep the slot":
                  factoryTab&&!picker.usedKey.empty()?(picker.original?"Hold L: Listen    A: Use / Keep    B: Undo":"Hold L: Listen    A: Use / Keep    B: Close"):
                  factoryTab?"Hold L: Listen    A: Use    B: Back    Select: Help":
                  "Hold L: Audition    A: Choose    B: Back    Select: Help";
 label(c,keys,22,458,t.textValue,438);
 if(picker.previewSounding){
  c.draw_label("OUT",465,456,t.textParam,12);
  scope(c,499,451,117,20,s.waveform,WAVEFORM_SIZE,t);
 }
}

void input_page(Canvas& c,const AppState& s){
 const auto& t=s.theme;const auto& v=s.capture;const auto& rv=s.resampleView;
 const bool internal=v.internal_source();
 const CaptureInput* selected=v.inputs.empty()?nullptr:&v.inputs[std::clamp(v.selected,0,int(v.inputs.size())-1)];
 const bool phone=selected&&selected->kind.rfind("iphone",0)==0;
 const bool disconnected=selected&&!selected->connected&&selected->kind!="iphone-action";
 const bool receiving=internal?rv.active&&rv.frames>0:v.receiving();
 const int64_t frames=internal?rv.frames:v.frames;const int rate=internal?rv.rate:v.actualRate;
 studio_card(c,16,102,608,70,t);
 c.draw_label("RECORD / Internal + input",30,113,t.textValue,18);
 label(c,internal?"Internal mix includes Punch + Master. Start: playback.":s.captureBackend?"Microphone / USB interface / experimental iPhone":"External input unavailable; internal resampling works",30,145,t.textParam,477);
 char time[32];std::snprintf(time,sizeof(time),"%02lld:%02lld",static_cast<long long>(frames/std::max(rate,1)/60),static_cast<long long>(frames/std::max(rate,1)%60));
 c.draw_text(time,531,119,receiving?t.meterHigh:t.textValue,1,3);
 const std::string input=selected?selected->name+(disconnected?" / disconnected":""):"Internal master / resample";
 const bool empty=v.targetInstrument>=0&&s.project&&songcore::instrument_is_free(s.project->instruments[std::clamp(v.targetInstrument,0,127)]);
 const char* lengths[]={"FREE / 60 sec","1 BAR","2 BARS","4 BARS","8 BARS"};
 const std::string names[]={!internal&&v.active()?"Source < internal":"Source  < / >",internal?"Length  < / >":"Channels  < / >",internal?"Format / master":"Sample rate  < / >","New take slot  < / >"};
 const std::string values[]={input,internal?lengths[std::clamp(rv.lengthIndex,0,4)]:v.channels==1?"Mono":"Stereo",internal?"Stereo / live engine rate":std::to_string(v.rate)+" Hz / 16 bit",v.targetInstrument<0?"Auto: first empty slot":songcore::hex2(v.targetInstrument)+(empty?" / empty":" / occupied - protected")};
 for(int i=0;i<4;++i)row(c,16,184+i*31,390,names[i],values[i],t,s.studioRow==i);
 studio_card(c,16,312,390,34,t,s.studioRow==4);
 const std::string record=internal?(rv.saving?"SAVING TAKE...":rv.active?"A  STOP & SAVE RESAMPLE":rv.failed?"A  RETRY SAVING TAKE":"A  START RESAMPLE"):v.active()?"A  STOP & KEEP TAKE":"A  START RECORDING";
 label(c,record,31,320,s.studioRow==4?t.rowCursor:t.textValue,359,18);
 studio_card(c,16,350,390,28,t,s.studioRow==5);
 const bool hasTake=internal?rv.frames>0&&!rv.active&&!rv.saving&&!rv.failed&&!rv.path.empty():!v.path.empty();
 label(c,hasTake?"A  USE TAKE > EDIT & SLICE":"USE TAKE  / record first",31,357,s.studioRow==5?t.rowCursor:t.textValue,359,16);
 // MONITOR and REFRESH share the last band side by side: two full-size cells whose bottom edge meets the
 // level card on the right, instead of two cramped strips.
 {
  const bool monitorFocus=s.studioRow==6,refreshFocus=s.studioRow==7;
  studio_card(c,16,382,190,44,t,monitorFocus);
  label(c,"A  MONITOR",28,389,monitorFocus?t.rowCursor:t.textParam,168,12);
  label(c,internal?"external inputs only":(v.monitor?"ON":"OFF"),28,404,monitorFocus?t.rowCursor:internal?t.textParam:(v.monitor?t.meterHigh:t.textValue),168,internal?12:18);
  studio_card(c,210,382,196,44,t,refreshFocus);
  label(c,"A  REFRESH",222,389,refreshFocus?t.rowCursor:t.textParam,174,12);
  label(c,"MIC / USB inputs",222,404,refreshFocus?t.rowCursor:t.textValue,174,16);
 }
 studio_card(c,416,184,208,242,t);
 std::string status,detail;
 if(internal){
  c.draw_label("Internal output",430,196,t.textParam);
  label(c,"POST FX / MASTER",430,220,t.rowCursor,180,12);
  label(c,"Punch-in included",430,238,t.textParam,180,12);
  status=rv.active?(rv.frames>0?"REC - internal mix":"Waiting for frames"):rv.saving?"Saving take...":rv.failed?"Save failed - retry":rv.frames>0?"Resample ready":"Ready: internal mix";
  detail=rv.message.empty()?"Start: playback. A starts or stops resampling. No USB input needed. Stop playback before Use Take.":rv.message;
 }else{
  c.draw_label("Actual input level",430,196,t.textParam);
  meter(c,430,221,180,12,v.receiving()?v.peakL:0.f,t);meter(c,430,242,180,12,v.receiving()?v.peakR:0.f,t);
  status=studio_capture_status(v);detail=v.inputError;
  if(detail.empty()&&disconnected)detail=selected->detail.empty()?"Selected input disconnected. Reconnect it or choose another source; recording has not started.":selected->detail;
  if(detail.empty()&&phone){
   const bool current=v.requestedId==selected->id&&v.state!="idle";
   detail=(v.receiving()?"Receiving audio. ":v.phoneTrusted&&current?"Trust verified; waiting for audio. ":selected->connected?"Phone detected; audio not verified. ":"No phone detected. ");
   detail+=current&&!v.message.empty()?v.message:selected->detail;
   detail+=" Power warning is not an audio connection. Direct iPhone USB remains unverified.";
  }else if(detail.empty())detail=v.state=="idle"&&selected&&!selected->detail.empty()?selected->detail:v.message.empty()?"Choose a source, then Start. USB charging alone is not audio. Software monitoring is off.":v.message;
 }
 label(c,status,430,266,receiving?t.meterHigh:t.rowCursor,180,14);
 // Keep real backend errors readable rather than silently clipping to one line.
 size_t offset=0;for(int line=0;line<8&&offset<detail.size();++line){
  size_t end=std::min(detail.size(),offset+26);
  if(end<detail.size()){const auto space=detail.rfind(' ',end);if(space!=std::string::npos&&space>offset)end=space;}
  label(c,detail.substr(offset,end-offset),430,291+line*15,t.textParam,180,12);
  offset=end;while(offset<detail.size()&&detail[offset]==' ')++offset;
 }
}

void style_page(Canvas& c,const AppState& s){
 const auto& t=s.theme;
 const int font=theme_font(t,s.settings.fontStyle);
 studio_card(c,16,102,608,54,t,s.studioRow==0);c.draw_label("Design collection",30,109,t.textParam);
 c.draw_label(t.name,30,128,t.textValue,18);label(c,"A  SCENES",503,130,t.textParam,100);
 studio_card(c,16,166,608,46,t,s.studioRow==1);c.draw_label("Material",30,182,t.textParam);
 label(c,surface_style_name(t.surfaceStyle),145,178,t.rowCursor,460,18);
 studio_card(c,16,222,608,64,t,s.studioRow==2);c.draw_label("Typeface",30,237,t.textParam);
 // Where this choice lives (studio.cpp): with a Scene look it belongs to the Scene and is replaced
 // when a Scene is applied again; otherwise it is the typeface of every look.
 const bool sceneFont=t.sceneChrome>0&&t.sceneChrome<=kSceneCount;
 const char* scope=!sceneFont?"ALL LOOKS":font==kScenePresets[size_t(t.sceneChrome-1)].font?"SCENE FONT":"THIS SCENE ONLY";
 label(c,demo_font_style_name(font),145,233,t.rowCursor,330,18);
 label(c,scope,608-Canvas::label_width(scope,12),240,sceneFont&&std::string(scope)=="THIS SCENE ONLY"?t.rowCursor:t.textParam,140,12);
 label(c,demo_font_style_detail(font),30,263,t.textParam,574,12);
 studio_card(c,16,294,608,38,t,s.studioRow==3);c.draw_label("Motion",30,305,t.textParam);
 label(c,s.settings.sceneMotion?"ON":"OFF",145,301,t.rowCursor,60,18);
 label(c,s.settings.sceneMotion?"Scene animations while playing":"Still screens: no Scene animation",215,307,t.textParam,390,12);
 studio_card(c,16,340,608,76,t);c.draw_label("TRACKWERK / TYPE LAB",30,348,t.textValue,18);
 label(c,std::to_string(valid_demo_font_style(font)+1)+" / "+std::to_string(kDemoFontStyleCount),536,351,t.rowCursor,75,12);
 c.draw_text("C#4 A-3 120 00 FF",30,375,t.rowCursor,1,3);
 c.draw_text("0/O  1/I/l  8/B  E/F  #",330,378,t.textValue,1,2);
}
Argb punch_ink(const Theme& theme,int bank){
 // Keep custom monochrome/OLED palettes exact; other themes gain a restrained family tint.
 if(theme.name=="MONO"||theme.name.find("OLED")!=std::string::npos)return theme.rowCursor;
 static constexpr Argb colors[]={0xFF72E2D4,0xFFF3BE73,0xFFB3EB83,0xFF77BFF2,
                                0xFFFF91C4,0xFFBCA3FF,0xFF7FE2EA,0xFFFF9D7B};
 return mix(theme.rowCursor,colors[std::clamp(bank,0,7)],theme.surfaceStyle>=12?.62f:.42f);
}
void punch_page(Canvas& c,const AppState& s){
 const auto& t=s.theme;const int bank=studio::punch_bank(s.studioPunch),pad=studio::punch_pad(s.studioPunch);
 const Argb accent=punch_ink(t,bank);
 const bool monitorBelow=scene_layout(t).scopeBottom;
 const int monitorY=monitorBelow?275:126;
 const int padsY=monitorBelow?126:188;
 const char* banks[]={"RHYTHM","TAPE","FILTER","DUB","GLITCH","GRAIN","MOD","DAMAGE"};
 for(int i=0;i<studio::kPunchBanks;++i)chip(c,banks[i],16+i*77,94,72,t,i==bank);
 c.fill_rect(28+bank*77,115,48,1,accent);
 studio_card(c,16,monitorY,608,51,t,s.studioAUsed);
 c.draw_label(studio::punch_bank_name(bank),31,monitorY+11,t.textValue,18);
 label(c,studio::punch_name(s.studioPunch),31,monitorY+34,s.studioAUsed?accent:t.textParam,227);
 // A large effect diagram supplies the performance movement. Its raster never crosses labels.
 c.fill_rect(275,monitorY+7,242,36,recess(t.background,t.vizBackground,.72f));
 for(int yy=monitorY+10;yy<monitorY+43;yy+=6)c.fill_rect(278,yy,236,1,with_alpha(t.textParam,.07f));
 brand::punch_gesture(c,bank,s.studioPunch,279,monitorY+9,234,31,s.blinkPhaseMs,s.studioAUsed,accent);
 c.panel_rect(535,monitorY+13,73,23,mix(t.rowEvery4th,accent,s.studioAUsed?.17f:.04f),with_alpha(accent,s.studioAUsed?.8f:.32f),4);
 label(c,s.studioAUsed?"HELD A":"READY",546,monitorY+17,accent,53);
 // This cursor is real sequencer readback, not a decorative beat/level estimate.
 int sequenceStep=-1;
 if(s.isPlaying)for(const auto& playhead:s.playheads)if(playhead.step>=0){sequenceStep=playhead.step&15;break;}
 for(int i=0;i<studio::kPunchPads;++i){
  const int x=16+(i%4)*154,y=padsY+(i/4)*74,id=studio::punch_id(bank,i);
  const bool selected=i==pad,active=selected&&s.studioAUsed;
  studio_card(c,x,y,146,65,t,selected);
  if(selected)c.fill_rect(x+1,y+8,3,49,active?accent:with_alpha(t.rowCursor,.65f));
  label(c,std::to_string(i+1),x+12,y+7,selected?accent:t.textParam,20);
  if(selected)label(c,active?"HELD":"READY",x+94,y+7,accent,42);
  if(active)c.fill_rect(x+10,y+23,126,16,with_alpha(accent,.075f));
  brand::punch_gesture(c,bank,id,x+12,y+24,122,15,s.blinkPhaseMs,active,
                       selected?accent:with_alpha(accent,.50f));
  label(c,studio::punch_name(id),x+12,y+43,selected?t.textValue:t.textParam,124);
  c.fill_rect(x+12,y+60,122,1,with_alpha(selected?t.rowCursor:t.textParam,selected?.32f:.12f));
  if(active){
   c.stroke_rect(x+5,y+4,136,57,with_alpha(accent,.50f));
   for(int edge:{0,1}){
    const int xx=edge?x+131:x+5;
    c.fill_rect(xx,y+4,10,2,accent);c.fill_rect(xx,y+59,10,2,accent);
   }
  }
  if(selected&&sequenceStep>=0){
   for(int step=0;step<16;++step)c.fill_rect(x+12+step*7,y+60,5,step==sequenceStep?2:1,
                                      with_alpha(accent,step==sequenceStep?.9f:.24f));
  }
 }
 studio_card(c,16,340,608,27,t);
 label(c,studio::punch_detail(s.studioPunch),30,348,t.textValue,416);
 c.draw_label("OUT",467,348,t.textParam);
 scope(c,503,346,104,16,s.waveform,WAVEFORM_SIZE,t);
 draw_live_resample_strip(c,s,16,382,608,35);
}

} // namespace
void draw_studio(Canvas& c,const AppState& s){
 c.set_appearance(s.theme,s.settings.fontStyle);heading(c,s);const auto& t=s.theme;
 if(s.studioHelp){draw_studio_help(c,s);return;}
 if(draw_studio_workspace(c,s))return;
 if(studio_synth_picker_open(s)){synth_picker_page(c,s);return;}
 if(sample_granular_preset_picker_open(s)){draw_sample_granular(c,s);return;}
 c.mark_stage(0,88,640,392);   // below the heading and tabs: the page that changes (shell/app.cpp)
 if(sample_granular_studio(s))draw_sample_granular(c,s);
 else if(s.studioTab==0)synth_page(c,s);
 else if(s.studioTab==1||s.studioTab==2)list_page(c,s);
 else if(s.studioTab==3)punch_page(c,s);
 else if(s.studioTab==4)input_page(c,s);
 else if(s.studioTab==5){drumlab_draw(c,s.drumLab,t,*s.project);return;}
 else if(s.studioTab==6)style_page(c,s);
 else if(s.studioTab==7){source_sampler_draw(c,s.sourceSampler,t,*s.project,s.currentInstrument);return;}
 else if(s.studioTab==8){harmony_draw(c,s);return;}
 // Drumlab draws its own grid between the shared heading and footer.
 c.fill_rect(18,424,606,1,with_alpha(t.textParam,.2f));
 const bool granular=sample_granular_studio(s);
 const char* synthHint=s.studioRow==1?"A: Choose model    A + Down: Sample Granular":
    s.studioRow==2?"A: Choose preset    A + Left / Right: Step presets":
    s.studioRow==0?"A + arrows: Change target slot":
    s.studioRow==studio_synth_apply_row(s)?(s.studioSynthCompare.focused?
      "Left: Apply    A: Store or swap    A + Up: Store new A":"Right: A/B compare    A: Apply    Hold L: Audition"):
    "A + arrows: Edit    R + Left / Right: Parameter page";
 const char* granularHint=s.studioRow==1?"A: Choose synth model":
    s.studioRow==2?"A: Browse presets    A + arrows: Quick change":
    s.studioRow==kGranularWaveRow?(s.sampleGranular.waveformMode==GranularWaveformMode::Position?
      "A: View mode    A + arrows: Move grain position":"A: Position mode    A + Up / Down: Zoom    A + Left / Right: Pan"):
    s.studioRow==studio_synth_apply_row(s)?(s.sampleGranular.action==kGranularCompareAction?
      "A: Store / toggle    A + Left / Right: Morph    A + Up: Set A":"Left / Right: Action    A: Run"):
    s.studioRow==0?"A + arrows: Change target slot":"A + arrows: Edit    R + Left / Right: Parameter page";
 const int hintControl=s.studioRow-3;
 const bool lyricRow=!granular&&s.studioTab==0&&s.studioModel==studio::kSingFrogModel&&hintControl>=0&&hintControl<studio_synth_control_count(s)&&studio_synth_control_label(s,hintControl)=="Lyrics";
 const bool stageRow=!granular&&s.studioTab==0&&studio_synth_frog_model(s)&&s.studioSynthPage==0&&hintControl>=0&&hintControl<studio_synth_control_count(s);
 const std::string hint=lyricRow?"A + Right: type the words    A + Up / Down: templates (Presets/Lyrics)":stageRow?"A + arrows: Edit    Hold L + D-pad: move the firefly":granular?granularHint:s.studioAppearanceSettings?"Appearance / your saved themes and typefaces":s.studioTab==3?"L / R: Category     D-pad: Select pad     Select: Help":s.studioTab==0?synthHint:s.studioTab==4?"B + Left / Right: Studio pages    Select: Recording help":"B + Left / Right: Page     A + D-pad: Edit     Select: Help";
 label(c,s.studioMessage.empty()?hint:s.studioMessage,22,435,s.studioMessage.empty()?t.textParam:t.rowCursor,590);
 const auto controls=granular?"Hold R + Select: Audition    Select: Help    B: Back":s.studioAppearanceSettings?"Up / Down: Choose     A + arrows: Change     B: Back to Settings":s.studioTab==3?"Hold A: Punch    Release A: Restore    Start: Play / Stop    B: Back":s.studioTab==0?"L + R + Start: Record / Stop    B + Down: Length    Select: Help    B: Back":s.studioTab==4?"Up / Down: Select    Left / Right: Change    A: Action    B: Back":"A: Apply     Start: Playback     B: Back";
 label(c,controls,22,458,t.textEmpty,590);
}
} // namespace pt::ui
