#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2((map_y-y),(map_x-x));

	double angle = fabs(theta-heading);
  angle = min(2*pi() - angle, angle);

  if(angle > pi()/4)
  {
    closestWaypoint++;
  if (closestWaypoint == maps_x.size())
  {
    closestWaypoint = 0;
  }
  }

  return closestWaypoint;
}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}


#define LANE_WIDTH 4.0
#define CHANGE_LANE_PUNISHMENT 0
#define SAFE_DISTANCE 4

#define DEBUG_SWITCH 1

/******************************************************************************
    cal target cost by disance
        input:
            vehicle_s: vehicle's latest predict s 
            vehicle_current_s: vehicle's current s
            target_s: target vehicle's latest predict s 
            target_current_s: target vehicle's current s
            punishment: punish to the target vehicle
        ouput:
            cost, it may cause crash when the cost is too large
*******************************************************************************/
double calCost(double vehicle_s, double vehicle_current_s,double target_s, double target_current_s,double punishment) {
    double cost;
    double distance = 0;

    if((vehicle_s-target_s) * (vehicle_current_s-target_current_s) <= 0)
    {
        //will crash
        cost = 1.0;
        return cost;
    }

    if(abs(vehicle_s-target_s) < SAFE_DISTANCE)
    {
        //too close,can't change lane, otherise it may cause crash
        cost = 1.0;
        return cost;
    }

    distance = abs(vehicle_s - target_s);

    //add punishment
    if(distance < punishment)
    {
        distance = 0;
    }
    else
    {
        distance -= punishment;
    }

    distance = distance / 100;
    
    cost = exp(0-distance);

    return cost;
}

//cal lane number by d
int getLane(float target_d, float lane_width){

    float lane = (target_d/lane_width);
    if(lane < 0)
    {
        return -1;
    }
    else if(lane < 1.0)
    {
        return 0;
    }
    else if(lane < 2.0)
    {
        return 1;
    }
    else if(lane < 3.0)
    {
        return 2;
    }
    else
    {
        return -1;
    }
}


