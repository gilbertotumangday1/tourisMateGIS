/*
 * Copyright 2025 University of Toronto
 *
 * Permission is hereby granted, to use this software and associated
 * documentation files (the "Software") in course work at the University
 * of Toronto, or for personal use. Other uses are prohibited, in
 * particular the distribution of the Software either publicly or to third
 * parties.
 */

#include "m1.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <unordered_set>
#include <vector>

#include "OSMDatabaseAPI.h"
#include "StreetsDatabaseAPI.h"
// loadMap will be called with the name of the file that stores the "layer-2"
// map data accessed through StreetsDatabaseAPI: the street and intersection
// data that is higher-level than the raw OSM data).
// This file name will always end in ".streets.bin" and you
// can call loadStreetsDatabaseBIN with this filename to initialize the
// layer 2 (StreetsDatabase) API.
// If you need data from the lower level, layer 1, API that provides raw OSM
// data (nodes, ways, etc.) you will also need to initialize the layer 1
// OSMDatabaseAPI by calling loadOSMDatabaseBIN. That function needs the
// name of the ".osm.bin" file that matches your map -- just change
// ".streets" to ".osm" in the map_streets_database_filename to get the proper
// name.

// GLOBAL VARIABLES
std::vector<std::vector<IntersectionIdx>> intersectionAdjacentIntersections;
std::multimap<std::string, StreetIdx> streetNamesMultimap;
std::unordered_map<OSMID, const OSMNode*> osmNodePointerMap;
std::unordered_map<OSMID, const OSMWay*> osmWayPointerMap;
std::vector<std::vector<StreetSegmentIdx>> segmentsOfIntersections;
std::vector<std::vector<StreetSegmentIdx>> streetSegsOfStreet;
std::vector<std::unordered_set<IntersectionIdx>> uniqueIntersections;

std::vector<StreetSegmentInfo> allStreetSegmentsInfos;
std::vector<double> streetSegmentLengths;
std::vector<double> streetSegmentTravelTimes;

