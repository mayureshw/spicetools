#ifndef _SPICECONF_H
#define _SPICECONF_H

#include <string>

using namespace std;

const string rawopfile = "simuop.raw";
const double vdd = 1.8;
const string vddstr = to_string(vdd);
const double logicthresh = 0.81; // Can be .45 to .55 of Vdd

#endif
