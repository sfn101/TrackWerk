#include "ui/studio.h"
#include "ui/studio_synth_picker.h"
#include "ui/fm_library.h"
#include "ui/filesystem.h"
#include "ui/sample_granular_recording.h"
#include "ui/sound_browser.h"
#include "songcore/project_io.h"
#include <cstring>
#include "studio/synth_params.h"
#include "songcore/note_tables.h"
#include "songcore/engine_setup.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace pt::ui {
namespace {
int extra_pages(const AppState& s){return (studio::synth_param_count(s.studioModel)+2)/3;}
// 0 oscillator, 1 engine pages, 2 filter, 3 amplifier, 4 ADSR, 5 XY pad (the first two macros on one surface).
int kind(const AppState& s){const int n=extra_pages(s);return s.studioSynthPage==0?0:s.studioSynthPage<=n?1:s.studioSynthPage-n+1;}
int extra_index(const AppState& s,int i){return (s.studioSynthPage-1)*3+i;}
std::string patch_uri(const AppState& s){return s.studioSynthCustom?studio::custom_uri(s.studioModel,s.studioHarmonics,s.studioTimbre,s.studioMorph):studio::uri(studio::preset_code(s.studioModel,s.studioVariation));}
void macros(AppState& s){auto p=studio::preset_macros(studio::preset_code(s.studioModel,s.studioVariation));s.studioHarmonics=p[0];s.studioTimbre=p[1];s.studioMorph=p[2];s.studioSynthCustom=false;}
songcore::Instrument new_synth_draft(int instrument){
 songcore::Instrument draft(instrument);draft.volume=170;draft.filterCut=255;draft.loopMode="fwd";
 auto& m=draft.modSlots[0];m.type=songcore::ModType::ADSR;m.dest=songcore::ModDest::VOLUME;m.attack=3;m.decay=60;m.sustain=180;m.release=45;
 return draft;
}
const songcore::ModSlot* envelope(const songcore::Instrument& ins){
 for(const auto& m:ins.modSlots)if(m.dest==songcore::ModDest::VOLUME&&(m.type==songcore::ModType::ADSR||m.type==songcore::ModType::TRIG||m.type==songcore::ModType::AHD||m.type==songcore::ModType::DRUM))return &m;
 return nullptr;
}
songcore::ModSlot* edit_envelope(songcore::Instrument& ins){
 if(auto* m=const_cast<songcore::ModSlot*>(envelope(ins))){m->type=songcore::ModType::ADSR;return m;}
 for(auto& m:ins.modSlots)if(m.type==songcore::ModType::NONE){m=songcore::ModSlot{};m.type=songcore::ModType::ADSR;m.dest=songcore::ModDest::VOLUME;return &m;}
 return nullptr;
}
// FROGGY's lyric templates: the .txt files of Presets/Lyrics, by name. The first look at an empty folder writes a few
// starter lines (public domain or our own); nothing is ever overwritten. A file's first 120 characters count.
struct LyricTemplate{std::string name,text;};
std::vector<LyricTemplate> lyric_templates(FileSystem* fs){
 std::vector<LyricTemplate> out;
 if(!fs)return out;
 const std::string dir=fs->make_app_folder("Presets/Lyrics");
 if(dir.empty())return out;
 auto entries=fs->list_files(dir);
 const auto is_txt=[](const FileInfo& f){
  if(f.isDirectory||f.isAction||f.isRoot)return false;
  std::string e=f.extension;for(auto& ch:e)ch=char(std::tolower(static_cast<unsigned char>(ch)));
  return e=="txt";};
 bool any=false;for(const auto& e:entries)if(is_txt(e))any=true;
 if(!any){
  static const char* starters[][2]={{"Pond Song","la la ribbit ribbit quack la la"},{"Hello Frog","hello hello i am a frog"},
   {"Counting","one two three four five six seven eight"},{"Do Re Mi","do re mi fa so la ti do"},
   {"Row Boat","row row row your boat gently down the stream"},{"Rain","rain rain go away come again another day"}};
  for(const auto& st:starters)fs->write_file(dir+"/"+st[0]+".txt",std::string(st[1])+"\n");
  entries=fs->list_files(dir);
 }
 for(const auto& e:entries){
  if(!is_txt(e)||e.size>4096)continue;
  std::string content;if(!fs->read_file(e.path,content))continue;
  for(auto& ch:content)if(ch=='\n'||ch=='\r'||ch=='\t')ch=' ';
  while(!content.empty()&&content.back()==' ')content.pop_back();
  size_t first=content.find_first_not_of(' ');if(first==std::string::npos)continue;
  content=studio::sing::utf8_cut(content.substr(first),size_t(studio::sing::kMaxTextChars));
  out.push_back({e.name_without_extension(),content});
 }
 std::sort(out.begin(),out.end(),[](const LyricTemplate& a,const LyricTemplate& b){return a.name<b.name;});
 return out;
}
// FM SYSEX: the bank and voice the draft plays — its parameters, else its named sound's.
std::array<int,2> fm_selection(const AppState& s){
 const auto& p=s.studioDraft.studioSynthParams;
 if(p[studio::kFmBankParam]<0&&p[studio::kFmVoiceParam]<0&&!s.studioSynthCustom)return studio::fm_preset_voice(s.studioVariation);
 return {studio::fm_bank_index(p[studio::kFmBankParam]),studio::fm_voice_index(p[studio::kFmVoiceParam])};
}
// The name stored in the voice the draft plays.
std::string fm_voice_label(const AppState& s){
 studio::fm_sysex::PackedVoice v{};
 const auto sel=fm_selection(s);
 if(s.studioDraft.fmVoice)v=*s.studioDraft.fmVoice;
 else studio::fm_builtin_voice(sel[0]<studio::kFmFactoryBanks?sel[0]:0,sel[1],v.data());
 return std::to_string(sel[1]+1)+" "+studio::fm_sysex::voice_name(v);
}
std::string fm_bank_label(const AppState& s){
 const int bank=fm_selection(s)[0];
 if(bank>=studio::kFmFactoryBanks&&!s.fmLibrary.scanned)return "File bank "+std::to_string(bank-studio::kFmFactoryBanks+1);
 return fm_library_bank_label(s.fmLibrary,bank);
}
// One step through banks or voices. A file bank's voice is copied into the draft (the song keeps it);
// a built-in one needs no copy. Rescans the SysEx folder on every bank step, so new files appear.
void fm_step(AppState& s,FileSystem* fs,int param,int direction,bool coarse){
 auto& lib=s.fmLibrary;
 if(fs&&(!lib.scanned||param==studio::kFmBankParam))fm_library_scan(lib,*fs);
 auto sel=fm_selection(s);
 const int banks=fm_library_bank_count(lib);
 int bank=std::clamp(sel[0],0,banks-1),voice=sel[1];
 if(param==studio::kFmBankParam)bank=std::clamp(bank+direction*(coarse?4:1),0,banks-1);
 const int voices=std::max(1,fm_library_voice_count(lib,bank));
 voice=param==studio::kFmVoiceParam?std::clamp(voice+direction*(coarse?8:1),0,voices-1):std::min(voice,voices-1);
 auto& p=s.studioDraft.studioSynthParams;
 p[studio::kFmBankParam]=studio::fm_index_value(bank);
 p[studio::kFmVoiceParam]=studio::fm_index_value(voice);
 studio::fm_sysex::PackedVoice v{};
 if(bank<studio::kFmFactoryBanks||!fm_library_voice(lib,bank,voice,v))s.studioDraft.fmVoice.reset();
 else s.studioDraft.fmVoice=v;
 s.studioMessage=fm_library_bank_label(lib,bank)+": "+fm_voice_label(s);
 if(banks==studio::kFmFactoryBanks&&param==studio::kFmBankParam&&direction>0)
  s.studioMessage="No .syx banks found. Put DX7 bank files into the SysEx folder.";
 else if(lib.skipped>0&&param==studio::kFmBankParam)
  s.studioMessage+=" ("+std::to_string(lib.skipped)+" unreadable file"+(lib.skipped==1?")":"s)");
}
std::string percent(int v){return std::to_string(static_cast<int>(std::lround(v*100./255)))+" %";}
int filter_index(const songcore::Instrument& ins){return ins.filterType=="lp"?1:ins.filterType=="hp"?2:ins.filterType=="bp"?3:0;}
bool publish(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,bool common,const songcore::Instrument* oldDraft=nullptr){
 if(studio_synth_live(s)){
  const auto before=host.project().instruments[s.currentInstrument];
  const int oldCode=studio::uri_code(before.sampleFilePath->c_str());
  const auto oldAutoName=(oldCode&studio::kCustomFlag)?std::string(studio::model_name(studio::preset_model(oldCode)))+" / CUSTOM":studio::preset_name(oldCode);
  const bool autoName=before.name==oldAutoName||songcore::instrument_has_default_name(before);
  const auto uri=patch_uri(s);
  if(!host.update_studio_synth(s.currentInstrument,uri,s.studioDraft.studioSynthParams,&s.studioDraft.fmVoice,&s.studioDraft.lyric)){
   s.studioLiveBound=false;s.studioBoundUri.clear();
   s.studioMessage="Synth source unavailable. Apply the draft to reload the slot.";return false;
  }
  s.studioBoundUri=uri;
  auto& actual=host.edit_project().instruments[s.currentInstrument];
  if(common){actual.volume=s.studioDraft.volume;actual.pan=s.studioDraft.pan;actual.detune=s.studioDraft.detune;
   actual.filterType=s.studioDraft.filterType;actual.filterCut=s.studioDraft.filterCut;actual.filterRes=s.studioDraft.filterRes;
   actual.drive=s.studioDraft.drive;actual.modSlots=s.studioDraft.modSlots;host.push_instrument(s.currentInstrument);host.update_studio_voice(before,actual);}
  if(autoName)actual.name=s.studioSynthCustom?std::string(studio::model_name(s.studioModel))+" / CUSTOM":studio::preset_name(studio::preset_code(s.studioModel,s.studioVariation));
  s.studioDraft.name=actual.name;s.studioDraft.sampleFilePath=uri;++s.projectVersion;
  s.studioMessage="Live: changes are heard immediately";
 }else s.studioMessage="Draft ready. Apply to use this sound in the song.";
 if(s.studioAuditionHeld){
  if(studio::synth_needs_strike(s.studioModel)){
   if(studio_synth_live(s))host.preview_instrument(s.currentInstrument);
   else host.preview_studio_synth(s.studioDraft,patch_uri(s),true);
  }else if(!studio_synth_live(s)){
   host.preview_studio_synth(s.studioDraft,patch_uri(s),false);
   if(common){engine.refreshSoundingInstrument(songcore::PREVIEW_SAMPLE_SLOT);if(oldDraft)host.update_studio_voice(*oldDraft,s.studioDraft,songcore::PREVIEW_SAMPLE_SLOT);}
  }
 }
 return true;
}

StudioSynthSound current_sound(const AppState& s){
 return {s.studioDraft,s.studioModel,s.studioVariation,s.studioHarmonics,s.studioTimbre,s.studioMorph,s.studioSynthCustom};
}

void select_sound(AppState& s,const StudioSynthSound& sound){
 s.studioDraft=sound.draft;
 s.studioModel=sound.model;
 s.studioVariation=sound.variation;
 s.studioHarmonics=sound.harmonics;
 s.studioTimbre=sound.timbre;
 s.studioMorph=sound.morph;
 s.studioSynthCustom=sound.custom;
}

void store_synth_reference(AppState& s){
 s.studioSynthCompare.alternate=current_sound(s);
 s.studioSynthCompare.listeningToA=false;
 s.studioMessage="A stored. Edit B, then press A here to compare.";
}

void select_model(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,int model){
 const bool wasGranular=sample_granular_studio(s);
 if(model==(wasGranular?kSampleGranularModel:s.studioModel))return;
 if(model==kSampleGranularModel){
  studio_synth_stop_audition(s,host);
  s.studioSynthCompare={};
  sample_granular_select(s,host,true);
  if(studio_synth_live(s))s.studioRow=1;
  return;
 }
 if(model<0||model>=studio::model_count())return;
 if(wasGranular){
  studio_synth_stop_audition(s,host);
  s.sampleGranular.open=false;
  s.studioDraft=new_synth_draft(s.currentInstrument);
  s.studioSynthPage=0;
 }
 s.studioModel=model;
 s.studioDraft.studioSynthParams.fill(-1);
 s.studioDraft.fmVoice.reset();
 macros(s);
 s.studioSynthPage=std::min(s.studioSynthPage,studio_synth_page_count(s)-1);
 publish(s,host,engine,false);
}

void select_preset(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,int variation){
 if(variation==s.studioVariation&&!s.studioSynthCustom)return;
 s.studioVariation=variation;
 s.studioDraft.studioSynthParams.fill(-1);
 s.studioDraft.fmVoice.reset();
 macros(s);
 publish(s,host,engine,false);
}

void preview_picker_choice(AppState& s,songcore::SongcoreHost& host){
 auto& picker=s.studioSynthPicker;
 const bool modelPicker=picker.mode==StudioSynthPickerMode::Model;
 const int model=modelPicker?studio_synth_group_model(studio_synth_model_groups()[picker.group],picker.item):s.studioModel;
 if(model==kSampleGranularModel){
  if(picker.previewSounding)host.stop_preview(true);
  picker.previewSounding=false;
  return;
 }
 const bool fromGranular=sample_granular_studio(s);
 auto draft=fromGranular?new_synth_draft(s.currentInstrument):s.studioDraft;
 const bool current=!fromGranular&&model==s.studioModel&&
  (modelPicker||(!s.studioSynthCustom&&picker.item==s.studioVariation));
 if(!current)draft.studioSynthParams.fill(-1);
 draft.volume=std::clamp(static_cast<int>(std::lround(draft.volume*s.settings.soundPreviewLevel/100.0)),0,255);   // preview only
 const int variation=modelPicker?s.studioVariation:picker.item;
 const auto uri=current?patch_uri(s):studio::uri(studio::preset_code(model,variation));
 const bool wasSounding=picker.previewSounding;
 picker.previewSounding=host.preview_studio_synth(draft,uri,true);
 if(wasSounding&&!picker.previewSounding)host.stop_preview(true);
}

// ── The sound browser (PRESET mode's factory tabs) ──────────────────────────────────────────────

std::vector<SoundEntry>& browser_scratch(){static std::vector<SoundEntry> v;return v;}
const std::vector<SoundEntry>& browser_entries(AppState& s,FileSystem& fs){
 return sound_tab_entries(s.soundLibrary,fs,s.studioSynthPicker.tab,s.settings.soundFavorites,s.settings.soundRecent,browser_scratch());
}
const SoundEntry* browser_entry(AppState& s,FileSystem& fs){
 auto& picker=s.studioSynthPicker;
 const auto& v=browser_entries(s,fs);
 if(v.empty())return nullptr;
 auto& row=picker.rows[static_cast<size_t>(picker.tab)];
 row=std::clamp(row,0,static_cast<int>(v.size())-1);
 return &v[static_cast<size_t>(row)];
}
std::string entry_name(const SoundEntry& e){return e.variant.empty()?e.family:e.family+" "+e.variant;}
// Recording or resampling hears the preview lane: a browse preview must not end up in a take.
const char* preview_blocked(const AppState& s){
 if(s.capture.active())return "STOP THE RECORDING TO PREVIEW";
 if(s.resampleView.active||s.resampleView.saving)return "FINISH THE RESAMPLE TO PREVIEW";
 if(granular_recording_busy(s.sampleGranular.recording))return "FINISH THE GRANULAR TAKE TO PREVIEW";
 return nullptr;
}
// A factory .pti: parsed and checked like INSTRUMENT > LOAD would, a Studio synth only.
bool read_sound(FileSystem& fs,const std::string& path,songcore::InstrumentPreset& out){
 // The same checks as INSTRUMENT > LOAD (songcore::parse_instrument_preset_checked), then: a Studio
 // synth only, the browser's one kind of sound.
 std::string blob;
 if(!fs.read_file(path,blob)||!songcore::parse_instrument_preset_checked(blob,out))return false;
 return !out.instrument.granularEnabled&&out.instrument.sampleFilePath&&
        out.instrument.sampleFilePath->rfind("/@mutable/",0)==0&&
        studio::uri_code(out.instrument.sampleFilePath->c_str())>=0;
}
void preview_sound(AppState& s,songcore::SongcoreHost& host,FileSystem& fs){
 auto& picker=s.studioSynthPicker;
 const auto* e=browser_entry(s,fs);
 const bool wasSounding=picker.previewSounding;
 if(!e){if(wasSounding)host.stop_preview(true);picker.previewSounding=false;return;}
 if(const char* why=preview_blocked(s)){if(wasSounding)host.stop_preview(true);picker.previewSounding=false;s.studioMessage=why;return;}
 songcore::InstrumentPreset ip;
 if(!read_sound(fs,sound_pack_root(fs)+"/"+e->key,ip)){
  if(wasSounding)host.stop_preview(true);picker.previewSounding=false;
  s.studioMessage="Cannot read "+entry_name(*e)+": missing or damaged";return;
 }
 auto draft=ip.instrument;
 // The level scales the sound's own volume, which tops out at 255: a loud preset reaches its ceiling
 // below 200 %. That ceiling is kept and shown, not hidden.
 picker.previewCeiling=draft.volume>0?std::min(kSoundPreviewLevelMax,255*100/draft.volume):kSoundPreviewLevelMax;
 draft.volume=std::clamp(static_cast<int>(std::lround(draft.volume*s.settings.soundPreviewLevel/100.0)),0,255);
 picker.previewSounding=host.preview_studio_synth(draft,*draft.sampleFilePath,true);
 if(wasSounding&&!picker.previewSounding)host.stop_preview(true);
}
bool slot_has_way_back(const songcore::Instrument& ins){
 if(songcore::instrument_is_free(ins))return true;
 return ins.instrumentType==songcore::InstrumentType::SAMPLER&&!ins.granularEnabled&&
        ins.sampleFilePath&&ins.sampleFilePath->rfind("/@mutable/",0)==0;
}
// The Studio's synth page follows the slot's new sound; the browser stays as it was.
void follow_slot(AppState& s){
 const auto picker=s.studioSynthPicker;
 open_studio_synth(s,s.project->instruments[static_cast<size_t>(s.currentInstrument)]);
 s.studioSynthPicker=picker;
 s.studioRow=2;
}
}