bool loadMap(std::string map_streets_database_filename) {
  bool load_successful = false;  // Indicates whether the map has loaded
                                 // successfully

  std::cout << "loadMap: " << map_streets_database_filename
            << std::endl;  // indicate that map loading has started

  // make new string out of map filename for osm database
  std::string map_OSM_database_filename = map_streets_database_filename;
  map_OSM_database_filename.replace(map_OSM_database_filename.size() - 12, 8,
                                    ".osm");

  // load both BIN data and return false if failure occurs
  if (!loadStreetsDatabaseBIN(map_streets_database_filename)) {
    return load_successful;
  }
  if (!loadOSMDatabaseBIN(map_OSM_database_filename)) {
    return load_successful;
  }

  // DATA STRUCTURES LOADING

  // Adjacent intersection vector loading
  // Also, 2-D vector to pre-process all the intersections and their respective
  // street segments.
  // Lastly, resizing the set of unique intersections arranged by streets in a vector of unordered sets 

  intersectionAdjacentIntersections.resize(getNumIntersections());
  segmentsOfIntersections.resize(getNumIntersections());
  uniqueIntersections.resize(getNumStreets());

  for (int intersection = 0; intersection < getNumIntersections(); intersection++) {  // we are loading data for every intersection
    for (int segment = 0; segment < getNumIntersectionStreetSegment(intersection); segment++) {  // we are checking every street segment per intersection

      // populate the street segments for each intersection
      int segmentID = getIntersectionStreetSegment(intersection, segment);  // get the street segment ID
      StreetSegmentInfo info = getStreetSegmentInfo(
          segmentID);  // get the info on the street segment
      segmentsOfIntersections[intersection].push_back(
          segmentID);  // add the segment into the 2D vector of each
                       // intersections segments

      // populate adjacent intersections for each intersection
      if (!(info.oneWay &&
            info.to ==
                intersection)) {  // this essentially checks that the road is
                                  // going towards another intersection

        if (info.to == info.from) {  // if the street is a cul-de-sac or dead
                                     // end
          if (std::count(
                  intersectionAdjacentIntersections[intersection].begin(),
                  intersectionAdjacentIntersections[intersection].end(),
                  info.to) == 0) {
            // check to make sure the intersection hasnt already been added
            // (imagine the case where intersections are connected by multiple
            // streets)
            intersectionAdjacentIntersections[intersection].push_back(
                info.to);  // push the intersection into its own contents
          }
        }

        else if (info.to ==
                 intersection) {  // if the current intersection is the current
                                  // segment's "to" intersection
          if (std::count(
                  intersectionAdjacentIntersections[intersection].begin(),
                  intersectionAdjacentIntersections[intersection].end(),
                  info.from) == 0) {
            // check to make sure the intersection hasnt already been added
            // (imagine the case where intersections are connected by multiple
            // streets)
            intersectionAdjacentIntersections[intersection].push_back(
                info.from);  // store the segments other intersection into the
                             // adjacent vector
          }
        }

        else {  // the only other option is that the current intersection is the
                // current segment's "from" intersection
          if (std::count(
                  intersectionAdjacentIntersections[intersection].begin(),
                  intersectionAdjacentIntersections[intersection].end(),
                  info.to) == 0) {
            // check to make sure the intersection hasnt already been added
            // (imagine the case where intersections are connected by multiple
            // streets)
            intersectionAdjacentIntersections[intersection].push_back(
                info.to);  // store the segments other intersection into the
                           // adjacent vector
          }
        }
      }
    }
  }

  // multimap of all streets sorted in order of names loading
  std::vector<std::string>
      streetnames;  // this will help make sure no duplicate street names are
                    // added into the multimap
  streetnames.resize(getNumStreets());  // give the vector a size

  for (StreetIdx streetIndex = 0; streetIndex < getNumStreets(); streetIndex++) {  // loop through every street and add its info

    // pre process street name to get rid of cases and spaces
    std::string currentStreetName =
        getStreetName(streetIndex);   // get stree name to be processed
    std::string processedStreetName;  // new prefix
    for (auto c :
         currentStreetName) {  // iterator c iterates through street_prefix
      if (!std::isspace(
              c)) {  // if the current character isnt a space, lowercase it and
                     // add it into the processed prefix
        processedStreetName.push_back(std::tolower(c));
      }
    }

    // add processed street name and corresponding street id into multimap
    if (std::count(streetnames.begin(), streetnames.end(),
                   processedStreetName) ==
        0) {  // make sure streetname hasnt already been added into streetnames
              // vector
      streetNamesMultimap.insert(
          {processedStreetName,
           streetIndex});  // add each name and streetIndex into the multimap,
                           // it is sorted by name.
    }
  }

  // Unordered map loading: Allows to get OSMNode pointer if given osmID
  for (int osmIndex = 0; osmIndex < getNumberOfNodes();
       osmIndex++) {  // loop through all OSMNodes
    const OSMNode* currentNode =
        getNodeByIndex(osmIndex);         // get current OSMNode
    OSMID currentId = currentNode->id();  // get OSMID of current OSMNode
    osmNodePointerMap[currentId] =
        currentNode;  // add OSMNode into location of unordered map
                      // corresponding to its OSMID
  }

  // Unordered map loading: Allows to get OSMWay pointer if given way_id
  for (int osmIndex = 0; osmIndex < getNumberOfWays(); osmIndex++) {  // loop through all OSMWays
    const OSMWay* currentWay = getWayByIndex(osmIndex);  // get current OSMWay
    OSMID currentId = currentWay->id();  // get OSMID of current OSMWay
    osmWayPointerMap[currentId] = currentWay;  // add OSMWay into location of unordered map corresponding to its OSMID
  }

  // Loading data in 2-D vector for streets and their corresponding street
  // segments Alsom, data pre-processing using 2-D vector for streets and their
  // corresponding intersections
  allStreetSegmentsInfos.resize(getNumStreetSegments());
  streetSegsOfStreet.resize(getNumStreets());
  for (StreetSegmentIdx k = 0; k < getNumStreetSegments(); k++) {
    StreetSegmentInfo segInfo = getStreetSegmentInfo(k);
    allStreetSegmentsInfos.push_back(segInfo); 

    streetSegsOfStreet[segInfo.streetID].push_back(k);
    // Store both 'to' and 'from' intersections at corresponding street in uniqueIntersections
    // Since it is an unordered_set, these will be unique and no duplicates exist
    uniqueIntersections[segInfo.streetID].insert(segInfo.from); 
    uniqueIntersections[segInfo.streetID].insert(segInfo.to); 
  }

  // Resize vectors to store lengths and travel times of street segments in vectors
  streetSegmentLengths.resize(getNumStreetSegments(), -1.0);
  streetSegmentTravelTimes.resize(getNumStreetSegments(), -1.0);

  // load all streetSegmentLengths into vector
  for (StreetSegmentIdx k = 0; k < getNumStreetSegments(); k++) {

    StreetSegmentInfo segInfo = getStreetSegmentInfo(k);
    // store starting point in fromPoint and ending point in toPoint
    LatLon fromPoint = getIntersectionPosition(segInfo.from);
    LatLon toPoint = getIntersectionPosition(segInfo.to);

    // initialize sum to 0
    double distanceSum = 0;

    // loop finds distance between from and the first curve points
    // then from that curved point to the next until the final curved point
    for (int i = 0; i < segInfo.numCurvePoints; i++) {
      LatLon curvePoint = getStreetSegmentCurvePoint(k, i);
      distanceSum += findDistanceBetweenTwoPoints(curvePoint, fromPoint);
      fromPoint = curvePoint;
    }

    // distance between from and two if no curved points
    // distance between final curved point and to otherwise
    distanceSum += findDistanceBetweenTwoPoints(fromPoint, toPoint);
    streetSegmentLengths[k] = distanceSum;

    // also precompute travel times and store in vector
    streetSegmentTravelTimes[k] = distanceSum / segInfo.speedLimit;
  }

  load_successful = true;
  return load_successful;
}

