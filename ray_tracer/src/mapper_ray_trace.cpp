#include <ros/ros.h>
#include <std_msgs/String.h>
#include <lab2_msgs/occupancy_update.h>
#include <lab2_msgs/index.h>
#include <nav_msgs/MapMetaData.h>
#include <geometry_msgs/Pose.h>
#include <sensor_msgs/LaserScan.h>
#include <set>
#include <tf/transform_datatypes.h>
#include <vector>

// Definitions
struct Point {
	float x;
	float y;

    friend bool operator<(const Point &curr, const Point &other) {
		if (curr.x != other.x) {
			return curr.x < other.x;
		} else {
			return curr.y < other.y;
		}
	}
}; 


template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

// Global variables
nav_msgs::MapMetaData _map_data;
geometry_msgs::Pose _robot_pose;
sensor_msgs::LaserScan _laser_scan;


// Outputs an array of unfilled cell coordinates (normalized to occupancy grid index format)
//Bresenham line algorithm (pass empty vectors)
// Usage: (x0, y0) is the first point and (x1, y1) is the second point. The calculated
//        points (x, y) are stored in the x and y vector. x and y should be empty 
//	  vectors of integers and shold be defined where this function is called from.
void bresenham(Point pt1, Point pt2, std::set<Point>& cells) {

	int x0 = int(round(pt1.x));
	int x1 = int(round(pt1.y));
	int y0 = int(round(pt2.x));
	int y1 = int(round(pt2.y));

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int dx2 = x1 - x0;
    int dy2 = y1 - y0;
    
    const bool s = abs(dy) > abs(dx);

    if (s) {
        int dx2 = dx;
        dx = dy;
        dy = dx2;
    }

    int inc1 = 2 * dy;
    int d = inc1 - dx;
    int inc2 = d - dx;

    Point cell0 = {.x = x0, .y = y0 };
    cells.insert(cell0);

    while (x0 != x1 || y0 != y1) {
        if (s) y0+=sgn(dy2); else x0+=sgn(dx2);
        if (d < 0) d += inc1;
        else {
            d += inc2;
            if (s) x0+=sgn(dx2); else y0+=sgn(dy2);
        }

        //Add point to vector
        Point new_cell = {.x = x0, .y = y0 };
    	cells.insert(new_cell);
    }
}



// Outputs the normalized position for occupancy grid
Point robot_pose_to_norm_position(geometry_msgs::Pose &robot_pose, nav_msgs::MapMetaData &map_data, Point min_xy_corner, Point max_xy_corner) {

	// find robot position using top-left corner origin convention
	float top_left_x = max_xy_corner.y - robot_pose.position.y;
	float top_left_y = max_xy_corner.x - robot_pose.position.x;

	// convert to occupancy grid distances
	float norm_x = top_left_x / map_data.resolution;
	float norm_y = top_left_y / map_data.resolution;

	Point norm_position = {norm_x, norm_y};
	return norm_position;
}

float robot_pose_to_yaw(geometry_msgs::Pose &robot_pose) {
	geometry_msgs::Quaternion robot_quat = robot_pose.orientation;
	double yaw = tf::getYaw(robot_quat);
	return yaw;
}


// conversion to normalized range for occupancy grid
float range_to_norm_range(float range, float map_resolution) {
	float norm_range = range / map_resolution;
	return norm_range;
}


// Outputs the normalized laser points for occupancy grid
void laser_scan_to_points(Point robot_norm_position, float yaw, sensor_msgs::LaserScan &scan, nav_msgs::MapMetaData &map_data, std::vector<Point> &norm_pts) {

	std::vector<float> ranges = scan.ranges;
	float curr_angle = scan.angle_min;
	float true_laser_angle = curr_angle + yaw;
	float curr_range;
	for (int i=0; i<ranges.size(); ++i) {
		curr_angle += scan.angle_increment;
		true_laser_angle = curr_angle + yaw;
		curr_range = range_to_norm_range(ranges[i], map_data.resolution);


		float laser_add_x = curr_range * cos(true_laser_angle);
		float laser_add_y = robot_norm_position.y + curr_range * sin(true_laser_angle);

		int laser_x = robot_norm_position.x + laser_add_x;
		int laser_y = robot_norm_position.y + laser_add_y;
		Point curr_laser_point = {.x= laser_x, .y= laser_y };
		norm_pts[i] = curr_laser_point;
	}

	return;
}