const std::array<StudioSynthModelGroup,12>& studio_synth_model_groups(){
 constexpr int gravityStart=studio::kOctaveOrbitModel+1;
 constexpr int spectralStart=studio::kSpectralFirstModel;
 // The tabs are for finding a sound, not for remembering where it came from: a sung voice and a croaking frog sit
 // together under Voices, the bass engines under Low End. The model IDs never move; a tab lists them.
 static constexpr int voices[]={studio::kFormantChoirModel,studio::kFrogVoiceModel,studio::kSingFrogModel};
 static constexpr int lowEnd[]={studio::kDubSirenModel,studio::kAcidBoxModel,studio::kReeseBassModel};
 static const std::array<StudioSynthModelGroup,12> groups{{
  {"Sample Granular",kSampleGranularModel,1},
  {"Plaits: Macro",0,24},
  {"Braids: Digital",24,48},
  {"Rings: Strings",72,6},
  {"Elements: Modal",78,16},
  {"Grain Oscillators",94,8},
  {"Studio DSP",102,std::min(studio::model_count(),gravityStart)-102},
  {"Gravity",gravityStart,std::max(0,std::min(studio::model_count(),spectralStart)-gravityStart)},
  {"Spectral",spectralStart,std::max(0,std::min(studio::model_count(),studio::kVoiceFirstModel)-spectralStart)},
  {"Voices and Frogs",studio::kFormantChoirModel,3,voices},
  {"Vintage Chips",studio::kSidChipModel,3},
  {"Low End and Sirens",studio::kDubSirenModel,3,lowEnd},
 }};
 return groups;
}

