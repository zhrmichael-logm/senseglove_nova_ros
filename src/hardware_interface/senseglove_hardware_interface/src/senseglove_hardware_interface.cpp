// Copyright 2020 SenseGlove
#include "senseglove_hardware_interface/senseglove_hardware_interface.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <sstream>
#include <string>

#include <urdf/model.h>

using hardware_interface::JointHandle;
using hardware_interface::JointStateHandle;
using hardware_interface::PositionJointInterface;

SenseGloveHardwareInterface::SenseGloveHardwareInterface(std::unique_ptr<senseglove::SenseGloveSetup> setup)
  : senseglove_setup_(std::move(setup)), num_gloves_(this->senseglove_setup_->size())
{
}

bool SenseGloveHardwareInterface::init(ros::NodeHandle& nh, ros::NodeHandle& /* robot_hw_nh */)
{
  // Initialize realtime publisher for the SenseGlove states
  std::string handedness[2] = { "/lh", "/rh" };
  this->senseglove_state_pub_ =
      std::make_unique<realtime_tools::RealtimePublisher<senseglove_shared_resources::SenseGloveState>>(
          nh,
          "/" + this->senseglove_setup_->getSenseGloveRobot(0).getName() + handedness[this->senseglove_setup_->getSenseGloveRobot(0).getRight()] + "/senseglove_states/",
          1);
  
  this->vive_tracker_sub_ = std::make_unique<ros::Subscriber>(nh.subscribe("/" + this->senseglove_setup_->getSenseGloveRobot(0).getName() + handedness[this->senseglove_setup_->getSenseGloveRobot(0).getRight()] + "/vive_tracker_pose", 1, &SenseGloveHardwareInterface::viveTrackerCallback, this));
  // this->vive_tracker_sub_ = std::make_unique<ros::Subscriber>(nh.subscribe("/vive/LHR_D381725E_pose", 1, &SenseGloveHardwareInterface::viveTrackerCallback, this));

  this->uploadJointNames(nh);

  num_joints_ = 20;  // make dependent on size of senseglove robot
  this->reserveMemory();

  // Start ethercat cycle in the hardware
  this->senseglove_setup_->startCommunication(true);

  for (size_t i = 0; i < num_gloves_; ++i)
  {
    // Initialize interfaces for each joint
    for (size_t k = 0; k < num_joints_; ++k)
    {
      senseglove::Joint& joint = this->senseglove_setup_->getSenseGloveRobot(i).getJoint(k);
      ROS_DEBUG_STREAM("Obtained necessary joint");

      // Create joint state interface
      JointStateHandle joint_state_handle(joint.getName(), &joint_position_[i][k], &joint_velocity_[i][k], &joint_effort_[i][k]);
      ROS_DEBUG_STREAM("joint state interface handle created");
      joint_state_interface_.registerHandle(joint_state_handle);
      ROS_DEBUG_STREAM("joint state interface handle registered");

      if (joint.getActuationMode() == senseglove::ActuationMode::position)
      {
        // Create position joint interface
        JointHandle joint_position_handle(joint_state_handle, &joint_position_command_[i][k]);
        position_joint_interface_.registerHandle(joint_position_handle);
      }
      else if (joint.getActuationMode() == senseglove::ActuationMode::torque)
      {
        // Create effort joint interface
        JointHandle joint_effort_handle_(joint_state_handle, &joint_effort_command_[i][k]);
        effort_joint_interface_.registerHandle(joint_effort_handle_);
      }
      ROS_DEBUG_STREAM("Handles registered");

      // Create velocity joint interface
      JointHandle joint_velocity_handle(joint_state_handle, &joint_velocity_command_[i][k]);
      velocity_joint_interface_.registerHandle(joint_velocity_handle);

      // Prepare joints for actuation
      if (joint.canActuate())
      {
        // Set the first target as the current position
        if (senseglove_setup_->getSenseGloveRobot(i).updateGloveData(ros::Duration(0.0)))
        {
          joint_position_[i][k] = joint.getPosition();
          joint_velocity_[i][k] = joint.getVelocity();
          joint_effort_[i][k] = 0.0;
        }

        if (joint.getActuationMode() == senseglove::ActuationMode::position)
        {
          joint_effort_command_[i][k] = 0.0;
        }
        else if (joint.getActuationMode() == senseglove::ActuationMode::torque)
        {
          joint_effort_command_[i][k] = 0.0;
        }
      }
    }
  }
  ROS_INFO("Successfully actuated all joints");

  this->registerInterface(&this->joint_state_interface_);
  this->registerInterface(&this->position_joint_interface_);
  this->registerInterface(&this->effort_joint_interface_);

  this->calibrate();

  return true;
}