void closeMap() {
  // Clean-up your map related data structures here
  intersectionAdjacentIntersections.clear();
  segmentsOfIntersections.clear();
  osmNodePointerMap.clear();
  osmWayPointerMap.clear();
  streetNamesMultimap.clear();
  streetSegsOfStreet.clear();
  allStreetSegmentsInfos.clear();
  streetSegmentLengths.clear();
  streetSegmentTravelTimes.clear();
  uniqueIntersections.clear();

  closeStreetDatabase();  // close street map
  closeOSMDatabase();     // close osm map
}

// Returns the distance between two (latitude, longitude) coordinates in meters.
// Speed Requirement --> moderate ****OLIVIA*****
double findDistanceBetweenTwoPoints(LatLon point_1, LatLon point_2) {
  // change latitue of points to radians
  double Lat1 = point_1.latitude() * (kDegreeToRadian);
  double Lat2 = point_2.latitude() * (kDegreeToRadian);

  // Found difference of Latitudes and converted to meters for pythagorean
  // theorem
  double differenceLat =
      (kEarthRadiusInMeters * Lat2) - (kEarthRadiusInMeters * Lat1);

  // Average of Latitudes for longitude conversion to meters
  double latAverage = ((Lat1 + Lat2) / 2);

  // Convert longitude of points to radians
  double long1Rad = point_1.longitude() * (kDegreeToRadian);
  double long2Rad = point_2.longitude() * (kDegreeToRadian);

  // Convert Longitude to meters
  double long1InM = kEarthRadiusInMeters * long1Rad * cos(latAverage);
  double long2InM = kEarthRadiusInMeters * long2Rad * cos(latAverage);
  double differenceLong = long2InM - long1InM;

  // Return distance using Pythagorean theorem
  return sqrt(pow(differenceLat, 2) +
              pow(differenceLong,
                  2));  // use pythagorean theorum to find total distance
}

