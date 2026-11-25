#include "studio.h"
#include "ui/scene_picker.h"
#include "ui/studio_help.h"
#include "ui/studio_navigation.h"
#include "ui/studio_synth_picker.h"
#include "ui/demo_font_style.h"
#include "studio/synth.h"
#include "studio/rack.h"
#include "studio/punch.h"
#include "songcore/project_io.h"
#include "ui/helpers.h"
#include "ui/live_resample.h"
#include "ui/studio_preset_io.h"
#include "ui/modules/theme_editor.h"
#include <algorithm>
#include <cstdio>
namespace pt::ui {
namespace {
constexpr const char* kWorkspaceDescriptions[] = {
    "Choose an engine, shape a patch or open Sample Granular",
    "Shape the mix with effects and presets",
    "Set dynamics, stereo width and output level",
    "Play momentary effects across eight banks",
    "Record an input or resample the mix",
    "Generate and arrange drum patterns",
    "Find, preview and sample recordings",
    "Play chords, vary a progression and write an editable phrase",
};
static_assert(std::size(kWorkspaceDescriptions) == kStudioMusicTabs.size());

uint16_t button_bit(Button button) {
    return uint16_t(1u << static_cast<unsigned>(button));
}

void change_studio_tab(AppState& s, songcore::SongcoreHost& host, AudioEngine& engine,
                       int tab) {
    harmony_stop(s.harmony, host);
    s.harmony.confirm = false;
    studio_synth_stop_audition(s, host);
    studio_synth_close_picker(s);
    sample_granular_close_preset_picker(s);
    drumlab_stop(s.drumLab, host, engine);
    s.drumLab.gridFocused = false;
    source_sampler_stop(s.sourceSampler);
    if (s.studioTab == 4 && s.captureBackend && s.capture.active()) s.captureBackend->stop();
    s.studioConfirm = false;
    s.studioPunchBankHeld = 0;
    s.studioMessage.clear();
    s.studioTab = tab;
    s.studioRow = 0;
    engine.studioRack.punch.store(0);
    if (tab == 4) studio_capture_refresh(s, false);
    if (tab == 7) {
        s.sourceSampler.hasPolled = false;
        s.sourceSampler.youtubeEnabled = s.settings.sourceYoutube;
        source_sampler_poll(s.sourceSampler, 0);
    }
}

bool studio_workspace_button(const ButtonEvent& event, AppState& s,
                             songcore::SongcoreHost& host, AudioEngine& engine) {
    auto& workspace = s.studioWorkspace;
    const auto mask = button_bit(event.button);
    const bool pressed = event.action == ButtonAction::PRESSED;
    if (!s.studioOpen || s.studioAppearanceSettings || s.studioHelp) {
        workspace.open = false;
        return false;
    }
    if (!workspace.open) {
        if (event.button != Button::SELECT || !pressed || !event.mods.b
                || event.mods.a || event.mods.l || event.mods.r) return false;
        const auto active = std::find_if(kStudioMusicTabs.begin(), kStudioMusicTabs.end(),
                                        [&](const auto& tab) { return tab.id == s.studioTab; });
        workspace.selected = active == kStudioMusicTabs.end() ? 0 :
                             static_cast<int>(active - kStudioMusicTabs.begin());
        workspace.open = true;
        workspace.held = 0;
        s.studioInputSuppress |= button_bit(Button::B) | button_bit(Button::SELECT);
        studio_synth_stop_audition(s, host);
        studio_synth_close_picker(s);
        sample_granular_close_preset_picker(s);
        drumlab_clear_grid_input(s.drumLab);
        s.studioPunchBankHeld = 0;
        s.studioConfirm = false;
        return true;
    }
    if (event.button == Button::START) return false;
    if (pressed) {
        workspace.held |= mask;
        if (event.button == Button::DPAD_UP || event.button == Button::DPAD_DOWN) {
            const int step = event.button == Button::DPAD_DOWN ? 1 : -1;
            workspace.selected = std::clamp(workspace.selected + step, 0,
                                            static_cast<int>(kStudioMusicTabs.size()) - 1);
        }
        return true;
    }
    const bool wasHeld = (workspace.held & mask) != 0;
    workspace.held &= ~mask;
    if (wasHeld && (event.button == Button::A || event.button == Button::B ||
                    event.button == Button::SELECT)) {
        workspace.open = false;
        s.studioInputSuppress |= workspace.held;
        workspace.held = 0;
        if (event.button == Button::A) {
            const int target = kStudioMusicTabs[workspace.selected].id;
            if (target != s.studioTab) change_studio_tab(s, host, engine, target);
        }
    }
    return true;
}

void restore_appearance_context(AppState& s) {
 if(!s.studioAppearanceSettings)return;
 s.studioAppearanceSettings=false;
 s.studioTab=studio_is_music_tab(s.studioAppearanceReturnTab)?s.studioAppearanceReturnTab:0;
 s.studioRow=0;
}
const char* controls[]={"ACTIVE","MUTABLE EFFECT","WET / DRY","COLOR","TEXTURE","FEEDBACK","SATURATION","COMP THRESHOLD","COMP RATIO","ATTACK","RELEASE","STEREO WIDTH","OUTPUT GAIN","PEAK CEILING","HIGH PASS"};
int masterKey(int row){static const int keys[]={studio::ENABLE,studio::DRIVE,studio::THRESHOLD,studio::RATIO,studio::ATTACK,studio::RELEASE,studio::WIDTH,studio::OUTPUT,studio::CEILING,studio::HIGH_PASS};return keys[std::clamp(row-1,0,9)];}
int rows(int tab){return tab==0?7:tab==1?11:tab==2?15:tab==4?8:tab==5?20:tab==6?4:tab==7?source_sampler_row_count():2;}
void changed(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine){++s.projectVersion;auto& p=host.edit_project();for(int i=0;i<studio::COUNT;++i)engine.setStudioControl(i,p.studioMaster[i]);engine.studioRack.tempo.store(p.tempo);}
// FX and mastering have independent parameter groups. Loading one must not erase the other.
void apply_preset(bool effect,const std::array<int,studio::COUNT>& source,songcore::Project& project){
 apply_studio_preset(effect,source,project);
}
bool load_custom_preset(bool effect,const std::string& data,songcore::Project& project){
 std::array<int,studio::COUNT> checked{};
 if(!parse_studio_preset(effect,data,checked,false))return false;
 apply_preset(effect,checked,project);return true;
}
std::string display_control(int key,int v){
 char text[48];
 switch(key){
 case studio::ENABLE:return v?"ON":"OFF";
 case studio::THRESHOLD:std::snprintf(text,sizeof(text),"%.1f dB",-48+48*v/255.f);break;
 case studio::RATIO:std::snprintf(text,sizeof(text),"%.1f : 1",1+19*v/255.f);break;
 case studio::ATTACK:std::snprintf(text,sizeof(text),"%.1f ms",1+99*v/255.f);break;
 case studio::RELEASE:std::snprintf(text,sizeof(text),"%.0f ms",20+980*v/255.f);break;
 case studio::WIDTH:std::snprintf(text,sizeof(text),"%.0f %%",100*v/128.f);break;
 case studio::OUTPUT:std::snprintf(text,sizeof(text),"%+.1f dB",20*(v-128)/128.f);break;
 case studio::CEILING:std::snprintf(text,sizeof(text),"%.1f dBFS",-12+12*v/255.f);break;
 case studio::HIGH_PASS:if(!v)return "OFF";std::snprintf(text,sizeof(text),"%.0f Hz",10+190*v/255.f);break;
 default:std::snprintf(text,sizeof(text),"%.0f %%",100*v/255.f);break;
 }return text;
}
std::string value(const AppState& s,int row){const auto& p=*s.project;
 if(sample_granular_studio(s)){if(row==0)return std::to_string(s.currentInstrument);if(row==1)return "SAMPLE GRANULAR";if(row==2)return s.sampleGranular.presetName;return sample_granular_control_value(s,row-3);}
 if(s.studioTab==0){if(row==0)return std::to_string(s.currentInstrument);if(row==1)return studio::model_name(s.studioModel);if(row==2)return s.studioSynthCustom?"CUSTOM MACROS":studio::preset_name(studio::preset_code(s.studioModel,s.studioVariation));if(row>=3&&row<=5){const int values[]={s.studioHarmonics,s.studioTimbre,s.studioMorph};return display_control(-1,values[row-3]);}return s.studioConfirm?"A AGAIN TO REPLACE":"LOAD INTO INSTRUMENT";}
 if(s.studioTab==1){if(row==10)return "BROWSE CATEGORIES";if(row==0)return studio::effect_preset_name(s.studioEffectPreset);if(row==7)return std::to_string(s.studioFxUserSlot+1)+" / 16";if(row>=8)return row==8?"SAVE CUSTOM":"LOAD CUSTOM";int key=row==1?studio::FX:row==2?studio::FX_MIX:row==3?studio::COLOR:row==4?studio::TEXTURE:row==5?studio::FEEDBACK:studio::ENABLE;return key==studio::FX?studio::effect_name(p.studioMaster[key]):display_control(key,p.studioMaster[key]);}
 if(s.studioTab==2){if(row==14)return "BROWSE CATEGORIES";if(row==0)return studio::master_preset_name(s.studioMasterPreset);if(row==11)return std::to_string(s.studioMasterUserSlot+1)+" / 16";if(row>=12)return row==12?"SAVE CUSTOM":"LOAD CUSTOM";return display_control(masterKey(row),p.studioMaster[masterKey(row)]);}
 return row==0?studio::punch_name(s.studioPunch):"HOLD A TO PERFORM";
}
std::string label(const AppState& s,int row){
 if(sample_granular_studio(s)){if(row==0)return "TARGET SLOT";if(row==1)return "SAMPLE ENGINE";if(row==2)return "GRAIN PRESET";if(row==studio_synth_apply_row(s))return "ACTION";return sample_granular_control_label(s,row-3);}
 if(s.studioTab==0){const char* x[]={"TARGET SLOT","SYNTH MODEL","PRESET","HARMONICS","TIMBRE","MORPH","APPLY"};return x[row];}
 if(s.studioTab==1){const char* x[]={"FACTORY PRESET","MODULE","MIX","COLOR","TEXTURE","FEEDBACK","ACTIVE","USER SLOT","SAVE CUSTOM","LOAD CUSTOM","FX LIBRARY"};return x[row];}
 if(s.studioTab==2)return row==14?"MASTER LIBRARY":row==0?"FACTORY PRESET":row==11?"USER SLOT":row>=12?"MASTER PRESET":controls[masterKey(row)];
 return row==0?"PUNCH EFFECT":"PERFORMANCE";
}
}
std::string studio_value(const AppState& s,int row){return value(s,row);}
std::string studio_label(const AppState& s,int row){return label(s,row);}
int studio_rows(int tab){return rows(tab);}

bool draw_studio_workspace(Canvas& c, const AppState& s) {
    if (!s.studioWorkspace.open) return false;
    const auto& theme = s.theme;
    const auto accent = theme.rowCursor;
    c.reset_clip();
    c.panel_rect(25, 103, 590, 339, theme.background, with_alpha(accent, .72f), 7);
    c.draw_label("GO TO STUDIO SECTION", 43, 117, theme.textTitle, 18);
    for (int i = 0; i < static_cast<int>(kStudioMusicTabs.size()); ++i) {
        const auto& tab = kStudioMusicTabs[i];
        const bool selected = i == s.studioWorkspace.selected;
        const bool current = tab.id == s.studioTab;
        const int y = 145 + i * 33;
        c.panel_rect(42, y, 556, 30, selected ? theme.rowSelection : theme.rowEvery4th,
                     with_alpha(selected ? accent : theme.textParam, selected ? .95f : .18f), 4);
        if (selected) c.fill_rect(44, y + 6, 3, 22, accent);
        const auto titleInk = selected ? theme.rowEvery4th : theme.textTitle;
        const auto detailInk = selected ? theme.rowEvery4th : theme.textValue;
        c.draw_label(tab.label, 55, y + 8, titleInk, 14);
        c.draw_label(Canvas::clip_label(kWorkspaceDescriptions[i], 368), 163, y + 9, detailInk);
        if (current) c.draw_label("OPEN", 547, y + 9, detailInk);
    }
    c.draw_label("Up / Down  Choose       A  Open       B  Back", 43, 419, theme.textParam);
    return true;
}

void open_studio_appearance(AppState& s){
 if(!s.studioAppearanceSettings)s.studioAppearanceReturnTab=studio_is_music_tab(s.studioTab)?s.studioTab:0;
 s.studioAppearanceSettings=true;s.studioOpen=true;s.studioTab=6;s.studioRow=0;
 s.studioHelp=false;s.studioConfirm=false;s.studioAUsed=false;s.studioBUsed=false;
 s.studioTabNavHeld=0;s.studioMessage.clear();
}
// The Sources screen's YouTube opt-in is a saved setting; the sampler edits a mirror of it.
static bool studio_source_button(const ButtonEvent& e,AppState& s,songcore::SongcoreHost& host,AudioEngine& engine){
 s.sourceSampler.youtubeEnabled=s.settings.sourceYoutube;
 s.sourceSampler.currentSlot=s.currentInstrument;
 const bool handled=source_sampler_button(e,s.sourceSampler,host,engine);
 s.settings.sourceYoutube=s.sourceSampler.youtubeEnabled;
 return handled;
}

bool source_sampler_link_open(const AppState& s){return s.studioOpen&&s.studioTab==7&&s.sourceSampler.linkOpen;}

bool studio_button(const ButtonEvent& e,AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs){
 // The YouTube link field holds the controller: help, tabs and chords wait until A or B closes it.
 if(source_sampler_link_open(s))return studio_source_button(e,s,host,engine);
 // A replace question on Sources belongs to that screen: elsewhere, or after Studio closed, it is gone.
 if(!s.studioOpen||s.studioTab!=7||e.cancelled)source_sampler_disarm(s.sourceSampler);
 const uint16_t modalReturnBit=e.button==Button::A?1u:e.button==Button::B?2u:0u;
 const auto buttonMask=button_bit(e.button);
 // A release must clear the tab latch even if Help owns that key-up.
 const uint16_t directionBit=e.button==Button::DPAD_LEFT?1u:e.button==Button::DPAD_RIGHT?2u:0u;
 if(e.action==ButtonAction::RELEASED){
  s.studioTabNavHeld&=~directionBit;
  if(e.button==Button::B)s.studioTabNavHeld=0;
 }
 if(!s.studioOpen){
  if(s.studioHelp)s.studioInputSuppress|=s.studioHelpHeldButtons&button_bit(Button::SELECT);
  s.studioHelp=false;
  s.studioHelpHeldButtons&=~button_bit(Button::SELECT);
 }
 if(studio_help_captures(e,s)){
  if(s.studioInputSuppress&buttonMask){
   if(e.action==ButtonAction::RELEASED||e.cancelled){
    s.studioInputSuppress&=~buttonMask;
    s.studioHelpHeldButtons&=~buttonMask;
    s.sampleGranular.returnReleaseMask&=~modalReturnBit;
    if(e.button==Button::A)s.aHeld=false;
    if(e.button==Button::B)s.studioBUsed=false;
   }
   return true;
  }
  return studio_help_button(e,s,host,engine);
 }
 if(e.button!=Button::SELECT&&e.action==ButtonAction::PRESSED&&e.mods.select)
  s.studioHelpHeldButtons&=~button_bit(Button::SELECT);
 if(s.sampleGranular.returnReleaseMask&modalReturnBit){
  s.sampleGranular.returnReleaseMask&=~modalReturnBit;
  if(e.action==ButtonAction::RELEASED){if(e.button==Button::A)s.aHeld=false;return true;}
 }
 if((!sample_granular_preset_picker_open(s)||e.action==ButtonAction::RELEASED)
     &&sample_granular_audition_chord(e,s,host))return true;
 if(e.cancelled&&s.studioOpen&&!s.localSampleRange.isOpen&&!s.qwerty.isOpen){
  sample_granular_preset_picker_button(e,s,host);
  const auto mask=button_bit(e.button);
  s.studioInputSuppress&=~mask;
  s.studioWorkspace.held&=~mask;
  s.studioHelpHeldButtons&=~mask;
  s.studioSynthPicker.chooseArmed=false;
  s.studioShortcutRelease=false;
  s.studioTabNavHeld=0;
  s.studioConfirm=false;
  s.studioPunchBankHeld=0;
  s.sourceSampler.collectionArmed=false;
  studio_synth_stop_audition(s,host);
  if(e.button==Button::A){
   s.aHeld=false;s.studioAUsed=false;s.sourceSampler.aUsed=false;s.sourceSampler.quickClipArmed=false;
   engine.studioRack.punch.store(0);
   if(s.studioTab==5)drumlab_stop(s.drumLab,host,engine);
  }
  if(e.button==Button::B)s.studioBUsed=false;
  return true;
 }
 if(s.studioInputSuppress&buttonMask){
  if(e.action==ButtonAction::RELEASED){
   s.studioInputSuppress&=~buttonMask;
   if(e.button==Button::B)s.studioBUsed=false;
  }
  return true;
 }
 if(s.localSampleRange.isOpen)return false;
 // B together with A or R (the tracker's delete / mute reflexes) is a combo, not "leave Studio": its
 // release must not close the page, whichever of the two went down first, on every page.
 if(e.action==ButtonAction::PRESSED&&((e.button==Button::B&&(e.mods.a||e.mods.r))||
    ((e.button==Button::A||e.button==Button::R_SHIFT)&&e.mods.b)))
  s.studioBUsed=true;
 {
  const uint8_t bit=e.button==Button::L_SHIFT?1:e.button==Button::R_SHIFT?2:0;
  if(e.action==ButtonAction::PRESSED){
   const bool other=bit==1?e.mods.r:bit==2?e.mods.l:false;
   if(bit&&!e.mods.a&&!e.mods.b&&!e.mods.select&&!other)s.studioShoulderSolo|=bit;
   else s.studioShoulderSolo=0;   // a combo: no shoulder held so far acts alone on its release
  }else if(e.action==ButtonAction::RELEASED&&bit){
   const bool solo=(s.studioShoulderSolo&bit)!=0;
   s.studioShoulderSolo&=static_cast<uint8_t>(~bit);
   if(!solo&&s.studioOpen&&(s.studioTab==5||s.studioTab==7||s.studioTab==8))return true;
  }
 }
 if(is_live_resample_chord(e)){
  if(s.currentScreen==ScreenType::FILE_BROWSER&&s.fileBrowser.mode==BrowserMode::DELETE)return true;
  if(e.action==ButtonAction::PRESSED){
   s.studioPunchBankHeld=0;
   if(s.studioOpen&&s.studioTab==4)studio_capture_select_internal(s);
   live_resample_toggle(s,host,engine,fs);
  }
  return true;
 }
 if(s.qwerty.isOpen)return false;
 if(s.studioOpen&&s.studioTab==6&&s.scenePicker>=0){
  if(e.button==Button::START)return false;
  if(e.cancelled)return true;
  const bool applyOrBack=e.button==Button::A||e.button==Button::B;
  if((e.action==ButtonAction::RELEASED)==applyOrBack)scene_picker_button(e.button,s.scenePicker,s.theme);
  return true;
 }
 if(s.studioShortcutRelease&&e.button==Button::SELECT&&e.action==ButtonAction::RELEASED){s.studioShortcutRelease=false;return true;}
 if(e.button==Button::SELECT&&studio_help_button(e,s,host,engine))return true;
 if(studio_workspace_button(e,s,host,engine))return true;
 if(!s.studioOpen)return false;
 if(sample_granular_preset_picker_button(e,s,host))return true;
 if(studio_synth_picker_button(e,s,host,engine,fs))return true;
 // An external stop/panic may have released DSP without a physical A-up event.
 if(s.studioTab==3&&s.studioAUsed&&engine.studioRack.punch.load()==0)s.studioAUsed=false;
 if((e.button==Button::SELECT&&e.mods.r)||(e.button==Button::R_SHIFT&&e.mods.select)){
   s.studioPunchBankHeld=0;
   if(e.action==ButtonAction::PRESSED){studio_synth_stop_audition(s,host);drumlab_stop(s.drumLab,host,engine);s.drumLab.gridFocused=false;source_sampler_stop(s.sourceSampler);if(s.captureBackend&&s.capture.active())s.captureBackend->stop();s.studioShortcutRelease=true;s.studioOpen=false;s.studioHelp=false;source_sampler_disarm(s.sourceSampler);s.studioAUsed=false;engine.studioRack.punch.store(0);host.stop_preview(true);restore_appearance_context(s);}return true;
 }
 // The direct Punch chord also closes/switches a Studio page without a hidden action.
 if(!e.mods.r&&!e.mods.a&&!e.mods.b&&((e.button==Button::SELECT&&e.mods.l)||(e.button==Button::L_SHIFT&&e.mods.select))){
  s.studioPunchBankHeld=0;
  if(e.action==ButtonAction::PRESSED){
   studio_synth_stop_audition(s,host);drumlab_stop(s.drumLab,host,engine);s.drumLab.gridFocused=false;source_sampler_stop(s.sourceSampler);
   if(s.captureBackend&&s.capture.active())s.captureBackend->stop();
   engine.studioRack.punch.store(0);s.studioAUsed=false;s.studioHelp=false;s.studioShortcutRelease=true;
    const bool wasPunch=!s.studioAppearanceSettings&&s.studioTab==3;
    restore_appearance_context(s);
    if(wasPunch){s.studioOpen=false;host.stop_preview(true);}else{s.studioTab=3;s.studioRow=0;}
  }return true;
 }
 if(s.studioTab==0)studio_synth_sync_target(s);
 if(s.studioTab==0&&e.button==Button::L_SHIFT){
  if(s.resampleView.ownsSynthRoot&&s.resampleView.active)return true;
  if(sample_granular_studio(s)&&s.sampleGranular.recording.ownsRoot)return true;
  if(e.action==ButtonAction::RELEASED)studio_synth_audition(s,host,false);else if(!s.studioHelp&&!e.mods.r)studio_synth_audition(s,host,true);return true;
 }
 const bool grainEditDuringAudition=sample_granular_studio(s)&&s.studioAuditionHeld&&e.mods.a&&e.mods.select;
 if(s.studioTab==0&&e.action==ButtonAction::PRESSED&&e.mods.r&&!e.mods.b&&!grainEditDuringAudition&&(e.button==Button::DPAD_LEFT||e.button==Button::DPAD_RIGHT)){
  s.studioSynthPage=(s.studioSynthPage+studio_synth_page_count(s)+(e.button==Button::DPAD_RIGHT?1:-1))%studio_synth_page_count(s);s.studioRow=3;s.studioAUsed|=e.mods.a;s.studioConfirm=false;s.studioMessage.clear();return true;
 }
 if(s.studioTab==8 && e.button==Button::START)harmony_stop(s.harmony,host);
 if(e.button==Button::START){studio_synth_stop_audition(s,host);drumlab_stop(s.drumLab,host,engine);
  // On the press only: the search keyboard applies on the START press, and its release must not cancel that search.
  if(e.action==ButtonAction::PRESSED)source_sampler_stop(s.sourceSampler);engine.studioRack.punch.store(0);s.studioPunchBankHeld=0;if(s.studioTab==3)s.studioAUsed=false;s.studioConfirm=false;return false;}
 // The Sources browser after the Studio-wide chords: R + SELECT leaves, L + SELECT opens Punch and START
 // keeps the transport (source_sampler_stop above also closes the browser), as on every other page.
 if(s.studioTab==7&&s.sourceSampler.collectionOpen)return studio_source_button(e,s,host,engine);
 if(s.studioTab==8&&e.button!=Button::B&&!(is_dpad(e.button)&&e.mods.b))
  return harmony_button(e,s,host,engine);
 if(s.studioTab==7&&s.sourceSampler.listFocused&&!s.studioBUsed&&e.button==Button::B)
  return studio_source_button(e,s,host,engine);
 // B answers Drumlab's CLEAR PATTERN? (keep); its release must not also leave the Studio.
 if(s.studioTab==5&&s.drumLab.confirmClear&&e.button==Button::B&&e.action==ButtonAction::PRESSED){
  s.studioBUsed=true;return drumlab_button(e,s.drumLab,host,engine);
 }
 if(s.studioTab==5&&s.drumLab.gridFocused&&!s.studioBUsed&&
    (e.button==Button::B||e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN))
  return drumlab_button(e,s.drumLab,host,engine);
 if(s.studioTab==5&&e.button!=Button::B&&!(is_dpad(e.button)&&e.mods.b)){
  if(e.button==Button::A&&e.action==ButtonAction::RELEASED&&s.studioAUsed){s.studioAUsed=false;s.drumLab.aUsed=false;return true;}
  const auto serial=s.drumLab.applySerial;
  const bool handled=drumlab_button(e,s.drumLab,host,engine);
  if(s.drumLab.applySerial!=serial)++s.projectVersion;
  if(s.drumLab.openInstrument>=0){   // L+A: leave as B does, onto INSTRUMENT (the browser follows in the dispatcher)
   studio_synth_stop_audition(s,host);source_sampler_stop(s.sourceSampler);if(s.captureBackend&&s.capture.active())s.captureBackend->stop();
   s.studioOpen=false;s.studioConfirm=false;s.studioMessage.clear();engine.studioRack.punch.store(0);host.stop_preview();restore_appearance_context(s);
   s.currentInstrument=s.drumLab.openInstrument;s.currentScreen=ScreenType::INSTRUMENT;s.instrumentFromDrumlab=true;
   if(!s.drumLab.openBrowser)s.drumLab.openInstrument=-1;
  }
  return handled;
 }
 if(s.studioTab==7&&e.button!=Button::B&&!(is_dpad(e.button)&&e.mods.b)){
  if(e.button==Button::A&&e.action==ButtonAction::RELEASED&&s.studioAUsed){s.studioAUsed=false;s.sourceSampler.aUsed=false;return true;}
  const bool handled=studio_source_button(e,s,host,engine);
  source_sampler_bind_import_context(s);
  source_sampler_apply_import(s,host,engine,fs);
  return handled;
 }
 if(s.studioTab==3&&(e.button==Button::L_SHIFT||e.button==Button::R_SHIFT)){
  if(e.action==ButtonAction::PRESSED){
   s.studioPunchBankHeld=e.mods.l&&e.mods.r?0:buttonMask;
  }else if(s.studioPunchBankHeld==buttonMask){
   s.studioPunchBankHeld=0;
   engine.studioRack.punch.store(0);
   s.studioAUsed=false;
   const int step=e.button==Button::R_SHIFT?1:studio::kPunchBanks-1;
   const int bank=(studio::punch_bank(s.studioPunch)+step)%studio::kPunchBanks;
   s.studioPunch=studio::punch_id(bank,studio::punch_pad(s.studioPunch));
  }
  return true;
 }
 if(e.action==ButtonAction::RELEASED){
  if(e.button==Button::A){engine.studioRack.punch.store(0);if(s.studioAUsed){s.studioAUsed=false;return true;}
   if(s.studioTab==6&&s.studioRow==0){s.scenePicker=scene_picker_start(s.theme);return true;}
   if(sample_granular_open_preset_picker(s)){studio_synth_stop_audition(s,host);return true;}
   if(sample_granular_studio(s)&&s.studioRow==kGranularWaveRow)return sample_granular_wave_action(s);
   if(studio_synth_open_picker_for_row(s,host)){
    // Reopened on a factory tab: read it now (on the pack the app loads from today), so the first
    // frame lists it instead of waiting for a press.
    std::vector<SoundEntry> scratch;
    if(s.studioSynthPicker.mode==StudioSynthPickerMode::Preset)
     sound_tab_entries(s.soundLibrary,fs,s.studioSynthPicker.tab,s.settings.soundFavorites,s.settings.soundRecent,scratch);
    return true;
   }
   if(s.studioTab==4){studio_capture_action(s,host,engine,fs);return true;}
   if(s.studioTab==0&&s.studioRow==studio_synth_apply_row(s)){
    if(sample_granular_studio(s))sample_granular_action(s,host,engine,fs);
    else if(s.studioSynthCompare.focused)studio_synth_compare(s,host,engine);
    else studio_synth_apply(s,host,engine);
   }else if(s.studioTab==1&&s.studioRow==0){apply_preset(true,studio::effect_preset(s.studioEffectPreset),host.edit_project());changed(s,host,engine);
   }else if(s.studioTab==2&&s.studioRow==0){apply_preset(false,studio::master_preset(s.studioMasterPreset),host.edit_project());changed(s,host,engine);
   }else if((s.studioTab==1&&s.studioRow==10)||(s.studioTab==2&&s.studioRow==14)){
    s.pendingStudioPresetBrowser=true;
   }else if((s.studioTab==2&&s.studioRow>=12)||(s.studioTab==1&&s.studioRow>=8)){
    const auto folder=fs.make_app_folder("Studio");if(folder.empty()){s.studioMessage="CHOOSE A WRITABLE HOME FOLDER";return true;}
    const int slot=s.studioTab==1?s.studioFxUserSlot:s.studioMasterUserSlot;
    char suffix[24];std::snprintf(suffix,sizeof(suffix),"-%02d.json",slot+1);
    const auto path=folder+(s.studioTab==1?"/Custom-FX":"/Custom-Master")+(slot==0?".json":suffix);
    if((s.studioTab==2&&s.studioRow==13)||(s.studioTab==1&&s.studioRow==9)){std::string data;if(fs.read_file(path,data)){if(load_custom_preset(s.studioTab==1,data,host.edit_project())){changed(s,host,engine);s.studioMessage="CUSTOM PRESET LOADED";}else{const auto newer=songcore::newer_file_message(data,songcore::STUDIO_PRESET_FORMAT);s.studioMessage=newer.empty()?"INVALID PRESET - SETTINGS KEPT":newer;}}else s.studioMessage="NO CUSTOM PRESET FOUND";}
    else{songcore::json j;songcore::stamp_format(j,songcore::STUDIO_PRESET_FORMAT);j["kind"]=s.studioTab==1?"effects":"master";j["master"]=host.project().studioMaster;s.studioMessage=fs.write_file(path,j.dump(2))?"CUSTOM PRESET SAVED":"SAVE FAILED - CHECK FOLDER";}
   }
   }else if(e.button==Button::B){if(s.studioBUsed)s.studioBUsed=false;else{studio_synth_stop_audition(s,host);drumlab_stop(s.drumLab,host,engine);s.drumLab.gridFocused=false;source_sampler_stop(s.sourceSampler);if(s.captureBackend&&s.capture.active())s.captureBackend->stop();s.studioOpen=false;s.studioConfirm=false;s.studioMessage.clear();engine.studioRack.punch.store(0);host.stop_preview();restore_appearance_context(s);}}
  return true;
 }
 if(e.button==Button::B&&e.action==ButtonAction::PRESSED&&(e.mods.a||e.mods.r))return true;
 if(s.studioTab==3){
  if(is_dpad(e.button)&&!e.mods.b){
   s.studioPunchBankHeld=0;   // R + D-pad moved the pad: R's release is no bank change
   const int pad=studio::punch_pad(s.studioPunch),x=pad%4,y=pad/4;
   const int nx=std::clamp(x+(e.button==Button::DPAD_RIGHT?1:e.button==Button::DPAD_LEFT?-1:0),0,3);
   const int ny=std::clamp(y+(e.button==Button::DPAD_DOWN?1:e.button==Button::DPAD_UP?-1:0),0,1);
   s.studioPunch=studio::punch_id(studio::punch_bank(s.studioPunch),ny*4+nx);
   if(s.studioAUsed&&e.mods.a)engine.studioRack.punch.store(s.studioPunch);return true;
  }
 }
 if(e.button==Button::A){if(s.studioTab==3){engine.studioRack.tempo.store(host.project().tempo);engine.studioRack.punch.store(s.studioPunch);s.studioAUsed=true;}return true;}
 const int dir=e.button==Button::DPAD_RIGHT||e.button==Button::DPAD_UP?1:-1;
 if(is_dpad(e.button)){
  if(e.mods.b&&(e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN)){
   s.studioBUsed=true;
   if(s.studioAppearanceSettings)return true;
   if((s.studioTab==0&&!sample_granular_studio(s))||s.studioTab==3||(s.studioTab==4&&s.capture.internal_source())){
    if(e.button==Button::DPAD_UP){
     live_resample_use(s,host,engine,fs,s.studioTab==4?s.capture.targetInstrument:-1);
     if(!s.studioOpen)s.studioInputSuppress|=button_bit(Button::B)|buttonMask;
    }else live_resample_cycle_length(s);
   }
   return true;
  }
  if(e.mods.b){
   const bool repeated=(s.studioTabNavHeld&directionBit)!=0;s.studioTabNavHeld|=directionBit;
   s.studioBUsed=true;s.studioAUsed|=e.mods.a;
   if(s.studioAppearanceSettings||repeated||e.mods.a)return true;
   change_studio_tab(s,host,engine,studio_next_music_tab(s.studioTab,dir));return true;
  }
  // XY pad page: hold L (the audition) and the D-pad moves the dot instead of the cursor.
  if(s.studioTab==0&&e.mods.l&&!e.mods.a&&!e.mods.r&&!e.mods.b&&studio_synth_xy_page(s)){studio_synth_xy_move(s,host,engine,e.button);return true;}
  if(!e.mods.a&&s.studioTab==0){studio_synth_navigate(s,e.button);return true;}
  if(!e.mods.a&&s.studioTab==4&&(e.button==Button::DPAD_LEFT||e.button==Button::DPAD_RIGHT)){studio_capture_edit(s,e.button==Button::DPAD_RIGHT?1:-1);return true;}
  if(!e.mods.a){if(e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN){s.studioConfirm=false;s.studioMessage.clear();const int delta=e.button==Button::DPAD_UP?-1:1;s.studioRow=std::clamp(s.studioRow+delta,0,(s.studioTab==0?studio_synth_apply_row(s)+1:rows(s.studioTab))-1);if(s.studioTab==0&&s.studioRow>=3+studio_synth_control_count(s)&&s.studioRow<studio_synth_apply_row(s))s.studioRow=delta>0?studio_synth_apply_row(s):2+studio_synth_control_count(s);}return true;}
  s.studioAUsed=true;s.studioConfirm=false;auto& p=host.edit_project();
  // A + Up / Down is the coarse step on 0…255 values (±16), as on the tracker screens.
  const bool coarse=e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN;
  if(s.studioTab==0)studio_synth_edit(s,host,engine,s.studioRow,dir,e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN,&fs);
  else if(s.studioTab==1){if(s.studioRow==0)s.studioEffectPreset=std::clamp(s.studioEffectPreset+dir,0,studio::effect_preset_count()-1);else if(s.studioRow==7)s.studioFxUserSlot=std::clamp(s.studioFxUserSlot+dir,0,15);else if(s.studioRow<7){int key=s.studioRow==1?studio::FX:s.studioRow==2?studio::FX_MIX:s.studioRow==3?studio::COLOR:s.studioRow==4?studio::TEXTURE:s.studioRow==5?studio::FEEDBACK:studio::ENABLE;const int range=key==studio::FX?19:key==studio::ENABLE?1:255;p.studioMaster[key]=std::clamp(p.studioMaster[key]+dir*(range==255&&coarse?16:1),0,range);changed(s,host,engine);}}
  else if(s.studioTab==2){if(s.studioRow==0)s.studioMasterPreset=std::clamp(s.studioMasterPreset+dir,0,studio::master_preset_count()-1);else if(s.studioRow==11)s.studioMasterUserSlot=std::clamp(s.studioMasterUserSlot+dir,0,15);else if(s.studioRow<11){int key=masterKey(s.studioRow);const int range=key==studio::ENABLE?1:255;p.studioMaster[key]=std::clamp(p.studioMaster[key]+dir*(range==255&&coarse?16:1),0,range);changed(s,host,engine);}}
  
  else if(s.studioTab==4){studio_capture_edit(s,dir);
  }else if(s.studioTab==6){
   if(s.studioRow==0)theme_cycle_builtin(s.theme,dir);
   if(s.studioRow==1)s.theme.surfaceStyle=(s.theme.surfaceStyle+dir+kSurfaceStyleCount)%kSurfaceStyleCount;
   if(s.studioRow==2){
    const int font=(valid_demo_font_style(theme_font(s.theme,s.settings.fontStyle))+dir+kDemoFontStyleCount)%kDemoFontStyleCount;
    if(s.theme.sceneChrome)s.theme.fontOverride=font;
    else {s.settings.fontStyle=font;s.theme.fontOverride=-1;}
   }
   // A two-item list, ON then OFF: Right sets OFF, Left sets ON. Direction-bound, so a held key's
   // auto-repeat cannot flip it back (a toggle per PRESSED did).
   if(s.studioRow==3)s.settings.sceneMotion=dir<0;
  }
 }
 return true;
}
}