void SenseGloveHardwareInterface::read(const ros::Time& /* time */, const ros::Duration& elapsed_time)
{
  // read senseglove robot data
  for (size_t i = 0; i < num_gloves_; ++i)
  {
    if (senseglove_setup_->getSenseGloveRobot(i).updateGloveData(elapsed_time))
    {
      for (size_t k = 0; k < num_joints_; ++k)
      {
        senseglove::Joint& joint = senseglove_setup_->getSenseGloveRobot(i).getJoint(k);

        
        if(!senseglove_setup_->getSenseGloveRobot(i).getRight() && k%4 == 3) {
            joint_position_[i][k] = -joint.getPosition();       // finger_brake
            // std::cout << joint.getName() << std::endl;
        }

        else joint_position_[i][k] = joint.getPosition();
        joint_velocity_[i][k] = joint.getVelocity();
        joint_effort_[i][k] = joint.getTorque();
      }
      this->updateSenseGloveState();
    }
  }
}

void SenseGloveHardwareInterface::write(const ros::Time& /* time */, const ros::Duration& /*&elapsed_time*/)
{
  for (size_t i = 0; i < num_gloves_; ++i)
  {
    // Splice joint_effort_command vector into vectors for ffb and buzz commands
    int j = 0;
    int h = 0;
    senseglove::SenseGloveRobot& robot = senseglove_setup_->getSenseGloveRobot(i);
    for (size_t k = 0; k < num_joints_; k++)
    {
      senseglove::Joint& joint = robot.getJoint(k);
      if (joint.canActuate())
      {
        if (joint.getActuationMode() == senseglove::ActuationMode::position)
        {
          if (j % 2 == 1)
          {
            joint_last_position_command_[i][h] = joint_position_command_[i][k];
            h++;
          }
          else
          {
            joint_last_buzz_command_[i][h] = joint_position_command_[i][k];
          }

          if (j == 9)  // actuators 0 - 9
          {
            // robot.actuateEffort(joint_last_position_command_[i]);
            // robot.actuateBuzz(joint_last_buzz_command_[i]);
            robot.actuateEffortBuzz(joint_last_position_command_[i], joint_last_buzz_command_[i]);
            // std::vector<double> temp = joint_last_position_command_[i];
            // std::stringstream ss;
            // ss << "Data Retrieved: \n";
            // std::copy(temp.begin(), temp.end(), std::ostream_iterator<double>(ss, " "));
            // ss << std::endl;
            // ROS_INFO_STREAM("joint_last_position_command_[i]: \n" << ss.str());
            
          }
        }
        else if (joint.getActuationMode() == senseglove::ActuationMode::torque)
        {
          if (j % 2 == 1)
          {
            joint_last_effort_command_[i][j] = joint_effort_command_[i][k];
            h++;
          }
          else
          {
            joint_last_buzz_command_[i][j] = joint_effort_command_[i][k];
          }

          if (j == 9)  // actuators 0 - 9
          {
            robot.actuateEffort(joint_last_effort_command_[i]);
            robot.actuateBuzz(joint_last_buzz_command_[i]);
          }
        }
        j++;
      }
    }
    j = 0;
    h = 0;
  }
}

void SenseGloveHardwareInterface::calibrate()
{
  for (size_t i = 0; i < num_gloves_; ++i)
  {
    senseglove::SenseGloveRobot& robot = senseglove_setup_->getSenseGloveRobot(i);
    robot.calibrteHandProfile();
  }
}

void SenseGloveHardwareInterface::viveTrackerCallback(const geometry_msgs::PoseWithCovarianceStampedConstPtr& msg)
{
  // ROS_INFO("Tracker data received");
  senseglove::SenseGloveRobot& robot = senseglove_setup_->getSenseGloveRobot(0);
  SGCore::Kinematics::Vect3D tracker_position;
  SGCore::Kinematics::Quat tracker_rotation;
  tracker_position.x = msg->pose.pose.position.x * 1000;
  tracker_position.y = msg->pose.pose.position.y * 1000;
  tracker_position.z = msg->pose.pose.position.z * 1000;
  tracker_rotation.x = msg->pose.pose.orientation.x;
  tracker_rotation.y = msg->pose.pose.orientation.y;
  tracker_rotation.z = msg->pose.pose.orientation.z;
  tracker_rotation.w = msg->pose.pose.orientation.w;
  robot.updateTrackerData(tracker_position, tracker_rotation);
  
}

void SenseGloveHardwareInterface::uploadJointNames(ros::NodeHandle& nh) const
{
  std::vector<std::string> joint_names;
  for (const auto& joint : *this->senseglove_setup_)
  {
    joint_names.push_back(joint.getName());
  }
  std::sort(joint_names.begin(), joint_names.end());
  nh.setParam("/senseglove/joint_names", joint_names);
}