int studio_synth_model_group_count(){
 const auto& groups=studio_synth_model_groups();
 return static_cast<int>(groups.size())-(groups.back().count==0?1:0);   // an empty last tab stays hidden
}

int studio_synth_model_group(int model){
 const auto& groups=studio_synth_model_groups();
 for(std::size_t i=0;i<groups.size();++i){
  const auto& g=groups[i];
  if(studio_synth_group_index(g,model)>=0)return static_cast<int>(i);
 }
 return 1;
}

std::string studio_synth_preset_label(int model,int variation){
 auto name=studio::preset_name(studio::preset_code(model,variation));
 const auto separator=name.find(" / ");
 if(separator!=std::string::npos)name.erase(0,separator+3);
 return name;
}

bool studio_synth_picker_open(const AppState& s){
 return s.studioOpen&&s.studioTab==0&&s.studioSynthPicker.mode!=StudioSynthPickerMode::Closed;
}

void studio_synth_close_picker(AppState& s){s.studioSynthPicker={};}

bool studio_synth_open_picker_for_row(AppState& s,songcore::SongcoreHost& host){
 if(!s.studioOpen||s.studioTab!=0||!s.project)return false;
 const bool granular=sample_granular_studio(s);
 if(s.studioRow!=1&&(s.studioRow!=2||granular))return false;
 studio_synth_stop_audition(s,host);
 s.studioConfirm=false;
 s.studioMessage.clear();
 auto& picker=s.studioSynthPicker;
 picker={};
 picker.instrument=s.currentInstrument;
 if(s.studioRow==1){
  picker.mode=StudioSynthPickerMode::Model;
  const int current=granular?kSampleGranularModel:s.studioModel;
  picker.group=studio_synth_model_group(current);
  const auto& group=studio_synth_model_groups()[picker.group];
  picker.item=std::clamp(studio_synth_group_index(group,current),0,group.count-1);
 }else{
  picker.mode=StudioSynthPickerMode::Preset;
  picker.item=std::clamp(s.studioVariation,0,studio::variation_count()-1);
  picker.tab=s.soundLastTab;     // the browser reopens where it was left
  picker.rows=s.soundLastRows;
 }
 return true;
}

