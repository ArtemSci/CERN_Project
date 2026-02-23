#pragma once

#include <fstream>
#include <map>
#include <string>

#include "G4ThreeVector.hh"

#include "PhaseIConfig.hh"
#include "SurfaceRegistry.hh"

namespace phase1 {

struct TrackSummary {
  int event_id = 0;
  int track_id = 0;
  int parent_id = 0;
  std::string particle;
  std::string creator_process;
  int is_delta_secondary = 0;
  std::string status;
  int node_count = 0;
  double first_x_mm = 0.0;
  double first_y_mm = 0.0;
  double first_z_mm = 0.0;
  double first_t_ns = 0.0;
  double first_px_mev = 0.0;
  double first_py_mev = 0.0;
  double first_pz_mev = 0.0;
  double first_pol_x = 1.0;
  double first_pol_y = 0.0;
  double first_pol_z = 0.0;
  std::string first_material;
  double last_x_mm = 0.0;
  double last_y_mm = 0.0;
  double last_z_mm = 0.0;
  double last_t_ns = 0.0;
  double track_length_mm = 0.0;
};

class OutputWriter {
 public:
  explicit OutputWriter(const OutputConfig& config);

  bool Open();
  void WriteSurfaces(const SurfaceRegistry& surfaces);
  void RecordStep(int event_id,
                  int track_id,
                  int parent_id,
                  int step_index,
                  const G4ThreeVector& pos_mm,
                  double time_ns,
                  const G4ThreeVector& mom_mev,
                  const G4ThreeVector& pol,
                  double step_length_mm,
                  double edep_mev,
                  const std::string& pre_material,
                  const std::string& post_material);
  void RecordNode(int event_id,
                  int track_id,
                  int parent_id,
                  int step_index,
                  int surface_id,
                  int pre_layer,
                  int post_layer,
                  const G4ThreeVector& pos_mm,
                  double time_ns,
                  const G4ThreeVector& mom_mev,
                  double beta,
                  const G4ThreeVector& normal,
                  const std::string& pre_material,
                  const std::string& post_material,
                  double correction_mm);
  void RecordSummary(const TrackSummary& summary);
  void Finalize();

 private:
  OutputConfig config_;
  std::ofstream nodes_;
  std::ofstream summary_;
  std::ofstream surfaces_;
  std::ofstream steps_;

  int total_tracks_ = 0;
  int total_nodes_ = 0;
  std::map<int, int> nodes_per_surface_;
  std::map<std::string, int> status_counts_;
};

}  // namespace phase1
