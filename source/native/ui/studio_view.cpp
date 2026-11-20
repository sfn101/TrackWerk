#include "ui/studio.h"
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
 static constexpr Argb colors[]={0xFF5CD8D0,0xFFB699F3,0xFF78B7EE,0xFFE4BD7A,0xFF8FDEA6,0xFFE89BBE,0xFFE8A37A,0xFF9FC4F8,0xFFF0A0A8,0xFFB8E07A};
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
const char* gravity_description(int model){
 switch(model-studio::kGravityFirstModel){
 case 0:return "Each rebound strikes a tuned resonator";
 case 1:return "Crossing orbit sectors strikes bells";
 default:return "Independent drops trigger tuned impacts";
 }
}
void synth_diagram(Canvas& c,int model,int x,int y,Argb color){
 if(model==studio::kFmSysexModel){
  // FM SYSEX: six operators, a stack of three modulating a carrier beside a second stack.
  const auto dim=with_alpha(color,.38f);
  static constexpr int ox[]={8,8,8,44,44,44},oy[]={0,7,14,0,7,14};
  for(int i=0;i<6;++i){c.panel_rect(x+ox[i],y+oy[i],14,5,0x17000000u,i==2||i==5?color:dim,1);if(oy[i]<14)c.line(x+ox[i]+7,y+oy[i]+5,x+ox[i]+7,y+oy[i]+7,dim);}
  c.line(x+15,y+19,x+51,y+19,color);
  return;
 }
 if(model==studio::kDcoPolyModel){
  // DCO POLY: a resonant low-pass response, flat, a peak at the cutoff, then the four-pole fall.
  const auto dim=with_alpha(color,.38f);
  c.line(x,y+18,x+76,y+18,dim);
  int previous=y+8;
  for(int px=1;px<=76;++px){
   const float t=(px-44)/6.f;
   const int py=px<44?y+8-int(6.f/(1.f+t*t)):y+8-int(6.f/(1.f+t*t))+(px-44)*(px-44)/80;
   c.line(x+px-1,previous,x+px,std::min(py,y+18),px<44?color:dim);
   previous=std::min(py,y+18);
  }
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
 label(c,studio::is_gravity_model(s.studioModel)?gravity_description(s.studioModel):group.name,194,153,t.textParam,316);
 synth_diagram(c,s.studioModel,526,134,t.rowCursor);

 studio_card(c,16,178,608,42,t,s.studioRow==2);
 label(c,"PRESET",28,191,t.textParam,75);
 const auto variation=s.studioSynthCustom?"Custom macros":studio_synth_preset_label(s.studioModel,s.studioVariation);
 label(c,variation,111,188,t.textValue,399,18);
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
 const int cellWidth=608/controls;
 studio_card(c,16,248,608,105,t);
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
  label(c,studio_synth_control_label(s,i),x+16,265,selected?ink:t.textParam,cellWidth-32,14);
  label(c,studio_synth_control_value(s,i),x+16,290,ink,cellWidth-32,20);
  const int level=c.tween(0x2000000u+static_cast<uint32_t>(i),std::clamp(studio_synth_control_level(s,i),0,255));   // the blocks fill smoothly
  const int blocks=16,step=(cellWidth-32)/blocks;
  for(int j=0;j<blocks;++j){
   const bool lit=j*255/blocks<level;
   c.fill_rect(x+16+j*step,326,step-2,7,with_alpha(t.rowCursor,lit?(selected?.92f:.57f):.15f));
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
  const int familyStep=families>10?23:families>9?25:families>8?27:31;
  const int familyTop=families>10?158:161;   // eleven rows end 3 px above the card's lower edge
  for(int i=0;i<families;++i){
   const bool focus=i==picker.group;
   const int y=familyTop+i*familyStep;
   if(focus)c.panel_rect(familyX+6,y,166,familyStep-2,mix(t.rowEvery4th,t.rowCursor,.16f),t.rowCursor,4);
   label(c,groups[i].name,familyX+14,y+7,focus?t.rowCursor:t.textValue,150,14);
  }
  studio_card(c,modelX,153,422,259,t);
  const auto& group=groups[picker.group];
  const int highlighted=group.firstModel+picker.item;
  constexpr int visible=8;
  const int first=std::clamp(picker.item-visible/2,0,std::max(0,group.count-visible));
  const int active=sample_granular_studio(s)?kSampleGranularModel:s.studioModel;
  for(int i=first;i<std::min(group.count,first+visible);++i){
   const int y=161+(i-first)*27;
   const int id=group.firstModel+i;
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
 label(c,internal?"Internal mix includes Punch + Master. Start: play song.":s.captureBackend?"Microphone / USB interface / experimental iPhone":"External input unavailable; internal resampling works",30,145,t.textParam,477);
 char time[32];std::snprintf(time,sizeof(time),"%02lld:%02lld",static_cast<long long>(frames/std::max(rate,1)/60),static_cast<long long>(frames/std::max(rate,1)%60));
 c.draw_text(time,531,119,receiving?t.meterHigh:t.textValue,1,3);
 const std::string input=selected?selected->name+(disconnected?" / disconnected":""):"Internal master / resample";
 const bool empty=v.targetInstrument>=0&&s.project&&songcore::instrument_is_free(s.project->instruments[std::clamp(v.targetInstrument,0,127)]);
 const char* lengths[]={"FREE / 60 sec","1 BAR","2 BARS","4 BARS","8 BARS"};
 const std::string names[]={!internal&&v.active()?"Source < internal":"Source  < / >",internal?"Length  < / >":"Channels  < / >",internal?"Format / master":"Sample rate  < / >","New take slot  < / >"};
 const std::string values[]={input,internal?lengths[std::clamp(rv.lengthIndex,0,4)]:v.channels==1?"Mono":"Stereo",internal?"Stereo / live engine rate":std::to_string(v.rate)+" Hz / 16 bit",v.targetInstrument<0?"Auto: first empty slot":songcore::hex2(v.targetInstrument)+(empty?" / empty":" / occupied - protected")};
 for(int i=0;i<4;++i)row(c,16,184+i*31,390,names[i],values[i],t,s.studioRow==i);
 studio_card(c,16,312,390,40,t,s.studioRow==4);
 const std::string record=internal?(rv.saving?"SAVING TAKE...":rv.active?"A  STOP & SAVE RESAMPLE":rv.failed?"A  RETRY SAVING TAKE":"A  START RESAMPLE"):v.active()?"A  STOP & KEEP TAKE":"A  START RECORDING";
 label(c,record,31,323,s.studioRow==4?t.rowCursor:t.textValue,359,18);
 studio_card(c,16,358,390,35,t,s.studioRow==5);
 const bool hasTake=internal?rv.frames>0&&!rv.active&&!rv.saving&&!rv.failed&&!rv.path.empty():!v.path.empty();
 label(c,hasTake?"A  USE TAKE > EDIT & SLICE":"USE TAKE  / record first",31,368,s.studioRow==5?t.rowCursor:t.textValue,359,16);
 studio_card(c,16,399,390,20,t,s.studioRow==6);
 label(c,"A  REFRESH MIC / USB INPUTS",31,402,s.studioRow==6?t.rowCursor:t.textParam,359,12);
 studio_card(c,416,184,208,235,t);
 std::string status,detail;
 if(internal){
  c.draw_label("Internal output",430,196,t.textParam);
  label(c,"POST FX / MASTER",430,220,t.rowCursor,180,12);
  label(c,"Punch-in included",430,238,t.textParam,180,12);
  status=rv.active?(rv.frames>0?"REC - internal mix":"Waiting for frames"):rv.saving?"Saving take...":rv.failed?"Save failed - retry":rv.frames>0?"Resample ready":"Ready: internal mix";
  detail=rv.message.empty()?"Start: play song. A starts or stops resampling. No USB input needed. Stop playback before Use Take.":rv.message;
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
 const std::string hint=granular?granularHint:s.studioAppearanceSettings?"Appearance / your saved themes and typefaces":s.studioTab==3?"L / R: Category     D-pad: Select pad     Select: Help":s.studioTab==0?synthHint:s.studioTab==4?"B + Left / Right: Studio pages    Select: Recording help":"B + Left / Right: Page     A + D-pad: Edit     Select: Help";
 label(c,s.studioMessage.empty()?hint:s.studioMessage,22,435,s.studioMessage.empty()?t.textParam:t.rowCursor,590);
 const auto controls=granular?"Hold R + Select: Audition    Select: Help    B: Back":s.studioAppearanceSettings?"Up / Down: Choose     A + arrows: Change     B: Back to Settings":s.studioTab==3?"Hold A: Punch    Release A: Restore    Start: Play / Stop    B: Back":s.studioTab==0?"L + R + Start: Record / Stop    B + Down: Length    Select: Help    B: Back":s.studioTab==4?"Up / Down: Select    Left / Right: Change    A: Action    B: Back":"A: Apply     Start: Playback     B: Back";
 label(c,controls,22,458,t.textEmpty,590);
}
} // namespace pt::ui
