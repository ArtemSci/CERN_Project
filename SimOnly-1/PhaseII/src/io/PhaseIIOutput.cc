#include "PhaseIIOutput.hh"

#include <cstdint>
#include <filesystem>

#include "nlohmann/json.hpp"

namespace phase2 {

PhaseIIOutput::PhaseIIOutput(const OutputConfig& config) : config_(config) {}

bool PhaseIIOutput::Open() {
  std::filesystem::create_directories(config_.dir);
  photons_.open(std::filesystem::path(config_.dir) / config_.photons_csv);
  summaries_.open(std::filesystem::path(config_.dir) / config_.summary_csv);
  if (!photons_ || !summaries_) {
    return false;
  }
  photons_ << "event_id,track_id,parent_id,node_id,surface_id,step_index,origin,material,x_mm,y_mm,z_mm,t_ns,lambda_nm,vg_mm_per_ns,"
              "dir_x,dir_y,dir_z,pol_x,pol_y,pol_z,weight\n";
  summaries_ << "event_id,track_id,step_index,material,beta,step_length_mm,mean_cherenkov,cherenkov_count,"
                "mean_scint,scint_count,mean_window,window_count\n";
  return true;
}

void PhaseIIOutput::WritePhotons(const std::vector<Photon>& photons) {
  for (const auto& photon : photons) {
    photons_ << photon.event_id << ',' << photon.track_id << ',' << photon.parent_id << ','
             << photon.node_id << ',' << photon.surface_id << ',' << photon.step_index << ','
             << photon.origin << ',' << photon.material << ',' << photon.x_mm << ',' << photon.y_mm << ','
             << photon.z_mm << ',' << photon.t_ns << ',' << photon.lambda_nm << ',' << photon.vgroup_mm_per_ns << ','
             << photon.dir_x << ',' << photon.dir_y << ',' << photon.dir_z << ','
             << photon.pol_x << ',' << photon.pol_y << ',' << photon.pol_z << ',' << photon.weight << '\n';
  }
}

void PhaseIIOutput::WritePhotonsSoA(const PhotonSoA& photons) {
  if (!config_.write_soa) {
    return;
  }
  const std::filesystem::path path = std::filesystem::path(config_.dir) / config_.photons_soa_bin;
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return;
  }
  const char magic[8] = {'P', 'H', '2', 'S', 'O', 'A', '1', '\0'};
  out.write(magic, sizeof(magic));
  const uint64_t count = static_cast<uint64_t>(photons.x_mm.size());
  const uint32_t origin_count = static_cast<uint32_t>(photons.origin_table.size());
  const uint32_t material_count = static_cast<uint32_t>(photons.material_table.size());
  out.write(reinterpret_cast<const char*>(&count), sizeof(count));
  out.write(reinterpret_cast<const char*>(&origin_count), sizeof(origin_count));
  out.write(reinterpret_cast<const char*>(&material_count), sizeof(material_count));

  auto write_string = [&out](const std::string& value) {
    const uint16_t len = static_cast<uint16_t>(value.size());
    out.write(reinterpret_cast<const char*>(&len), sizeof(len));
    out.write(value.data(), len);
  };

  for (const auto& name : photons.origin_table) {
    write_string(name);
  }
  for (const auto& name : photons.material_table) {
    write_string(name);
  }

  auto write_ints = [&out](const std::vector<int>& values) {
    out.write(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(int));
  };
  auto write_doubles = [&out](const std::vector<double>& values) {
    out.write(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(double));
  };

  write_ints(photons.event_id);
  write_ints(photons.track_id);
  write_ints(photons.parent_id);
  write_ints(photons.node_id);
  write_ints(photons.surface_id);
  write_ints(photons.step_index);
  write_ints(photons.origin_id);
  write_ints(photons.material_id);
  write_doubles(photons.x_mm);
  write_doubles(photons.y_mm);
  write_doubles(photons.z_mm);
  write_doubles(photons.t_ns);
  write_doubles(photons.lambda_nm);
  write_doubles(photons.vgroup_mm_per_ns);
  write_doubles(photons.dir_x);
  write_doubles(photons.dir_y);
  write_doubles(photons.dir_z);
  write_doubles(photons.pol_x);
  write_doubles(photons.pol_y);
  write_doubles(photons.pol_z);
  write_doubles(photons.weight);
}

void PhaseIIOutput::WriteStepSummaries(const std::vector<StepSummary>& summaries) {
  for (const auto& summary : summaries) {
    summaries_ << summary.event_id << ',' << summary.track_id << ',' << summary.step_index << ','
               << summary.material << ',' << summary.beta << ',' << summary.step_length_mm << ','
               << summary.mean_cherenkov << ',' << summary.cherenkov_count << ','
               << summary.mean_scint << ',' << summary.scint_count << ','
               << summary.mean_window << ',' << summary.window_count << '\n';
  }
}

void PhaseIIOutput::WriteMetrics(const Metrics& metrics) {
  nlohmann::json json;
  json["total_photons"] = metrics.total_photons;
  json["generated_photons"] = metrics.generated_photons;
  json["thinned_photons"] = metrics.thinned_photons;
  json["capped_photons"] = metrics.capped_photons;
  json["capped_steps"] = metrics.capped_steps;
  json["extrapolated_photons"] = metrics.extrapolated_photons;
  json["extrapolated_events"] = metrics.extrapolated_events;
  json["unknown_material_steps"] = metrics.unknown_material_steps;
  json["origin_counts"] = metrics.origin_counts;
  json["material_counts"] = metrics.material_counts;

  std::ofstream out(std::filesystem::path(config_.dir) / config_.metrics_json);
  out << json.dump(2);
}

}  // namespace phase2
