/**
 * @brief PX4Flow plugin
 * @file px4flow.cpp
 * @author M.H.Kabir <mhkabir98@gmail.com>
 * @author Vladimir Ermakov <vooon341@gmail.com>
 *
 * @addtogroup plugin
 * @{
 */
/*
 * Copyright 2014 M.H.Kabir.
 * Copyright 2016 Vladimir Ermakov
 *
 * This file is part of the mavros package and subject to the license terms
 * in the top-level LICENSE file of the mavros repository.
 * https://github.com/mavlink/mavros/tree/master/LICENSE.md
 */

#include <mavros/mavros_plugin.h>

#include <mavros_msgs/OpticalFlow.h>
#include <mavros_msgs/OpticalFlowRad.h>
#include <sensor_msgs/Temperature.h>
#include <sensor_msgs/Range.h>

namespace mavros {
namespace extra_plugins{
/**
 * @brief PX4 Optical Flow plugin
 *
 * This plugin can publish data from PX4Flow camera to ROS
 */
class PX4FlowPlugin : public plugin::PluginBase {
public:
	PX4FlowPlugin() : PluginBase(),
		flow_nh("~px4flow"),
		ranger_fov(0.0),
		ranger_min_range(0.3),
		ranger_max_range(5.0)
	{ }

	void initialize(UAS &uas_)
	{
		PluginBase::initialize(uas_);

		flow_nh.param<std::string>("frame_id", frame_id, "px4flow");

		/** Default rangefinder is Maxbotix HRLV-EZ4 */
		flow_nh.param("ranger_fov", ranger_fov, 0.0);	/** @todo Check MAxbotix HRLV-EZ4 Field-of-View */
		flow_nh.param("ranger_min_range", ranger_min_range, 0.3);
		flow_nh.param("ranger_max_range", ranger_max_range, 5.0);

		flow_pub = flow_nh.advertise<mavros_msgs::OpticalFlow>("raw/optical_flow", 10);
		flow_rad_pub = flow_nh.advertise<mavros_msgs::OpticalFlowRad>("raw/optical_flow_rad", 10);
		range_pub = flow_nh.advertise<sensor_msgs::Range>("ground_distance", 10);
		temp_pub = flow_nh.advertise<sensor_msgs::Temperature>("temperature", 10);
	}

	Subscriptions get_subscriptions()
	{
		return {
			       make_handler(&PX4FlowPlugin::handle_optical_flow),
			       make_handler(&PX4FlowPlugin::handle_optical_flow_rad)
		};
	}

private:
	ros::NodeHandle flow_nh;

	std::string frame_id;

	double ranger_fov;
	double ranger_min_range;
	double ranger_max_range;

	ros::Publisher flow_pub;
	ros::Publisher flow_rad_pub;
	ros::Publisher range_pub;
	ros::Publisher temp_pub;

	void handle_optical_flow(const mavlink::mavlink_message_t *msg, mavlink::common::msg::OPTICAL_FLOW &flow)
	{
		auto header = m_uas->synchronized_header(frame_id, flow.time_usec);

		/**
		 * Raw message with axes mapped to ROS conventions.
		 */
		auto comp_xy = ftf::transform_frame_aircraft_baselink(
			Eigen::Vector3d(
				flow.flow_comp_m_x,
				flow.flow_comp_m_y,
				0.0));

		auto flow_msg = boost::make_shared<mavros_msgs::OpticalFlow>();

		flow_msg->header = header;

		/**
		 * Raw message (no transform for int)
		 *
		 */
		flow_msg->flow_x = flow.flow_x;
		flow_msg->flow_y = flow.flow_y;

		flow_msg->flow_comp_m_x = comp_xy.x();
		flow_msg->flow_comp_m_y = comp_xy.y();

		flow_msg->quality = flow.quality;
		flow_msg->ground_distance = flow.ground_distance;

		flow_pub.publish(flow_msg);
	}

	void handle_optical_flow_rad(const mavlink::mavlink_message_t *msg, mavlink::common::msg::OPTICAL_FLOW_RAD &flow_rad)
	{
		auto header = m_uas->synchronized_header(frame_id, flow_rad.time_usec);

		/**
		 * Raw message with axes mapped to ROS conventions and temp in degrees celsius.
		 *
		 * The optical flow camera is essentially an angular sensor, so conversion is like
		 * gyroscope. (aircraft -> baselink)
		 */
		auto int_xy = ftf::transform_frame_aircraft_baselink(
				Eigen::Vector3d(
					flow_rad.integrated_x,
					flow_rad.integrated_y,
					0.0));
		auto int_gyro = ftf::transform_frame_aircraft_baselink(
				Eigen::Vector3d(
					flow_rad.integrated_xgyro,
					flow_rad.integrated_ygyro,
					flow_rad.integrated_zgyro));

		auto flow_rad_msg = boost::make_shared<mavros_msgs::OpticalFlowRad>();

		flow_rad_msg->header = header;
		flow_rad_msg->integration_time_us = flow_rad.integration_time_us;

		flow_rad_msg->integrated_x = int_xy.x();
		flow_rad_msg->integrated_y = int_xy.y();

		flow_rad_msg->integrated_xgyro = int_gyro.x();
		flow_rad_msg->integrated_ygyro = int_gyro.y();
		flow_rad_msg->integrated_zgyro = int_gyro.z();

		flow_rad_msg->temperature = flow_rad.temperature / 100.0f;	// in degrees celsius
		flow_rad_msg->time_delta_distance_us = flow_rad.time_delta_distance_us;
		flow_rad_msg->distance = flow_rad.distance;
		flow_rad_msg->quality = flow_rad.quality;

		flow_rad_pub.publish(flow_rad_msg);

		// Temperature
		auto temp_msg = boost::make_shared<sensor_msgs::Temperature>();

		temp_msg->header = header;
		temp_msg->temperature = flow_rad_msg->temperature;

		temp_pub.publish(temp_msg);

		// Rangefinder
		/**
		 * @todo: use distance_sensor plugin only to publish this data
		 * (which receives DISTANCE_SENSOR msg with multiple rangefinder
		 * sensors data)
		 *
		 * @todo: suggest modification on MAVLink OPTICAL_FLOW_RAD msg
		 * which removes sonar data fields from it
		 */
		auto range_msg = boost::make_shared<sensor_msgs::Range>();

		range_msg->header = header;

		range_msg->radiation_type = sensor_msgs::Range::ULTRASOUND;
		range_msg->field_of_view = ranger_fov;
		range_msg->min_range = ranger_min_range;
		range_msg->max_range = ranger_max_range;
		range_msg->range = flow_rad.distance;

		range_pub.publish(range_msg);
	}
};
}	// namespace extra_plugins
}	// namespace mavros

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(mavros::extra_plugins::PX4FlowPlugin, mavros::plugin::PluginBase)