namespace {
void close_browser(AppState& s){
 const auto& picker=s.studioSynthPicker;
 if(picker.mode==StudioSynthPickerMode::Preset){s.soundLastTab=picker.tab;s.soundLastRows=picker.rows;}
 studio_synth_close_picker(s);
}

// A on a factory sound: into the marked slot, now. A sample, SoundFont or granular slot asks first
// (its sound cannot come back); an empty or synth slot is kept as it was, for B.
void use_sound(AppState& s,songcore::SongcoreHost& host,FileSystem& fs){
 auto& picker=s.studioSynthPicker;
 const auto* e=browser_entry(s,fs);
 if(!e)return;
 const SoundEntry entry=*e;   // the FAV / RECENT list may change below
 const int slot=s.currentInstrument;
 if(entry.key==picker.usedKey){
  studio_synth_stop_audition(s,host);
  s.studioMessage="Kept: "+entry_name(entry)+" on slot "+songcore::hex2(slot);
  close_browser(s);
  return;
 }
 const auto& before=s.project->instruments[static_cast<size_t>(slot)];
 const bool firstUse=picker.usedKey.empty();
 if(firstUse&&!slot_has_way_back(before)&&!picker.confirmReplace){
  picker.confirmReplace=true;
  s.studioMessage="Replaces the sound in slot "+songcore::hex2(slot)+", no undo. A: Use  B: Keep";
  return;
 }
 studio_synth_stop_audition(s,host);
 songcore::InstrumentPreset check;
 const std::string path=sound_pack_root(fs)+"/"+entry.key;
 if(!read_sound(fs,path,check)){s.studioMessage="Cannot use "+entry_name(entry)+": missing or damaged - slot kept";picker.confirmReplace=false;return;}
 std::optional<StudioSynthPickerState::Original> original;
 if(firstUse&&slot_has_way_back(before)){
  original=StudioSynthPickerState::Original{before,std::nullopt,s.projectVersion,s.settings.soundRecent};
  if(slot<static_cast<int>(s.project->tables.size()))original->table=s.project->tables[static_cast<size_t>(slot)];
 }
 if(!host.load_instrument_preset(slot,path)){
  s.studioMessage="Could not load "+entry_name(entry)+" - slot kept";
  picker.confirmReplace=false;
  return;
 }
 if(original)picker.original=std::move(original);
 picker.usedKey=entry.key;
 picker.confirmReplace=false;
 ++s.projectVersion;
 sound_push_recent(s.settings.soundRecent,entry.key);   // a use, not a preview, makes it recent
 follow_slot(s);
 s.studioMessage="Slot "+songcore::hex2(slot)+": "+entry_name(entry)+(picker.original?"   A: Keep  B: Undo":"   A: Keep");
 // The slot's new sound at its root, until the next button: owned by the picker like a held preview,
 // so B, closing, help and focus loss end it (studio_synth_stop_audition).
 if(!host.is_playing()){host.preview_instrument(slot);s.studioSynthPicker.previewSounding=true;}
}

// B: back out. A pending question is answered no; after a use the slot goes back to how it was,
// recent list included, and the project is as clean as before.
void cancel_browser(AppState& s,songcore::SongcoreHost& host){
 auto& picker=s.studioSynthPicker;
 studio_synth_stop_audition(s,host);
 if(picker.confirmReplace){picker.confirmReplace=false;s.studioMessage.clear();return;}
 if(!picker.usedKey.empty()&&picker.original){
  const auto original=*picker.original;
  const int slot=s.currentInstrument;
  if(host.restore_instrument(slot,original.instrument,original.table)){
   s.projectVersion=original.projectVersion;
   s.settings.soundRecent=original.recent;
   follow_slot(s);
   s.studioMessage="Slot "+songcore::hex2(slot)+" is back as it was";
  }else s.studioMessage="Could not restore slot "+songcore::hex2(slot)+" - its source is missing";
 }else if(!picker.usedKey.empty())s.studioMessage="Kept: the new sound stays on slot "+songcore::hex2(s.currentInstrument);
 close_browser(s);
}

void star_sound(AppState& s,FileSystem& fs){
 auto& picker=s.studioSynthPicker;
 if(picker.tab==SoundTab::MODEL){s.studioMessage="Stars are for the factory sounds: Right to their tabs";return;}
 const auto* e=browser_entry(s,fs);
 if(!e)return;
 const SoundEntry entry=*e;
 bool starred=false;
 if(!sound_toggle_favorite(s.settings.soundFavorites,entry.key,starred)){
  s.studioMessage="Favorites are full ("+std::to_string(kSoundFavoritesMax)+"): remove a star first";return;
 }
 s.studioMessage=(starred?"Starred: ":"Star removed: ")+entry_name(entry);
}
}  // namespace