// Returns the length of the given street segment in meters.
// Speed Requirement --> moderate ****OLIVIA****
double findStreetSegmentLength(StreetSegmentIdx street_segment_id) {
  return streetSegmentLengths[street_segment_id];
}
// Returns the travel time to drive from one end of a street segment
// to the other, in seconds, when driving at the speed limit.
// Note: (time = distance/speed_limit)
// Speed Requirement --> high *******OLIVIA******
double findStreetSegmentTravelTime(StreetSegmentIdx street_segment_id) {
  // return the speed by dividing the segment length by the segment speed limit
  return streetSegmentTravelTimes[street_segment_id];
}
// Returns the angle (in radians) that you would need to turn as you exit
// src_street_segment_id and enter dst_street_segment_id, if they share an
// intersection.
// If a street segment is not completely straight, use the last piece of the
// segment closest to the shared intersection.
// If the two street segments do not share an intersection, return a constant
// NO_ANGLE, which is defined above.
// Speed Requirement --> none
double findStreetSegmentTurnAngle(StreetSegmentIdx src_street_segment_id,
                                  StreetSegmentIdx dst_street_segment_id) {
  // first, get the street segment info for each street segment and store both
  // of them for future easier use
  StreetSegmentInfo srcInfo = getStreetSegmentInfo(src_street_segment_id);
  StreetSegmentInfo dstInfo = getStreetSegmentInfo(dst_street_segment_id);

  // for calculations will require the LatLon values for each street segments
  // begin and end points
  LatLon srcStart, srcFinish, dstStart,
      dstFinish;  // src and dst both travel in straight line from start -->
                  // finish

  // then check if the street segments share an intersection, if they do store
  // whether its the to and from for each intersection
  IntersectionIdx srcIdx;  // these should be the same value but easier to
                           // visualize of they are separate
  IntersectionIdx dstIdx;

  // these if statements check for every possible orientation of src and dst
  if (srcInfo.to == dstInfo.to) {
    srcIdx = srcInfo.to;
    dstIdx = dstInfo.to;
  } else if (srcInfo.to == dstInfo.from) {
    srcIdx = srcInfo.to;
    dstIdx = dstInfo.from;
  } else if (srcInfo.from == dstInfo.to) {
    srcIdx = srcInfo.from;
    dstIdx = dstInfo.to;
  } else if (srcInfo.from == dstInfo.from) {
    srcIdx = srcInfo.from;
    dstIdx = dstInfo.from;
  } else {
    return NO_ANGLE;  // if they dont share an intersection, return NO_ANGLE
  }

  // we also should check if dst is one way, and if src cannot legally turn into
  // dst, or if src is one way in the opposite direction
  if ((srcIdx == srcInfo.from && srcInfo.oneWay) ||
      (dstIdx == dstInfo.to && dstInfo.oneWay)) {
    return NO_ANGLE;
  }

  // for calculations, we need to identify the start and end points for each
  // street segment;
  dstStart = getIntersectionPosition(
      dstIdx);  // the start point of dst is the intersection itself
  srcFinish = getIntersectionPosition(
      srcIdx);  // likewise, the finish point of src is the intersection itself

  if (srcInfo.numCurvePoints !=
      0) {  // if the street curves. the closest straight line point will be a
            // curve point
    if (srcIdx == srcInfo.to) {
      srcStart = getStreetSegmentCurvePoint(src_street_segment_id,
                                            srcInfo.numCurvePoints - 1);
    } else {
      srcStart = getStreetSegmentCurvePoint(src_street_segment_id, 0);
    }
  } else {  // this means the street is a straight line. and the start points
            // are just the opposite intersection
    if (srcIdx == srcInfo.to) {
      srcStart = getIntersectionPosition(srcInfo.from);
    } else {
      srcStart = getIntersectionPosition(srcInfo.to);
    }
  }
  if (dstInfo.numCurvePoints != 0) {  // same process as for src
    if (dstIdx == dstInfo.to) {
      dstFinish = getStreetSegmentCurvePoint(dst_street_segment_id,
                                             dstInfo.numCurvePoints - 1);
    } else {
      dstFinish = getStreetSegmentCurvePoint(dst_street_segment_id, 0);
    }
  } else {
    if (dstIdx == dstInfo.to) {
      dstFinish = getIntersectionPosition(dstInfo.from);
    } else {
      dstFinish = getIntersectionPosition(dstInfo.to);
    }
  }

  // now we get the x and y value for the to and from intersection of src and
  // dst
  std::vector<double> srcVector,
      dstVector;  // these are the vectors for the x and y differences for each
                  // street segment

  // convert the latitudes and longitudes of each point
  // (srcStart,srcFinish,dstStart,dstFinish) to radians
  double srcFinishLat = kDegreeToRadian * srcFinish.latitude();
  double srcStartLat = kDegreeToRadian * srcStart.latitude();
  double srcFinishLon = kDegreeToRadian * srcFinish.longitude();
  double srcStartLon = kDegreeToRadian * srcStart.longitude();
  double dstFinishLat = kDegreeToRadian * dstFinish.latitude();
  double dstFinishLon = kDegreeToRadian * dstFinish.longitude();

  double latAvgsrc =
      (srcStartLat + dstFinishLat) /
      2.000000;  // get the average latitude of the region we are calculating
                 // over which is srcStat to dstFinish

  // convert latlon --> x,y using formula provided in m1 document
  double srcFinishX = kEarthRadiusInMeters * srcFinishLon * std::cos(latAvgsrc);
  double srcFinishY = kEarthRadiusInMeters * srcFinishLat;
  double dstStartX = srcFinishX;
  double dstStartY = srcFinishY;
  double srcStartX = kEarthRadiusInMeters * srcStartLon * std::cos(latAvgsrc);
  double srcStartY = kEarthRadiusInMeters * srcStartLat;
  double dstFinishX = kEarthRadiusInMeters * dstFinishLon * std::cos(latAvgsrc);
  double dstFinishY = kEarthRadiusInMeters * dstFinishLat;

  // first, get the x values
  double srcXdifference = srcFinishX - srcStartX;
  double dstXdifference = dstFinishX - dstStartX;

  // then, get the Y values
  double srcYdifference = srcFinishY - srcStartY;
  double dstYdifference = dstFinishY - dstStartY;

  // finally, push all values into their vectors
  srcVector.push_back(srcXdifference);
  srcVector.push_back(srcYdifference);
  dstVector.push_back(dstXdifference);
  dstVector.push_back(dstYdifference);

  // finally, dot product the two vectors, then divide the result by the
  // magnitude and use cmath acos to find the angle in radians
  double dotProduct =
      srcVector[0] * dstVector[0] +
      srcVector[1] * dstVector[1];  // get dot product using x*x + y*y
  double srcMagnitude =
      std::sqrt(srcVector[0] * srcVector[0] +
                srcVector[1] * srcVector[1]);  // sqrt(x^2+y^2)
  double dstMagnitude =
      std::sqrt(dstVector[0] * dstVector[0] + dstVector[1] * dstVector[1]);

  // finally return acos(dot product of both vectors divided by product of their
  // magnitudes)
  double result = std::acos(dotProduct / (srcMagnitude * dstMagnitude));
  return result;
}

