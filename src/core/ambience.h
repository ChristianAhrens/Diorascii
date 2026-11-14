/// @file core/ambience.h
/// @brief Procedurally synthesized ambient sound for a scene.

#pragma once
#include <cstdint>

#include "core/layout.h"
#include "core/rng.h"
#include "core/spatializer.h"
#include "core/soundstage.h"

namespace dio {

class World;

/// What a scene sounds like.
enum class AmbientKind { Silent, Forest, Aquarium, Desert, Moon, Mountains, City };
/// Number of AmbientKind values.
inline constexpr int kAmbientKinds = 7;

/// What the sound should reflect right now. All levels run from 0 to 1.
/// The audio backends hand the whole struct to the audio thread as one copy (core/seqlock.h), so a
/// new field needs no change in them. It must stay trivially copyable (plain numbers and enums only).
struct AmbientParams {
  AmbientKind kind = AmbientKind::Silent;
  float night = 0;   ///< how dark it is
  float rain = 0;    ///< how hard it rains (or a sandstorm blows)
  float volume = 1;  ///< master volume; 0 mutes
  /// Counts lightning flashes (World::flashCount()). Every increase makes thunder roll in
  /// a moment later, as sound travels slower than light.
  unsigned flashes = 0;
  /// How near a thundercloud is, from 0 to 1 (World::stormProximity()): distant rumbling and
  /// far-off claps start as it moves in, before the first strike.
  float storm = 0;
  /// Counts fish nibbling at food (World::foodEatenCount()); each increase is a small blip.
  unsigned nibbles = 0;
  /// How near a whale is, from 0 to 1 (World::nearness("whale")): it sings as it passes.
  float whale = 0;
  /// How near a tornado is, from 0 to 1 (World::nearness("tornado")).
  float tornado = 0;
  /// Counts meteorite strikes (World::impactCount()); each one thuds through the ground.
  unsigned impacts = 0;
  /// How loud an avalanche is, from 0 to 1 (World::avalancheLevel()): it cracks, rumbles and roars.
  float avalanche = 0;
  /// How near a sandworm is, from 0 to 1 (World::nearness("sandworm")): it bellows as it crosses.
  float worm = 0;
  /// Counts things crushed by the sandworm or the kraken (World::crushCount()); in the desert each
  /// one is chewed, in the aquarium it is gulped down.
  unsigned crushes = 0;
  /// How near a siren is, from 0 to 1 (World::nearness("siren"): the ambulance, the fire engine).
  float siren = 0;
  /// How near a fire engine with its siren on is, from 0 to 1 (World::nearness("wail")): a slow, wailing
  /// siren, unlike the ambulance's quick two-tone one.
  float wail = 0;
  /// How near a train is, from 0 to 1 (World::nearness("train")): it rumbles, clatters and sounds its horn.
  float train = 0;
  /// How fast that train is rolling, from 0 (standing at the station) to 1 (cruising speed): the
  /// rumble and the clatter of the wheels follow it and stop when the train does (World::trainSound()).
  float trainMotion = 1;
  /// How hard that train is braking, from 0 to 1: its brakes squeal as it rolls into the station.
  float trainBrake = 0;
  /// The hiss of the compressed air that the train lets out as it comes to a stop, from 0 to 1.
  float trainHiss = 0;
  /// How near a plane is, from 0 to 1 (World::nearness("plane")): a faint, distant jet.
  float plane = 0;
  /// How fiercely a building burns, from 0 to 1 (World::fireLevel()): it crackles and roars.
  float fire = 0;
  /// Counts animals buried by an avalanche (World::squealCount()); each one is a muffled squeal.
  unsigned squeals = 0;
  /// How strong the scene's second catastrophe is, from 0 to 1 (the largest of World::toxinLevel(),
  /// floodLevel(), rotorLevel(), yetiLevel(), titanLevel() and asteroidLevel(); only the current scene's is not
  /// zero): the leaking submarine's hissing gas, the flood's rush, the poachers' helicopter, the
  /// yeti's roars and stamping, the monsters' roars and stamping, the rumble of the big meteor coming down.
  float menace = 0;
  /// How strong the monsters' streams of fire are, from 0 to 1 (World::flameLevel()): a roaring whoosh.
  float flames = 0;
  /// How near the winged monster is, from 0 to 1 (World::nearness("winged_titan")): only while it is
  /// alive does its shrill screech sound.
  float winged = 0;
  /// Counts the poachers' rifle shots (World::shotCount()); each one cracks.
  unsigned shots = 0;
  /// Where each continuous sound source stands (World::emitter): its level is the matching field above
  /// (`whale`, `tornado`, ...) and its x, y and depth place it on the stage. Indexed by EmitterKey. Not
  /// yet used by the synth (the scenes switch to it in phase 6 of the multichannel plan).
  Emitter emitters[kEmitterKeys];
};

/// Derives the sound parameters from the state of a world (volume stays at 1).
AmbientParams ambientFor(const World& world);

/// Generates ambient sound: wind, rain, thunder and crickets at night in the forest, the deep
/// hum, bubbles and passing whales of the aquarium, the dry wind, sandstorms and tornadoes of
/// the desert, the wind, stream and avalanches of the mountains. Pure
/// computation without I/O, deterministic per seed, so it is testable without a sound device.
/// Parameter changes are smoothed, so switching scenes or muting never clicks.
class Ambience {
 public:
  /// @param sampleRate output rate in Hz
  /// @param seed seeds the noise
  /// @param layout the speakers to render for (stereo by default)
  explicit Ambience(int sampleRate, uint64_t seed = 1, const Layout& layout = Layout());
  /// Writes `frames` frames of interleaved float samples, `layout().channels()` per frame, each
  /// within -1 to 1. The continuous sounds (the bed) play in every full-range speaker with
  /// independent noise per speaker; the discrete voices are still a stereo pair (until phases 5 and
  /// 6 of the multichannel plan place them) that plays on the layout's front left and right speakers
  /// (`Layout::stereoPair`); the LFE channel plays the low end of the beds. With the stereo layout,
  /// and on the front pair of any layout, the output is exactly what the synth has always produced.
  void render(const AmbientParams& p, float* out, int frames);
  /// Changes the layout between two render calls (a route change). Nothing in the synth is reset:
  /// channels that keep their role play on without a break, new ones fade in over about 20 ms.
  void setLayout(const Layout& layout);
  /// The layout the synth renders for.
  const Layout& layout() const { return layout_; }
  /// Hands the synth a one-off sound with its place (see SoundEvent). The events wait until the next
  /// render call; at most kMaxPendingEvents are kept (the newest), and they are dropped after that
  /// call. Not used by any sound yet (the scenes switch to it in phase 6 of the multichannel plan);
  /// the counters in AmbientParams still trigger them.
  void event(const SoundEvent& e);
  /// The events handed in since the last render call (for the sounds and for tests).
  int pendingEvents() const { return pendingCount_; }
  /// The most events kept between two render calls.
  static constexpr int kMaxPendingEvents = 32;

