#include "OutputWriter.hh"

#include <filesystem>

#include "nlohmann/json.hpp"

namespace phase1 {

OutputWriter::OutputWriter(const OutputConfig& config) : config_(config) {}

bool OutputWriter::Open() {
  std::filesystem::create_directories(config_.dir);
  nodes_.open(std::filesystem::path(config_.dir) / config_.nodes_csv);
  summary_.open(std::filesystem::path(config_.dir) / config_.summary_csv);
  surfaces_.open(std::filesystem::path(config_.dir) / config_.surfaces_csv);
  steps_.open(std::filesystem::path(config_.dir) / config_.step_trace_csv);

  if (!nodes_ || !summary_ || !surfaces_ || !steps_) {
    return false;
  }

  nodes_ << "event_id,track_id,parent_id,step_index,surface_id,pre_layer,post_layer,x_mm,y_mm,z_mm,t_ns,"
            "px_mev,py_mev,pz_mev,beta,normal_x,normal_y,normal_z,pre_material,post_material,correction_mm\n";
  summary_ << "event_id,track_id,parent_id,particle,creator_process,is_delta_secondary,status,node_count,first_x_mm,first_y_mm,first_z_mm,first_t_ns,"
              "first_px_mev,first_py_mev,first_pz_mev,first_pol_x,first_pol_y,first_pol_z,first_material,"
              "last_x_mm,last_y_mm,last_z_mm,last_t_ns,track_length_mm\n";
  surfaces_ << "surface_id,pre_layer,post_layer,name,bowed,z_plane_mm,radius_mm,w0_mm\n";
  steps_ << "event_id,track_id,parent_id,step_index,x_mm,y_mm,z_mm,t_ns,px_mev,py_mev,pz_mev,pol_x,pol_y,pol_z,step_length_mm,"
            "edep_mev,pre_material,post_material\n";
  return true;
}

void OutputWriter::WriteSurfaces(const SurfaceRegistry& surfaces) {
  for (const auto& surface : surfaces.Surfaces()) {
    surfaces_ << surface.id << ',' << surface.pre_layer << ',' << surface.post_layer << ','
              << surface.name << ',' << (surface.bowed ? 1 : 0) << ',' << surface.z_plane_mm << ','
              << surface.radius_mm << ',' << surface.w0_mm << '\n';
  }
}

void OutputWriter::RecordStep(int event_id,
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
                              const std::string& post_material) {
  steps_ << event_id << ',' << track_id << ',' << parent_id << ',' << step_index << ',' << pos_mm.x() << ','
         << pos_mm.y() << ',' << pos_mm.z() << ',' << time_ns << ',' << mom_mev.x() << ',' << mom_mev.y() << ','
         << mom_mev.z() << ',' << pol.x() << ',' << pol.y() << ',' << pol.z() << ','
         << step_length_mm << ',' << edep_mev << ',' << pre_material << ',' << post_material
         << '\n';
}

void OutputWriter::RecordNode(int event_id,
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
                              double correction_mm) {
  nodes_ << event_id << ',' << track_id << ',' << parent_id << ',' << step_index << ',' << surface_id << ','
         << pre_layer << ',' << post_layer << ',' << pos_mm.x() << ',' << pos_mm.y() << ',' << pos_mm.z() << ','
         << time_ns << ',' << mom_mev.x() << ',' << mom_mev.y() << ',' << mom_mev.z() << ',' << beta << ','
         << normal.x() << ',' << normal.y() << ',' << normal.z() << ',' << pre_material << ',' << post_material
         << ',' << correction_mm << '\n';

  ++total_nodes_;
  if (surface_id >= 0) {
    ++nodes_per_surface_[surface_id];
  }
}

void OutputWriter::RecordSummary(const TrackSummary& summary) {
  summary_ << summary.event_id << ',' << summary.track_id << ',' << summary.parent_id << ',' << summary.particle
           << ',' << summary.creator_process << ',' << summary.is_delta_secondary
           << ',' << summary.status << ',' << summary.node_count << ',' << summary.first_x_mm << ','
           << summary.first_y_mm << ',' << summary.first_z_mm << ',' << summary.first_t_ns << ','
           << summary.first_px_mev << ',' << summary.first_py_mev << ',' << summary.first_pz_mev << ','
           << summary.first_pol_x << ',' << summary.first_pol_y << ',' << summary.first_pol_z << ','
           << summary.first_material << ','
           << summary.last_x_mm << ',' << summary.last_y_mm << ',' << summary.last_z_mm << ','
           << summary.last_t_ns << ',' << summary.track_length_mm << '\n';

  ++total_tracks_;
  ++status_counts_[summary.status];
}

void OutputWriter::Finalize() {
  nlohmann::json metrics;
  metrics["total_tracks"] = total_tracks_;
  metrics["total_nodes"] = total_nodes_;
  metrics["nodes_per_surface"] = nodes_per_surface_;
  metrics["status_counts"] = status_counts_;

  std::ofstream metrics_file(std::filesystem::path(config_.dir) / config_.metrics_json);
  metrics_file << metrics.dump(2);
}

}  // namespace phase1
