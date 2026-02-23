#pragma once

#include <map>
#include <string>
#include <vector>

namespace phase4 {

struct HitRecord {
  int event_id = 0;
  int track_id = 0;
  std::string detector_id;
  int channel_id = 0;
  double x_mm = 0.0;
  double y_mm = 0.0;
  double z_mm = 0.0;
  double t_ns = 0.0;
  double lambda_nm = 400.0;
  double incidence_angle_deg = 0.0;
  double dir_x = 0.0;
  double dir_y = 0.0;
  double dir_z = 1.0;
  double pol_x = 1.0;
  double pol_y = 0.0;
  double pol_z = 0.0;
};

class HitSource {
 public:
  bool LoadFromCsv(const std::string& path);
  int EventCount() const;
  int TotalHits() const;
  const std::vector<int>& EventIds() const;
  const std::vector<HitRecord>& HitsForEvent(int event_index) const;

 private:
  std::vector<int> event_ids_;
  std::map<int, std::vector<HitRecord>> hits_;
};

}  // namespace phase4