 private:
  /// Moves `v` towards `target`; `k` is the per-sample smoothing coefficient.
  static void approach(float& v, float target, float k) { v += (target - v) * k; }
  /// One-pole smoothing coefficient for a time constant in seconds.
  float coeff(float seconds) const;

  /// One clap of thunder: when it starts, how loud it is, how far its rumbling swells reach.
  struct Thunder {
    double start = 0, gain = 0;
    bool strike = false;  ///< lightning overhead: adds a sharp, crackling snap
    double swellAt[3] = {}, swellGain[3] = {};
  };
  /// Schedules a clap `delay` seconds from now with the given loudness (0 to 1).
  void clap(double delay, double gain, bool strike = false);
  /// A rising bubble blip, `delay` seconds from now. `f0` is its starting pitch in Hz.
  void bubble(double delay, float f0, float dur, float amp);
  /// One sample of the aquarium's sound: `out[c]` for each bed chain `c` below `chains_` (the others stay as they were).
  void aquariumSample(const AmbientParams& p, float out[kMaxChannels]);
  /// One sample of the desert's sound, per bed chain as for the aquarium.
  void desertSample(const AmbientParams& p, float out[kMaxChannels]);
  /// One sample of the moon's sound, per bed chain as for the aquarium.
  void moonSample(const AmbientParams& p, float out[kMaxChannels]);
  /// One sample of the city's sound, per bed chain as for the aquarium.
  void citySample(const AmbientParams& p, float out[kMaxChannels]);
  /// One sample of the mountains' sound, per bed chain as for the aquarium.
  void mountainSample(const AmbientParams& p, float out[kMaxChannels]);
  /// One sample of the second catastrophe of the current scene, per bed chain (silent without one).
  void menaceSample(const AmbientParams& p, float out[kMaxChannels]);
  /// Thunder envelopes at the current time, summed over all claps: a sharp crack and a rumble.
  void thunderLevels(float& crack, float& rumble, float& snap) const;

