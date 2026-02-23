#include "PhaseIIIOutput.hh"

#include <filesystem>
#include <iomanip>

namespace phase3 {

PhaseIIIOutput::PhaseIIIOutput(const std::string& dir,
                               const std::string& hits_csv,
                               const std::string& boundary_csv,
                               const std::string& transport_csv,
                               const std::string& steps_csv,
                               const std::string& metrics_json)
    : dir_(dir),
      hits_csv_(hits_csv),
      boundary_csv_(boundary_csv),
      transport_csv_(transport_csv),
      steps_csv_(steps_csv),
      metrics_json_(metrics_json) {}

bool PhaseIIIOutput::Open(bool record_steps, bool write_boundary, bool write_transport) {
  std::filesystem::create_directories(dir_);
  hits_.open(std::filesystem::path(dir_) / hits_csv_);
  if (!hits_) {
    return false;
  }
  hits_ << "event_id,track_id,detector_id,channel_id,x_mm,y_mm,z_mm,t_ns,lambda_nm,incidence_angle_deg,"
           "dir_x,dir_y,dir_z,pol_x,pol_y,pol_z\n";
  if (write_boundary) {
    boundary_events_.open(std::filesystem::path(dir_) / boundary_csv_);
    if (!boundary_events_) {
      return false;
    }
    boundary_events_ << "event_id,track_id,x_mm,y_mm,z_mm,t_ns,lambda_nm,pre_volume,post_volume,status\n";
  }
  if (write_transport) {
    transport_events_.open(std::filesystem::path(dir_) / transport_csv_);
    if (!transport_events_) {
      return false;
    }
    transport_events_ << "event_id,track_id,x_mm,y_mm,z_mm,t_ns,lambda_nm,type,process,pre_volume,post_volume,status\n";
  }
  if (record_steps) {
    steps_.open(std::filesystem::path(dir_) / steps_csv_);
    if (!steps_) {
      return false;
    }
    steps_ << "event_id,track_id,parent_track_id,step_index,x_mm,y_mm,z_mm,t_ns,volume,process\n";
  }
  return true;
}

void PhaseIIIOutput::WriteHit(const HitRecord& hit) {
  hits_ << hit.event_id << "," << hit.track_id << "," << hit.detector_id << "," << hit.channel_id << ","
        << std::setprecision(10)
        << hit.x_mm << "," << hit.y_mm << "," << hit.z_mm << ","
        << hit.t_ns << "," << hit.lambda_nm << "," << hit.incidence_angle_deg << ","
        << hit.dir_x << "," << hit.dir_y << "," << hit.dir_z << ","
        << hit.pol_x << "," << hit.pol_y << "," << hit.pol_z << "\n";
}

void PhaseIIIOutput::WriteStep(const StepRecord& step) {
  if (!steps_) {
    return;
  }
  steps_ << step.event_id << "," << step.track_id << ","
         << step.parent_track_id << "," << step.step_index << ","
         << std::setprecision(10)
         << step.x_mm << "," << step.y_mm << "," << step.z_mm << ","
         << step.t_ns << "," << step.volume << "," << step.process << "\n";
}

void PhaseIIIOutput::WriteTransportEvent(const TransportEventRecord& event) {
  if (!transport_events_) {
    return;
  }
  transport_events_ << event.event_id << "," << event.track_id << ","
                    << std::setprecision(10)
                    << event.x_mm << "," << event.y_mm << "," << event.z_mm << ","
                    << event.t_ns << "," << event.lambda_nm << "," << event.type << ","
                    << event.process << "," << event.pre_volume << "," << event.post_volume
                    << "," << event.status << "\n";
}

void PhaseIIIOutput::WriteBoundaryEvent(const BoundaryEventRecord& hit) {
  if (!boundary_events_) {
    return;
  }
  boundary_events_ << hit.event_id << "," << hit.track_id << ","
                   << std::setprecision(10)
                   << hit.x_mm << "," << hit.y_mm << "," << hit.z_mm << ","
                   << hit.t_ns << "," << hit.lambda_nm << "," << hit.pre_volume << ","
                   << hit.post_volume << "," << hit.status << "\n";
}

void PhaseIIIOutput::WriteMetrics(const Metrics& metrics) {
  std::ofstream output(std::filesystem::path(dir_) / metrics_json_);
  output << "{\n";
  output << "  \"total_photons\": " << metrics.total_photons << ",\n";
  output << "  \"classified_count\": " << metrics.classified_count << ",\n";
  output << "  \"unclassified_count\": " << metrics.unclassified_count << ",\n";
  output << "  \"hit_count\": " << metrics.hit_count << ",\n";
  output << "  \"absorbed_count\": " << metrics.absorbed_count << ",\n";
  output << "  \"lost_count\": " << metrics.lost_count << ",\n";
  output << "  \"boundary_reflections\": " << metrics.boundary_reflections << "\n";
  output << "}\n";
}

}  // namespace phase3