bool studio_synth_picker_button(const ButtonEvent& e,AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,FileSystem& fs){
 if(!studio_synth_picker_open(s))return false;
 auto& picker=s.studioSynthPicker;
 const bool browser=picker.mode==StudioSynthPickerMode::Preset;
 if(browser)sound_library_sync(s.soundLibrary,fs);   // the home folder may have moved since the last press
 const bool factory=browser&&picker.tab!=SoundTab::MODEL;
 if(picker.instrument!=s.currentInstrument){studio_synth_stop_audition(s,host);close_browser(s);return true;}
 if(e.button==Button::START){picker.chooseArmed=false;studio_synth_stop_audition(s,host);return false;}
 // A preview nobody holds (the root note after a use) ends at the next button, L excepted.
 if(e.action==ButtonAction::PRESSED&&picker.previewSounding&&!picker.previewHeld&&e.button!=Button::L_SHIFT)
  studio_synth_stop_audition(s,host);
 auto preview=[&]{if(browser&&picker.tab!=SoundTab::MODEL)preview_sound(s,host,fs);else preview_picker_choice(s,host);};
 if(e.button==Button::L_SHIFT){
  if(e.action==ButtonAction::RELEASED){studio_synth_stop_audition(s,host);return true;}
  if(!picker.previewHeld&&!e.mods.a&&!e.mods.b&&!e.mods.select&&!e.mods.r){
   picker.previewHeld=true;
   picker.chooseArmed=false;
   preview();
  }
  return true;
 }
 if(e.button==Button::B){
  picker.chooseArmed=false;
  if(e.action==ButtonAction::RELEASED){
   if(browser)cancel_browser(s,host);
   else{studio_synth_stop_audition(s,host);studio_synth_close_picker(s);}
  }
  return true;
 }
 if(e.button==Button::A){
  if(e.action==ButtonAction::PRESSED){
   picker.chooseArmed=!e.mods.b&&!e.mods.select;
  }else if(picker.chooseArmed){
   picker.chooseArmed=false;
   if(factory){use_sound(s,host,fs);return true;}
   const auto mode=picker.mode;
   const int chosen=mode==StudioSynthPickerMode::Model?
    studio_synth_group_model(studio_synth_model_groups()[picker.group],picker.item):picker.item;
   studio_synth_stop_audition(s,host);
   if(browser)close_browser(s);else studio_synth_close_picker(s);
   if(mode==StudioSynthPickerMode::Model){
    select_model(s,host,engine,chosen);
    if(chosen!=kSampleGranularModel){
     s.studioRow=2;
     studio_synth_open_picker_for_row(s,host);
    }
   }else select_preset(s,host,engine,chosen);
  }
  return true;
 }
 if(e.action==ButtonAction::RELEASED||e.mods.b||e.mods.select)return true;
 picker.chooseArmed=false;
 const int previousGroup=picker.group,previousItem=picker.item;
 const SoundTab previousTab=picker.tab;
 const int previousRow=picker.rows[static_cast<size_t>(picker.tab)];
 if(picker.mode==StudioSynthPickerMode::Model){
  const auto& groups=studio_synth_model_groups();
  if(e.button==Button::R_SHIFT){picker.group=0;picker.item=0;}
  if(e.button==Button::DPAD_LEFT||e.button==Button::DPAD_RIGHT){
   const int next=std::clamp(picker.group+(e.button==Button::DPAD_RIGHT?1:-1),0,studio_synth_model_group_count()-1);
   if(next!=picker.group){picker.group=next;picker.item=0;}
  }
  if(e.button==Button::DPAD_UP||e.button==Button::DPAD_DOWN)
   picker.item=std::clamp(picker.item+(e.button==Button::DPAD_DOWN?1:-1),0,groups[picker.group].count-1);
 }else{
  if(e.button==Button::R_SHIFT){if(!e.mods.a&&!e.mods.l)star_sound(s,fs);return true;}
  if(!is_dpad(e.button))return true;
  const bool horizontal=e.button==Button::DPAD_LEFT||e.button==Button::DPAD_RIGHT;
  const int step=(e.button==Button::DPAD_RIGHT||e.button==Button::DPAD_DOWN)?1:-1;
  if(e.mods.a&&horizontal){
   // A + Left / Right: the preview level, for every preview in the browser; the song never changes.
   auto& level=s.settings.soundPreviewLevel;
   level=std::clamp(level+step*10,kSoundPreviewLevelMin,kSoundPreviewLevelMax);
   s.studioMessage="Preview level "+std::to_string(level)+" %";
   if(picker.previewCeiling>0&&level>picker.previewCeiling)
    s.studioMessage+=" - this sound is at its loudest from "+std::to_string(picker.previewCeiling)+" %";
   if(picker.previewHeld)preview();
   return true;
  }
  const int jump=e.mods.a?8:1;   // A + Up / Down: a page
  picker.confirmReplace=false;
  if(horizontal){
   picker.tab=static_cast<SoundTab>(std::clamp(static_cast<int>(picker.tab)+step,0,kSoundTabCount-1));
   if(picker.tab!=SoundTab::MODEL)browser_entry(s,fs);   // first visit reads the tab's folders
  }else if(picker.tab==SoundTab::MODEL){
   picker.item=std::clamp(picker.item+step*jump,0,studio::variation_count()-1);
  }else{
   const int count=static_cast<int>(browser_entries(s,fs).size());
   auto& row=picker.rows[static_cast<size_t>(picker.tab)];
   row=std::clamp(row+step*jump,0,std::max(0,count-1));
  }
  if(picker.tab!=SoundTab::MODEL&&picker.tab!=previousTab)s.studioMessage.clear();
 }
 if(picker.previewHeld&&(picker.group!=previousGroup||picker.item!=previousItem||picker.tab!=previousTab||
                         picker.rows[static_cast<size_t>(picker.tab)]!=previousRow))
  preview();
 return true;
}