  /// Chooses which bed chain each output channel plays, derives the per-chain levels of every
  /// scene, and glides to the new choice.
  void route();
  /// Recomputes the slowly drifting level of every bed chain of every scene (called every few samples).
  void updateDrift();
  /// Where the placed sounds are (see Place): the ids of the sources of the city.
  enum PlaceId : int {
    kEngine0 = 0,                 ///< the four passing cars (kEngine0 + slot)
    kHorn = 4,                    ///< a car horn
    kSiren, kWail, kTrain,        ///< the ambulance, the fire engine, the train
    kPlane, kFire, kFlames,       ///< the jet, the burning building, the streams of fire
    kTitan, kWinged,              ///< the monsters' bodies and the winged one's screech
    kPlaceCount
  };
  /// A sound source placed in the room: its place (a direction, spread and bass send), the smoothed
  /// gains towards every speaker, and whether it is sounding.
  struct Place {
    SpatialVoice v;      ///< the gains and the bass send
    Source src;          ///< where it should be
    bool on = false;     ///< sounding: the gains are stepped every sample and the sound is added
  };
  /// Points a place at a direction and marks it as sounding (a place that was silent starts in
  /// position instead of gliding in).
  void aim(int id, const Source& src);
  /// `src` as it is used in the current layout: a layout without a bass channel gets no bass send, because
  /// the full-range speakers already play the whole sound (the spatializer's fold-in would double its bass).
  Source forLayout(Source src) const {
    if (layout_.lfeIndex() < 0) src.lfeSend = 0;
    return src;
  }
  /// Steps the gains of every sounding place by one sample.
  void stepPlaces();
  /// The weight of place `id` in bed chain `chain`: its gain towards the chain's speaker, scaled so
  /// that a source straight ahead in stereo is 1, the level the synth has always had for mono sounds
  /// (the sum of the squares of the weights over all chains is 2 in any layout). 0 if it is off.
  float weight(int id, int chain) const;
  /// Adds the sounds placed into `room_` to the chain bus `bus` and their bass send, times `bassScale`, to
  /// `bassExtra_`; then clears the room.
  void flushRoom(float bus[kMaxChannels], float bassScale);
  /// The direction of a stage position: the screen edges are +-45 degrees, +-1.5 is 110 degrees, 0 to 1 high is 0 to 60 degrees up.
  static Source stageSource(float x, float y, float spread, float lfeSend);
  /// Aims the city's places at the emitters of `p` (called once per render block).
  void aimCity(const AmbientParams& p);

  /// The random generator of bed chain `chain`: chains 0 and 1 (the stereo pair) draw from the shared
  /// generator, as the synth always has; the others have their own, so that the stereo sound and
  /// the timing of events are untouched by the layout.
  Rng& chainRng(int chain) { return chain < 2 ? rng_ : bedRng_[chain]; }
  /// True if bed chain `chain` is audible in the scene `kind` (its level there is not zero).
  bool live(AmbientKind kind, int chain) const { return (liveMask_[static_cast<int>(kind)] >> chain) & 1u; }

  /// Where an output channel gets its sound from: a bed chain (0 up to kMaxChannels - 1) or one of these.
  enum Feed : int8_t {
    kSilent = -1,  ///< nothing
    kBoth = -2,    ///< the mean of chains 0 and 1 (a layout with a single speaker)
    kBass = -3     ///< the low-frequency channel: the low-passed sum of all chains
  };