void SenseGloveHardwareInterface::reserveMemory()
{
  joint_position_.resize(num_gloves_);
  joint_position_command_.resize(num_gloves_);
  joint_last_position_command_.resize(num_gloves_);
  joint_velocity_.resize(num_gloves_);
  joint_velocity_command_.resize(num_gloves_);
  joint_effort_.resize(num_gloves_);
  joint_effort_command_.resize(num_gloves_);
  joint_last_effort_command_.resize(num_gloves_);
  joint_last_buzz_command_.resize(num_gloves_);
  for (unsigned int i = 0; i < num_gloves_; ++i)
  {
    joint_position_[i].resize(num_joints_, 0.0);
    joint_position_command_[i].resize(num_joints_, 0.0);
    joint_last_position_command_[i].resize(5, 0.0);
    joint_velocity_[i].resize(num_joints_, 0.0);
    joint_velocity_command_[i].resize(num_joints_, 0.0);
    joint_effort_[i].resize(num_joints_, 0.0);
    joint_effort_command_[i].resize(num_joints_, 0.0);
    joint_last_effort_command_[i].resize(5, 0.0);
    joint_last_buzz_command_[i].resize(5, 0.0);
  }

  // senseglove_state_pub_->msg_.joint_names.resize(num_gloves_ * num_joints_);
  // senseglove_state_pub_->msg_.position.resize(num_gloves_ * num_joints_);
  // senseglove_state_pub_->msg_.absolute_velocity.resize(num_gloves_ * num_joints_);
  // senseglove_state_pub_->msg_.hand_position.resize(num_gloves_ * num_joints_);
  // senseglove_state_pub_->msg_.finger_tip_positions.resize(5);
  senseglove_state_pub_->msg_.wrist_position.resize(num_gloves_);
  senseglove_state_pub_->msg_.wrist_rotation.resize(num_gloves_);
  senseglove_state_pub_->msg_.hand_angles.resize(num_gloves_ * 15);
  senseglove_state_pub_->msg_.joint_positions.resize(num_gloves_ * num_joints_);
  senseglove_state_pub_->msg_.joint_rotations.resize(num_gloves_ * num_joints_);
}

void SenseGloveHardwareInterface::updateSenseGloveState()
{
  if (!senseglove_state_pub_->trylock())
  {
    return;
  }

  senseglove_state_pub_->msg_.header.stamp = ros::Time::now();
  for (size_t i = 0; i < num_gloves_; ++i)
  {
    senseglove::SenseGloveRobot& robot = senseglove_setup_->getSenseGloveRobot(i);
    senseglove_state_pub_->msg_.wrist_position[i].x = robot.getWristPos().x;
    senseglove_state_pub_->msg_.wrist_position[i].y = robot.getWristPos().y;
    senseglove_state_pub_->msg_.wrist_position[i].z = robot.getWristPos().z;
    senseglove_state_pub_->msg_.wrist_rotation[i].x = robot.getWristRot().x;
    senseglove_state_pub_->msg_.wrist_rotation[i].y = robot.getWristRot().y;
    senseglove_state_pub_->msg_.wrist_rotation[i].z = robot.getWristRot().z;
    senseglove_state_pub_->msg_.wrist_rotation[i].w = robot.getWristRot().w;
    for (size_t k = 0; k < num_joints_; ++k)
    {
      // senseglove::Joint& joint = robot.getJoint(k);
      // senseglove_state_pub_->msg_.header.stamp = ros::Time::now();
      // senseglove_state_pub_->msg_.joint_names[i] = joint.getName();
      // senseglove_state_pub_->msg_.position[i] = joint.getPosition();
      // senseglove_state_pub_->msg_.absolute_velocity[i] = joint.getVelocity();     
      senseglove_state_pub_->msg_.joint_positions[k].x = robot.getJointPos(k).x;
      senseglove_state_pub_->msg_.joint_positions[k].y = robot.getJointPos(k).y;
      senseglove_state_pub_->msg_.joint_positions[k].z = robot.getJointPos(k).z;

      senseglove_state_pub_->msg_.joint_rotations[k].x = robot.getJointRot(k).x;
      senseglove_state_pub_->msg_.joint_rotations[k].y = robot.getJointRot(k).y;
      senseglove_state_pub_->msg_.joint_rotations[k].z = robot.getJointRot(k).z;
      senseglove_state_pub_->msg_.joint_rotations[k].w = robot.getJointRot(k).w;
    }
    for (int j = 0; j < 5*3; ++j)
    {
      senseglove_state_pub_->msg_.hand_angles[j].x = robot.getHandAngles(j).x;
      senseglove_state_pub_->msg_.hand_angles[j].y = robot.getHandAngles(j).y;
      senseglove_state_pub_->msg_.hand_angles[j].z = robot.getHandAngles(j).z;
    }
  }

  senseglove_state_pub_->unlockAndPublish();
}