bool studio_synth_live(const AppState& s){
 if(sample_granular_studio(s))return s.project&&s.currentInstrument>=0&&s.currentInstrument<int(s.project->instruments.size())&&s.project->instruments[s.currentInstrument].granularEnabled;
 if(!s.studioLiveBound||!s.project||s.currentInstrument!=s.studioSynthInstrument||s.currentInstrument<0||s.currentInstrument>=int(s.project->instruments.size()))return false;
 const auto& ins=s.project->instruments[s.currentInstrument];
 return ins.instrumentType==songcore::InstrumentType::SAMPLER&&ins.sampleId>=0&&ins.sampleId<256&&
  ins.sampleFilePath&&*ins.sampleFilePath==s.studioBoundUri;
}
int studio_synth_page_count(const AppState& s){return sample_granular_studio(s)?sample_granular_page_count():1+extra_pages(s)+4;}
std::string studio_synth_page_name(const AppState& s){
 if(sample_granular_studio(s))return sample_granular_page_name(s);
 // The frog models name their pages by what is on them, not by the engine's anatomy.
 if(studio_synth_frog_model(s)){
  static const char* sing[]={"Words","Voice","Timing"};static const char* frog[]={"Frog","Croak","Air"};
  if(kind(s)==0)return "Stage";
  if(kind(s)==1&&s.studioSynthPage>=1&&s.studioSynthPage<=3)return std::string(s.studioModel==studio::kSingFrogModel?sing[s.studioSynthPage-1]:frog[s.studioSynthPage-1])+"   "+std::to_string(s.studioSynthPage)+" / "+std::to_string(extra_pages(s));
 }
 switch(kind(s)){case 0:return "Oscillator";case 1:return "Engine "+std::to_string(s.studioSynthPage)+" / "+std::to_string(extra_pages(s));case 2:return "Filter / Drive";case 3:return "Amplifier / Tune";case 5:return "XY pad";default:return "ADSR envelope";}
}
int studio_synth_control_count(const AppState& s){if(sample_granular_studio(s))return sample_granular_control_count(s);switch(kind(s)){case 0:return studio::macro_count(s.studioModel);case 1:return std::clamp(studio::synth_param_count(s.studioModel)-extra_index(s,0),0,3);case 3:return 3;case 5:return 2;default:return 4;}}
int studio_synth_apply_row(const AppState& s){return std::max(6,3+studio_synth_control_count(s));}
std::string studio_synth_control_label(const AppState& s,int i){
 if(sample_granular_studio(s))return sample_granular_control_label(s,i);
 if(i<0||i>=studio_synth_control_count(s))return {};
 switch(kind(s)){case 0:return studio::macro_name(s.studioModel,i);case 1:return studio::synth_param_name(s.studioModel,extra_index(s,i));case 2:{const char* n[]={"Filter type","Cutoff","Resonance","Drive"};return n[i];}case 3:{const char* n[]={"Level","Pan","Fine tune"};return n[i];}case 5:return std::string(i==0?"X  ":"Y  ")+studio::macro_name(s.studioModel,i);default:{const char* n[]={"Attack","Decay","Sustain","Release"};return n[i];}}
}
int studio_synth_control_level(const AppState& s,int i){
 if(sample_granular_studio(s))return sample_granular_control_level(s,i);
 if(i<0||i>=studio_synth_control_count(s))return 0;const auto& p=s.studioDraft;
 switch(kind(s)){case 0:{const int v[]={s.studioHarmonics,s.studioTimbre,s.studioMorph};return v[i];}
 case 1:{const int x=extra_index(s,i),v=p.studioSynthParams[x];return v<0?studio::synth_param_default(s.studioModel,x):v;}
 case 2:{const int v[]={filter_index(p)*85,p.filterCut,p.filterRes,p.drive};return v[i];}
 case 3:{const int v[]={p.volume,p.pan,p.detune};return v[i];}
 case 5:{const int v[]={s.studioHarmonics,s.studioTimbre};return v[i];}
 default:{const auto* m=envelope(p);if(!m)return 0;const int v[]={m->attack,m->decay,m->sustain,m->release};return v[i];}}
}
std::string studio_synth_control_value(const AppState& s,int i){
 if(sample_granular_studio(s))return sample_granular_control_value(s,i);
 if(i<0||i>=studio_synth_control_count(s))return {};
 const int v=studio_synth_control_level(s,i);
 if(kind(s)==0&&s.studioModel==studio::kOctaveOrbitModel&&i==0){
  const float rate=studio::octave_orbit_rate(v/255.f);
  if(rate==0.f)return "Hold";
  char out[32];std::snprintf(out,sizeof(out),"%+.2f oct/s",rate);return out;
 }
 if(kind(s)==1&&s.studioModel==studio::kFmSysexModel&&extra_index(s,i)==studio::kFmBankParam)return fm_bank_label(s);
 if(kind(s)==1&&s.studioModel==studio::kFmSysexModel&&extra_index(s,i)==studio::kFmVoiceParam)return fm_voice_label(s);
 if(kind(s)==1&&s.studioModel==studio::kSingFrogModel&&extra_index(s,i)==studio::kSingLyricParam){
  const std::string& text=s.studioDraft.lyric;
  if(text.empty())return "ribbit ribbit quack...";
  return text.size()>22?text.substr(0,21)+"~":text;
 }
 if(kind(s)==1)return studio::synth_param_value(s.studioModel,extra_index(s,i),s.studioDraft.studioSynthParams[extra_index(s,i)]);
 if(kind(s)==2&&i==0){const char* n[]={"Off","Low pass","High pass","Band pass"};return n[filter_index(s.studioDraft)];}
 if(kind(s)==3&&i==1){if(std::abs(v-128)<=1)return "Center";return std::to_string(std::abs(v-128)*100/128)+(v<128?" % L":" % R");}
 if(kind(s)==3&&i==2){char out[32];std::snprintf(out,sizeof(out),"%+.1f cents",1200*std::log2(songcore::detune_multiplier(v)));return out;}
 if(kind(s)==4){if(!envelope(s.studioDraft))return std::any_of(s.studioDraft.modSlots.begin(),s.studioDraft.modSlots.end(),[](const auto& m){return m.type==songcore::ModType::NONE;})?"Off":"No free slot";if(i!=2)return std::to_string(v)+" ticks";}
 return percent(v);
}
std::string studio_synth_note_name(const AppState& s){return songcore::note_name(sample_granular_studio(s)&&s.project?s.project->instruments[std::clamp(s.currentInstrument,0,127)].root:s.studioDraft.root);}
void open_studio_synth(AppState& s,const songcore::Instrument& ins){
 studio_synth_close_picker(s);
 s.studioSynthCompare={};
 s.studioOpen=true;s.studioTab=0;s.studioRow=3;s.studioConfirm=false;s.studioMessage.clear();
 s.studioHelp=false;s.studioAUsed=false;s.studioBUsed=false;s.studioSynthPage=0;s.studioAuditionHeld=false;
 s.studioSynthInstrument=s.currentInstrument;s.studioShortcutRelease=false;
 const int code=ins.sampleFilePath?studio::uri_code(ins.sampleFilePath->c_str()):-1;
 s.sampleGranular.open=false;
 if(ins.granularEnabled&&ins.instrumentType==songcore::InstrumentType::SAMPLER&&code<0){s.studioLiveBound=false;s.studioBoundUri.clear();sample_granular_open(s,ins);return;}
 s.studioLiveBound=code>=0&&ins.instrumentType==songcore::InstrumentType::SAMPLER;s.studioBoundUri=s.studioLiveBound?*ins.sampleFilePath:"";
 if(s.studioLiveBound){s.studioDraft=ins;
  if(code&studio::kCustomFlag){s.studioModel=studio::custom_model(code);s.studioHarmonics=studio::custom_macro(code,0)*2;s.studioTimbre=studio::custom_macro(code,1)*2;s.studioMorph=studio::custom_macro(code,2)*2;s.studioSynthCustom=true;}
  else{s.studioModel=studio::preset_model(code);s.studioVariation=studio::preset_variation(code);macros(s);}
 }else{
  s.studioDraft=new_synth_draft(s.currentInstrument);macros(s);
 }
}
void studio_synth_sync_target(AppState& s){
 if(!s.project||s.currentInstrument<0||s.currentInstrument>=int(s.project->instruments.size()))return;
 if(sample_granular_studio(s)){sample_granular_sync(s);return;}
 if(s.studioSynthInstrument!=s.currentInstrument){
  const int row=s.studioRow;const bool a=s.studioAUsed,b=s.studioBUsed,shortcut=s.studioShortcutRelease,help=s.studioHelp;
  open_studio_synth(s,s.project->instruments[s.currentInstrument]);s.studioRow=std::clamp(row,0,studio_synth_apply_row(s));
  s.studioAUsed=a;s.studioBUsed=b;s.studioShortcutRelease=shortcut;s.studioHelp=help;
 }
}
// The frog models show their stage on the FIRST page too (it is their gimmick), so the firefly moves there as well.
bool studio_synth_frog_model(const AppState& s){return s.studioModel==studio::kFrogVoiceModel||s.studioModel==studio::kSingFrogModel;}
bool studio_synth_xy_page(const AppState& s){return !sample_granular_studio(s)&&(kind(s)==5||(kind(s)==0&&studio_synth_frog_model(s)));}
// Hold L (the audition) and press the D-pad: the dot moves, the sound follows at once. X is the first
// macro and Y the second; up raises Y. Each press is one step, and holding repeats it.
void studio_synth_xy_move(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,Button button){
 if(!studio_synth_xy_page(s))return;
 studio_synth_sync_target(s);s.studioConfirm=false;
 constexpr int step=4;
 const int dx=button==Button::DPAD_RIGHT?1:button==Button::DPAD_LEFT?-1:0,dy=button==Button::DPAD_UP?1:button==Button::DPAD_DOWN?-1:0;
 s.studioHarmonics=std::clamp(s.studioHarmonics+dx*step,0,255);s.studioTimbre=std::clamp(s.studioTimbre+dy*step,0,255);
 s.studioSynthCustom=true;publish(s,host,engine,false);
}
// Where the pad is drawn (studio_view.cpp keeps the same numbers): the plain pad, or the frog stage beside three cards
// on the first page, or wide beside two on the last.
static bool pad_rect(const AppState& s,int& x,int& y,int& w,int& h,bool& frog){
 y=254;h=92;frog=studio_synth_frog_model(s);
 if(frog&&s.studioSynthPage==0){x=304;w=312;}
 else if(frog){x=224;w=392;}
 else{x=398;w=218;}
 return true;
}
bool studio_touch_canvas(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,int cx,int cy,int phase,uint64_t finger){
 if(phase!=0&&s.canvasTouchActive&&finger!=s.canvasFinger)return false;   // another finger: not ours
 if(phase==2){const bool was=s.canvasTouchActive;s.canvasTouchActive=false;return was;}
 if(!s.studioOpen||s.studioTab!=0||s.qwerty.isOpen||!studio_synth_xy_page(s)||studio_synth_picker_open(s)){s.canvasTouchActive=false;return false;}
 int x,y,w,h;bool frog;pad_rect(s,x,y,w,h,frog);
 if(phase==0){
  if(cx<x||cx>=x+w||cy<y||cy>=y+h)return false;
  s.canvasTouchActive=true;s.canvasFinger=finger;
 }else if(!s.canvasTouchActive)return false;
 float gx,gy;
 if(frog){   // the firefly: x from x+10 over w-20; y from y+8 over h-43 (studio_view.cpp frog_stage)
  gx=(cx-(x+10))/float(w-20);gy=1.f-(cy-(y+8))/float(h-43);
 }else{      // the plain pad: an 8 px margin, up is more
  gx=(cx-(x+8))/float(w-17);gy=1.f-(cy-(y+8))/float(h-17);
 }
 const int nx=std::clamp(int(std::lround(gx*255.f)),0,255),ny=std::clamp(int(std::lround(gy*255.f)),0,255);
 if(nx==s.studioHarmonics&&ny==s.studioTimbre)return true;
 studio_synth_sync_target(s);s.studioConfirm=false;
 s.studioHarmonics=nx;s.studioTimbre=ny;s.studioSynthCustom=true;publish(s,host,engine,false);
 return true;
}
void studio_synth_navigate(AppState& s,Button button){
 if(sample_granular_studio(s)){sample_granular_navigate(s,button);return;}
 const int count=studio_synth_control_count(s),apply=studio_synth_apply_row(s);
 s.studioSynthColumn=std::clamp(s.studioSynthColumn,0,std::max(0,count-1));
 const bool left=button==Button::DPAD_LEFT,right=button==Button::DPAD_RIGHT;
 if(left||right){
  if(s.studioRow<=1)s.studioRow=right?1:0;
  else if(s.studioRow==apply)s.studioSynthCompare.focused=right;
  else if(s.studioRow>=3&&s.studioRow<3+count){s.studioSynthColumn=std::clamp(s.studioRow-3+(right?1:-1),0,count-1);s.studioRow=3+s.studioSynthColumn;}
 }else if(button==Button::DPAD_UP){
  if(s.studioRow==2)s.studioRow=1;
  else if(s.studioRow>=3&&s.studioRow<3+count){s.studioSynthColumn=s.studioRow-3;s.studioRow=2;}
  else if(s.studioRow==apply)s.studioRow=3+s.studioSynthColumn;
 }else if(button==Button::DPAD_DOWN){
  if(s.studioRow<=1)s.studioRow=2;
  else if(s.studioRow==2)s.studioRow=3+s.studioSynthColumn;
  else if(s.studioRow>=3&&s.studioRow<3+count){s.studioSynthColumn=s.studioRow-3;s.studioRow=apply;}
 }
 s.studioConfirm=false;s.studioMessage.clear();
}
void studio_synth_stop_audition(AppState& s,songcore::SongcoreHost& host){
 if(s.studioAuditionHeld||s.studioSynthPicker.previewSounding)host.stop_preview(true);
 s.studioAuditionHeld=false;
 s.studioSynthPicker.previewHeld=false;
 s.studioSynthPicker.previewSounding=false;
}
void studio_synth_audition(AppState& s,songcore::SongcoreHost& host,bool on){
 if(!on){studio_synth_stop_audition(s,host);return;}if(s.studioAuditionHeld)return;studio_synth_sync_target(s);
 if(sample_granular_studio(s)){
  if(s.sampleGranular.waitingForSource){s.studioMessage="Preparing the new sample";return;}
  s.sampleGranular.publisherFocused=true;
  if(studio_synth_live(s)&&host.sample_granular_snapshot(s.currentInstrument,AudioEngine::PREVIEW_LANE).sourceValid){host.preview_instrument(s.currentInstrument);s.studioAuditionHeld=true;s.sampleGranular.nextPollMs=0;}
  else s.studioMessage="Enable Sample Granular on a loaded sample first";return;
 }
 if(studio_synth_live(s)){host.preview_instrument(s.currentInstrument);s.studioAuditionHeld=true;}
 else{s.studioAuditionHeld=host.preview_studio_synth(s.studioDraft,patch_uri(s),true);if(!s.studioAuditionHeld)s.studioMessage="Audition unavailable";}
}
void studio_synth_edit(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine,int row,int direction,bool coarse,FileSystem* fs){
 studio_synth_sync_target(s);s.studioConfirm=false;
 if(row==0){const bool held=s.studioAuditionHeld;studio_synth_stop_audition(s,host);s.currentInstrument=std::clamp(s.currentInstrument+direction*(coarse?16:1),0,127);studio_synth_sync_target(s);if(held)studio_synth_audition(s,host,true);return;}
 if(row==1){
  // UI-only selector entry0 is Sample Granular. Old oscillator IDs and URIs never move.
  const bool wasGranular=sample_granular_studio(s);const int old=wasGranular?0:s.studioModel+1;
  const int selected=coarse&&direction<0?0:std::clamp(old+direction,0,studio::model_count());
  select_model(s,host,engine,selected-1);return;
 }
 if(sample_granular_studio(s)){sample_granular_edit(s,host,row,direction,coarse);return;}
 if(row==studio_synth_apply_row(s)&&s.studioSynthCompare.focused){
  if(coarse&&direction>0)store_synth_reference(s);
  return;
 }
 if(row==2){select_preset(s,host,engine,(s.studioVariation+direction+studio::variation_count())%studio::variation_count());return;}
 const int i=row-3;if(i<0||i>=studio_synth_control_count(s))return;int* value=nullptr;auto& p=s.studioDraft;const int k=kind(s);
 const auto previousDraft=p;
 if(k==0){int* v[]={&s.studioHarmonics,&s.studioTimbre,&s.studioMorph};value=v[i];s.studioSynthCustom=true;}
 else if(k==1&&s.studioModel==studio::kFmSysexModel&&(extra_index(s,i)==studio::kFmBankParam||extra_index(s,i)==studio::kFmVoiceParam)){
  fm_step(s,fs,extra_index(s,i),direction,coarse);publish(s,host,engine,false);return;}
 else if(k==1&&s.studioModel==studio::kSingFrogModel&&extra_index(s,i)==studio::kSingLyricParam&&coarse){
  // A + Up / Down: the next or previous template of Presets/Lyrics replaces the words.
  const auto templates=lyric_templates(fs);
  if(templates.empty()){s.studioMessage="No lyric templates: put .txt files in Presets/Lyrics";return;}
  int at=-1;for(size_t n=0;n<templates.size();++n)if(templates[n].text==s.studioDraft.lyric)at=int(n);
  const int count=int(templates.size());
  const int next=at<0?(direction>0?0:count-1):((at+direction)%count+count)%count;
  s.studioDraft.lyric=templates[size_t(next)].text;
  publish(s,host,engine,false);
  s.studioMessage="Lyrics: "+templates[size_t(next)].name+"   ("+std::to_string(next+1)+" / "+std::to_string(count)+")";return;}
 else if(k==1&&s.studioModel==studio::kSingFrogModel&&extra_index(s,i)==studio::kSingLyricParam){
  // The words are typed on the keyboard; A+D-pad on this row opens it with the current text.
  auto& k2=s.qwerty;k2=QwertyKeyboardState{};k2.isOpen=true;
  k2.text=s.studioDraft.lyric.empty()?std::string("ribbit ribbit quack la la"):s.studioDraft.lyric;
  k2.maxLength=studio::sing::kMaxTextChars;k2.textCursor=static_cast<int>(k2.text.size());
  k2.fieldLabel="LYRICS:";k2.context=QwertyContext::LYRIC_EDIT;k2.insertBefore=s.settings.insertBefore;
  s.studioMessage="Type the words. Each note sings the next syllable.";return;}
 else if(k==1){const int x=extra_index(s,i);p.studioSynthParams[x]=studio::synth_param_adjust(s.studioModel,x,p.studioSynthParams[x],direction,coarse);publish(s,host,engine,false);return;}
 else if(k==2){if(i==0){const char* n[]={"off","lp","hp","bp"};p.filterType=n[std::clamp(filter_index(p)+direction,0,3)];if(p.filterType!="off"&&!p.filterCut)p.filterCut=255;}else{int* v[]={&p.filterCut,&p.filterRes,&p.drive};value=v[i-1];}}
 else if(k==3){int* v[]={&p.volume,&p.pan,&p.detune};value=v[i];}
 else if(k==5){int* v[]={&s.studioHarmonics,&s.studioTimbre};value=v[i];s.studioSynthCustom=true;}
 else{auto* m=edit_envelope(p);if(!m){s.studioMessage="All modulation slots are used - free one in MODS for ADSR";return;}int* v[]={&m->attack,&m->decay,&m->sustain,&m->release};value=v[i];}
 if(value)*value=std::clamp(*value+direction*(coarse?16:1),0,255);publish(s,host,engine,k>=2&&k!=5,&previousDraft);
}