  Layout layout_;                               ///< the speakers to render for
  int8_t feed_[kMaxChannels];                   ///< what each output channel plays (a Feed or a chain index)
  int speakerOf_[kMaxChannels];                 ///< the output channel that each bed chain plays on (-1 if none)
  Place place_[kPlaceCount];                    ///< the placed sources
  float room_[kMaxChannels] = {};               ///< what the placed sounds add in this sample, by output channel
  float voiceOut_[kMaxChannels] = {};           ///< the placed sounds of the scenes in this sample, by chain (scaled by the scene's presence)
  float menaceBus_[kMaxChannels] = {};          ///< the placed sounds of the second catastrophe in this sample, by chain
  float bassExtra_ = 0;                         ///< the bass sends of the placed sounds in this sample
  SoundEvent pending_[kMaxPendingEvents];       ///< events handed in since the last render call
  int pendingCount_ = 0;                        ///< how many of them
  int chains_ = 2;                              ///< how many bed chains run: one per directional speaker, at least two
  Rng bedRng_[kMaxChannels];                    ///< generators of the chains from 2 up (0 and 1 use rng_)
  float trim_[kAmbientKinds][kMaxChannels];     ///< level of each chain in each scene (1 for the stereo pair)
  float bedGain_[kAmbientKinds][kMaxChannels];  ///< trim_ with the slow drift applied
  uint32_t liveMask_[kAmbientKinds] = {};       ///< bit c set: chain c is audible in the scene
  float driftDepth_[kAmbientKinds] = {};        ///< how far the chains of a scene drift in level (0 to 1)
  unsigned driftTick_ = 0;                      ///< counts samples, to update the drift every few
  float bassLow_[2] = {};                       ///< the two low-pass stages of the bass channel
  float bassK_ = 0;                             ///< their coefficient
  double padCos_[kMaxChannels] = {}, padSin_[kMaxChannels] = {};  ///< phase offset of each chain's moon pad, as cosine and sine
  float routeGain_[kMaxChannels] = {};          ///< smoothed gain of each output channel
  float routeTarget_[kMaxChannels] = {};        ///< gain each output channel glides to (1 or 0)
  int rate_;
  float noiseScale_;  ///< keeps the noise density per Hz the same at every sample rate
  Rng rng_;
  double t_ = 0;  ///< seconds rendered so far
  float mix_[kAmbientKinds] = {};  ///< how present each kind of scene is; they crossfade
  float rain_ = 0, night_ = 0, volume_ = 0;  ///< smoothed parameters
  float gust_ = 0;  ///< current gust strength, 0 to 1, shared by the wind-like sounds
  float windLow_[kMaxChannels][2] = {};  ///< two low-pass stages of the wind, per channel
  double gustPhaseA_ = 0, gustPhaseB_ = 0;  ///< phases of the two slow gust swells (they speed up with rain)
  float wander_ = 0;          ///< slow random drift that keeps gusts irregular
  float rainLow_[kMaxChannels] = {};
  float snapLow_[kMaxChannels] = {}, snapBand_[kMaxChannels] = {};  ///< resonator that the crack's clicks ring, per channel
  float crackLow_[kMaxChannels] = {}, rumbleLow_[kMaxChannels][2] = {};  ///< thunder filter state, per channel
  Thunder thunder_[3];                              ///< claps under way or waiting (start 0 = free)
  float storm_ = 0, roll_ = 0;  ///< smoothed storm proximity; slow random roll of the distant rumble
  double nextFarClap_ = 0;      ///< when the next far-off clap comes (0 = none planned)
  unsigned seenFlashes_ = 0;
  bool flashesSynced_ = false;  ///< the first counter value seen is the starting point, not lightning

  /// A bubble blip under way or waiting (start 0 = free).
  struct Bubble {
    double start = 0;
    float f0 = 0, dur = 0.1f, amp = 0, pan = 0.5f;
  };
  Bubble bubbles_[10];
  float lowHum_, lowMuffle_, lowDeep_;              ///< aquarium filter coefficients
  double nextBubble_ = 0;                           ///< when the next random bubble rises
  float waterLow_[kMaxChannels][2] = {}, muffleLow_[kMaxChannels] = {};   ///< the deep hum and the muffled flow, per ear
  float deepLow_[kMaxChannels][2] = {};                        ///< the growl of something huge, per ear
  float whale_ = 0;                                 ///< smoothed whale proximity
  double nextSong_ = 0;                             ///< when the whale sings next (0 = not planned)
  /// A whale phrase: a long, gliding moan with a faint overtone.
  struct Song {
    double start = 0, dur = 0;
    float f0 = 100, pan = 0.5f;
    double phase = 0;
  } song_;
  unsigned seenNibbles_ = 0;
  bool nibblesSynced_ = false;

  float desertLow_[kMaxChannels][2] = {};  ///< two low-pass stages of the desert wind, per ear
  float sandLow_[kMaxChannels][2] = {};    ///< band limits of the hiss of blown sand (low cut, high cut), per ear
  float tornadoLow_[kMaxChannels][2] = {}; ///< the roar of a tornado, per ear
  float tornado_ = 0;           ///< smoothed tornado proximity
  double tornadoPhase_ = 0;     ///< phase of the roar's rotation
  float lowSandCut_, lowSandTop_, lowTornado_;  ///< desert filter coefficients

