// Copyright 2020 SenseGlove.
#ifndef ROS_WORKSPACE_SENSEGLOVE_ROBOT_H
#define ROS_WORKSPACE_SENSEGLOVE_ROBOT_H

#include "senseglove_hardware/joint.h"
// #include "SenseGlove.h"
#include "HapticGlove.h"
#include "NovaGlove.h"
#include "BasicHandModel.h"
#include "HandPose.h"
#include "DeviceList.h"
#include "Tracking.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <urdf/model.h>

namespace senseglove
{
class SenseGloveRobot
{
private:
  // SGCore::SG::SenseGlove senseglove_;
  std::shared_ptr<SGCore::HapticGlove> senseglove_;
  // SGCore::SG::SG_GloveInfo model_;
  SGCore::Nova::Nova_SensorData sensor_data_;
  // SGCore::SG::SG_GlovePose glove_pose_;
  // SGCore::SG::SG_HandProfile hand_profile_;
  SGCore::HandProfile hand_profile_;
  SGCore::Kinematics::BasicHandModel hand_model_;
  // std::vector<SGCore::Kinematics::Vect3D> tip_positions_;
  SGCore::HandPose hand_pose_;
  SGCore::Kinematics::Vect3D wrist_position_; 
  SGCore::Kinematics::Quat wrist_rotation_;
  SGCore::Kinematics::Vect3D trackerPosition = SGCore::Kinematics::Vect3D::zero; // TODO: Take these from your 3rd party API
  SGCore::Kinematics::Quat trackerRotation = SGCore::Kinematics::Quat::identity; //TODO: Take these from your 3rd party API
  ::std::vector<Joint> joint_list_;
  // urdf::Model urdf_;
  const std::string name_;
  const SGCore::DeviceType device_type_;
  const int robot_index_;
  bool is_right_;
  bool updated_;
  std::shared_ptr<SGCore::Nova::NovaGlove> novaglove_;
  SGCore::Tracking tracking_;

public:
  using iterator = std::vector<Joint>::iterator;

  SenseGloveRobot(
    std::shared_ptr<SGCore::HapticGlove> glove, 
    ::std::vector<Joint> jointList, 
    // urdf::Model urdf, 
    int robotIndex,
    bool is_right
  );

  ~SenseGloveRobot();

  /* Delete copy constructor/assignment since the unique_ptr cannot be copied */
  SenseGloveRobot(SenseGloveRobot&) = delete;
  SenseGloveRobot& operator=(SenseGloveRobot&) = delete;

  /* Delete move assignment since string cannot be move assigned */
  SenseGloveRobot(SenseGloveRobot&&) = default;
  SenseGloveRobot& operator=(SenseGloveRobot&&) = delete;

  std::string getName() const;
  int getIndex() const;
  bool getRight();

  void calibrteHandProfile();

  Joint& getJoint(::std::string jointName);

  Joint& getJoint(size_t index);

  void updateTrackerData(SGCore::Kinematics::Vect3D position, SGCore::Kinematics::Quat rotation);

  SGCore::Kinematics::Vect3D getWristPos();

  SGCore::Kinematics::Quat   getWristRot();

  // SGCore::Kinematics::Vect3D getHandPos(int i);

  SGCore::Kinematics::Vect3D getHandAngles(int i);

  SGCore::Kinematics::Vect3D getJointPos(int i);

  SGCore::Kinematics::Quat   getJointRot(int i);

  // SGCore::Kinematics::Vect3D getFingerTip(int i);

  // ros control works exclusively with doubles, but the sendHaptics function works with integers
  void actuateEffort(std::vector<double> effort_command);
  void actuateEffort(double e_0, double e_1, double e_2, double e_3, double e_4);
  void actuateBuzz(std::vector<double> buzz_command);
  void actuateBuzz(double b_0, double b_1, double b_2, double b_3, double b_4);
  void actuateEffortBuzz(std::vector<double> effort_command, std::vector<double> buzz_command);
  void stopActuating();

  size_t size() const;

  iterator begin();
  iterator end();

  // const urdf::Model& getUrdf() const;

  bool updateGloveData(const ros::Duration period);

  /** @brief Override comparison operator */
  friend bool operator==(const SenseGloveRobot& lhs, const SenseGloveRobot& rhs)
  {
    if (lhs.joint_list_.size() != rhs.joint_list_.size())
    {
      return false;
    }
    for (unsigned int i = 0; i < lhs.joint_list_.size(); i++)
    {
      const senseglove::Joint& lhsJoint = lhs.joint_list_.at(i);
      const senseglove::Joint& rhsJoint = rhs.joint_list_.at(i);
      if (lhsJoint != rhsJoint)
      {
        return false;
      }
    }
    return true;
  }

  friend bool operator!=(const SenseGloveRobot& lhs, const SenseGloveRobot& rhs)
  {
    return !(lhs == rhs);
  }

  /** @brief Override stream operator for clean printing */
  friend ::std::ostream& operator<<(std::ostream& os, const SenseGloveRobot& senseGloveRobot)
  {
    for (unsigned int i = 0; i < senseGloveRobot.joint_list_.size(); i++)
    {
      os << senseGloveRobot.joint_list_.at(i) << "\n";
    }
    return os;
  }
};
}  // namespace senseglove

#endif  // ROS_WORKSPACE_SENSEGLOVE_ROBOT_H