int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }


  //start lane
  int lane = 1;
  //limit velocity target
  double ref_vel = 0.0;

  h.onMessage([&ref_vel, &map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy, &lane](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;
            
          	// TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
          	//prev path size
            int prev_size = previous_path_x.size();
            // punishment to changing lane�� when find it too close to the vehicle in front
            double punishment = 10;
            //Current s of the vehicle
            double current_s = car_s;
            //Current s of the target vehicle
            double current_check_s = 0;

            if(prev_size > 0)
            {
                //using prev path to predict new path
                car_s = end_path_s;
            }

            //find it close and try to slow down or to change lanes
            bool too_close = false;
            //cost of each lane
            vector<double>lane_cost = {0,0,0};
            //current lane that the vehicle runs on
            int cur_lane;

            //get the lane that the vehicle runs on
            if(prev_size > 0)
            {
                cur_lane = getLane(end_path_d, LANE_WIDTH);
                
            }
            else
            {
                cur_lane = lane;
            }


            //check target vehicles, and cal cost of each lane
            for(int i = 0; i < sensor_fusion.size(); i++)
            {
                float d = sensor_fusion[i][6];
                int target_lane = getLane(d, LANE_WIDTH);
                
                double vx = sensor_fusion[i][3];
                double vy = sensor_fusion[i][4];
                double check_speed = sqrt(vx*vx + vy*vy);
                double check_car_s = sensor_fusion[i][5];

                current_check_s = check_car_s;
                
                check_car_s += ((double)prev_size*0.02*check_speed);


                double cost = 0;

                if(target_lane>=0 && target_lane <=2)
                {
                    //valid lane number

                    if(target_lane == cur_lane)
                    {
                        //the target vehicle runs on the same lane
                        if(check_car_s > car_s)
                        {
                            cost = calCost(car_s, current_s,check_car_s,current_check_s, CHANGE_LANE_PUNISHMENT);
                        }
                    }
                    //can't change 2 lanes, so make it cost 1.0(max cost value)
                    else if(abs(cur_lane-target_lane)>1.0)
                    {
                        lane_cost[target_lane] = 1.0;
                    }
                    else 
                    {
                        //cal cost and select lane
                        cost = calCost(car_s, current_s,check_car_s,current_check_s, 0);

                        
                    }

                    //select the max cost as the lane's final cost
                    if(lane_cost[target_lane] < cost)
                    {
                        lane_cost[target_lane] = cost;    
                    }
                }
                
                
                if(d < (2+4*lane+2) && d>(2+4*lane-2))
                {
                    //check if the vehicle is too close to the target vehicle in front

                    if(check_car_s > car_s && (check_car_s - car_s)<30)
                    {
                        //it's too close    
                        too_close = true;
                        
                    }
                   
                }
            }

            if(DEBUG_SWITCH)
            {
                printf("************************************\r\n");
                printf("Lane cose: %f, %f, %f\r\n",lane_cost[0],lane_cost[1],lane_cost[2]);
                printf("************************************\r\n");
            }

            if(too_close)
            {
                //when it's too close, we should select a lane which has the min cost
                vector<double>::iterator best_cost = min_element(begin(lane_cost), end(lane_cost));
               
                int change_lane = distance(begin(lane_cost), best_cost);
   

                if(lane == change_lane || (lane_cost[lane]-lane_cost[change_lane])<0.025 || lane_cost[change_lane] > 0.8)
                {
                    //if the min cost is almost the same as current lane's cost, the vehicle would keep lane and slow down.
                    ref_vel -= 0.224;                  
                }
                else
                {
                    //decide to chane lane
                    if(lane_cost[change_lane] > 0.72)
                    {
                        //slow down if the target lane's cost is too much.
                        ref_vel -= 0.224; 
                    }
                    lane = change_lane;
                }
                if(DEBUG_SWITCH)
                {
                    printf("select lane[%d]\r\n", lane);
                }
            }
            else if(ref_vel<49.5)
            {
                //it is safe, just speed up to the limit speed
                ref_vel += 0.224;
            }

            vector<double> ptsx;
            vector<double> ptsy;

            double ref_x = car_x;
            double ref_y = car_y;
            double ref_yaw = deg2rad(car_yaw);
            
            if(prev_size < 2)
            {
                //no prev path, try to cal it
                double prev_car_x = car_x - cos(car_yaw);
                double prev_car_y = car_y - sin(car_yaw);

                ptsx.push_back(prev_car_x);
                ptsx.push_back(car_x);

                ptsy.push_back(prev_car_y);
                ptsy.push_back(car_y);
            }
            else
            {
                //get 2 prev path point
                ref_x = previous_path_x[prev_size-1];
                ref_y = previous_path_y[prev_size-1];

                double ref_x_prev = previous_path_x[prev_size-2];
                double ref_y_prev = previous_path_y[prev_size-2];

                ref_yaw = atan2(ref_y-ref_y_prev, ref_x-ref_x_prev);
                
                ptsx.push_back(ref_x_prev);
                ptsx.push_back(ref_x);

                ptsy.push_back(ref_y_prev);
                ptsy.push_back(ref_y);

                
            }

            //cal sample point
            vector<double> next_wp0 = getXY(car_s+30, 2+4*lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp1 = getXY(car_s+60, 2+4*lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp2 = getXY(car_s+90, 2+4*lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);

            
            ptsx.push_back(next_wp0[0]);
            ptsx.push_back(next_wp1[0]);
            ptsx.push_back(next_wp2[0]);
            
            ptsy.push_back(next_wp0[1]);
            ptsy.push_back(next_wp1[1]);
            ptsy.push_back(next_wp2[1]);

            for(int i = 0; i<ptsx.size(); i++)
            {
                double shift_x = ptsx[i] - ref_x;
                double shift_y = ptsy[i] - ref_y;

                ptsx[i] = shift_x*cos(0-ref_yaw) - shift_y*sin(0-ref_yaw);
                ptsy[i] = shift_x*sin(0-ref_yaw) + shift_y*cos(0-ref_yaw);
            }

            //creat a spline
            tk::spline s;
            s.set_points(ptsx, ptsy);


          	vector<double> next_x_vals;
          	vector<double> next_y_vals;

            //push the prev path
            for(int i=0; i<previous_path_x.size(); i++)
            {
                next_x_vals.push_back(previous_path_x[i]);
                next_y_vals.push_back(previous_path_y[i]);
            }

            double target_x = 30;
            double target_y = s(target_x);
            double target_dist = sqrt((target_x*target_x) + (target_y*target_y));

            double x_add_on = 0;

            //use spline to cal path base
            for(int i=0; i<50-previous_path_x.size(); i++)
            {
                double N = target_dist/(0.02*ref_vel/2.24);
                double points_x = x_add_on + target_dist/N;
                double points_y = s(points_x);
                x_add_on = points_x;

                double x_ref = points_x;
                double y_ref = points_y;
                
                points_x = x_ref*cos(ref_yaw) - y_ref*sin(ref_yaw);
                points_y = x_ref*sin(ref_yaw) + y_ref*cos(ref_yaw);

                points_x += ref_x;
                points_y += ref_y;

                next_x_vals.push_back(points_x);
                next_y_vals.push_back(points_y);
            }

            //finishd predict path
          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