// Get all filled points for publishing
// input: normalized float points from laser scan
// output: integer points for publishing
void get_filled_cells(std::vector<Point> &norm_pts, std::vector<lab2_msgs::index> &filled_cells) {

	for (int i=0; i<norm_pts.size(); i++) {
		Point scan_point = norm_pts[i];
		lab2_msgs::index curr_index;
		curr_index.row = int(round(scan_point.x));
		curr_index.col = int(round(scan_point.y));
		filled_cells.push_back(curr_index);
	}

	return;
}



// Utility Functions
void points_to_indices(std::vector<Point> points, std::vector<lab2_msgs::index> indices) {

	for (int i=0; i<points.size(); i++) {
		lab2_msgs::index curr_index;
		curr_index.row = int(round(points[i].x));
		curr_index.col = int(round(points[i].y));
		indices.push_back(curr_index);
	}
}

// ******************
// Callback functions

void map_details_callback(const nav_msgs::MapMetaData::ConstPtr& map_details) {

	_map_data = *map_details;

}


void refined_pose_callback(const geometry_msgs::Pose::ConstPtr& refined_pose) {

	_robot_pose = *refined_pose;

}


void scan_callback(const sensor_msgs::LaserScan::ConstPtr& scan) {

	_laser_scan = *scan;

}


// *********************
// Main function
int main(int argc, char **argv) {

	ros::init(argc, argv, "mapper_ray_trace");

	ros::NodeHandle n;

	// Initialize rostopics
	// Note: 2nd param is buffer size
	ros::Publisher raytrace_output = n.advertise<lab2_msgs::occupancy_update>("raytrace_output", 100);

	ros::Subscriber map_details_sub = n.subscribe<nav_msgs::MapMetaData>("map_details", 100, map_details_callback);
	ros::Subscriber refined_pose_sub = n.subscribe<geometry_msgs::Pose>("refined_pose", 100, refined_pose_callback);
	ros::Subscriber scan_sub = n.subscribe<sensor_msgs::LaserScan>("scan", 100, scan_callback);


	Point min_xy_corner = {.x=-2, .y=-2};
	Point max_xy_corner = {.x=2, .y=2};

	ros::Rate loop_rate(10);

	while (ros::ok()) {

		// *******************************
		// Conversion
		Point robot_norm_position = robot_pose_to_norm_position(_robot_pose, _map_data, min_xy_corner, max_xy_corner);

		float robot_yaw = robot_pose_to_yaw(_robot_pose);

		// *******************************
		// Get normalized Laser Points
		std::vector<Point> laser_pts;
		laser_scan_to_points(robot_norm_position, robot_yaw, _laser_scan, _map_data, laser_pts);


		// *******************************
		// Get filled cells
		std::vector<lab2_msgs::index> filled_cells;
		get_filled_cells(laser_pts, filled_cells);


		// *******************************
		// Getting unfilled_cells
		std::set<Point> unfilled_cells_set;
		for (int i=0; i<laser_pts.size(); i++) {

			bresenham(robot_norm_position, laser_pts[i], unfilled_cells_set);
		}

		//Convert set to vector for publishing
		std::vector<lab2_msgs::index> unfilled_cells;
		std::set<Point>::iterator it;
		for (it = unfilled_cells_set.begin(); it != unfilled_cells_set.end(); ++it) {
			Point curr_pt = *it;
			lab2_msgs::index curr_index;
			curr_index.row = int(curr_pt.x);
			curr_index.col = int(curr_pt.y);
			unfilled_cells.push_back(curr_index);
		}

		// *******************************
		// Publish ray_trace output

		lab2_msgs::occupancy_update output_msg;
		output_msg.filled = filled_cells;
		output_msg.unfilled = unfilled_cells;

		raytrace_output.publish(output_msg);

	}

	return 0;

}


