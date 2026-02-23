#pragma once

#include <map>
#include <string>

#include "G4UserEventAction.hh"
#include "G4UserRunAction.hh"
#include "G4UserSteppingAction.hh"
#include "G4UserTrackingAction.hh"
#include "G4UserStackingAction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"

#include "PhaseIConfig.hh"
#include "OutputWriter.hh"
#include "SurfaceRegistry.hh"

class G4ParticleGun;
class G4Track;
class G4Step;

namespace phase1 {

struct TrackAccumulator {
  int event_id = 0;
  int track_id = 0;
  int parent_id = 0;
  std::string particle;
  std::string creator_process;
  int step_count = 0;
  int node_count = 0;
  double track_length_mm = 0.0;
  bool killed_by_wall = false;
  bool has_first = false;
  G4ThreeVector first_pos_mm;
  double first_time_ns = 0.0;
  G4ThreeVector first_mom_mev;
  G4ThreeVector first_pol;
  std::string first_material;
  G4ThreeVector last_pos_mm;
  double last_time_ns = 0.0;
};

class PrimaryGeneratorAction final : public G4VUserPrimaryGeneratorAction {
 public:
  explicit PrimaryGeneratorAction(const PhaseIConfig& config);
  ~PrimaryGeneratorAction() override;
  void GeneratePrimaries(G4Event* event) override;

 private:
  const PhaseIConfig& config_;
  G4ParticleGun* particle_gun_;
};

class RunAction final : public G4UserRunAction {
 public:
  RunAction(OutputWriter& writer, const SurfaceRegistry& surfaces);
  void BeginOfRunAction(const G4Run*) override;
  void EndOfRunAction(const G4Run*) override;

 private:
  OutputWriter& writer_;
  const SurfaceRegistry& surfaces_;
};

class EventAction final : public G4UserEventAction {
 public:
  explicit EventAction(uint64_t seed);
  void BeginOfEventAction(const G4Event* event) override;

 private:
  uint64_t seed_;
};

class TrackingAction final : public G4UserTrackingAction {
 public:
  TrackingAction(OutputWriter& writer, const PhaseIConfig& config, std::map<int, TrackAccumulator>& tracks);
  void PreUserTrackingAction(const G4Track* track) override;
  void PostUserTrackingAction(const G4Track* track) override;

 private:
  OutputWriter& writer_;
  const PhaseIConfig& config_;
  std::map<int, TrackAccumulator>& tracks_;
};

class SteppingAction final : public G4UserSteppingAction {
 public:
  SteppingAction(OutputWriter& writer,
                 const PhaseIConfig& config,
                 const SurfaceRegistry& surfaces,
                 std::map<int, TrackAccumulator>& tracks);
  void UserSteppingAction(const G4Step* step) override;

 private:
  OutputWriter& writer_;
  const PhaseIConfig& config_;
  const SurfaceRegistry& surfaces_;
  std::map<int, TrackAccumulator>& tracks_;

  int ExtractLayerIndex(const std::string& name) const;
};

class StackingAction final : public G4UserStackingAction {
 public:
  explicit StackingAction(const PhaseIConfig& config);
  G4ClassificationOfNewTrack ClassifyNewTrack(const G4Track* track) override;

 private:
  const PhaseIConfig& config_;
};

}  // namespace phase1
