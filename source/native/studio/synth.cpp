// Host integration: GPL-3.0-or-later. Mutable DSP retains Emilie Gillet's MIT notices.
#include "synth.h"
#include "native_clock.h"
#include "synth_extra.h"
#include "spectral_additive.h"
#include "spectral_freeze.h"
#include "spectral_morph.h"
#include "formant_choir.h"
#include "sid_chip.h"
#include "dco_poly.h"
#include "dub_siren.h"
#include "frog_voice.h"
#include "sing_frog.h"
#include "bass_synths.h"
#include "mic_pitch.h"
#include "fm_sysex_voice.h"
#include "plaits/resources.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
// Each firmware's generated resources use global macro names. Its inline DSP is
// parsed with its own values; discard conflicting names before the next module.
#include "plaits/dsp/voice.h"
#undef LUT_FM_FREQUENCY_QUANTIZER
#undef LUT_FM_FREQUENCY_QUANTIZER_SIZE
#include "braids/macro_oscillator.h"
#undef LUT_FM_FREQUENCY_QUANTIZER
#undef LUT_SINE_SIZE
#undef LUT_SVF_SHIFT
#undef LUT_STIFFNESS
#undef LUT_STIFFNESS_SIZE
#include "rings/dsp/part.h"
#undef LUT_SINE_SIZE
#undef LUT_4_DECADES
#undef LUT_STIFFNESS
#undef LUT_ENV_EXPO
#undef LUT_ENV_EXPO_SIZE
#undef LUT_FM_FREQUENCY_QUANTIZER
#undef LUT_SVF_SHIFT
#include "elements/dsp/part.h"
namespace studio {
namespace {
constexpr int N=24;
const char* plaitsNames[]={"P VCF","P PHASE DIST","P DX BANK A","P DX BANK B","P DX BANK C","P WAVE TERRAIN","P STRING MACHINE","P CHIPTUNE","P VIRTUAL ANALOG","P WAVESHAPER","P FM","P GRAINS","P ADDITIVE","P WAVETABLE","P CHORD","P SPEECH","P SWARM","P NOISE","P PARTICLES","P STRING","P MODAL","P KICK","P SNARE","P HI HAT"};
const char* ringsNames[]={"R MODAL","R SYMPATHETIC","R STRING","R FM","R QUANT STRINGS","R STRING REVERB"};
const char* elementsNames[]={"E MODAL","E STRING","E CHORD","E OMINOUS"};
constexpr int braidsCount=static_cast<int>(braids::MACRO_OSC_SHAPE_LAST);
constexpr int legacyModels = 24 + braidsCount + 6 + 4;
constexpr int elementsProfiles = 12;
constexpr int additionalModels = 34 + kSpectralModelCount + kVoiceModelCount + kVintageModelCount;
static_assert(legacyModels == 82, "Existing model IDs must remain stable");
static_assert(legacyModels + additionalModels <= kMaxModels, "Synth codes carry an eight-bit model field (synth.h)");
const char* additionalNames[] = {
 "E BOWED PLATE", "E AIR PLATE", "E MALLET PLATE", "E HYBRID PLATE",
 "E BOWED STRING", "E AIR STRING", "E PLUCK STRING", "E HYBRID STRING",
 "E BOWED CHORD", "E AIR CHORD", "E STRUCK CHORD", "E HYBRID CHORD",
 "G TAPE CLOUD", "G SHIMMER CLOUD", "G REVERSE DUST", "G CHORD SWARM",
 "G FORMANT GRAINS", "G SPRAY CLOUD", "G RESONANT GRAINS", "G STUTTER CLOUD",
 "S SUPER SAW", "S VECTOR", "S PHASE FM", "S RING BANK", "S ADDITIVE TILT",
 "S KARPLUS NYLON", "S BOWED COMB", "S FORMANT VOWEL", "S HARD SYNC", "S WAVEFOLD",
 "S OCTAVE ORBIT", "S GRAVITY BOUNCE", "S ORBIT BELLS", "S GRAVITY RAIN",
 "SP ADDITIVE", "SP FREEZE", "SP MORPH",
 "FORMANT CHOIR",
 "SID CHIP", "DCO POLY", "FM SYSEX", "DUB SIREN", "CROAKER", "FROGGY", "ACID BOX", "REESE BASS"
};
static_assert(sizeof(additionalNames) / sizeof(*additionalNames) == additionalModels);
static_assert(kGravityFirstModel + kGravityModelCount == kSpectralFirstModel);
static_assert(kSpectralFirstModel + kSpectralModelCount == kVoiceFirstModel);
static_assert(kVoiceFirstModel + kVoiceModelCount == kVintageFirstModel);
static_assert(kVintageFirstModel + kVintageModelCount == legacyModels + additionalModels);
static_assert(kOctaveOrbitModel + 1 == kGravityFirstModel);
constexpr int extendedFlag = kExtendedFlag;
const char* variants[]={"WARM","GLASS","DARK","AIR","PLUCK","BELL","HOLLOW","SOFT","ORGAN","METALLIC","THIN","MOTION","TAPE","PUNCH","VAPOUR","BIT DUST"};
constexpr float macroA[]={.35f,.72f,.17f,.88f,.28f,.61f,.12f,.47f,.78f,.93f,.08f,.56f,.29f,.66f,.84f,.21f};
constexpr float macroB[]={.42f,.63f,.24f,.77f,.21f,.82f,.54f,.16f,.38f,.94f,.69f,.46f,.29f,.74f,.57f,.89f};
constexpr float macroC[]={.50f,.23f,.75f,.60f,.14f,.37f,.85f,.29f,.68f,.48f,.11f,.92f,.59f,.18f,.77f,.33f};
struct NamedPreset { const char* name; std::array<int, 3> macros; };
constexpr NamedPreset kOrbitPresets[] = {
    {"Slow Rise",     {162, 186, 102}}, {"Slow Descent", { 90, 186, 102}},
    {"Deep Drift",    {109, 204, 181}}, {"Still Choir",  {128, 255,  75}},
    {"Gravity Keys",  {136, 107, 232}}, {"Crystal Lift", {183, 112,  44}},
    {"Falling Glass", { 68, 104,  51}}, {"Low Orbit",    {120, 161, 251}},
    {"Wide Halo",     {150, 254,  19}}, {"High Spiral",  {224, 238,  32}},
    {"Falling Debris",{ 16, 209,  49}}, {"Fast Ascent",  {255, 161,   8}},
};
constexpr int kLegacyNamedVariations = std::size(kOrbitPresets);
static_assert(std::size(variants) == 16);
static_assert(std::size(macroA) == std::size(variants) &&
              std::size(macroB) == std::size(variants) && std::size(macroC) == std::size(variants));
constexpr NamedPreset kGravityPresets[kGravityModelCount][kLegacyNamedVariations] = {
    {{"Pinball Keys", {164,192, 96}}, {"Rubber Mallet", {111,165, 38}},
     {"Drop Counter", {202, 32, 65}}, {"Coin Cascade",  {237,222,184}},
     {"Low Rebound",  { 78,144, 13}}, {"Copper Spring", {175,255,137}},
     {"Slow Hopper",  { 33,186, 80}}, {"Moon Drop",     { 12,102,214}},
     {"Loose Marbles",{209,126,213}}, {"Skitter",        {255,234,246}},
     {"One Pitch",    {125,205,  0}}, {"Glass Floor",   {157, 64,239}}},
    {{"Clock Bells",  {119, 75,154}}, {"Dorian Moon",    { 65,160,200}},
     {"Tidal Clock",  { 88,239, 96}}, {"Two Satellites",{164,207,130}},
     {"Slow Halo",    { 26,137,231}}, {"Bright Crossing",{192,171,185}},
     {"Pendulum",     {126,249, 63}}, {"Soft Ellipse",   { 56, 48,163}},
     {"Bell Dial",    {185, 15, 40}}, {"Comet Tail",     {239,233,255}},
     {"Single Moon",  {111,162,  0}}, {"Fast Eclipse",   {255,255,219}}},
    {{"Tin Roof",     {135,164, 96}}, {"Light Shower",  { 58, 26,145}},
     {"Metal Drizzle",{104,108,187}}, {"Spring Rain",   { 79,142,205}},
     {"Low Weather",  { 36,173, 36}}, {"Hail Bells",    {196,198,247}},
     {"Window Drops", { 43, 76,113}}, {"Chime Mist",    { 21,216,254}},
     {"Tin Spouts",   {154,233, 59}}, {"Glass Storm",   {246,238,229}},
     {"Mono Rain",    {123,173,  0}}, {"Cloudburst",    {255,255,255}}},
};

// SP ADDITIVE: A brightness, B spectral comb, C stretch (spectral_additive.h).
constexpr NamedPreset kSpectralAdditivePresets[16] = {
    {"Glass Organ",   {150,  0,   0}}, {"Hollow Pipe",   { 90,  0,   0}},
    {"Bright Choir",  {205, 40,  20}}, {"Comb Sweep",    {170,190,   0}},
    {"Stretched Bell",{180,  0, 200}}, {"Dark Drone",    { 30, 60,   0}},
    {"Metal Plate",   {225, 90, 255}}, {"Soft Pad",      { 70, 25,  30}},
    {"Laser Comb",    {245,255,  40}}, {"Frozen Bell",   {140,120, 150}},
    {"Gamelan",       {165, 70, 230}}, {"Night Strings", {120,150,  15}},
    {"Sub Hum",       { 10,  0,   0}}, {"Prism",         {235,215, 110}},
    {"Tape Organ",    {110, 95,  60}}, {"Spectral Rain", {200,240, 185}},
};

// SP FREEZE: A position, B smear, C tilt (spectral_freeze.h); the source slot is a parameter.
constexpr NamedPreset kSpectralFreezePresets[16] = {
    {"First Breath",  {  8,  0, 128}}, {"Held Middle",   {128,  0, 128}},
    {"Last Ring",     {240,  0, 128}}, {"Slow Bloom",    {110, 120, 140}},
    {"Glass Freeze",  {128,  0, 230}}, {"Velvet Freeze", {128, 40,  50}},
    {"Drift Cloud",   { 90, 220, 128}}, {"Attack Shard",  {  0,  0, 200}},
    {"Dark Sustain",  {170, 60,  20}}, {"Bright Halo",   {150, 90, 250}},
    {"Moving Choir",  { 60, 180, 150}}, {"Still Water",   {200,  0, 100}},
    {"Tape Ghost",    {100, 140,  70}}, {"Ice Pad",       {180, 30, 200}},
    {"Wide Smear",    {128, 255, 128}}, {"Low Hum",       { 40, 20,   0}},
};

// SP MORPH: A morph, B motion, C warp (spectral_morph.h, 128 = formants as drawn). Each sound also
// brings its own pair of shapes (kSpectralMorphShapes), used while Shape A/B stay at Factory.
constexpr NamedPreset kSpectralMorphPresets[16] = {
    {"Vowel Glide",   {128,  0, 128}}, {"Talking Pad",   {100,150, 128}},
    {"Saw to Bell",   { 70,  0, 128}}, {"Breathing Organ",{128, 90, 110}},
    {"Glass Chorus",  {150,110, 170}}, {"Metal Mouth",   { 90,170, 140}},
    {"Reed Bloom",    { 60, 70, 128}}, {"Brass Vowel",   {128,120, 120}},
    {"Hollow Sweep",  {128,200, 128}}, {"Low Choir",     {110, 80,  60}},
    {"Bright Wah",    {128,230, 200}}, {"String Morph",  { 90, 60, 140}},
    {"Bell Choir",    {160, 50, 150}}, {"Square Speak",  {128,140, 110}},
    {"Flute to Reed", { 80, 90, 128}}, {"Shifting Prism",{140,255, 180}},
};
// Shape A, shape B per sound (indices into SpectralMorph::kShapeNames).
constexpr std::array<int, 2> kSpectralMorphShapes[16] = {
    {{3, 6}}, {{4, 7}}, {{0, 8}}, {{2, 6}}, {{9, 5}}, {{10, 3}}, {{11, 7}}, {{12, 3}},
    {{14, 4}}, {{7, 6}}, {{3, 5}}, {{13, 4}}, {{8, 3}}, {{1, 4}}, {{15, 11}}, {{9, 10}},
};

// FORMANT CHOIR: A vowel (A 0, E 64, I 128, O 191, U 255), B ensemble, C breath (formant_choir.h).
// Each sound sings with its own voice type (kFormantChoirVoices), used while Voice stays at Factory.
constexpr NamedPreset kFormantChoirPresets[16] = {
    {"Ah Choir",       {  0, 200,  40}}, {"Oo Pad",         {255, 220,  70}},
    {"Monks",          {191, 150,  20}}, {"Soprano Ah",     { 10, 180,  45}},
    {"Solo Tenor",     { 30,   0,  15}}, {"Whisper Choir",  { 64, 200, 235}},
    {"Eh Ensemble",    { 64, 170,  35}}, {"Bright Ee",      {128, 160,  25}},
    {"Low Drone",      {240, 255,  50}}, {"Cathedral",      {191, 255,  60}},
    {"Between Vowels", { 96, 190,  50}}, {"Breath Pad",     {220, 230, 170}},
    {"Duet",           { 20,  40,  25}}, {"Chant",          {170, 120,  15}},
    {"Angels",         {110, 255,  95}}, {"Night Hum",      {255, 100, 140}},
};
// Voice type per sound: 0 Bass, 1 Tenor, 2 Alto, 3 Soprano.
constexpr int kFormantChoirVoices[16] = {1, 2, 0, 3, 1, 2, 1, 3, 0, 3, 2, 2, 1, 0, 3, 0};

// SID CHIP: A colour, B arp (chord = B·9/256: Off 14, Octave 43, Fifth 71, Major 99, Minor 128,
// Major 7 156, Minor 7 184, Sus 4 212, Diminished 241), C cutoff (sid_chip.h).
constexpr NamedPreset kSidChipPresets[16] = {
    {"Lead Pulse",   { 60,  14, 200}}, {"Major Arp",    { 30,  99, 190}},
    {"Minor Arp",    {  0, 128, 170}}, {"Fat Bass",     {  0,  14,  90}},
    {"Sync Lead",    {120,  14, 230}}, {"Ring Bell",    {150,  14, 255}},
    {"Noise Wind",   {  0,  14, 130}}, {"Thin PWM",     {180,  14, 210}},
    {"Chip Organ",   {  0,  14, 220}}, {"Pulse Brass",  { 80,  14, 150}},
    {"Soft Flute",   { 40,  14, 160}}, {"Octave Jump",  { 50,  43, 200}},
    {"Power Fifths", { 20,  71, 180}}, {"Wah Bass",     { 60,  14,  70}},
    {"Dim Arp",      { 70, 241, 200}}, {"Tri Bass",     {  0,  14, 255}},
};
// Each sound's own chip settings, used while the parameter stays at Factory:
// wave, chip (0 = 6581, 1 = 8580), filter mode, resonance 0..255, PWM 0..255, arp speed in frames.
struct SidSound { int wave, chip, mode, resonance, pwm, speed; };
constexpr SidSound kSidChipSounds[16] = {
    {SidChip::kPulse, 1, 0, 60, 140, 1},     {SidChip::kPulse, 1, 0, 40, 60, 1},
    {SidChip::kSaw, 0, 0, 90, 0, 1},         {SidChip::kSaw, 0, 0, 170, 0, 1},
    {SidChip::kSync, 1, 0, 30, 0, 1},        {SidChip::kRing, 1, 3, 0, 0, 1},
    {SidChip::kNoise, 1, 1, 200, 0, 1},      {SidChip::kPulse, 0, 0, 50, 230, 1},
    {SidChip::kSawTri, 1, 0, 20, 0, 1},      {SidChip::kPulseSaw, 0, 0, 120, 90, 1},
    {SidChip::kPulseTri, 1, 0, 30, 40, 1},   {SidChip::kPulse, 1, 0, 40, 100, 2},
    {SidChip::kSaw, 0, 0, 80, 0, 1},         {SidChip::kPulse, 0, 1, 150, 120, 1},
    {SidChip::kPulseSaw, 1, 0, 40, 60, 1},   {SidChip::kTriangle, 1, 3, 0, 0, 1},
};

// DCO POLY: A cutoff, B resonance, C chorus (Off 32, I 96, II 160, I+II 224) (dco_poly.h).
constexpr NamedPreset kDcoPolyPresets[16] = {
    {"Poly Strings",   {150,  30,  96}}, {"PWM Pad",        {140,  40, 160}},
    {"Brass Stab",     { 90,  60,  32}}, {"Sub Bass",       { 62,  80,  32}},
    {"Pluck Keys",     {110,  70,  96}}, {"Reso Sweep",     {110, 215,  32}},
    {"Hollow Square",  {200,  20,  96}}, {"Warm Organ",     {170,  10, 224}},
    {"Wide Choir",     {160,  50, 160}}, {"Funk Bass",      { 85, 150,  32}},
    {"Noise Sweep",    {130, 190,  96}}, {"Thin Lead",      {180,  90,  96}},
    {"Glass Keys",     {130, 120, 160}}, {"Night Drive",    {120, 100,  96}},
    {"Bright Lead",    {210,  60,  32}}, {"Ensemble",       {175,  30, 224}},
};
// Each sound's own voice settings (parameter rows 2…14, 0..255; HPF as position 0…3), used while a
// parameter stays at Factory: saw, pulse, width, PWM, sub, noise, HPF, env amount, decay, sustain,
// LFO rate, vibrato, key follow.
constexpr std::array<int, 13> kDcoPolySounds[16] = {
    {{255,   0,   0,   0,   0,   0, 1,  60, 150, 150, 120,  30, 128}},
    {{  0, 255,  60, 200,  80,   0, 1,  40, 180, 180,  90,   0, 128}},
    {{255,   0,   0,   0,  60,   0, 1, 200, 110,  90, 120,   0, 128}},
    {{180,   0,   0,   0, 200,   0, 0, 120,  90,  40, 120,   0,  60}},
    {{255, 120,  40,   0,   0,   0, 1, 220,  70,  70, 120,   0, 128}},
    {{255,   0,   0,   0,   0,   0, 1, 150, 160,  60, 120,   0, 128}},
    {{  0, 255,   0,   0, 100,   0, 1,  30, 128, 200, 120,   0, 128}},
    {{  0, 200,   0,   0, 200,   0, 1,   0, 128, 255, 120,   0, 128}},
    {{200, 150, 100, 150,   0,  20, 1,  40, 200, 200, 110,  20, 128}},
    {{  0, 255, 120,   0, 120,   0, 0, 230,  60,  40, 120,   0,  80}},
    {{  0,   0,   0,   0,   0, 255, 1, 200, 200,  30, 120,   0, 128}},
    {{  0, 255, 200,   0,   0,   0, 1,  80, 120, 150, 140,  60, 128}},
    {{  0, 255, 150,   0,   0,   0, 2, 160,  80,  70, 120,   0, 128}},
    {{255,   0,   0,   0, 180,   0, 1, 100, 140, 120,  40,  10, 128}},
    {{255, 180,  60, 100,   0,   0, 1,  70, 120, 180, 130,  50, 128}},
    {{255, 200,   0, 255,   0,   0, 1,  30, 128, 200, 100,   0, 128}},
};

// FM SYSEX: A brightness, B envelope, C velocity (fm_sysex_voice.h). The named sounds are voices of
// Plaits' built-in banks with neutral names of our own (several stored names carry trademarks).
constexpr NamedPreset kFmSysexPresets[16] = {
    {"Solid Bass",     {128, 128, 180}}, {"Electric Piano", {128, 128, 160}},
    {"Grand Piano",    {128, 128, 170}}, {"Clavinet",       {128, 128, 180}},
    {"Harpsichord",    {128, 128, 170}}, {"Koto",           {128, 128, 170}},
    {"Marimba",        {128, 128, 170}}, {"Vibraphone",     {128, 128, 160}},
    {"Glockenspiel",   {128, 128, 160}}, {"Tubular Bells",  {128, 128, 170}},
    {"Pipe Organ",     {128, 128, 170}}, {"Ice Pad",        {128, 128, 150}},
    {"Soft Touch",     {128, 128, 150}}, {"String Section", {128, 128, 160}},
    {"Brass Section",  {128, 128, 180}}, {"Wine Glass",     {128, 128, 160}},
};
// Bank and voice of each named sound in Plaits' built-in banks (resources.cc, fm_patches_table).
constexpr std::array<int, 2> kFmSysexVoices[16] = {
    {{0, 0}}, {{1, 0}}, {{1, 9}}, {{1, 7}}, {{1, 14}}, {{1, 13}}, {{1, 17}}, {{1, 18}},
    {{1, 19}}, {{1, 22}}, {{2, 5}}, {{2, 10}}, {{2, 13}}, {{2, 23}}, {{2, 29}}, {{0, 23}},
};

// DUB SIREN: A rate, B depth, C sweep (dub_siren.h). Eight sounds of the shape the siren is known for
// and eight of our own; the names are neutral.
constexpr NamedPreset kDubSirenPresets[16] = {
    {"Sonar Ping",     { 40,  10, 128}}, {"Depth Charge",   { 70, 150,  30}},
    {"Hull Creak",     { 25,  60, 110}}, {"Periscope",      {110,  90, 170}},
    {"Ballast Drain",  { 60, 120,  10}}, {"Dive Alarm",     {170, 200, 128}},
    {"Cold Current",   { 35,  45, 150}}, {"Trench Wail",    { 90, 230,  95}},
    {"Kelp Warble",    {140, 110, 128}}, {"Pressure Siren", {120, 255, 190}},
    {"Abyss Pulse",    {200, 170, 128}}, {"Surface Call",   { 55, 140, 235}},
    {"Engine Room",    {230,  70, 128}}, {"Submerge",       { 45,  80,   0}},
    {"Radar Sweep",    {150,  30, 255}}, {"Last Bubble",    {185, 215,  60}},
};
// Each sound's own voice settings (parameter rows 2…8, 0..255), used while a parameter stays at
// Factory: wave (value/64), LFO shape (value/32), cutoff, resonance, sweep time, fine tune, ping.
constexpr std::array<int, 7> kDubSirenSounds[16] = {
    {{ 32,  16, 255,  40, 100, 128, 220}}, {{ 96,  48, 140,  90, 150, 128,   0}},
    {{ 32,  80, 100, 120, 200, 124,   0}}, {{ 32, 112, 230,  30, 120, 128, 120}},
    {{160,  16, 120, 100, 180, 128,   0}}, {{ 96,  16, 200, 140,  80, 128,   0}},
    {{ 32,  80, 110,  60, 210, 128,  60}}, {{160, 144, 190, 130, 140, 132,   0}},
    {{ 32, 144, 230,  50, 100, 128,   0}}, {{ 96, 208, 220,  90, 120, 128, 140}},
    {{ 96, 240, 255,  50,  90, 128,   0}}, {{ 32,  16, 255,  30, 160, 128, 255}},
    {{160,  48, 150, 150, 120, 128,   0}}, {{ 32,  16,  90, 110, 230, 128,   0}},
    {{ 32, 208, 255,  60,  60, 128, 180}}, {{ 32, 176, 245,  70, 110, 128,  80}},
};

// CROAKER: A croak rate, B throat, C dip (frog_voice.h). Sixteen animals and ponds.
constexpr NamedPreset kFrogVoicePresets[16] = {
    {"Pond Starter",  { 70, 128,  60}}, {"Big Ribbit",    { 60,  60, 110}},
    {"Tiny Peep",     {120, 190,  20}}, {"Night Chorus",  { 95, 140,  70}},
    {"Grumpy Toad",   { 45,  90, 140}}, {"Rain Frog",     {150, 170,  40}},
    {"Lily Pad",      { 55, 150,  30}}, {"Swamp Bass",    { 35,  30, 160}},
    {"Bubble Talk",   { 85, 110,  90}}, {"Jumpy",         {180, 150,  50}},
    {"Moon Croak",    { 40, 100, 180}}, {"Marsh Machine", {210,  80,  70}},
    {"Froggy Phone",  {100, 200, 120}}, {"Deep Pond",     { 30,  20,  90}},
    {"Tadpole",       {230, 220,  10}}, {"King of Frogs", { 65,  45, 200}},
};
// Each sound's own settings (rows 2…7, 0..255): species (value/64), pulses (1 + value/64), croak depth,
// throat, chirp, bubble.
constexpr std::array<int, 6> kFrogVoiceSounds[16] = {
    {{  32,  64, 200, 128,  80,  60}}, {{  96, 128, 230,  90, 120,  90}},
    {{ 224,   0, 160, 170,  60,   0}}, {{  32, 192, 210, 128,  70,  40}},
    {{ 160,  64, 240,  70, 140, 120}}, {{ 224,  64, 180, 160,  50,  20}},
    {{  32, 128, 170, 140,  40,  30}}, {{  96,   0, 255,  40, 160, 150}},
    {{ 160, 128, 190, 120, 100, 220}}, {{  32,  64, 220, 150, 130,  20}},
    {{  96,  64, 200, 100, 180,  80}}, {{ 160, 192, 255,  90,  60,  60}},
    {{ 224, 128, 150, 180,  90,  10}}, {{  96, 128, 230,  50,  70, 100}},
    {{ 224, 192, 120, 200,  30,   0}}, {{  96, 192, 255,  60, 200, 110}},
};

// FROGGY: A mouth, B breath, C throat (sing_frog.h). Sixteen singers.
constexpr NamedPreset kSingFrogPresets[16] = {
    {"Pond Choir",    {128,  20, 128}}, {"Lily Lead",     {150,  10, 150}},
    {"Deep Ribbit",   {110,  30,  60}}, {"Tadpole Pop",   {170,  40, 190}},
    {"Night Singer",  {120,  60, 110}}, {"Marsh Man",     {128,  15,  90}},
    {"Rain Diva",     {140,  25, 170}}, {"Croaky Bass",   {100,  50,  30}},
    {"Whisper Frog",  {128, 200, 128}}, {"Bell Frog",     {160,   5, 200}},
    {"Toad Tenor",    {130,  20,  80}}, {"Kero Lead",     {145,  15, 140}},
    {"Glass Voice",   {128,  35, 230}}, {"Mud Crooner",   { 95,  45,  50}},
    {"Moon Chant",    {115,  70, 120}}, {"Frog King",     {105,  20,  40}},
};
// Each singer's own settings (rows 3…8, 0..255): voice (value/64), croak, vibrato, glide, coda, syllable.
constexpr std::array<int, 6> kSingFrogSounds[16] = {
    {{  32, 100,  80,  85, 120,   0}}, {{  32,  60, 120,  60, 100,   0}},
    {{  32, 180,  40, 110, 140,   0}}, {{ 224,  60,  90,  40,  90,   0}},
    {{  32, 120, 100,  90, 150,   0}}, {{  96,  40,  90,  70, 130,   0}},
    {{ 160,  20, 150,  60, 110,   0}}, {{  32, 230,  20, 120, 160,   0}},
    {{  32,  80,  40,  90, 100,   0}}, {{ 160,  10, 130,  30,  60,   0}},
    {{  96,  50,  80,  80, 130,   0}}, {{ 160,  30,  60,  50,  90,   0}},
    {{ 224,  10, 160,  45,  80,   0}}, {{  96,  70,  60, 100, 150,   0}},
    {{  32,  90,  70, 100, 170,   0}}, {{  32, 200,  60,  95, 120,   0}},
};

// ACID BOX: A cutoff, B resonance, C env mod (bass_synths.h).
constexpr NamedPreset kAcidBoxPresets[16] = {
    {"Basic Line",    { 70, 150, 120}}, {"Squelch",       { 60, 210, 170}},
    {"Rubber",        { 90, 110, 140}}, {"Wobble Acid",   { 80, 190, 200}},
    {"Deep Acid",     { 40, 130, 110}}, {"Hard Accent",   {100, 170, 230}},
    {"Sweep Up",      { 30, 200, 255}}, {"Dusty Box",     { 75, 120,  80}},
    {"Liquid",        { 85, 160, 150}}, {"Screamer",      {120, 240, 190}},
    {"Short Tick",    { 95, 140, 180}}, {"Long Slide",    { 55, 180, 210}},
    {"Rave Stab",     {110, 200, 220}}, {"Mellow",        { 65,  90,  90}},
    {"Night Drive",   { 50, 170, 160}}, {"Chrome",        {130, 150, 200}},
};
// Each sound's own settings (rows 2…6, 0..255): wave (value/64), decay, accent, squash, sub.
constexpr std::array<int, 5> kAcidBoxSounds[16] = {
    {{ 32, 100,  60,  90,   0}}, {{ 32,  80, 180, 140,   0}}, {{ 96, 120,  60,  70,  60}}, {{ 32, 160, 120, 120,   0}},
    {{ 96, 140,  40,  60, 120}}, {{ 32,  60, 255, 170,   0}}, {{ 32, 180, 140, 110,   0}}, {{ 96,  90,  80, 100,  40}},
    {{ 32, 130, 100,  80,   0}}, {{ 32,  50, 220, 200,   0}}, {{ 96,  20, 150, 110,   0}}, {{ 32, 220, 120, 100,   0}},
    {{ 96,  40, 200, 150,   0}}, {{ 96, 150,  40,  50,  80}}, {{ 32, 170, 110,  90,  30}}, {{ 32,  70, 170, 160,   0}},
};
// REESE BASS: A detune, B cutoff, C movement (bass_synths.h).
constexpr NamedPreset kReeseBassPresets[16] = {
    {"Neuro Growl",   {160, 120, 180}}, {"Dark Rolling",  {110,  80, 120}},
    {"Detuned Sub",   {200,  60,  60}}, {"Wide Mono",     {230, 140,  80}},
    {"Moving Wall",   {140, 110, 230}}, {"Tight Reese",   { 90, 130,  40}},
    {"Phase Rider",   {180, 100, 200}}, {"Fat Saw",       {210, 150,  60}},
    {"Hollow Reese",  {120,  70, 160}}, {"Wobble Slow",   {150,  90, 255}},
    {"Rumble",        {100,  50,  90}}, {"Cavern",        {170,  60, 140}},
    {"Rusty",         {190, 170, 110}}, {"Steel Bass",    {130, 190,  50}},
    {"Slow Tide",     {145,  85, 210}}, {"Heavy Weather", {220, 100, 150}},
};
// Each sound's own settings (rows 2…6, 0..255): voices (value/128 + 2), sub, resonance, squash, drift speed.
constexpr std::array<int, 5> kReeseBassSounds[16] = {
    {{192, 128,  70, 140, 120}}, {{192, 170,  40,  90,  60}}, {{ 64, 200,  20,  60,  40}}, {{192, 110,  50, 100,  90}},
    {{192, 140,  90, 120, 130}}, {{ 64, 140,  60, 110,  50}}, {{192, 100, 110, 100, 150}}, {{192, 160,  30, 160,  70}},
    {{ 64,  90, 120,  80,  80}}, {{192, 150, 100, 130, 100}}, {{192, 220,  30, 120,  30}}, {{192, 120,  80,  70,  50}},
    {{192, 100,  60, 180, 110}}, {{ 64, 130, 140, 150,  90}}, {{192, 150,  60,  90,  40}}, {{192, 180,  50, 170, 100}},
};

const NamedPreset* named_preset(int model, int variation) {
    if (model == kAcidBoxModel && variation >= 0 && variation < 16) return &kAcidBoxPresets[variation];
    if (model == kReeseBassModel && variation >= 0 && variation < 16) return &kReeseBassPresets[variation];
    if (model == kSingFrogModel && variation >= 0 && variation < 16) return &kSingFrogPresets[variation];
    if (model == kFrogVoiceModel && variation >= 0 && variation < 16) return &kFrogVoicePresets[variation];
    if (model == kDubSirenModel && variation >= 0 && variation < 16) return &kDubSirenPresets[variation];
    if (model == kFmSysexModel && variation >= 0 && variation < 16) return &kFmSysexPresets[variation];
    if (model == kDcoPolyModel && variation >= 0 && variation < 16) return &kDcoPolyPresets[variation];
    if (model == kSidChipModel && variation >= 0 && variation < 16) return &kSidChipPresets[variation];
    if (model == kFormantChoirModel && variation >= 0 && variation < 16) return &kFormantChoirPresets[variation];
    if (model == kSpectralMorphModel && variation >= 0 && variation < 16) return &kSpectralMorphPresets[variation];
    if (model == kSpectralFreezeModel && variation >= 0 && variation < 16) return &kSpectralFreezePresets[variation];
    if (model == kSpectralAdditiveModel && variation >= 0 && variation < 16) return &kSpectralAdditivePresets[variation];
    static constexpr NamedPreset gravityExtras[4][4] = {
        {{"Cassette Drift",{121,168,112}},{"Octave Drop",{57,110,82}},{"Long Ascent",{160,240,28}},{"Jump Cut",{255,191,15}}},
        {{"Felt Ball",{94,152,46}},{"Fast Bounce",{235,182,28}},{"Slow Moon",{31,199,150}},{"Split Pin",{202,85,233}}},
        {{"Reel Bells",{87,58,160}},{"Bright Ellipse",{186,227,78}},{"Far Orbit",{42,176,197}},{"Broken Clock",{239,249,236}}},
        {{"Tape Rain",{97,115,166}},{"Heavy Drops",{215,126,55}},{"Thin Mist",{39,56,225}},{"Static Shower",{229,246,181}}},
    };
    static constexpr NamedPreset drumExtras[4] = {
        {"Tape Drum",{78,77,145}}, {"Hard Transient",{158,196,39}},
        {"Soft Room",{101,65,191}}, {"Digital Grit",{179,210,92}},
    };
    if (variation >= kLegacyNamedVariations) {
        const int extra = variation - kLegacyNamedVariations;
        if (model >= kOctaveOrbitModel && model < kGravityFirstModel + kGravityModelCount)
            return &gravityExtras[model-kOctaveOrbitModel][extra];
        if ((model >= 21 && model <= 23) || (model >= 57 && model <= 60))
            return &drumExtras[extra];
        return nullptr;
    }
    if (model == kOctaveOrbitModel) return &kOrbitPresets[variation];
    if (is_gravity_model(model)) return &kGravityPresets[model - kGravityFirstModel][variation];
    return nullptr;
}

constexpr const char* kGravityScaleNames[] = {"Minor pentatonic", "Major pentatonic", "Dorian", "Chromatic"};
constexpr int kGravityScaleValues[] = {0, 85, 170, 255};
int gravity_scale_index(int value) { return std::clamp(value, 0, 255) * 4 / 256; }
int gravity_default_scale(int model) {
    return model == kGravityFirstModel + 1 ? 2 : model == kGravityFirstModel + 2 ? 1 : 0;
}
bool factory_code_valid(int code) {
 if(code<0 || (code&(1<<30)))return false;
 if(code&extendedFlag)return (code&~(extendedFlag|0xfff))==0 && extended_model(code)<model_count() && (code&15)>=4 && (code&15)<variation_count();
 return code<model_count()*variations;
}
}
static const char* braidsNames[]={"B CSAW","B MORPH","B SAW SQUARE","B SINE TRIANGLE","B BUZZ","B SQUARE SUB","B SAW SUB","B SQUARE SYNC","B SAW SYNC","B TRIPLE SAW","B TRIPLE SQUARE","B TRIPLE TRIANGLE","B TRIPLE SINE","B TRIPLE RING MOD","B SAW SWARM","B SAW COMB","B TOY","B DIGITAL FILTER LP","B DIGITAL FILTER PK","B DIGITAL FILTER BP","B DIGITAL FILTER HP","B VOSIM","B VOWEL","B VOWEL FOF","B HARMONICS","B FM","B FEEDBACK FM","B CHAOTIC FEEDBACK FM","B PLUCKED","B BOWED","B BLOWN","B FLUTED","B STRUCK BELL","B STRUCK DRUM","B KICK","B CYMBAL","B SNARE","B WAVETABLES","B WAVE MAP","B WAVE LINE","B WAVE PARAPHONIC","B FILTERED NOISE","B TWIN PEAKS NOISE","B CLOCKED NOISE","B GRANULAR CLOUD","B PARTICLE NOISE","B DIGITAL MODULATION","B QUESTION MARK"};
int model_count(){return legacyModels + additionalModels;}
bool synth_needs_strike(int model){
 return (model>=2&&model<=4)||(model>=19&&model<=23)||model==52||(model>=56&&model<=60)||
        (model>=72&&model<=81)||(model>=82&&model<94&&(model-82)%4==2)||model==107;
}
const char* model_name(int m){
 if(m<0||m>=model_count())return "UNKNOWN";
 if(m>=legacyModels)return additionalNames[m-legacyModels];
 if(m<24)return plaitsNames[m];m-=24;
 if(m<braidsCount)return braidsNames[m];m-=braidsCount;
 if(m<6)return ringsNames[m];return elementsNames[m-6];
}
int macro_count(int model){
 if(model<0||model>=model_count())return 0;
 return model>=24&&model<24+braidsCount?2:3;
}
const char* macro_name(int model,int index){
 if(index<0||index>=macro_count(model))return "";
 // Names follow the parameter derivations in the corresponding Plaits engine*/..._engine.cc.
 // Several macros deliberately span two functions; both are named rather than implying an
 // independent control that the firmware does not expose.
 static const char* plaits[24][3]={
  {"Resonance / slope","Cutoff","Wave / sub"},
  {"Ratio","PD amount","Pulse width"},
  {"DX patch","Brightness","Envelope"},{"DX patch","Brightness","Envelope"},{"DX patch","Brightness","Envelope"},
  {"Terrain","Radius","Offset"},{"Chord","Filter / ensemble","Registration"},{"Chord","Arp / inversion","Waveform"},
  {"Detune","Square / sync","Saw shape"},{"Wave shape","Fold amount","Pulse / slope"},
  {"FM ratio","FM index","Feedback"},{"Ratio / bleed","Formant","Grain shape"},
  {"Spectral bumps","Centroid","Slope"},{"Table Z","Table X","Table Y"},
  {"Chord","Inversion","Registration"},{"Speech model","Formant shift","Vowel / word"},
  {"Spread","Density","Grain size"},{"Mode / cutoff","Clock","Resonance"},
  {"Spread","Density","Diffusion / Q"},{"Structure","Brightness","Decay"},
  {"Structure","Brightness","Damping"},{"FM / drive","Tone","Decay"},
  {"Snappy","Tone","Decay"},{"Noisiness","Tone","Decay"}
 };
 static const char* braids[]={"Timbre","Color"};
 static const char* rings[]={"Structure","Brightness","Damping"};
 static const char* elements[]={"Geometry","Brightness","Damping"};
 static const char* extras[][3]={
  {"Source color","Grain size","Spread / density"}, // Tape cloud
  {"Source color","Grain size","Spread / density"}, // Shimmer cloud
  {"Source color","Grain size","Spread / density"}, // Reverse dust
  {"Source color","Grain size","Spread / density"}, // Chord swarm
  {"Formant","Grain size","Pitch spread"},          // Formant grains: density follows the note
  {"Source color","Grain size","Spread / density"}, // Spray cloud
  {"Partial spread","Grain size","Spread / density"},
  {"Read position","Grain size","Gap / spread"},    // Stutter: C sets a gap, not overlap density
  {"Detune","Side level","Sub level"},
  {"Shape X","Shape Y","Phase blend"},
  {"Ratio","Index","Feedback"},
  {"Ring ratio 1","Ring ratio 2","Ring blend"},
  {"Rolloff","Even harmonics","Phase warp"},
  {"Decay","Brightness","Dispersion"},
  {"Bow speed","Friction","Brightness"},
  {"Formant 1","Formant 2","Upper formant"},
  {"Sync ratio","Wave blend","Pulse width"},
  {"Fold amount","Wave blend","Symmetry"},
  {"Glide rate","Octave spread","Root anchor"},
  {"Gravity","Rebound","Pitch spread"},
  {"Orbit speed","Eccentricity","Pitch spread"},
  {"Gravity","Rain density","Pitch spread"},
  {"Brightness","Spectral comb","Stretch"},         // SP ADDITIVE
  {"Position","Smear","Tilt"},                      // SP FREEZE
  {"Morph","Motion","Warp"},                        // SP MORPH
  {"Vowel","Ensemble","Breath"},                    // FORMANT CHOIR
  {"Colour","Arp","Cutoff"},                        // SID CHIP
  {"Cutoff","Resonance","Chorus"},                  // DCO POLY
  {"Brightness","Envelope","Velocity"},             // FM SYSEX
  {"LFO rate","LFO depth","Sweep"},                 // DUB SIREN
  {"Croak","Throat","Dip"},                         // CROAKER
  {"Mouth","Breath","Throat"},                      // FROGGY
  {"Cutoff","Resonance","Env mod"},                 // ACID BOX
  {"Detune","Cutoff","Movement"}                    // REESE BASS
 };
 if(model<24)return plaits[model][index];
 if(model<24+braidsCount){
  // Braids digital_oscillator.cc: parameter0 is FM depth, parameter1 modulator frequency.
  if(model>=49&&model<=51)return index==0?"FM index":"FM ratio";
  if(model==46)return index==0?"Vowel":"Formant shift";
  if(model==48)return index==0?"Harmonic peak":"Spectral width";
  if(model==52)return index==0?"Decay / loss":"Pick position";
  if(model==56)return index==0?"Partial balance":"Inharmonicity";
  if(model==57)return index==0?"Partial balance":"Noise / cutoff";
  if(model==58)return index==0?"Decay":"Tone";
  if(model==59)return index==0?"Cutoff":"Metal / noise";
  if(model==60)return index==0?"Tone balance":"Snappy / decay";
  if(model==61)return index==0?"Table position":"Table bank";
  if(model==62)return index==0?"Wave X":"Wave Y";
  if(model==63)return index==0?"Wave position":"Wave mix";
  if(model==64)return index==0?"Waveform":"Chord";
  return braids[index];
 }
 if(model<24+braidsCount+6)return rings[index];
 if(model==81){static const char* ominous[]={"Filter mode / Q","Cutoff","Damping"};return ominous[index];}
 if(model<legacyModels+elementsProfiles)return elements[index];
 return extras[model-legacyModels-elementsProfiles][index];
}
int variation_count(){return static_cast<int>(sizeof(variants)/sizeof(*variants));}
int preset_code(int model,int variant){
 if(model<0||model>=model_count()||variant<0||variant>=variation_count())return -1;
 return variant<variations?model*variations+variant:encode_extended(model,variant);
}
int preset_model(int code){return (code&kCustomFlag)?custom_model(code):(code&extendedFlag)?extended_model(code):code/variations;}
int preset_variation(int code){return (code&(1<<30))?0:(code&extendedFlag)?code&15:code%variations;}
std::array<int,3> preset_macros(int code){
 const int v=std::clamp(preset_variation(code),0,variation_count()-1);
 if(const auto* preset=named_preset(preset_model(code),v))return preset->macros;
 return {static_cast<int>(std::lround(macroA[v]*255)),static_cast<int>(std::lround(macroB[v]*255)),static_cast<int>(std::lround(macroC[v]*255))};
}
std::string preset_name(int code){
 if(!factory_code_valid(code))return "UNKNOWN";
 const int model=preset_model(code),variant=preset_variation(code);
 const auto* preset=named_preset(model,variant);
 const char* name=preset?preset->name:variants[variant];
 return std::string(model_name(model))+" / "+name;
}
std::string uri(int code){return "/@mutable/"+std::to_string(code)+".wav";}
int custom_code(int model,int a,int b,int c){return encode_custom(std::clamp(model,0,model_count()-1),std::clamp(a,0,255)/2,std::clamp(b,0,255)/2,std::clamp(c,0,255)/2);}
std::string custom_uri(int model,int a,int b,int c){return "/@mutable/p"+std::to_string(model)+"-"+std::to_string(a)+"-"+std::to_string(b)+"-"+std::to_string(c)+".wav";}
int model_id(int code){return preset_model(code);}
bool valid_code(int code){return code>=0&&(factory_code_valid(code)||((code&kCustomFlag)&&!(code&kExtendedFlag)&&model_id(code)<model_count()));}
namespace {
bool plaits_lpg(int model){return !(model>=2&&model<=4)&&model<19;}
bool elements_modal(int model){return model==78||(model>=82&&model<86);}
// Physical firmware parameter slot <-> the visible, contiguous per-model control array.
int elements_slot(int model,int row){return row<12?row:model==81?row+1:elements_modal(model)?row:row+2;}
int elements_row(int model,int slot){
 if(slot<12)return slot;
 if(model==81)return slot==12?-1:slot-1;
 if(elements_modal(model))return slot;
 return slot<14?-1:slot-2;
}
}

int synth_param_count(int model){
 if(model<0||model>=model_count())return 0;
 if(is_gravity_model(model))return 3;
 if(model==kSpectralFreezeModel)return 4;
 if(is_voice_model(model))return 6;
 if(model==kSidChipModel)return 8;
 if(model==kDcoPolyModel)return 15;
 if(model==kDubSirenModel)return 9;
 if(model==kFrogVoiceModel)return 8;
 if(model==kSingFrogModel)return 10;
 if(model==kAcidBoxModel||model==kReeseBassModel)return 7;
 if(model==kFmSysexModel)return 4;
 if(is_spectral_model(model))return 6;
 if(model<24)return plaits_lpg(model)?4:2;if(model<72)return 3;if(model<78)return model==76?4:3;
 if(model<94)return model==81?15:elements_modal(model)?16:14;if(model<102)return 5;return 2;
}
const char* synth_param_name(int model,int index){
 if(index<0||index>=synth_param_count(model))return "";
 static const char* plaits[]={"Decay","LPG colour","Aux mix","Accent"};
 static const char* braids[]={"Bit depth","Sample rate","Output tone"};
 static const char* rings[]={"Exciter position","Polyphony","Aux mix","Chord"};
 static const char* elements[]={"Bow level","Bow timbre","Blow level","Blow texture","Blow timbre","Strike level","Strike type","Strike timbre","Exciter position","Space","Exciter envelope","Excitation strength","Modulation rate","Modulation depth","Reverb diffusion","Reverb tone"};
 static const char* ominous[]={"Osc 2 detune","Feedback","Osc 1 level","Osc 1 ratio","Osc 1 FM index","Osc 2 level","Osc 2 ratio","Osc 2 FM index","Spatial position","Space","Envelope shape","Excitation strength","Filter separation","Reverb diffusion","Reverb tone"};
 static const char* granular[]={"Grain density","Pitch spread","Grain level","Source noise","Output tone"};
 static const char* original[]={"Output tone","Drive"};
 if(is_gravity_model(model)&&index==2)return "Pitch scale";
 if(model==kSpectralFreezeModel){static const char* freeze[]={"Output tone","Drive","Source slot","Jitter"};return freeze[index];}
 if(model==kSpectralMorphModel){static const char* morph[]={"Output tone","Drive","Shape A","Shape B","Partials","Motion rate"};return morph[index];}
 if(model==kFormantChoirModel){static const char* choir[]={"Output tone","Drive","Voice","Singers","Vibrato","Vibrato rate"};return choir[index];}
 if(model==kSidChipModel){static const char* sid[]={"Output tone","Drive","Wave","Chip","Filter","Resonance","PWM","Arp speed"};return sid[index];}
 if(model==kFmSysexModel){static const char* fm[]={"Output tone","Drive","Bank","Voice"};return fm[index];}
 if(model==kAcidBoxModel){static const char* acid[]={"Output tone","Drive","Wave","Decay","Accent","Squash","Sub"};return acid[index];}
 if(model==kReeseBassModel){static const char* reese[]={"Output tone","Drive","Voices","Sub","Resonance","Squash","Drift speed"};return reese[index];}
 if(model==kSingFrogModel){static const char* sing[]={"Output tone","Drive","Lyrics","Voice","Croak","Vibrato","Glide","Coda","Syllable","Pitch source"};return sing[index];}
 if(model==kFrogVoiceModel){static const char* frog[]={"Output tone","Drive","Species","Pulses","Croak depth","Throat","Chirp","Bubble"};return frog[index];}
 if(model==kDubSirenModel){static const char* dub[]={"Output tone","Drive","Wave","LFO shape","Cutoff","Resonance","Sweep time","Fine tune","Ping"};return dub[index];}
 if(model==kDcoPolyModel){static const char* dco[]={"Output tone","Drive","Saw","Pulse","Pulse width","PWM","Sub","Noise","HPF","Env amount","Env decay","Env sustain","LFO rate","Vibrato","Key follow"};return dco[index];}
 if(is_spectral_model(model)){static const char* spectral[]={"Output tone","Drive","Partials","Even harmonics","Formant","Shimmer"};return spectral[index];}
 if(model<24)return plaits[plaits_lpg(model)?index:index+2];
 if(model==81)return ominous[index];
 return model<72?braids[index]:model<78?rings[index]:model<94?elements[elements_slot(model,index)]:model<102?granular[index]:original[index];
}
int synth_param_default(int model,int index){
 if(index<0||index>=synth_param_count(model))return 0;
 if(is_gravity_model(model)&&index==2)return kGravityScaleValues[gravity_default_scale(model)];
 // SP FREEZE: tone, drive, source slot 00, a little jitter
 if(model==kSpectralFreezeModel){static constexpr int freeze[]={255,0,0,24};return freeze[index];}
 // SP MORPH: tone, drive, Vowel A → Vowel O (shown when no sound's own pair applies), 64 partials, ~0.4 Hz
 if(model==kSpectralMorphModel){static constexpr int morph[]={255,0,3*16+8,6*16+8,119,96};return morph[index];}
 // FORMANT CHOIR: tone, drive, Tenor (shown when no sound's own voice applies), 8 singers, 20 cents, 5.5 Hz
 if(model==kFormantChoirModel){static constexpr int choir[]={255,0,1*64+32,255,64,128};return choir[index];}
 // SID CHIP: tone, drive, Pulse, 8580, low pass, some resonance, no PWM, 1 frame per arp step
 // (wave, chip, filter, resonance, PWM and speed shown when no sound's own settings apply)
 if(model==kSidChipModel){static constexpr int sid[]={255,0,2*28+14,192,32,64,0,0};return sid[index];}
 // DCO POLY: tone, drive, a plain saw through the open-ish filter, HPF flat, a little envelope, key follow half
 // (shown when no sound's own settings apply)
 // FM SYSEX: tone, drive, built-in bank A, voice 1 (shown when no sound's own voice applies)
 if(model==kFmSysexModel){static constexpr int fm[]={255,0,fm_index_value(0),fm_index_value(0)};return fm[index];}
 if(model==kDcoPolyModel){static constexpr int dco[]={255,0,255,0,0,0,0,0,1*64+32,64,128,128,120,0,128};return dco[index];}
 // DUB SIREN: tone, drive, sine, triangle LFO, filter open, a little resonance, ~1.5 s sweep, in tune, no ping
 // CROAKER: tone, drive, tree frog, two pulses, deep croak, throat centred, a little chirp and bubble
 // FROGGY: tone, drive, frog voice, some croak, a little vibrato, medium glide, coda after ~0.55 s, next syllable
 // ACID BOX: tone, drive, saw, a medium decay, a little accent, some squash, no sub
 if(model==kAcidBoxModel){static constexpr int acid[]={255,0,32,100,40,90,0};return acid[index];}
 // REESE BASS: tone, drive, three voices, half sub, a little resonance, some squash, a slow drift
 if(model==kReeseBassModel){static constexpr int reese[]={255,0,192,128,40,90,80};return reese[index];}
 if(model==kSingFrogModel){static constexpr int sing[]={255,0,0,32,128,77,85,120,0,0};return sing[index];}
 if(model==kFrogVoiceModel){static constexpr int frog[]={255,0,32,64,204,128,77,51};return frog[index];}
 if(model==kDubSirenModel){static constexpr int dub[]={255,0,32,16,255,51,128,128,0};return dub[index];}
 // SP ADDITIVE: tone, drive, partials (119 → 64), even harmonics, formant (off), shimmer
 if(is_spectral_model(model)){static constexpr int spectral[]={255,0,119,255,0,48};return spectral[index];}
 const int plaits[]={204,128,0,255},braids[]={255,255,255},rings[]={77,0,0,0},granular[]={128,128,128,0,255},original[]={255,0};
 const int elements[]={0,128,0,128,128,166,128,128,77,77,128,179,23,51,159,179};
 if(model>=78&&model<94){
  index=elements_slot(model,index);
  if(model>=82){int articulation=(model-82)%4;
   if(index==0)return articulation==0?242:articulation==3?153:0;
   if(index==2)return articulation==1?230:articulation==3?115:0;
   if(index==5)return articulation==2?199:articulation==3?102:28;
   if(index==10)return articulation==2?64:219;
   if(index==11)return 204;
  }return elements[index];
 }
 return model<24?plaits[plaits_lpg(model)?index:index+2]:model<72?braids[index]:model<78?rings[index]:model<102?granular[index]:original[index];
}
namespace {
int braids_rate(int value){static constexpr int rates[]={4000,6000,8000,12000,16000,24000,32000,48000,96000};return rates[(std::clamp(value,0,255)*8+127)/255];}
int rings_polyphony(int value){return value<64?1:value<192?2:4;}
int choir_singers(int value){return 1+(std::clamp(value,0,255)*7+127)/255;}
int sid_wave(int value){return std::min(std::clamp(value,0,255)/28,SidChip::kWaveCount-1);}
}
int synth_param_adjust(int model,int index,int value,int direction,bool coarse){
 if(index<0||index>=synth_param_count(model))return -1;
 value=value<0?synth_param_default(model,index):std::clamp(value,0,255);
 const int sign=direction>0?1:direction<0?-1:0;
 if(is_gravity_model(model)&&index==2)return kGravityScaleValues[std::clamp(gravity_scale_index(value)+sign,0,3)];
 if(model>=24&&model<72&&index==0){const int bits=std::clamp(4+value*12/255+sign,4,16);return ((bits-4)*255+11)/12;}
 if(model>=24&&model<72&&index==1){const int step=std::clamp((value*8+127)/255+sign,0,8);return (step*255+4)/8;}
 if(model>=72&&model<78&&index==1){const int current=value<64?0:value<192?1:2;const int step=std::clamp(current+sign,0,2);return step==0?0:step==1?128:255;}
 if(model==76&&index==3){const int chord=std::clamp(value*10/255+sign,0,10);return (chord*255+9)/10;}
 // SP MORPH shapes: one press, one shape (value = shape·16 + 8)
 if(model==kSpectralMorphModel&&(index==2||index==3))return std::clamp(value/16+sign,0,SpectralMorph::kShapeCount-1)*16+8;
 // FORMANT CHOIR voice and singers: one press, one step
 if(model==kFormantChoirModel&&index==2)return std::clamp(value/64+sign,0,FormantChoir::kVoiceCount-1)*64+32;
 if(model==kFormantChoirModel&&index==3){const int n=std::clamp(choir_singers(value)+sign,1,FormantChoir::kMaxSingers);return ((n-1)*255+6)/7;}
 // SID CHIP wave, chip, filter and arp speed: one press, one step
 if(model==kSidChipModel&&index==2)return std::clamp(sid_wave(value)+sign,0,SidChip::kWaveCount-1)*28+14;
 if(model==kSidChipModel&&index==3)return std::clamp(value/128+sign,0,1)*128+64;
 if(model==kSidChipModel&&index==4)return std::clamp(value/64+sign,0,SidChip::kModeCount-1)*64+32;
 if(model==kSidChipModel&&index==7){const int n=std::clamp(choir_singers(value)+sign,1,8);return ((n-1)*255+6)/7;}
 // FM SYSEX bank and voice: one press, one step (the app limits the bank to what it found)
 if(model==kFmSysexModel&&index==kFmBankParam)return fm_index_value(std::clamp(fm_bank_index(value)+sign,0,kFmMaxBanks-1));
 if(model==kFmSysexModel&&index==kFmVoiceParam)return fm_index_value(std::clamp(fm_voice_index(value)+sign,0,fm_sysex::kVoicesPerBank-1));
 // DCO POLY HPF: one press, one position
 if(model==kDcoPolyModel&&index==8)return std::clamp(value/64+sign,0,DcoPoly::kHpfCount-1)*64+32;
 // DUB SIREN wave and LFO shape: one press, one step
 if(model==kAcidBoxModel&&index==2)return std::clamp(value/64+sign,0,AcidBox::kWaveCount-1)*64+32;
 if(model==kReeseBassModel&&index==2)return std::clamp(value/128+sign,0,1)*128+64;
 if(model==kSingFrogModel&&index==kSingVoiceParam)return std::clamp(value/64+sign,0,SingFrog::kVoiceCount-1)*64+32;
 if(model==kSingFrogModel&&index==kSingPitchParam)return std::clamp(value/128+sign,0,1)*128+64;
 if(model==kSingFrogModel&&index==kSingSyllableParam)return std::clamp(value/5+sign*(coarse?4:1),0,sing::kMaxSyllables)*5;
 if(model==kSingFrogModel&&index==kSingLyricParam)return value;
 if(model==kFrogVoiceModel&&index==2)return std::clamp(value/64+sign,0,FrogVoice::kSpeciesCount-1)*64+32;
 if(model==kFrogVoiceModel&&index==3)return std::clamp(value/64+sign,0,3)*64+32;
 if(model==kDubSirenModel&&index==2)return std::clamp(value/64+sign,0,DubSiren::kWaveCount-1)*64+32;
 if(model==kDubSirenModel&&index==3)return std::clamp(value/32+sign,0,DubSiren::kLfoCount-1)*32+16;
 return std::clamp(value+sign*(coarse?16:1),0,255);
}
namespace {
int spectral_partials(int value){return 8+std::clamp(value,0,255)*(SpectralAdditive::kMaxPartials-8)/255;}
int morph_shape(int value){return std::clamp(value,0,255)/16;}
float morph_rate_hz(int value){return .05f*std::exp2(6.32f*std::clamp(value,0,255)/255.f);}
float spectral_formant_hz(int value){return 200.f*std::exp2(6.3f*std::clamp(value,0,255)/255.f);}
int choir_voice(int value){return std::clamp(value,0,255)/64;}
float choir_vibrato_cents(int value){return 80.f*std::clamp(value,0,255)/255.f;}
float choir_vibrato_hz(int value){return 3.5f+4.f*std::clamp(value,0,255)/255.f;}
float dco_decay_seconds(int value){return .01f*std::exp2(8.6f*std::clamp(value,0,255)/255.f);}
float dco_lfo_hz(int value){return .1f*std::exp2(7.6f*std::clamp(value,0,255)/255.f);}
float dco_vibrato_cents(int value){return 60.f*std::clamp(value,0,255)/255.f;}
}
std::string synth_param_value(int model,int index,int value){
 if(is_gravity_model(model)&&index==2)return kGravityScaleNames[value<0?gravity_default_scale(model):gravity_scale_index(value)];
 if(value<0)return "Factory";
 value=std::clamp(value,0,255);char text[48];
 if(model>=24&&model<72&&index==0)std::snprintf(text,sizeof(text),"%d bit",4+value*12/255);
 else if(model>=24&&model<72&&index==1)std::snprintf(text,sizeof(text),"%.1f kHz",braids_rate(value)/1000.f);
 else if(model>=72&&model<78&&index==1)std::snprintf(text,sizeof(text),"%d voices",rings_polyphony(value));
 else if(model==76&&index==3)std::snprintf(text,sizeof(text),"Chord %d",1+value*10/255);
 else if(model==kSpectralFreezeModel&&index==kFreezeSourceParam)std::snprintf(text,sizeof(text),"Slot %02X",value);
 else if(model==kSpectralAdditiveModel&&index==2)std::snprintf(text,sizeof(text),"%d partials",spectral_partials(value));
 else if(model==kSpectralMorphModel&&(index==2||index==3))std::snprintf(text,sizeof(text),"%s",SpectralMorph::kShapeNames[morph_shape(value)]);
 else if(model==kSpectralMorphModel&&index==4)std::snprintf(text,sizeof(text),"%d partials",spectral_partials(value));
 else if(model==kSpectralMorphModel&&index==5)std::snprintf(text,sizeof(text),"%.2f Hz",morph_rate_hz(value));
 else if(model==kFormantChoirModel&&index==2)std::snprintf(text,sizeof(text),"%s",FormantChoir::kVoiceNames[choir_voice(value)]);
 else if(model==kFormantChoirModel&&index==3)std::snprintf(text,sizeof(text),choir_singers(value)==1?"%d singer":"%d singers",choir_singers(value));
 else if(model==kFormantChoirModel&&index==4)std::snprintf(text,sizeof(text),"%.0f cents",choir_vibrato_cents(value));
 else if(model==kFormantChoirModel&&index==5)std::snprintf(text,sizeof(text),"%.1f Hz",choir_vibrato_hz(value));
 else if(model==kSidChipModel&&index==2)std::snprintf(text,sizeof(text),"%s",SidChip::kWaveNames[sid_wave(value)]);
 else if(model==kSidChipModel&&index==3)std::snprintf(text,sizeof(text),"%s",value<128?"6581":"8580");
 else if(model==kSidChipModel&&index==4)std::snprintf(text,sizeof(text),"%s",SidChip::kModeNames[value/64]);
 else if(model==kFmSysexModel&&index==kFmBankParam&&fm_bank_index(value)<kFmFactoryBanks)std::snprintf(text,sizeof(text),"Built-in %c",'A'+fm_bank_index(value));
 else if(model==kFmSysexModel&&index==kFmBankParam)std::snprintf(text,sizeof(text),"Bank %d",fm_bank_index(value)-kFmFactoryBanks+1);
 else if(model==kFmSysexModel&&index==kFmVoiceParam)std::snprintf(text,sizeof(text),"Voice %d",fm_voice_index(value)+1);
 else if(model==kAcidBoxModel&&index==2)std::snprintf(text,sizeof(text),"%s",AcidBox::kWaveNames[std::min(value/64,AcidBox::kWaveCount-1)]);
 else if(model==kAcidBoxModel&&index==3)std::snprintf(text,sizeof(text),"%.2f s",AcidBox::decay_seconds(value/255.f));
 else if(model==kReeseBassModel&&index==2)std::snprintf(text,sizeof(text),"%d voices",value>=128?3:2);
 else if(model==kReeseBassModel&&index==6)std::snprintf(text,sizeof(text),"%.2f Hz",ReeseBass::drift_hz(value/255.f));
 else if(model==kSingFrogModel&&index==kSingVoiceParam)std::snprintf(text,sizeof(text),"%s",SingFrog::kVoiceNames[std::min(value/64,SingFrog::kVoiceCount-1)]);
 else if(model==kSingFrogModel&&index==5)std::snprintf(text,sizeof(text),"%.0f cents",45.f*value/255.f);
 else if(model==kSingFrogModel&&index==6)std::snprintf(text,sizeof(text),"%.0f ms",SingFrog::glide_ms(value/255.f));
 else if(model==kSingFrogModel&&index==7){const float ms=SingFrog::coda_ms(value/255.f);if(ms<=0.f)std::snprintf(text,sizeof(text),"Off");else std::snprintf(text,sizeof(text),"%.2f s",ms/1000.f);}
 else if(model==kSingFrogModel&&index==kSingSyllableParam){if(value<5)std::snprintf(text,sizeof(text),"Auto");else std::snprintf(text,sizeof(text),"No. %d",value/5);}
 else if(model==kSingFrogModel&&index==kSingPitchParam)std::snprintf(text,sizeof(text),"%s",value>=128?"Mic":"Note");
 else if(model==kSingFrogModel&&index==kSingLyricParam)std::snprintf(text,sizeof(text),"A+Right: type");
 else if(model==kFrogVoiceModel&&index==2)std::snprintf(text,sizeof(text),"%s",FrogVoice::kSpeciesNames[std::min(value/64,FrogVoice::kSpeciesCount-1)]);
 else if(model==kFrogVoiceModel&&index==3)std::snprintf(text,sizeof(text),value<64?"%d pulse":"%d pulses",1+std::min(value/64,3));
 else if(model==kDubSirenModel&&index==2)std::snprintf(text,sizeof(text),"%s",DubSiren::kWaveNames[std::min(value/64,DubSiren::kWaveCount-1)]);
 else if(model==kDubSirenModel&&index==3)std::snprintf(text,sizeof(text),"%s",DubSiren::kLfoNames[std::min(value/32,DubSiren::kLfoCount-1)]);
 else if(model==kDubSirenModel&&index==4)std::snprintf(text,sizeof(text),"%.0f Hz",DubSiren::cutoff_hz(value/255.f));
 else if(model==kDubSirenModel&&index==6)std::snprintf(text,sizeof(text),"%.2f s",DubSiren::sweep_seconds(value/255.f));
 else if(model==kDubSirenModel&&index==7)std::snprintf(text,sizeof(text),"%+.0f cents",(value-128)*50.f/128.f);
 else if(model==kDcoPolyModel&&index==8)std::snprintf(text,sizeof(text),"%s",DcoPoly::kHpfNames[value/64]);
 else if(model==kDcoPolyModel&&index==10)std::snprintf(text,sizeof(text),"%.2f s",dco_decay_seconds(value));
 else if(model==kDcoPolyModel&&index==12)std::snprintf(text,sizeof(text),"%.2f Hz",dco_lfo_hz(value));
 else if(model==kDcoPolyModel&&index==13)std::snprintf(text,sizeof(text),"%.0f cents",dco_vibrato_cents(value));
 else if(model==kSidChipModel&&index==7)std::snprintf(text,sizeof(text),choir_singers(value)==1?"%d frame":"%d frames",choir_singers(value));
 else if(model==kSpectralAdditiveModel&&index==4&&value==0)std::snprintf(text,sizeof(text),"Off");
 else if(model==kSpectralAdditiveModel&&index==4)std::snprintf(text,sizeof(text),"%.0f Hz",spectral_formant_hz(value));
 else if(model>=78&&model<94&&elements_slot(model,index)==12)std::snprintf(text,sizeof(text),"%.2f Hz",.05f+4.95f*value/255.f);
 else std::snprintf(text,sizeof(text),"%.0f %%",100.f*value/255.f);
 return text;
}
int uri_code(const char* p){
 int code=-1,n=0,model=0,a=0,b=0,c=0;
 if(p&&std::sscanf(p,"/@mutable/p%d-%d-%d-%d.wav%n",&model,&a,&b,&c,&n)==4&&n>0&&p[n]==0&&model>=0&&model<model_count()&&a>=0&&a<=255&&b>=0&&b<=255&&c>=0&&c<=255)return custom_code(model,a,b,c);
 n=0;if(p&&std::sscanf(p,"/@mutable/%d.wav%n",&code,&n)==1&&n>0&&p[n]==0&&factory_code_valid(code))return code;return -1;
}
struct Synth::State {
 plaits::Voice plaits{}; braids::MacroOscillator braids{};
 rings::Part rings{}; elements::Part elements{}; extra::Voice additional{}; SpectralAdditive spectral{}; SpectralFreeze freeze; SpectralMorph morph; FormantChoir choir; SidChip sid; DcoPoly dco; DubSiren dub; FrogVoice frog; SingFrog singer; AcidBox acid; ReeseBass reese; FmSysexVoice fm;
 uint8_t lyric[sing::kPackedSize]{},defaultLyric[sing::kPackedSize]{}; bool lyricSet=false; uint32_t noteIndex=0; float micSmooth=0.f;
 fm_sysex::PackedVoice fmVoice{}; bool fmVoiceSet=false;
 alignas(8) char memory[65536]{};
 uint16_t reverbR[32768]{},reverbE[32768]{};
 float out[48]{}, aux[48]{},zero[48]{};
 plaits::Voice::Frame frames[24]{};
 uint8_t sync[48]{}; int16_t shorts[48]{};
 NativeClock clock{24,0}; int size=24; bool strike=false; int triggerBlocks=0;
 SynthParams params=default_synth_params();
 float lastOutput=0,transitionFrom=0,tone=0,braidsHeld=0;
 int transitionRemaining=0;float braidsHold=0;
 int silentSamples=0;float lastRate=48000;
 float parameter(int index,float authored) const{return params[index]<0?authored:params[index]/255.f;}
 void elements_controls(elements::Patch* p,int model){
  const auto ep=[&](int slot,float value){const int row=elements_row(model,slot);return row<0?value:parameter(row,value);};
  p->exciter_bow_level=ep(0,p->exciter_bow_level);p->exciter_bow_timbre=ep(1,p->exciter_bow_timbre);
  p->exciter_blow_level=ep(2,p->exciter_blow_level);p->exciter_blow_meta=ep(3,p->exciter_blow_meta);p->exciter_blow_timbre=ep(4,p->exciter_blow_timbre);
  p->exciter_strike_level=ep(5,p->exciter_strike_level);p->exciter_strike_meta=ep(6,p->exciter_strike_meta);p->exciter_strike_timbre=ep(7,p->exciter_strike_timbre);
  p->resonator_position=ep(8,p->resonator_position);p->space=ep(9,p->space);p->exciter_envelope_shape=ep(10,p->exciter_envelope_shape);
  const int rateRow=elements_row(model,12);
  p->resonator_modulation_frequency=rateRow<0||params[rateRow]<0?.5f/32000:(.05f+4.95f*parameter(rateRow,0))/32000;
  p->resonator_modulation_offset=ep(13,.2f)*.5f;
  p->reverb_diffusion=ep(14,.625f);p->reverb_lp=ep(15,.7f);
 }
 // Braids' empty constructor leaves interpolation history (DigitalOscillator::pitch_ among it) as the
 // heap left it; the firmware ran from zeroed static storage, so start from that (PT-LOCAL-PATCHES.md).
 // In practice State is big enough to arrive as fresh zero pages; this keeps it so on any allocator.
 // Rings and Elements likewise: OminousVoice::Init leaves damping_ and feedback_ unset, and with a
 // reused heap block the Ominous model read them as garbage (ptperform-check, MallocPreScribble=1).
 State(){sing::pack_lyric("ribbit ribbit quack la la",defaultLyric);std::memset(static_cast<void*>(&braids),0,sizeof(braids));
  std::memset(static_cast<void*>(&rings),0,sizeof(rings));std::memset(static_cast<void*>(&elements),0,sizeof(elements));stmlib::BufferAllocator a(memory,sizeof(memory));plaits.Init(&a);braids.Init();rings.Init(reverbR);rings.set_polyphony(1);elements.Init(reverbE);}
 void fill(int code,float hz){
  const int model=model_id(code),var=code&(1<<30)?0:preset_variation(code);
  float a=macroA[var],b=macroB[var],c=macroC[var];
  if(code&kCustomFlag){a=custom_macro(code,0)/127.f;b=custom_macro(code,1)/127.f;c=custom_macro(code,2)/127.f;}
  else if(const auto* preset=named_preset(model,var)){const auto& m=preset->macros;a=m[0]/255.f;b=m[1]/255.f;c=m[2]/255.f;}
  float note=69.f+12.f*std::log2(std::max(8.f,hz)/440.f);
  if(model<24){
   const bool lpg=plaits_lpg(model);
   plaits::Patch p{};p.engine=model;p.note=note;p.harmonics=a;p.timbre=b;p.morph=c;p.decay=lpg?parameter(0,.8f):.8f;p.lpg_colour=lpg?parameter(1,.5f):.5f;
   plaits::Modulations m{};m.trigger=strike?1.f:0.f;// FM velocity LUT interpolates index+1: keep the endpoint below 1.
   m.level=std::min(parameter(lpg?3:1,1.f),std::nextafter(1.f,0.f));
   // Keep oscillator models sustained; struck engines receive their trigger pulse.
   m.trigger_patched=model>=19||(model>=2&&model<=4);m.level_patched=true;
   if(triggerBlocks>0){m.trigger=triggerBlocks==7?0.f:1.f;--triggerBlocks;}
   plaits.Render(p,m,frames,24);size=24;
   const float auxMix=parameter(lpg?2:0,0);
   for(int i=0;i<size;++i)out[i]=(frames[i].out*(1-auxMix)+frames[i].aux*auxMix)/32768.f;
  }else if(model<24+braidsCount){
   braids.set_shape(static_cast<braids::MacroOscillatorShape>(model-24));
   braids.set_pitch(static_cast<int16_t>(std::clamp(note*128.f,0.f,16383.f)));
   braids.set_parameters(static_cast<int16_t>(a*32767),static_cast<int16_t>(b*32767));
   if(strike)braids.Strike();braids.Render(sync,shorts,24);size=24;
   for(int i=0;i<size;++i){
    float sample=shorts[i]/32768.f;
    if(params[0]>=0){const float levels=std::exp2(float(3+params[0]*12/255));sample=std::round(sample*levels)/levels;}
    if(params[1]>=0){if(braidsHold<=0){braidsHeld=sample;braidsHold+=96000.f/braids_rate(params[1]);}braidsHold-=1;sample=braidsHeld;}
    out[i]=sample;
   }
  }else if(model<24+braidsCount+6){
   rings.set_model(static_cast<rings::ResonatorModel>(model-24-braidsCount));
   const int poly=params[1]<0?1:rings_polyphony(params[1]);if(rings.polyphony()!=poly)rings.set_polyphony(poly);
   rings::Patch p{};p.structure=a;p.brightness=b;p.damping=c;p.position=parameter(0,.3f);
   rings::PerformanceState v{};v.note=note;v.strum=strike;v.internal_exciter=true;v.chord=params[3]<0?0:params[3]*10/255;
   rings.Process(v,p,zero,out,aux,24);size=24;
   const float auxMix=parameter(2,0);for(int i=0;i<size;++i)out[i]=out[i]*(1-auxMix)+aux[i]*auxMix;
  }else if(model<legacyModels){
   int mode=model-24-braidsCount-6;
   elements.set_resonator_model(static_cast<elements::ResonatorModel>(std::min(mode,2)));elements.set_easter_egg(mode==3);
   auto p=elements.mutable_patch();p->resonator_geometry=a;p->resonator_brightness=b;p->resonator_damping=c;p->resonator_position=.3f;
   p->exciter_strike_level=.65f;p->exciter_strike_meta=.5f;p->exciter_strike_timbre=b;p->exciter_bow_level=var==3?.7f:0.f;p->exciter_bow_timbre=.5f;p->exciter_blow_level=var==1?.5f:0.f;p->exciter_blow_timbre=.5f;p->exciter_envelope_shape=.5f;p->space=.3f;
   elements_controls(p,model);
   elements::PerformanceState v{};v.note=note;v.gate=triggerBlocks>0?(triggerBlocks<7):(var==3);if(triggerBlocks>0)--triggerBlocks;v.strength=parameter(11,.7f);
   elements.Process(v,zero,zero,out,aux,16);size=16;
  }else if(model<legacyModels+elementsProfiles){
   // These are additional authored exciter articulations of the three real Elements
   // resonators, not additional Mutable firmware models. A=geometry/exciter, B=colour,
   // C=damping. Sustaining articulations keep the exciter's gate open; the instrument
   // envelope and note-off still control the host voice amplitude.
   const int profile=model-legacyModels,mode=profile/4,articulation=profile%4;
   elements.set_resonator_model(static_cast<elements::ResonatorModel>(mode));elements.set_easter_egg(false);
   auto p=elements.mutable_patch();
   p->resonator_geometry=.04f+.92f*a;p->resonator_brightness=.15f+.8f*b;
   p->resonator_damping=.08f+.88f*c;p->resonator_position=.15f+.65f*a;
   p->exciter_bow_level=articulation==0?.95f:articulation==3?.6f:0.f;
   p->exciter_bow_timbre=.2f+.7f*b;
   p->exciter_blow_level=articulation==1?.9f:articulation==3?.45f:0.f;
   p->exciter_blow_meta=.05f+.9f*a;p->exciter_blow_timbre=.25f+.7f*b;
   p->exciter_strike_level=articulation==2?.78f:articulation==3?.4f:.11f;
   p->exciter_strike_meta=articulation==2?(mode==1?.64f:.47f+.09f*a):.53f;
   p->exciter_strike_timbre=.2f+.75f*b;p->exciter_envelope_shape=articulation==2?.25f:.86f;
   p->space=.15f+.2f*c;
   elements_controls(p,model);
   elements::PerformanceState v{};v.note=note;v.strength=parameter(11,.8f);
   v.gate=triggerBlocks>0?(triggerBlocks<7):(articulation!=2);
   if(triggerBlocks>0)--triggerBlocks;
   elements.Process(v,zero,zero,out,aux,16);size=16;
   for(int i=0;i<size;++i)out[i]*=.7f;
  }else if(model==kSpectralFreezeModel){
   if(strike)freeze.start();
   const auto fp=[&](int index){return params[index]<0?synth_param_default(model,index):params[index];};
    freeze.render(out,24,hz,a,b,c,fp(3));size=24;
  }else if(model==kSpectralMorphModel){
   if(strike)morph.start();
   const auto mp=[&](int index){return params[index]<0?synth_param_default(model,index):params[index];};
   // Shapes at Factory: the sound's own pair (a custom macro patch uses the model defaults).
   const bool named=!(code&kCustomFlag)&&var>=0&&var<16;
   const int shapeA=params[2]>=0?morph_shape(params[2]):named?kSpectralMorphShapes[var][0]:morph_shape(mp(2));
   const int shapeB=params[3]>=0?morph_shape(params[3]):named?kSpectralMorphShapes[var][1]:morph_shape(mp(3));
   morph.render(out,24,hz,a,b,c,shapeA,shapeB,spectral_partials(mp(4)),mp(5));size=24;
  }else if(model==kFormantChoirModel){
   if(strike)choir.start();
   const auto cp=[&](int index){return params[index]<0?synth_param_default(model,index):params[index];};
   // Voice at Factory: the sound's own voice type (a custom macro patch uses the model default).
   const bool named=!(code&kCustomFlag)&&var>=0&&var<16;
   const int voice=params[2]>=0?choir_voice(params[2]):named?kFormantChoirVoices[var]:choir_voice(cp(2));
   choir.render(out,24,hz,a,b,c,voice,choir_singers(cp(3)),choir_vibrato_cents(cp(4)),choir_vibrato_hz(cp(5)));size=24;
  }else if(model==kFmSysexModel){
   if(strike)fm.start();
   if(fmVoiceSet)fm.set_voice(fmVoice);
   else{
    // No voice of its own: a built-in one — the bank and voice parameters, else the named sound's.
    const bool named=!(code&kCustomFlag)&&var>=0&&var<16;
    int bank=0,index=0;
    if(params[kFmBankParam]>=0||params[kFmVoiceParam]>=0||!named){
     bank=fm_bank_index(params[kFmBankParam]);index=fm_voice_index(params[kFmVoiceParam]);
     if(bank>=kFmFactoryBanks)bank=0;   // a file bank without its bytes: nothing to read, built-in A
    }else{bank=kFmSysexVoices[var][0];index=kFmSysexVoices[var][1];}
    fm_sysex::PackedVoice builtIn;
    std::memcpy(builtIn.data(),plaits::fm_patches_table[bank]+index*fm_sysex::kPackedSize,builtIn.size());
    fm.set_voice(builtIn);
   }
   fm.render(out,24,hz,a,b,c);size=24;
  }else if(model==kAcidBoxModel){
   if(strike)acid.start();
   const bool named=!(code&kCustomFlag)&&var>=0&&var<16;
   const auto ap=[&](int index){
    if(params[index]>=0)return params[index];
    if(named)return kAcidBoxSounds[var][size_t(index-2)];
    return synth_param_default(model,index);
   };
   AcidBox::Settings set;
   set.wave=std::clamp(ap(2)/64,0,AcidBox::kWaveCount-1);set.decay=ap(3)/255.f;set.accent=ap(4)/255.f;set.drive=ap(5)/255.f;set.sub=ap(6)/255.f;
   acid.render(out,24,hz,a,b,c,set);size=24;
  }else if(model==kReeseBassModel){
   if(strike)reese.start();
   const bool named=!(code&kCustomFlag)&&var>=0&&var<16;
   const auto rp=[&](int index){
    if(params[index]>=0)return params[index];
    if(named)return kReeseBassSounds[var][size_t(index-2)];
    return synth_param_default(model,index);
   };
   ReeseBass::Settings set;
   set.voices=rp(2)>=128?3:2;set.sub=rp(3)/255.f;set.resonance=rp(4)/255.f;set.drive=rp(5)/255.f;set.drift=rp(6)/255.f;
   reese.render(out,24,hz,a,b,c,set);size=24;
  }else if(model==kSingFrogModel){
   const bool named=!(code&kCustomFlag)&&var>=0&&var<16;
   const auto sp=[&](int index){
    if(params[index]>=0)return params[index];
    if(named)return kSingFrogSounds[var][size_t(index-3)];
    return synth_param_default(model,index);
   };
   SingFrog::Settings set;
   set.voice=std::clamp(sp(3)/64,0,SingFrog::kVoiceCount-1);set.croak=sp(4)/255.f;set.vibrato=sp(5)/255.f;
   set.glideMs=SingFrog::glide_ms(sp(6)/255.f);set.codaMs=SingFrog::coda_ms(sp(7)/255.f);
   if(strike){
    const uint8_t* packed=lyricSet?lyric:defaultLyric;const int count=packed[0];
    sing::Syllable syl{};
    if(count>0){
     const int fixed=sp(kSingSyllableParam)/5;   // 0 = auto
     const int pick=fixed>0?(fixed-1)%count:int(noteIndex%uint32_t(count));
     std::memcpy(&syl,packed+1+pick*sing::kSyllableBytes,sizeof syl);
    }
    singer.start(syl,set);micSmooth=0.f;
   }
   // Pitch source Mic: the voice sings the pitch the microphone hears (while the recorder is listening); when nothing
   // is heard it holds the last pitch, and before any it uses the note.
   float singHz=hz;
   if(params[kSingPitchParam]>=128){
    const float heard=mic_pitch_read();
    if(heard>=60.f&&heard<=1200.f)micSmooth=micSmooth>0.f?micSmooth+(heard-micSmooth)*.35f:heard;
    if(micSmooth>0.f)singHz=micSmooth;
   }
   singer.render(out,24,singHz,a,b,c,set);size=24;
  }else if(model==kFrogVoiceModel){
   if(strike)frog.start();
   const bool named=!(code&kCustomFlag)&&var>=0&&var<16;
   const auto fp=[&](int index){
    if(params[index]>=0)return params[index];
    if(named)return kFrogVoiceSounds[var][size_t(index-2)];
    return synth_param_default(model,index);
   };
   FrogVoice::Settings set;
   set.species=std::clamp(fp(2)/64,0,FrogVoice::kSpeciesCount-1);set.pulses=1+std::clamp(fp(3)/64,0,3);
   set.depth=fp(4)/255.f;set.throat=fp(5)/255.f;set.chirp=fp(6)/255.f;set.bubble=fp(7)/255.f;
   frog.render(out,24,hz,a,b,c,set);size=24;
  }else if(model==kDubSirenModel){
   if(strike)dub.start();
   // Settings at Factory: the sound's own (a custom macro patch uses the model defaults).
   const bool named=!(code&kCustomFlag)&&var>=0&&var<16;
   const auto up=[&](int index){
    if(params[index]>=0)return params[index];
    if(named)return kDubSirenSounds[var][size_t(index-2)];
    return synth_param_default(model,index);
   };
   DubSiren::Settings set;
   set.wave=std::clamp(up(2)/64,0,DubSiren::kWaveCount-1);set.lfoShape=std::clamp(up(3)/32,0,DubSiren::kLfoCount-1);
   set.cutoff=up(4)/255.f;set.resonance=up(5)/255.f;set.sweepSeconds=DubSiren::sweep_seconds(up(6)/255.f);
   set.fineCents=(up(7)-128)*50.f/128.f;set.ping=up(8)/255.f;
   dub.render(out,24,hz,a,b,c,set);size=24;
  }else if(model==kDcoPolyModel){
   if(strike)dco.start();
   // Settings at Factory: the sound's own (a custom macro patch uses the model defaults).
   const bool named=!(code&kCustomFlag)&&var>=0&&var<16;
   const auto dp=[&](int index){
    if(params[index]>=0)return params[index];
    if(named)return index==8?kDcoPolySounds[var][6]*64+32:kDcoPolySounds[var][size_t(index-2)];
    return synth_param_default(model,index);
   };
   DcoPoly::Settings set;
   set.saw=dp(2)/255.f;set.pulse=dp(3)/255.f;set.width=dp(4)/255.f;set.pwm=dp(5)/255.f;set.sub=dp(6)/255.f;set.noise=dp(7)/255.f;
   set.hpf=std::clamp(dp(8)/64,0,DcoPoly::kHpfCount-1);set.envAmount=dp(9)/255.f;set.decaySeconds=dco_decay_seconds(dp(10));
   set.sustain=dp(11)/255.f;set.lfoHz=dco_lfo_hz(dp(12));set.vibratoCents=dco_vibrato_cents(dp(13));set.keyFollow=dp(14)/255.f;
   dco.render(out,24,hz,a,b,c,set);size=24;
  }else if(model==kSidChipModel){
   if(strike)sid.start();
   const auto sp=[&](int index){return params[index]<0?synth_param_default(model,index):params[index];};
   // Settings at Factory: the sound's own (a custom macro patch uses the model defaults).
   const bool named=!(code&kCustomFlag)&&var>=0&&var<16;
   const SidSound* own=named?&kSidChipSounds[var]:nullptr;
   const int wave=params[2]>=0||!own?sid_wave(sp(2)):own->wave;
   const int chip=params[3]>=0||!own?sp(3)/128:own->chip;
   const int mode=params[4]>=0||!own?sp(4)/64:own->mode;
   const float resonance=(params[5]>=0||!own?sp(5):own->resonance)/255.f;
   const float pwm=(params[6]>=0||!own?sp(6):own->pwm)/255.f;
   const int speed=params[7]>=0||!own?choir_singers(sp(7)):own->speed;
   sid.render(out,24,hz,a,b,c,wave,chip,mode,resonance,pwm,speed);size=24;
  }else if(is_spectral_model(model)){
   if(strike)spectral.start();
   const auto sp=[&](int index){return params[index]<0?synth_param_default(model,index):params[index];};
   spectral.render(out,24,hz,a,b,c,spectral_partials(sp(2)),sp(3),sp(4),sp(5));size=24;
  }else{
   const int extraModel=model-legacyModels-elementsProfiles;
   if(strike)additional.start(extraModel,a,b,c);
   additional.update(a,b,c,params);
   if(is_gravity_model(model))additional.set_gravity_scale(params[2]<0?gravity_default_scale(model):gravity_scale_index(params[2]));
   size=24;for(int i=0;i<size;++i)out[i]=additional.next(hz,a,b,c);
  }
  const int toneIndex=model>=24&&model<72?2:model>=94&&model<102?4:model>=102?0:-1;
  if(toneIndex>=0&&params[toneIndex]>=0&&params[toneIndex]<255){
   const float nativeRate=model<72?96000.f:48000.f;
   const float cutoff=120.f*std::pow((nativeRate*.45f)/120.f,parameter(toneIndex,1));
   const float coefficient=1-std::exp(-6.283185307f*cutoff/nativeRate);
   for(int i=0;i<size;++i){tone+=coefficient*(out[i]-tone);out[i]=tone;}
  }else tone=out[size-1];
  if(model>=102&&params[1]>0){const float drive=1+7*parameter(1,0);for(int i=0;i<size;++i)out[i]=std::tanh(out[i]*drive)/std::tanh(drive);}
  strike=false;
 }
};
Synth::Synth():s_(new State){}
Synth::~Synth()=default;
void Synth::reset(){s_->~State();new(s_.get()) State;code_=-1;}
void Synth::start(int code){code_=valid_code(code)?code:-1;s_->clock.restart(s_->size);std::fill(std::begin(s_->out),std::end(s_->out),0.f);s_->strike=true;s_->triggerBlocks=7;s_->transitionRemaining=0;s_->lastOutput=0;s_->silentSamples=0;s_->params=default_synth_params();}
void Synth::update(int code,const SynthParams& params){
 if(!valid_code(code))return;
 if(code!=code_){
  if(code_<0||model_id(code)!=model_id(code_)){const float previous=s_->lastOutput;start(code);s_->transitionFrom=previous;s_->transitionRemaining=96;}
  else code_=code;
 }
 for(int i=0;i<kSynthParams;++i)s_->params[i]=std::clamp(params[i],-1,255);
}
std::array<int,2> fm_preset_voice(int variation){
 return variation>=0&&variation<16?kFmSysexVoices[variation]:std::array<int,2>{{0,0}};
}
void fm_builtin_voice(int bank,int voice,uint8_t* out){
 std::memcpy(out,plaits::fm_patches_table[std::clamp(bank,0,kFmFactoryBanks-1)]+std::clamp(voice,0,31)*fm_sysex::kPackedSize,fm_sysex::kPackedSize);
}
void Synth::set_lyric(const uint8_t* packed){
 if(!packed){s_->lyricSet=false;return;}
 std::memcpy(s_->lyric,packed,sizeof(s_->lyric));
 if(s_->lyric[0]>sing::kMaxSyllables)s_->lyric[0]=sing::kMaxSyllables;
 s_->lyricSet=true;
}
void Synth::set_note_index(uint32_t index){s_->noteIndex=index;}
void Synth::set_fm_voice(const uint8_t* packed){
 if(!packed){s_->fmVoiceSet=false;return;}
 std::memcpy(s_->fmVoice.data(),packed,s_->fmVoice.size());
 s_->fmVoiceSet=true;
}
Synth::SourceLoan::SourceLoan(Synth& synth,const float* data,int length,uint32_t generation,int sampleRate):synth_(synth){synth_.s_->freeze.set_source(data,length,generation,sampleRate);}
Synth::SourceLoan::~SourceLoan(){synth_.s_->freeze.release();}
float Synth::next(float hz,float sampleRate){
 if(code_<0||!std::isfinite(hz)||hz<=0||!std::isfinite(sampleRate)||sampleRate<8000)return 0;
 const int model=model_id(code_);
 const double rate=model<24?48000:(model<24+braidsCount?96000:(model<24+braidsCount+6?48000:(model<legacyModels+elementsProfiles?32000:48000)));
 // Native clock conversion (native_clock.h): interpolation across block boundaries, box-filtered decimation.
 const double advance=rate/std::max(8000.f,sampleRate);
 const float value=s_->clock.next(s_->out,s_->size,advance,[&]{s_->fill(code_,hz);});
 float output=std::isfinite(value)?std::clamp(value,-2.f,2.f):0.f;
 if(s_->transitionRemaining>0){const float mix=(97-s_->transitionRemaining)/96.f;output=s_->transitionFrom*(1-mix)+output*mix;--s_->transitionRemaining;}
 s_->silentSamples=std::fabs(output)<1e-4f?std::min(s_->silentSamples+1,1<<24):0;s_->lastRate=sampleRate;
 s_->lastOutput=output;return output;
}
bool Synth::decayed() const{
 return code_>=0&&synth_needs_strike(model_id(code_))&&s_->transitionRemaining==0&&
        s_->silentSamples>=static_cast<int>(kDecayedSeconds*s_->lastRate);
}
}
