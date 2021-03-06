#ifndef BEHAVIOR_FSM_H_
#define BEHAVIOR_FSM_H_
#include <map>
#include <string>
#include <vector>
#include "simulator.h"
#include "vehicle.h"
using std::map;
using std::string;
using std::vector;

class BehaviorFSM {
 public:
  BehaviorFSM() { configure(); }
  map<string, int> lane_direction = {
      {"PLCL", -1}, {"LCL", -1}, {"LCR", 1}, {"PLCR", 1}};

  vector<Vehicle> choose_next_state(
      const map<int, vector<Vehicle>>& predictions);
  void refresh_ego(const Vehicle& ego, double dt);
  void realize_next_state(vector<Vehicle> trajectory);
  double cal_safe_distance(double v) const;
  bool get_vehicle_behind(const map<int, vector<Vehicle>>& predictions,
                          int lane, Vehicle& rVehicle,
                          bool in_safe = true) const;
  bool get_vehicle_ahead(const map<int, vector<Vehicle>>& predictions, int lane,
                         Vehicle& rVehicle, bool in_safe = true) const;

 private:
  vector<string> successor_states();
  vector<Vehicle> generate_trajectory(
      string state, const map<int, vector<Vehicle>>& predictions);
  vector<double> get_kinematics(const map<int, vector<Vehicle>>& predictions,
                                int lane);
  vector<Vehicle> constant_speed_trajectory();
  vector<Vehicle> keep_lane_trajectory(
      const map<int, vector<Vehicle>>& predictions);
  vector<Vehicle> lane_change_trajectory(
      string state, const map<int, vector<Vehicle>>& predictions);
  vector<Vehicle> prep_lane_change_trajectory(
      string state, const map<int, vector<Vehicle>>& predictions);
  void increment(double dt);
  vector<Vehicle> generate_predictions(int horizon = 2);
  void configure();

 public:
  Vehicle ego_;
  double dt;
  double target_speed;
  double max_acceleration;
  double goal_s;
  int goal_lane;
  int lanes_available;
};

#endif
