#include <iostream>
#include "vehicle.h"
#include <string>
#include <iterator>

/**
 * Initializes Vehicle
 */

Vehicle::Vehicle(){}

Vehicle::Vehicle(int lane, double s, double v, double a) {

    this->lane = lane;
    this->s = s;
    this->v = v;
    this->a = a;

}

Vehicle::~Vehicle() {}