// Returns all intersections reachable by traveling down one street segment
// from the given intersection. (hint: you can't travel the wrong way on a 1-way
// street)
// The returned vector should NOT contain duplicate intersections.
// Corner case: cul-de-sacs can connect an intersection to itself (from and to
// intersection on street segment are the same). In that case include the
// intersection in the returned vector (no special handling needed).
// Speed Requirement --> high
std::vector<IntersectionIdx> findAdjacentIntersections(
    IntersectionIdx intersection_id) {
  // loaded data structure in loadmap function, reference it now
  return intersectionAdjacentIntersections
      [intersection_id];  // most of the work for this function is done in the
                          // loapmap function
}

// Returns the geographically nearest intersection (i.e. as the crow flies) to
// the given position.
// Speed Requirement --> none ****OLIVIA******
IntersectionIdx findClosestIntersection(LatLon my_position) {
  // initialize closestIntersectionId and shortestDistance
  IntersectionIdx closestIntersectionId = 0;
  double shortestDistance = std::numeric_limits<double>::infinity();

  // number of intersections for for loop
  int numIntersections = getNumIntersections();

  // iterates from 0 - numIntersections and checks the distance of each from
  // my_position then updates closest_intersection accordingly
  for (int k = 0; k < numIntersections; k++) {
    LatLon intersectionPosition = getIntersectionPosition(k);
    double newDistance =
        findDistanceBetweenTwoPoints(my_position, intersectionPosition);

    if (newDistance < shortestDistance) {
      shortestDistance = newDistance;
      closestIntersectionId = k;
    }
  }
  return closestIntersectionId;
}
// Returns the street segments that connect to the given intersection.
// Speed Requirement --> high
std::vector<StreetSegmentIdx> findStreetSegmentsOfIntersection(
    IntersectionIdx intersection_id) {
  // return the vector of street segments IDs for given intersection from
  // pre-processed data.
  return segmentsOfIntersections[intersection_id];
}

