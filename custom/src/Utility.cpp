#include <cmath>
#include "Utility.h"

double angleWrap360(double angle)
{
    double new_angle = angle;
    while(new_angle > 360.0){
        new_angle -= 360.0;
    }

    while(new_angle < 0.0){
        new_angle += 360.0;
    }

    return new_angle;
}

double angleDelta360(double ang1, double ang2){
     return std::fmod(ang1 - ang2 + 540.0, 360.0) - 180.0;
}