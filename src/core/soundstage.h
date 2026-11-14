/// @file core/soundstage.h
/// @brief Where a sound comes from on the stage: emitters (continuous sources) and events (one-offs).

#pragma once
#include <cstdint>

namespace dio {

/// The creatures whose presence is heard as a continuous sound that follows them across the stage.
/// Each is found in the world by the tag or sprite name that `World::nearness` has always used.
enum class EmitterKey : uint8_t {
  Gloom,    ///< the thundercloud ("gloom")
  Whale,    ///< "whale"
  Tornado,  ///< "tornado"
  Worm,     ///< "sandworm"
  Siren,    ///< the ambulance ("siren")
  Wail,     ///< the fire engine ("wail")
  Train,    ///< "train"
  Plane,    ///< "plane"
  Winged,   ///< the winged monster ("winged_titan")
  Chopper,  ///< the poachers' helicopter ("chopper")
  Beast,    ///< the yeti ("beast")
  Titan,    ///< the city's monsters ("titan")
  Fire,     ///< the burning building with the strongest fire (not a creature: found in the city)
  Flames,   ///< the monsters' streams of fire (not a creature: the middle of the streams)
  Avalanche,  ///< the head of the avalanche as it comes down the slope (not a creature)
  Submarine,  ///< the poison-leaking submarine ("submarine")
  Flood       ///< the front of the flood's wall of water (not a creature: at ground level)
};
/// Number of EmitterKey values.
inline constexpr int kEmitterKeys = 17;

/// The tag or sprite name behind an emitter key, as `World::nearness` takes it; null for the keys that
/// are not creatures (Fire, Flames, Avalanche and Flood).
inline const char* emitterTag(EmitterKey key) {
  static constexpr const char* kTags[kEmitterKeys] = {"gloom", "whale", "tornado", "sandworm", "siren", "wail", "train",
                                                      "plane", "winged_titan", "chopper", "beast", "titan", nullptr, nullptr, nullptr, "submarine", nullptr};
  return kTags[static_cast<int>(key)];
}

/// A continuous sound source on the stage: how present it is and where it stands. The scene is a
/// window the listener looks through, so positions are relative to the screen.
struct Emitter {
  float level = 0;   ///< 0 to 1: how near it is (what `World::nearness` gives), 0 if there is none
  float x = 0;       ///< -1.5 to 1.5: -1 and 1 are the screen edges, beyond them it is off-screen
  float y = 0;       ///< 0 on the ground up to 1 at the top of the screen
  float depth = 0;   ///< 0 near to 1 far (from the layer: larger z is further away)
  float spread = 0;  ///< 0 a point to 1 fills the room; set where a sound is wide (not by the world)
};

/// What happened, for the one-off sounds.
enum class EventKind : uint8_t {
  Flash,   ///< a lightning flash, at the column it strikes (strength: 1)
  Nibble,  ///< a fish eats a food flake, at its mouth (strength: 1)
  Impact,  ///< a meteorite strikes the ground (strength: 1 small, 2 the big one)
  Crush,   ///< a sandworm or kraken crushes something (strength: how many things)
  Squeal,  ///< an animal is buried by an avalanche (strength: 1)
  Shot     ///< a poacher fires a rifle, at the muzzle (strength: 1)
};

/// A one-off sound with the place it happened. Plain data, so it can travel through a lock-free queue.
struct SoundEvent {
  EventKind kind = EventKind::Flash;  ///< what happened
  float x = 0;                        ///< as Emitter::x
  float y = 0;                        ///< as Emitter::y
  float depth = 0;                    ///< as Emitter::depth
  float strength = 1;                 ///< see EventKind
};

/// The layer number that counts as the far background: depths are `z / kStageDepthLayers`, at most 1.
inline constexpr int kStageDepthLayers = 32;
/// How far beyond the screen edge (in screen half-widths) positions are still told apart.
inline constexpr float kStageReach = 1.5f;

}  // namespace dio