// Returns all intersections along the given street.
// There should be no duplicate intersections in the returned vector.
// Speed Requirement --> high
std::vector<IntersectionIdx> findIntersectionsOfStreet(StreetIdx street_id) {
  std::vector<IntersectionIdx> streetIntersections;
  // Return the vector after copying all of the elements into streetIntersections from uniqueIntersections
  // goes from start to end of set (ie. begin() to end())

  streetIntersections.assign(uniqueIntersections[street_id].begin(), uniqueIntersections[street_id].end());

  return streetIntersections;

}

// Return all intersection ids at which the two given streets intersect.
// This function will typically return one intersection id for streets that
// intersect and a length 0 vector for streets that do not. For unusual curved
// streets it is possible to have more than one intersection at which two
// streets cross.
// There should be no duplicate intersections in the returned vector.
// Speed Requirement --> high
std::vector<IntersectionIdx> findIntersectionsOfTwoStreets(std::pair<StreetIdx, StreetIdx> street_ids) {
  
    // PLAN: Retrieve the intersections of both streets from
  //       findIntersectionsOfStreet Function
  //       and compare each intersection of one to all in the other to find
  //       which intersections are common to return.

  std::vector<IntersectionIdx> commonTwoStreetIntersections; // Store intersections that are common in both streets to return
  std::vector<IntersectionIdx> streetInter1 = findIntersectionsOfStreet(street_ids.first); // intersections of street 1
  std::vector<IntersectionIdx> streetInter2 = findIntersectionsOfStreet(street_ids.second); // intersections of street 2
  
  // Go through all intersections in street1 to find if there is matching in street 1
  // if there is a match, add to common vector to return
  for (int inter1 = 0; inter1 < streetInter1.size(); inter1++) {
    for (int inter2 = 0; inter2 < streetInter2.size(); inter2++) {
      if (streetInter1[inter1] == streetInter2[inter2]) {
        commonTwoStreetIntersections.push_back(streetInter1[inter1]);
      }
    }
  }

  return commonTwoStreetIntersections;
}

// Returns all street ids corresponding to street names that start with the
// given prefix.
// The function should be case-insensitive to the street prefix.
// The function should ignore spaces.
//  For example, both "bloor " and "BloOrst" are prefixes to
//  "Bloor Street East".
// If no street names match the given prefix, this routine returns an empty
// (length 0) vector.
// You can choose what to return if the street prefix passed in is an empty
// (length 0) string, but your program must not crash if street_prefix is a
// length 0 string.
// Speed Requirement --> high
std::vector<StreetIdx> findStreetIdsFromPartialStreetName(
    std::string street_prefix) {
  std::vector<StreetIdx>
      searchedStreets;  // vector for every street containing the prefix
  if (street_prefix.empty()) {
    return {};  // return nothing if prefix is empty
  }

  // processing the prefix to get rid of upper cases and white spaces
  std::string processedPrefix;    // new prefix
  for (auto c : street_prefix) {  // iterator c iterates through street_prefix
    if (!std::isspace(c)) {  // if the current character isnt a space, lowercase
                             // it and add it into the processed prefix
      processedPrefix.push_back(std::tolower(c));
    }
  }

  // next, create a multimap iterator and start its location ad the closest key
  // to the prefix
  std::multimap<std::string, StreetIdx>::iterator index =
      streetNamesMultimap.lower_bound(processedPrefix);
  // sets the iterator to the first key in the multimap greater than or equal to
  // prefix (in this case it means first street name to contain prefix)

  // loop to iterate through the multimap starting at the lower bound, adding
  // every matching prefix street into the searchedStreets vector
  while (index != streetNamesMultimap.end() &&
         index->first.compare(0, processedPrefix.size(), processedPrefix) ==
             0) {
    searchedStreets.push_back(index->second);
    ++index;
  }

  return searchedStreets;
}

