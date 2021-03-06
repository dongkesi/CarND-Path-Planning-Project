#include "trajectory.h"
#include <algorithm>
#include <map>
#include <vector>
#include "config.h"
#include "helper.h"
#include "lane_map.h"
#include "sensor_fusion.h"
#include "simulator.h"
#include "spline.h"
#include "vehicle.h"

using std::map;
using std::vector;

void Trajectory::UpdateLocationData(Simulator& simulator,
                                    SensorFusion& sensor) {
  EgoVehicle& ego = sensor.get_ego();
  PrevPathData& prev_path = simulator.get_prev_path();

  this->ref_x = ego.car_x;
  this->ref_y = ego.car_y;
  // Sensor output yaw is deg
  this->ref_yaw = deg2rad(ego.car_yaw);
  this->car_x = ego.car_x;
  this->car_y = ego.car_y;
  this->car_lane = (int)(ego.car_d / 4);
  this->car_s = ego.car_s;
  this->end_path_s = prev_path.x.size() > 2 ? prev_path.end_s : ego.car_s;
  this->previous_path_x = prev_path.x;
  this->previous_path_y = prev_path.y;
}

void Trajectory::UpdatePredictionData(
    const map<int, vector<Vehicle>>& prediction) {
  this->predictions = prediction;
}

// Input unit: m/s
double Trajectory::SmoothSpeed(double cur_v, double goal_v) {
  double new_v = cur_v;
  if (this->startup) {
    this->accl_w = 0.02;
  } else {
    this->accl_w = 0.01;
  }
  int diff_lane = abs(this->lane - this->car_lane);
  if (diff_lane == 1) {
    this->accl_w = 0.005;
  } else if (diff_lane == 2) {
    cout << "diff_lane == 2" << endl;
    this->accl_w = 0.001;
  } else {
  }
  double d_accl = ParameterConfig::max_acceleration * this->accl_w;
  if (this->is_accl) {
    if (cur_v < goal_v) new_v += d_accl;
  } else {
    if (cur_v > goal_v * 0.5) new_v -= d_accl;
  }
  if (new_v > ParameterConfig::target_speed) {
    new_v = ParameterConfig::target_speed;
  }
  if (new_v < 0) {
    new_v = 0.1;
  }
  this->car_speed = new_v;
  cout << "fast real ref_v: " << new_v << endl;
  return new_v;
}

// goal_v unit: m/s
void Trajectory::UpdateBehaviorData(double goal_v, int goal_lane) {
  this->is_accl = goal_v > this->car_speed ? true : false;
  if (this->car_speed < 0.7 * goal_v)
    this->startup = true;
  else
    this->startup = false;
  this->ref_vel = goal_v;  // mps2mph(SmoothSpeed(car_speed, goal_v));
  this->lane = goal_lane;
}

void Trajectory::GenerateTrajectory(const map<int, vector<Vehicle>>& prediction,
                                    Simulator& simulator, SensorFusion& sensor,
                                    double goal_v, int goal_lane) {
  UpdateLocationData(simulator, sensor);
  UpdateBehaviorData(goal_v, goal_lane);
  UpdatePredictionData(prediction);
  _GenerateTrajectory();
}

void Trajectory::CoordinateMap2Car(double& x_point, double& y_point) {
  double shift_x = x_point - ref_x;
  double shift_y = y_point - ref_y;

  x_point = (shift_x * cos(0 - ref_yaw) - shift_y * sin(0 - ref_yaw));
  y_point = (shift_x * sin(0 - ref_yaw) + shift_y * cos(0 - ref_yaw));
}

void Trajectory::CoordinateCar2Map(double& x_point, double& y_point) {
  double x_ref = x_point;
  double y_ref = y_point;

  x_point = (x_ref * cos(ref_yaw) - y_ref * sin(ref_yaw));
  y_point = (x_ref * sin(ref_yaw) + y_ref * cos(ref_yaw));

  x_point += ref_x;
  y_point += ref_y;
}