bool studio_synth_compare(AppState& s,songcore::SongcoreHost& host,AudioEngine& engine){
 studio_synth_sync_target(s);
 auto& compare=s.studioSynthCompare;
 if(!compare.focused||sample_granular_studio(s))return false;
 s.studioConfirm=false;
 if(s.studioLiveBound&&!studio_synth_live(s)){
  compare.alternate.reset();
  s.studioMessage="Instrument changed. Reopen Studio to compare.";
  return false;
 }
 if(!compare.alternate){store_synth_reference(s);return true;}

 auto previous=current_sound(s);
 const auto previousUri=s.studioBoundUri;
 const bool wasLinked=s.studioLiveBound;
 select_sound(s,*compare.alternate);
 if(!publish(s,host,engine,true,&previous.draft)){
  select_sound(s,previous);
  s.studioLiveBound=wasLinked;
  s.studioBoundUri=previousUri;
  s.studioMessage="Could not switch A/B. Current sound kept.";
  return false;
 }
 compare.alternate=std::move(previous);
 compare.listeningToA=!compare.listeningToA;
 s.studioSynthPage=std::min(s.studioSynthPage,studio_synth_page_count(s)-1);
 s.studioRow=studio_synth_apply_row(s);
 s.studioMessage=compare.listeningToA?"A is active. Press A here to return to B.":"B is active. Press A here to compare with A.";
 return true;
}