// Returns the length of a given street in meters.
// Speed Requirement --> high
double findStreetLength(StreetIdx street_id) {
  // Initialize the length to 0.0
  double streetLength = 0.0;
  // Load the street segments for given street_id from global data structure
  int numOfStreetSegOfStreet = streetSegsOfStreet[street_id].size();
  for (int iStreetSegment = 0; iStreetSegment < numOfStreetSegOfStreet;
       iStreetSegment++) {
    // add the i'th street segment length to running sum
    streetLength += findStreetSegmentLength(streetSegsOfStreet[street_id][iStreetSegment]);
  }

  return streetLength;
}

// Return the smallest axis-aligned rectangle that contains all the
// intersections and curve points of the given street (i.e. the min,max
// lattitude and longitude bounds that can just contain all points of the
// street).
// Speed Requirement --> none
LatLonBounds findStreetBoundingBox(StreetIdx street_id) {
  double minimumLat = std::numeric_limits<double>::infinity(); // initial minimum latitude to infinity
  double maximumLat = -std::numeric_limits<double>::infinity(); // initial maximum latitude to infinity
  double minimumLon = std::numeric_limits<double>::infinity(); // initial minimum longitude to infinity
  double maximumLon = -std::numeric_limits<double>::infinity(); // initial maximum longitude to infinity

  // Iterate through all the street segments of the street to find bounding box

  for (StreetSegmentIdx segId : streetSegsOfStreet[street_id]) {
    StreetSegmentInfo segInfo = getStreetSegmentInfo(segId);

    // Get the two intersections of the segment iterating
    LatLon fromPos = getIntersectionPosition(segInfo.from);
    LatLon toPos = getIntersectionPosition(segInfo.to);

    // identify and store the latitude and longitude for both 'to' and 'from' intersections
    double fromLat = fromPos.latitude();
    double fromLon = fromPos.longitude();
    double toLat = toPos.latitude();
    double toLon = toPos.longitude();

    // re-assess/compare the minimum and maximum latidude and longitudes for the intersections and store the values
    minimumLat = std::min(minimumLat, std::min(fromLat, toLat));
    maximumLat = std::max(maximumLat, std::max(fromLat, toLat));
    minimumLon = std::min(minimumLon, std::min(fromLon, toLon));
    maximumLon = std::max(maximumLon, std::max(fromLon, toLon));

    
    // Iterate through the curve points of the segment to find the LatLon extrema for them to compare with intersection extrema.
    for (int i = 0; i < segInfo.numCurvePoints; i++) {
      LatLon curvePointPosition = getStreetSegmentCurvePoint(segId, i);

      // get the latitude and longitude of the curve point
      double curveLat = curvePointPosition.latitude();
      double curveLon = curvePointPosition.longitude();

      // collection of if-statements checking to update the overall extrema for latitude and longitude.
      if (curveLat < minimumLat) {
        minimumLat = curveLat;
      }
      if (curveLat > maximumLat) {
        maximumLat = curveLat;
      }
      if (curveLon < minimumLon) {
        minimumLon = curveLon;
      }
      if (curveLon > maximumLon) {
        maximumLon = curveLon;
      }
    }
  }

  // Create a bounding box object to associate the minimum and maximum LatLon respectively to return the "box"
  LatLonBounds boundingBox;
  boundingBox.min = LatLon(minimumLat, minimumLon);
  boundingBox.max = LatLon(maximumLat, maximumLon);
  return boundingBox;
}

