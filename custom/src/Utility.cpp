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