bool Trajectory::EmergencyBraking() {
  double min_s = ParameterConfig::goal_s;
  bool found_vehicle = false;
  Vehicle temp_vehicle;
  for (auto it = predictions.cbegin(); it != predictions.cend(); ++it) {
    temp_vehicle = it->second[0];
    if (temp_vehicle.lane == this->car_lane && temp_vehicle.s > this->car_s &&
        temp_vehicle.s < min_s) {
      min_s = temp_vehicle.s;
    }
  }
  if (min_s - this->car_s < ParameterConfig::safe_distance) {
    // this->ref_vel = temp_vehicle.v;
    // this->startup = true;
    if (this->car_s > ParameterConfig::goal_s - 50) {
      this->ref_vel = this->car_speed * 0.9;
      this->accl_w  = 0.001;
      this->is_accl = false;
      cout << "EmergencyBraking: in the end of lap" << endl;
      return false;
    }
    this->ref_vel = this->car_speed * 0.5;
    this->accl_w  = 0.001;
    this->is_accl = false;
    return true;
  }


  return false;
}

void Trajectory::Fit() {
  vector<double> ptsx;
  vector<double> ptsy;
  auto prev_size = previous_path_x.size();
  //   prev_size = 0;
  if (prev_size < 2) {
    double prev_car_x = car_x - cos(ref_yaw);
    double prev_car_y = car_y - sin(ref_yaw);

    ptsx.push_back(prev_car_x);
    ptsx.push_back(car_x);

    ptsy.push_back(prev_car_y);
    ptsy.push_back(car_y);
  } else {
    ref_x = previous_path_x[prev_size - 1];
    ref_y = previous_path_y[prev_size - 1];
    double ref_x_prev = previous_path_x[prev_size - 2];
    double ref_y_prev = previous_path_y[prev_size - 2];

    // ref_x = previous_path_x[1];
    // ref_y = previous_path_y[1];
    // double ref_x_prev = previous_path_x[0];
    // double ref_y_prev = previous_path_y[0];

    ref_yaw = atan2(ref_y - ref_y_prev, ref_x - ref_x_prev);

    ptsx.push_back(ref_x_prev);
    ptsx.push_back(ref_x);

    ptsy.push_back(ref_y_prev);
    ptsy.push_back(ref_y);
  }
  Map& lm = Map::getInstance();
  vector<double> next_wp0 = lm.getXY(end_path_s + 30, (2 + 4 * lane));
  vector<double> next_wp1 = lm.getXY(end_path_s + 60, (2 + 4 * lane));
  vector<double> next_wp2 = lm.getXY(end_path_s + 90, (2 + 4 * lane));

  ptsx.push_back(next_wp0[0]);
  ptsx.push_back(next_wp1[0]);
  ptsx.push_back(next_wp2[0]);

  ptsy.push_back(next_wp0[1]);
  ptsy.push_back(next_wp1[1]);
  ptsy.push_back(next_wp2[1]);

  for (int i = 0; i < ptsx.size(); i++) {
    CoordinateMap2Car(ptsx[i], ptsy[i]);
  }

  s.set_points(ptsx, ptsy);
}

void Trajectory::_GenerateTrajectory() {
  if (EmergencyBraking()) {
    cout << "EmergencyBraking" << endl;
    size_t pop_size = previous_path_x.size() - 3;
    for (size_t i = 0; i < pop_size; i++) {
      this->previous_path_x.pop_back();
      this->previous_path_y.pop_back();
    }
  }
  Fit();
  next_x_vals.clear();
  next_y_vals.clear();
  for (int i = 0; i < previous_path_x.size(); i++) {
    next_x_vals.push_back(previous_path_x[i]);
    next_y_vals.push_back(previous_path_y[i]);
  }

  double target_x = 30.0;
  double target_y = s(target_x);
  double target_dist = sqrt(target_x * target_x + target_y * target_y);
  double x_add_on = 0;

  for (int i = 1; i <= 50 - previous_path_x.size(); i++) {
    double vel = SmoothSpeed(this->car_speed, this->ref_vel);
    // double vel = mps2mph(this->car_speed);
    // double x_point = x_add_on + (0.02 * vel);
    double N = target_dist / (0.02 * vel);
    double x_point = x_add_on + target_x / N;

    if (x_point > target_x) {
      cout << "break!!!" << endl;
      break;
    }
    double y_point = s(x_point);

    x_add_on = x_point;

    CoordinateCar2Map(x_point, y_point);

    next_x_vals.push_back(x_point);
    next_y_vals.push_back(y_point);
  }
  //   for(size_t i = 0; i != next_x_vals.size(); i++) {
  //       cout << "next_x: " << next_x_vals[i] << " next_y: " << next_y_vals[i]
  //       << endl;
  //   }
}