  /// A meteorite strike heard through the ground: when it began and how hard it hit.
  struct Impact {
    double start = 0;
    float gain = 0;
  };
  Impact impacts_[3];
  double zapStart_ = 0;         ///< when the alien's warble began (0 = none yet)
  double zapPhase_ = 0;         ///< phase of the warble
  float moonLow_[kMaxChannels][2] = {};    ///< the dull rumble of an impact, per ear
  float debrisBand_[kMaxChannels] = {}, debrisLow_[kMaxChannels] = {};  ///< resonator that rings the pattering debris, per ear
  float lowMoon_;               ///< moon filter coefficient

  float mountLow_[kMaxChannels][2] = {};   ///< two low-pass stages of the thin mountain wind, per ear
  float iceLow_[kMaxChannels] = {};        ///< low end of the hiss of ice crystals, per ear
  float streamLow_[kMaxChannels][2] = {};  ///< band limits of the stream (high cut, low cut), per ear
  float slideLow_[kMaxChannels][2] = {};   ///< the deep rumble of an avalanche, per ear
  float slideMid_[kMaxChannels] = {};      ///< the roar and crackle of tumbling snow and rock, per ear
  float avalanche_ = 0;         ///< smoothed avalanche level
  double whistlePhase_ = 0;     ///< phase of the whistle of an icy wind
  float crackLowAv_[2] = {}, crackBandAv_[2] = {};  ///< resonator that the clicks of tumbling rock ring, per ear
  float breakEnv_ = 0;          ///< how hard the slab is still breaking up (crackling), 0 to 1
  bool slideBroke_ = false;     ///< the current avalanche has already broken loose
  float thumpEnv_ = 0, thumpFreq_ = 80;  ///< a tree or boulder knocked down: a dull thump
  double thumpPhase_ = 0;
  double nextCry_ = 0;          ///< when an eagle cries next (0 = not planned)
  double cryStart_ = -10;       ///< when the current eagle cry began
  float cryPan_ = 0.5f, cryPitch_ = 1;
  double cryPhase_ = 0;
  double nextPeck_ = 0;         ///< when a woodpecker drums next (0 = not planned)
  double peckStart_ = -10;      ///< when the current drumming began
  int peckKnocks_ = 0;          ///< how many knocks it has
  float peckPan_ = 0.5f, peckPitch_ = 1;
  float lowSlide_, lowSlideMid_, lowStreamTop_, lowStreamBottom_, lowIce_;  ///< mountain filter coefficients
  float wormNear_ = 0;                ///< smoothed sandworm proximity
  double nextRoar_ = 0;               ///< when the worm bellows next (0 = not planned)
  double roarStart_ = -10, roarDur_ = 2.4;  ///< when the current bellow began, and how long it lasts
  double roarPhase_ = 0;              ///< phase of the bellow's tone
  float roarLow_[2] = {};             ///< two low-pass stages that keep the bellow deep
  float lowRoar_;                     ///< filter coefficient of the bellow
  /// Chewing, as the worm swallows something: a series of soft, grinding bites. `bite` holds when
  /// each one starts (seconds after `start`) and how hard it is.
  struct Crunch {
    static constexpr int kBites = 9;
    double start = 0;
    float gain = 0;
    int bites = 0;
    float bite[kBites] = {}, force[kBites] = {};
  };
  Crunch crunches_[4];
  float crunchLow_[2][2] = {};        ///< two low-pass stages of the crunch, per ear
  float lowCrunch_;                   ///< filter coefficient of the crunch
  /// A muffled gulp of the kraken swallowing something: two glugs in a row.
  struct Gulp {
    double start = 0;
    float pitch = 1;
    bool used = false;
  };
  Gulp gulps_[4];
  double gulpPhase_ = 0;              ///< phase of the glug's tone
  float gulpLow_[2] = {};             ///< two low-pass stages that muffle the gulp
  float lowGulp_;                     ///< filter coefficient of the gulp
  unsigned seenCrushes_ = 0;
  bool crushesSynced_ = false;
  /// A muffled squeal of an animal in an avalanche.
  struct Squeal {
    double start = 0, phase = 0;
    float pitch = 1;
    bool used = false;
  };
  Squeal squealVoices_[3];
  float squealLow_[2] = {};           ///< two low-pass stages that muffle the squeal
  float lowSqueal_;                   ///< filter coefficient of the squeal
  unsigned seenSqueals_ = 0;
  bool squealsSynced_ = false;
  /// A car passing by: a swell of engine noise that sinks in pitch, panned across.
  struct Pass {
    double start = 0, dur = 1, phase = 0;
    float pitch = 90, pan = 0.5f, gain = 0;
  };
  Pass passes_[4];
  double nextPass_ = 0;             ///< when the next car passes (0 = not planned)
  float carLow_[kMaxChannels][2] = {};         ///< traffic rumble, two low-pass stages per ear
  float carMid_[kMaxChannels][2] = {};         ///< the hiss of tyres: band limits per ear
  float engineLow_[4] = {};         ///< low-pass over each passing engine
  float lowCar_, lowCarMid_, lowCarTop_, lowEngine_;  ///< city filter coefficients
  /// A car horn: one or two honks.
  struct Honk {
    double start = 0, phaseA = 0, phaseB = 0;
    float pitch = 1, pan = 0.5f, gain = 0, long_ = 0;  ///< long_: 0 = short honks, 1 = one long blast
    int count = 1;
  };
  Honk honks_[3];
  double nextHonk_ = 0;             ///< when the next horn sounds (0 = not planned)
  float hornLow_ = 0;              ///< takes the edge off the horns
  float lowHorn_;                   ///< filter coefficient of the horns
  float sirenNear_ = 0, sirenFreq_ = 700;  ///< smoothed siren proximity, and the pitch it glides to
  double sirenPhase_ = 0;
  float wailNear_ = 0;              ///< smoothed proximity of the fire engine's wail
  double wailPhase_ = 0;            ///< phase of the wail's tone
  float trainNear_ = 0, planeNear_ = 0, fireNear_ = 0;  ///< smoothed proximity of train and plane, fire level
  double trainClock_ = 0;           ///< seconds into the current "klack-klack" of the wheels
  float trainMove_ = 1, trainBrake_ = 0, trainHiss_ = 0;  ///< smoothed motion, braking and air hiss of the train
  double trainSquealPhase_ = 0;     ///< phase of the brakes' squeal
  float trainHissLow_[kMaxChannels] = {};  ///< low-pass state that is taken off the noise to leave the hiss, per chain
  static constexpr double kKlackPeriod = 0.64;  ///< seconds from one "klack-klack" to the next
  double trainHornStart_ = -10, trainHornPhase_ = 0;  ///< the train's horn
  bool trainHorned_ = false;        ///< this train has sounded its horn already
  float trainLow_[kMaxChannels][2] = {}, trainBand_[kMaxChannels][2] = {};  ///< rumble and clatter filters, per chain
  float fireLow_[kMaxChannels][2] = {}, fireCrack_ = 0, planeLow_[kMaxChannels][2] = {};  ///< fire and jet filters
  float lowTrain_, lowTrainBand_, lowKlack_, lowFire_, lowJet_;  ///< their coefficients
  float menace_ = 0;                ///< smoothed strength of the second catastrophe
  float flames_ = 0, wingedNear_ = 0;  ///< smoothed strength of the fire streams, and proximity of the winged monster
  float flameLo_[kMaxChannels] = {}, flameHi_[kMaxChannels] = {};  ///< the two low-pass stages whose difference is the roar of fire, per ear
  float lowFlameTop_, lowFlameBottom_;       ///< their coefficients
  float flameWander_ = 0;                    ///< slow, uneven swelling of the roar
  double menacePhase_[2] = {};      ///< phases of its tones (the roar, the whine)
  float menaceLow_[kMaxChannels][2] = {};      ///< low-pass stages of its noise, per ear
  float menaceHigh_[kMaxChannels] = {};        ///< what the hiss and the rush take away, per ear
  float lowHiss_, lowThump_;        ///< coefficients of the hiss and of the low stages
  float lowFlood_, lowFloodTop_;    ///< coefficients of the flood's rush
  float lowChop_;                   ///< coefficient of the rotor's chop
  double nextMenaceRoar_ = 0;       ///< when the creature roars next (0 = not planned)
  double menaceRoarStart_ = -10, menaceRoarDur_ = 1.5;  ///< the current roar
  int menaceRoarVoice_ = 0;         ///< 0 the low growl, 1 the shrill cry
  /// A rifle shot: a crack with a thump.
  struct Shot {
    double start = -10;
    float pan = 0.5f;
  };
  Shot shotVoices_[4];
  unsigned seenShots_ = 0;
  bool shotsSynced_ = false;
  unsigned seenImpacts_ = 0;
  bool impactsSynced_ = false;
};

}  // namespace dio
