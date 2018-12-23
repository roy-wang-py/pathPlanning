#ifndef VEHICLE_H
#define VEHICLE_H
#include <iostream>
#include <vector>
#include <string>

using namespace std;

class Vehicle {
public:



  int lane;

  double s;

  double v;



  /**
  * Constructor
  */
  Vehicle();
  Vehicle(int lane, float s, float v);

  /**
  * Destructor
  */
  virtual ~Vehicle();

};

#endif