bool studio_synth_apply(AppState& s,songcore::SongcoreHost& host,AudioEngine&){
 if(sample_granular_studio(s)){s.studioMessage="Choose a Sample Granular action below";return false;}
 studio_synth_sync_target(s);if(studio_synth_live(s)){s.studioMessage="Instrument is linked for live editing";return true;}
 auto& old=host.edit_project().instruments[s.currentInstrument];
 if((old.sampleFilePath||old.soundfontPath||old.instrumentType!=songcore::InstrumentType::SAMPLER)&&!s.studioConfirm){s.studioConfirm=true;return false;}
 const bool held=s.studioAuditionHeld;studio_synth_stop_audition(s,host);const auto uri=patch_uri(s);
 if(!host.load_sample(s.currentInstrument,uri)){s.studioMessage="Synth load failed - instrument kept";return false;}
 const int sampleId=old.sampleId;old=s.studioDraft;old.id=s.currentInstrument;old.sampleId=sampleId;old.sampleFilePath=uri;old.soundfontPath.reset();old.instrumentType=songcore::InstrumentType::SAMPLER;
 old.name=s.studioSynthCustom?std::string(studio::model_name(s.studioModel))+" / CUSTOM":studio::preset_name(studio::preset_code(s.studioModel,s.studioVariation));
 host.push_instrument(s.currentInstrument);s.studioDraft=old;s.studioBoundUri=uri;s.studioLiveBound=true;s.studioConfirm=false;++s.projectVersion;
 s.studioMessage="Synth loaded - model, preset and controls now edit live";
 if(held)studio_synth_audition(s,host,true);else if(!host.is_playing())host.preview_instrument(s.currentInstrument);return true;
}
} // namespace pt::ui
