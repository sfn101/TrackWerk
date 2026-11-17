// SPDX-License-Identifier: GPL-3.0-or-later
// Generated fixtures only. Exercise the real readers, routing and playback, including
// the 32-bit RIFF offset boundary without allocating a multi-gigabyte file.
#include "audio-engine.h"
#include "common/riff_bounds.h"
#include "songcore/engine_setup.h"
#include "songcore/project_io.h"
#include "songcore/scheduler.h"
#include "soundfont-trim.h"
#include <cassert>
#include <climits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>

using namespace songcore;
namespace {
struct Capture : IMidiConsumer {
    size_t notes = 0;
    void consume(const Event& e) override { if (e.type == EV_NOTE_ON) ++notes; }
    void on_play(const std::string&, const std::string&, int64_t, int, int) override {}
    void on_stop() override {}
};
void project_checks() {
    for (const auto& value : {json(0),json(-12),json(1000000),json(INT_MAX),json(INT_MIN)}) {
        auto p = parse_project(json{{"tempo",value}}); normalize_and_migrate(p);
        assert(p.tempo >= 20 && p.tempo <= 999);
        p.tracks[0].chainRefs = {0}; p.chains[0].phraseRefs[0] = 0;
        p.phrases[0].steps[0].note = Note::C4();
        Capture c; MidiRouter router(&c); Sequencer seq(router,p,48000);
        assert(seq.scheduleSongRowRange(0,0,nullptr) == 16 * frames_per_step(p.tempo,48000));
        seq.playSong(0); seq.updatePlaybackBuffer(); seq.stop();
    }
    for (const auto& value : {json(UINT64_MAX),json(INT64_MIN),json(1e100),
                              json(std::numeric_limits<double>::infinity()),
                              json(std::numeric_limits<double>::quiet_NaN())}) {
        assert(detail::checked_int(value,42) == 42);
        auto p = parse_project(json{{"tempo",value}}); assert(p.tempo == Project{}.tempo);
    }
    assert(detail::checked_int(json(123.75),0) == 123);
    for (int n : {0,1,15,16,17,32}) {
        auto rows = json::array();
        for (int i=0;i<n;++i) rows.push_back({{"note",{{"pitch",0},{"octave",4}}}});
        auto p = parse_project(json{{"phrases",json::array({{{"steps",rows}}})}});
        normalize_and_migrate(p); assert(p.phrases[0].steps.size() == PHRASE_ROWS);
        p.tracks[0].chainRefs={0}; p.chains[0].phraseRefs[0]=0;
        Capture c; MidiRouter router(&c); Sequencer seq(router,p,48000);
        const auto length=16 * frames_per_step(p.tempo,48000);
        assert(seq.scheduleSongRowRange(0,0,nullptr) == length);
        assert(c.notes == static_cast<size_t>(std::min(n,16)));
        p.phrases[0].steps.clear();  // a non-file caller must also be safe
        Capture empty; MidiRouter r(&empty); Sequencer shortSeq(r,p,48000);
        assert(shortSeq.scheduleSongRowRange(0,0,nullptr)==length && empty.notes==0);
    }
    auto p = parse_project(json{{"grooves",json::array({{{"steps",json::array({-5,0,12,999,-1})}}})}});
    normalize_and_migrate(p); assert(p.grooves[0].steps[0]==-1);
    assert(p.grooves[0].steps[1]==0 && p.grooves[0].steps[3]==255);
    for(int tempo : {INT_MIN,0,1,20,120,999,1000000,INT_MAX})
        for(int rate : {-1,0,1,44100,48000,96000}) assert(frames_per_step(tempo,rate)>0);
    for(int clock : {0,-1,1,20,999,1000000}) {
        p.clockTempo=clock; p.tempo=0;
        assert(tempo_in_force(p)>=20 && tempo_in_force(p)<=999);
    }
    Groove g; g.steps={0,12,-1};
    assert(groove_step_duration(g,0,6000)==0 && groove_step_duration(g,1,6000)==6000);
    g.steps={-9,12,-1}; assert(groove_step_duration(g,0,6000)==0);
    auto initial=make_default_project();normalize_and_migrate(initial);
    const auto valid=serialize_project(initial);
    auto loaded=parse_project(json::parse(valid));normalize_and_migrate(loaded);
    assert(serialize_project(loaded)==valid);  // valid documents are not rewritten differently
    for(int id : {-1,7,128,5000000}) {
        auto broken=make_default_project();broken.instruments[3].id=id;
        broken.instruments[3].sampleId=23;
        assert(normalize_project(broken));
        assert(broken.instruments[3].id==3 && broken.instruments[3].sampleId==23);
        assert(!normalize_project(broken));
    }
}
void routing_checks(AudioEngine& engine) {
    Routing routing; int before[POOL_INSTRUMENTS]; std::memcpy(before,routing.sfSlot,sizeof(before));
    MediaRoots roots;
    for(int id : {-1,128,5000000,INT_MAX,INT_MIN}) {
        Instrument ins(id); ins.instrumentType=InstrumentType::SOUNDFONT;
        ins.soundfontPath="not-a-real-bank.sf2";
        assert(!sync_instrument_soundfont(engine,ins,routing,roots));
        assert(!request_instrument_soundfont(engine,ins,routing,roots));
        assert(std::memcmp(routing.sfSlot,before,sizeof(before))==0);
    }
}
using Bytes=std::vector<uint8_t>;
void tag(Bytes& b,const char* s){b.insert(b.end(),s,s+4);}
void u16(Bytes& b,uint16_t v){b.push_back(v);b.push_back(v>>8);}
void u32(Bytes& b,uint32_t v){for(int n=0;n<4;++n)b.push_back(v>>(n*8));}
Bytes header(const char* kind){Bytes b;tag(b,"RIFF");u32(b,0);tag(b,kind);return b;}
void fmt(Bytes& b,uint32_t rate=48000){tag(b,"fmt ");u32(b,16);u16(b,1);u16(b,1);u32(b,rate);u32(b,96000);u16(b,2);u16(b,16);}
void data(Bytes& b,uint32_t size=8){tag(b,"data");u32(b,size);for(int n=0;n<4;++n)u16(b,1000+n*1000);}
void write(const std::filesystem::path& p,const Bytes& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());assert(f.good());}
void media_checks(AudioEngine& engine,const std::filesystem::path& root) {
    const float original[]={.25f,-.5f,.75f,-.125f};
    auto reject=[&](const Bytes& b,const char* name){
        auto path=root/name;write(path,b);assert(engine.loadSample(0,original,4));
        assert(engine.loadSampleFromWavFile(0,path.string().c_str())==0);
        float after[4]{};assert(engine.getSampleLength(0)==4);engine.getSampleData(0,after);
        assert(std::memcmp(original,after,sizeof(after))==0);
    };
    for(uint32_t size : {0x7fffffffu,0x80000000u,0xfffffff8u,0xffffffffu}) {
        for(const char* kind : {"JUNK","fmt "}) {
            auto b=header("WAVE");tag(b,kind);u32(b,size);
            reject(b,"large-chunk.wav");
        }
        for(const char* kind : {"JUNK","LIST"}) {
            auto b=header("sfbk");tag(b,kind);u32(b,size);
            auto path=root/"large-chunk.sf2";write(path,b);
            std::vector<pt::SfPreset> index;
            assert(!pt::sf_read_preset_list(path.string().c_str(),index));
            assert(engine.loadSoundfont(0,path.string().c_str(),0,0)<0);
        }
        auto b=header("sfbk");tag(b,"LIST");u32(b,12);tag(b,"pdta");tag(b,"phdr");u32(b,size);
        auto path=root/"bad-subchunk.sf2";write(path,b);std::vector<uint8_t> out;
        assert(!pt::sf_build_trimmed_font(path.string().c_str(),0,0,out));
    }
    for(uint32_t rate : {0u,0x80000000u,0xffffffffu}) {
        auto b=header("WAVE");fmt(b,rate);data(b);reject(b,"invalid-rate.wav");
    }
    {auto b=header("WAVE");tag(b,"fmt ");u32(b,2);u16(b,1);data(b);reject(b,"short-fmt.wav");}
    for(bool oddJunk : {false,true}) for(bool truncatedData : {false,true}) {
        auto b=header("WAVE");
        if(oddJunk){tag(b,"JUNK");u32(b,3);b.insert(b.end(),{1,2,3,0});}
        fmt(b);data(b,truncatedData?0xffffffffu:8);
        auto path=root/"valid.wav";write(path,b);
        assert(engine.loadSampleFromWavFile(0,path.string().c_str())==48000);
        assert(engine.getSampleLength(0)==4);
        float pcm[4]{};engine.getSampleData(0,pcm);
        assert(pcm[0]==1000.f/32768 && pcm[3]==4000.f/32768);
    }
    // A normal, public, generated fixture must still work through trim and streamed load.
    const auto tone=std::filesystem::path(__FILE__).parent_path()/"fixtures/tone.sf2";
    std::vector<uint8_t> trimmed;
    assert(pt::sf_build_trimmed_font(tone.string().c_str(),0,0,trimmed));
    assert(engine.loadSoundfont(1,tone.string().c_str(),0,0)>=0);
    const auto expectedTrim=trimmed;
    for(int tail=0;tail<3;++tail) {
        std::ifstream input(tone,std::ios::binary);
        Bytes b((std::istreambuf_iterator<char>(input)),{});
        tag(b,"JUNK");u32(b,tail==0?100:3);
        b.insert(b.end(),tail==0?10:3,0);
        if(tail==2)b.push_back(0);
        const auto path=root/("tag-tail-"+std::to_string(tail)+".sf2");write(path,b);
        std::vector<pt::SfPreset> presets;
        assert(pt::sf_read_preset_list(path.string().c_str(),presets) && !presets.empty());
        assert(pt::sf_build_trimmed_font(path.string().c_str(),0,0,trimmed));
        assert(trimmed==expectedTrim);
    }
}
void offset_checks(){
    long next=-1;
    assert(pt::riff_body_end(16,3,20,next) && next==20);
    assert(!pt::riff_body_end(16,3,19,next));
    assert(!pt::riff_body_end(16,0xfffffff8u,24,next));
    assert(!pt::riff_body_end(INT32_MAX-4,4,INT32_MAX-1,next));
    assert(pt::riff_body_end(INT32_MAX-4,4,INT32_MAX,next) && next==INT32_MAX);
    assert(!pt::riff_body_end(LONG_MAX-4,5,LONG_MAX,next));
    assert(!pt::riff_body_end(-1,0,16,next));
}
}
int main(int argc,char** argv){
    assert(argc==2);const auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
    project_checks();offset_checks();
    auto engine=std::make_unique<AudioEngine>();engine->setDeviceSampleRate(48000);
    routing_checks(*engine);media_checks(*engine,root);
    std::cout<<"FILE_VALIDATION_PASS: project/phrase/tempo/groove/routing/RIFF/WAV/SF2 and valid-data preservation\n";
}
