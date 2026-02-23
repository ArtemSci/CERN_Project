#pragma once

#include <map>
#include <string>
#include <vector>

namespace phase3 {

struct PhotonRecord {
  int event_id = 0;
  int track_id = 0;
  int parent_id = 0;
  double x_mm = 0.0;
  double y_mm = 0.0;
  double z_mm = 0.0;
  double t_ns = 0.0;
  double lambda_nm = 0.0;
  double dir_x = 0.0;
  double dir_y = 0.0;
  double dir_z = 1.0;
  double pol_x = 1.0;
  double pol_y = 0.0;
  double pol_z = 0.0;
};

class PhotonSource {
 public:
  void SetStrictCsvParsing(bool enabled);
  bool LoadFromCsv(const std::string& path);
  int EventCount() const;
  int TotalPhotons() const;
  const std::vector<int>& EventIds() const;
  const std::vector<PhotonRecord>& PhotonsForEvent(int event_index) const;
  const std::string& LastError() const;

 private:
  bool strict_csv_parsing_ = true;
  std::string last_error_;
  std::vector<int> event_ids_;
  std::map<int, std::vector<PhotonRecord>> photons_;
};

}  // namespace phase3
