#pragma once

#include <map>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "PhaseIIConfig.hh"
#include "OpticsModels.hh"

namespace phase2 {

struct StepRecord {
  int event_id = 0;
  int track_id = 0;
  int parent_id = 0;
  int step_index = 0;
  double x_mm = 0.0;
  double y_mm = 0.0;
  double z_mm = 0.0;
  double t_ns = 0.0;
  double px_mev = 0.0;
  double py_mev = 0.0;
  double pz_mev = 0.0;
  double pol_x = 1.0;
  double pol_y = 0.0;
  double pol_z = 0.0;
  double step_length_mm = 0.0;
  double edep_mev = 0.0;
  std::string pre_material;
  std::string post_material;
};

struct TrackInfo {
  std::string particle;
  int parent_id = 0;
  bool has_birth_state = false;
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
};

struct Photon {
  int event_id = 0;
  int track_id = 0;
  int parent_id = 0;
  int node_id = -1;
  int surface_id = -1;
  int step_index = 0;
  std::string origin;
  std::string material;
  double x_mm = 0.0;
  double y_mm = 0.0;
  double z_mm = 0.0;
  double t_ns = 0.0;
  double lambda_nm = 0.0;
  double vgroup_mm_per_ns = 0.0;
  double dir_x = 0.0;
  double dir_y = 0.0;
  double dir_z = 1.0;
  double pol_x = 1.0;
  double pol_y = 0.0;
  double pol_z = 0.0;
  double weight = 1.0;
};

struct PhotonSoA {
  std::vector<int> event_id;
  std::vector<int> track_id;
  std::vector<int> parent_id;
  std::vector<int> node_id;
  std::vector<int> surface_id;
  std::vector<int> step_index;
  std::vector<int> origin_id;
  std::vector<int> material_id;
  std::vector<double> x_mm;
  std::vector<double> y_mm;
  std::vector<double> z_mm;
  std::vector<double> t_ns;
  std::vector<double> lambda_nm;
  std::vector<double> vgroup_mm_per_ns;
  std::vector<double> dir_x;
  std::vector<double> dir_y;
  std::vector<double> dir_z;
  std::vector<double> pol_x;
  std::vector<double> pol_y;
  std::vector<double> pol_z;
  std::vector<double> weight;

  std::vector<std::string> origin_table;
  std::vector<std::string> material_table;
  std::unordered_map<std::string, int> origin_lookup;
  std::unordered_map<std::string, int> material_lookup;

  void Clear();
  int OriginIndex(const std::string& name);
  int MaterialIndex(const std::string& name);
};

struct OpticalCache {
  double min_nm = 0.0;
  double max_nm = 0.0;
  int samples = 0;
  double ref_temperature_k = 293.15;
  double ref_pressure_pa = 101325.0;
  std::vector<double> lambda_nm;
  std::vector<double> n_ref;
  std::vector<double> n_group_ref;
  std::vector<double> dn_dlambda;
  std::vector<double> inv_lambda2;
};

struct StepSummary {
  int event_id = 0;
  int track_id = 0;
  int step_index = 0;
  std::string material;
  double beta = 0.0;
  double step_length_mm = 0.0;
  double mean_cherenkov = 0.0;
  int cherenkov_count = 0;
  double mean_scint = 0.0;
  int scint_count = 0;
  double mean_window = 0.0;
  int window_count = 0;
};

struct Metrics {
  int total_photons = 0;
  int generated_photons = 0;
  int thinned_photons = 0;
  int capped_photons = 0;
  int capped_steps = 0;
  int extrapolated_photons = 0;
  int extrapolated_events = 0;
  int unknown_material_steps = 0;
  std::map<std::string, int> origin_counts;
  std::map<std::string, int> material_counts;
};

class PhotonGenerator {
 public:
  explicit PhotonGenerator(const PhaseIIConfig& config);

  bool LoadPhase1Data();
  bool Generate();
  const std::vector<Photon>& Photons() const;
  const PhotonSoA& PhotonsSoA() const;
  const std::vector<StepSummary>& StepSummaries() const;
  const Metrics& GetMetrics() const;
  const std::string& LastError() const;

 private:
  bool BuildPhase1Overrides();
  bool LoadPhase1Nodes();
  bool ValidateTables();
  void RecordExtrapolation(int event_id);
  double ComputeBeta(const std::string& particle, double px_mev, double py_mev, double pz_mev) const;
  double ParticleCharge(const std::string& particle) const;
  double EvaluateField(const FieldConfig& field, double x_mm, double y_mm, double z_mm) const;
  const OpticalCache& GetCache(const std::string& material_key, const MaterialModel& material);
  double Interpolate(const std::vector<double>& xs, const std::vector<double>& ys, double x) const;
  double CherenkovMean(const MaterialModel& material, double beta, double step_length_mm,
                       double lambda_min_nm, double lambda_max_nm, double charge,
                       const OpticalCache& cache, double delta_n, std::vector<double>& lambda_grid,
                       std::vector<double>& cdf) const;
  double SampleWavelength(const std::vector<double>& lambda_grid, const std::vector<double>& cdf, double u) const;
  void AppendPhoton(Photon photon);

  PhaseIIConfig config_;
  MaterialLibrary materials_;
  std::string last_error_;

  std::map<std::pair<int, int>, TrackInfo> tracks_;
  std::map<std::tuple<int, int, int>, std::pair<int, int>> nodes_;
  std::map<int, bool> extrapolated_events_;
  std::map<std::string, OpticalCache> caches_;
  std::vector<StepRecord> steps_;
  std::vector<Photon> photons_;
  PhotonSoA photons_soa_;
  std::vector<StepSummary> summaries_;
  Metrics metrics_;

  std::mt19937_64 rng_;
  TabulatedFunction sensor_pde_;
  TabulatedFunction sensor_filter_;
  bool valid_ = true;
};

}  // namespace phase2