// Returns the nearest point of interest of the given type (e.g. "restaurant")
// to the given position.
// Speed Requirement --> none  ******OLIVIA*******
POIIdx findClosestPOI(LatLon my_position, std::string poi_type) {
  // initialize closest POI and shortest distance
  POIIdx closestPoiId = -1;
  double shortestDistance = std::numeric_limits<double>::infinity();

  // Number of POIs for for loop
  int numPoi = getNumPointsOfInterest();

  // check through POIs of same type and check each distance from my_position
  // update closest poi accordingly
  for (int k = 0; k < numPoi; k++) {
    if (getPOIType(k) == poi_type) {
      LatLon newPoi = getPOIPosition(k);
      double newDistance = findDistanceBetweenTwoPoints(my_position, newPoi);

      if (newDistance < shortestDistance) {
        shortestDistance = newDistance;
        closestPoiId = k;
      }
    }
  }
  return closestPoiId;
}
// Returns the area of the given closed feature in square meters.
// Assume a non self-intersecting polygon (i.e. no holes).
// Return 0 if this feature is not a closed polygon.
// Speed Requirement --> moderate ********OLIVIA*****
double findFeatureArea(FeatureIdx feature_id) {
  // number of points for use in for loop
  int featurePoints = getNumFeaturePoints(feature_id);

  // get start and end points to check if its a closed polygon
  LatLon startPoint = getFeaturePoint(feature_id, 0);
  LatLon endPoint = getFeaturePoint(feature_id, featurePoints - 1);

  // initialize area
  double totArea = 0.0;

  // check if end point is start point (if its a closed polygon)
  // check if less than two points (would me arealess)
  if (featurePoints <= 2 || startPoint.latitude() != endPoint.latitude() ||
      startPoint.longitude() != endPoint.longitude()) {
    return 0.0;
  }

  for (int k = 1; k < featurePoints; k++) {
    // initialize now point and next point
    LatLon nowPoint = getFeaturePoint(feature_id, k - 1);
    LatLon nextPoint = getFeaturePoint(feature_id, k);

    // convert points latitudes and longitudes to radians
    double nowLat = (kDegreeToRadian)*nowPoint.latitude();
    double nowLong = (kDegreeToRadian)*nowPoint.longitude();
    double nextLong = (kDegreeToRadian)*nextPoint.longitude();
    double nextLat = (kDegreeToRadian)*nextPoint.latitude();

    // latitude average for long to meters conversion
    double latAverage = ((nextLat + nowLat) / 2);

    // iteratively use trapezoid formula for feature points and convert long to
    // meters
    totArea += kEarthRadiusInMeters * (nextLat - nowLat) *
               (kEarthRadiusInMeters * nextLong * cos(latAverage) +
                kEarthRadiusInMeters * nowLong * cos(latAverage)) /
               2;
  }
  return fabs(totArea);
}
// Returns the length of the OSMWay that has the given OSMID, in meters.
// To implement this function you will have to  access the OSMDatabaseAPI.h
// functions.
// Speed Requirement --> high
double findWayLength(OSMID way_id) {
  // first, get the OSMWay pointer by calling the unordered map created in
  // loadmap
  const OSMWay* wayPointer = osmWayPointerMap[way_id];
  std::vector<OSMID> memberNodesOfWay = getWayMembers(wayPointer);

  double wayLength = 0.0; // running total length of the OSMWay
  
  // Iterate through all the members (nodes) in the OSMWay to access their OSMID's
  // Using the osmNodePointerMap, we use the OSMID to get the pointer to the node
  // Find the distance between all the nodes and return the wayLength
  for (int osmWayNodeIndex = 0; osmWayNodeIndex < memberNodesOfWay.size() - 1;
       osmWayNodeIndex++) {
    const OSMNode* node1 = osmNodePointerMap[memberNodesOfWay[osmWayNodeIndex]];
    const OSMNode* node2 = osmNodePointerMap[memberNodesOfWay[osmWayNodeIndex + 1]];

    wayLength += findDistanceBetweenTwoPoints(getNodeCoords(node1),
                                              getNodeCoords(node2));
  }

  return wayLength;
}

// Return the value associated with this key on the specified OSMNode.
// If this OSMNode does not exist in the current map, or the specified key is
// not set on the specified OSMNode, return an empty string.
// Speed Requirement --> high
std::string getOSMNodeTagValue(OSMID osm_id, std::string key) {
  // first, get the OSMNode pointer by calling the unordered map created in
  // loadmap
  const OSMNode* nodePointer = osmNodePointerMap[osm_id];

  // then, iterate through the tags of the node until a key matches the provided
  // key, and return the corresponding value
  for (int tagIndex = 0; tagIndex < getTagCount(nodePointer); tagIndex++) {
    std::string currentKey, currentValue;
    std::tie(currentKey, currentValue) = getTagPair(nodePointer, tagIndex);
    if (key == currentKey) {
      return currentValue;
    }
  }
  return "";
}