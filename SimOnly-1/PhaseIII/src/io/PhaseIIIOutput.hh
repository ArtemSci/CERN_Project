#pragma once

#include <fstream>
#include <string>

namespace phase3 {

struct HitRecord {
  int event_id = 0;
  int track_id = 0;
  std::string detector_id;
  int channel_id = 0;
  double x_mm = 0.0;
  double y_mm = 0.0;
  double z_mm = 0.0;
  double t_ns = 0.0;
  double lambda_nm = 0.0;
  double incidence_angle_deg = 0.0;
  double dir_x = 0.0;
  double dir_y = 0.0;
  double dir_z = 1.0;
  double pol_x = 1.0;
  double pol_y = 0.0;
  double pol_z = 0.0;
};

struct StepRecord {
  int event_id = 0;
  int track_id = 0;
  int parent_track_id = 0;
  int step_index = 0;
  double x_mm = 0.0;
  double y_mm = 0.0;
  double z_mm = 0.0;
  double t_ns = 0.0;
  std::string volume;
  std::string process;
};

struct TransportEventRecord {
  int event_id = 0;
  int track_id = 0;
  double x_mm = 0.0;
  double y_mm = 0.0;
  double z_mm = 0.0;
  double t_ns = 0.0;
  double lambda_nm = 0.0;
  std::string type;
  std::string process;
  std::string pre_volume;
  std::string post_volume;
  std::string status;
};

struct BoundaryEventRecord {
  int event_id = 0;
  int track_id = 0;
  double x_mm = 0.0;
  double y_mm = 0.0;
  double z_mm = 0.0;
  double t_ns = 0.0;
  double lambda_nm = 0.0;
  std::string pre_volume;
  std::string post_volume;
  std::string status;
};

struct Metrics {
  int total_photons = 0;
  int classified_count = 0;
  int unclassified_count = 0;
  int hit_count = 0;
  int absorbed_count = 0;
  int lost_count = 0;
  int boundary_reflections = 0;
};

class PhaseIIIOutput {
 public:
  PhaseIIIOutput(const std::string& dir,
                 const std::string& hits_csv,
                 const std::string& boundary_csv,
                 const std::string& transport_csv,
                 const std::string& steps_csv,
                 const std::string& metrics_json);

  bool Open(bool record_steps, bool write_boundary, bool write_transport);
  void WriteHit(const HitRecord& hit);
  void WriteStep(const StepRecord& step);
  void WriteTransportEvent(const TransportEventRecord& event);
  void WriteBoundaryEvent(const BoundaryEventRecord& hit);
  void WriteMetrics(const Metrics& metrics);

 private:
  std::string dir_;
  std::string hits_csv_;
  std::string boundary_csv_;
  std::string transport_csv_;
  std::string steps_csv_;
  std::string metrics_json_;
  std::ofstream hits_;
  std::ofstream transport_events_;
  std::ofstream boundary_events_;
  std::ofstream steps_;
};

}  // namespace phase3
