#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "G4UserRunAction.hh"
#include "G4UserSteppingAction.hh"
#include "G4UserTrackingAction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"
#include "G4VUserPrimaryParticleInformation.hh"

#include "PhaseIIIConfig.hh"
#include "PhaseIIIOutput.hh"
#include "PhotonSource.hh"

class G4Event;
class G4Track;

namespace phase3 {

class DetectorConstruction;

struct PhotonTag {
  int event_id = 0;
  int track_id = 0;
};

enum class PrimaryPhotonOutcome {
  kUnknown = 0,
  kHit = 1,
  kAbsorbed = 2,
  kLost = 3
};

class PhotonPrimaryInfo : public G4VUserPrimaryParticleInformation {
 public:
  PhotonPrimaryInfo(int event_id, int track_id);

  int event_id() const;
  int track_id() const;
  void Print() const override;

 private:
  int event_id_ = 0;
  int track_id_ = 0;
};

class PrimaryGeneratorAction : public G4VUserPrimaryGeneratorAction {
 public:
  explicit PrimaryGeneratorAction(const PhotonSource& source);

  void GeneratePrimaries(G4Event* event) override;
  int EventCount() const;

 private:
  const PhotonSource& source_;
};

class RunAction : public G4UserRunAction {
 public:
  explicit RunAction(PhaseIIIOutput* output);

  void BeginOfRunAction(const G4Run*) override;
  void EndOfRunAction(const G4Run*) override;

  Metrics& metrics();

 private:
  PhaseIIIOutput* output_ = nullptr;
  Metrics metrics_;
};

class TrackingAction : public G4UserTrackingAction {
 public:
  TrackingAction(const TrackingConfig& config,
                 RunAction* run_action,
                 PhaseIIIOutput* output,
                 std::unordered_map<uint64_t, PhotonTag>* track_tags,
                 std::unordered_map<uint64_t, PrimaryPhotonOutcome>* primary_outcomes,
                 std::unordered_set<uint64_t>* traced_photons,
                 std::unordered_set<uint64_t>* seeded_tracks);

  void PreUserTrackingAction(const G4Track* track) override;
  void PostUserTrackingAction(const G4Track* track) override;

 private:
  TrackingConfig config_;
  RunAction* run_action_ = nullptr;
  PhaseIIIOutput* output_ = nullptr;
  std::unordered_map<uint64_t, PhotonTag>* track_tags_ = nullptr;
  std::unordered_map<uint64_t, PrimaryPhotonOutcome>* primary_outcomes_ = nullptr;
  std::unordered_set<uint64_t>* traced_photons_ = nullptr;
  std::unordered_set<uint64_t>* seeded_tracks_ = nullptr;
};

class SteppingAction : public G4UserSteppingAction {
 public:
  SteppingAction(const TrackingConfig& tracking,
                 PhaseIIIOutput* output,
                 RunAction* run_action,
                 const std::unordered_map<uint64_t, PhotonTag>* track_tags,
                 std::unordered_map<uint64_t, PrimaryPhotonOutcome>* primary_outcomes,
                 const std::unordered_set<uint64_t>* traced_photons,
                 const std::unordered_set<uint64_t>* seeded_tracks,
                 const DetectorConstruction* detector);

  void UserSteppingAction(const G4Step* step) override;

 private:
  TrackingConfig tracking_;
  PhaseIIIOutput* output_ = nullptr;
  RunAction* run_action_ = nullptr;
  const std::unordered_map<uint64_t, PhotonTag>* track_tags_ = nullptr;
  std::unordered_map<uint64_t, PrimaryPhotonOutcome>* primary_outcomes_ = nullptr;
  const std::unordered_set<uint64_t>* traced_photons_ = nullptr;
  const std::unordered_set<uint64_t>* seeded_tracks_ = nullptr;
  const DetectorConstruction* detector_ = nullptr;
  std::unordered_map<uint64_t, int> step_counts_;
};

}  // namespace phase3
