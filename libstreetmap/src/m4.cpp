#include "m1.h"
#include "m2.h"
#include "m3.h"
#include "m4.h"
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <limits>
#include <algorithm>
#include <omp.h>
#include <math.h>
#include "StreetsDatabaseAPI.h"

#define TIME_LIMIT 50

double infinity = std::numeric_limits<double>::infinity();

// Delivery Items info structure
struct DeliveryItem{
    bool pickedUp;
    bool droppedOff;
};

// Intersection's complete delivery profile structure
struct DeliveryLocation{
    // stores pair of which delivery order serving and if it is a pick up or drop off for that delivery
    // Convention: 'true' means pick up location for corresponding delivery and 'false' means drop off.
    std::vector<std::pair<int, bool>> deliveryOrderProfile;
};

// Preprocessed travel times among any intersection to any other.
std::vector<std::vector<double>> timeMatrix;

// Delivery Info vectors
std::vector<DeliveryLocation> deliveryLocations;
std::vector<DeliveryItem> deliveryItems;

struct WaveElemDijkstra {
        IntersectionIdx nodeId;
        StreetSegmentIdx edgeId;
        double travelTime; 

        bool operator>(const WaveElemDijkstra& otherf)const{
                return travelTime > otherf.travelTime;
        }
};

struct keyIntersection{
    IntersectionIdx id;
    bool pickUp = false;
    bool dropOff = false;

    keyIntersection() = default;

    keyIntersection(IntersectionIdx _id, bool _pickUp, bool _dropOff){
        this->id = _id;
        this->pickUp = _pickUp;
        this->dropOff = _dropOff;
    }
};


// Function prototype declarations
std::vector<double> multiDijkstra(IntersectionIdx& start, std::vector<IntersectionIdx>& intersectionList , double turn_penalty);
bool legalPathCheck(const std::vector<CourierSubPath>& currentPath);
std::vector<CourierSubPath> perform2Opt(const float turn_penalty, const std::vector<CourierSubPath>& currentPath);
void cleanDataStructures();

std::vector<CourierSubPath> travelingCourier(
                            const float turn_penalty,
                            const std::vector<DeliveryInf>& deliveries,
                            const std::vector<IntersectionIdx>& depots){
    
    // Initialize delivery structures
    deliveryItems.resize(deliveries.size(), {false, false});
    deliveryLocations.resize(getNumIntersections());

    // Populate them appropriately
    for(int i = 0; i < deliveries.size(); i++) {
        // Add pickup info
        deliveryLocations[deliveries[i].pickUp].deliveryOrderProfile.push_back(std::make_pair(i, true));
        // Add dropoff info
        deliveryLocations[deliveries[i].dropOff].deliveryOrderProfile.push_back(std::make_pair(i, false));
    }
    
    //first, make a list of all the important intersections 
    std::vector<IntersectionIdx> keyIntersections;

    //also need vectors to keep track of whats been picked Up and what needs to be dropped off
    std::vector<IntersectionIdx> pickUps,dropOffs;

    //we will use unordered maps to keep track of the corresponding drop offs for each pickup and vice versa
    std::unordered_map<IntersectionIdx,std::vector<IntersectionIdx>> pickUpMap,dropOffMap;
    std::unordered_map<IntersectionIdx,keyIntersection> mapMultiStatus;
    std::vector<IntersectionIdx> pickedUp;
    int dropOffCount = 0;
    
    //loop through all the deliveries, taking their pickup and dropoff components and inserting them into the keyIntersections vector
    for(DeliveryInf currentDelivery : deliveries){
        //add everything into key intersections
        keyIntersections.push_back(currentDelivery.pickUp);
        keyIntersections.push_back(currentDelivery.dropOff);
        //add all pickups into vector
        pickUps.push_back(currentDelivery.pickUp);
        //add everything into unordered maps
        pickUpMap[currentDelivery.pickUp].push_back(currentDelivery.dropOff);
        dropOffMap[currentDelivery.dropOff].push_back(currentDelivery.pickUp);
    }

    //then loop through all depots as they are also intersections and we will need info about their travel time
    for(IntersectionIdx currentDepot : depots){
        keyIntersections.push_back(currentDepot);
    }

    //now, reprocess the keyIntersections vector to get rid off all duplicates (we only represent duplicates as one intersection when calculating travel time)
    std::sort(keyIntersections.begin(),keyIntersections.end());
    keyIntersections.erase(std::unique(keyIntersections.begin(),keyIntersections.end()),keyIntersections.end());
    //also reprocess pickUp vector
    std::sort(pickUps.begin(),pickUps.end());
    pickUps.erase(std::unique(pickUps.begin(),pickUps.end()),pickUps.end());

    //now, loop through our unique intersection vector, computing how long it would take to get from any intersection to any other intersection, storing it in a 2d vector
    timeMatrix.clear();
    timeMatrix.resize(getNumIntersections());

    #pragma omp parallel for
    for(int i = 0; i < keyIntersections.size(); ++i){
        IntersectionIdx src = keyIntersections[i];
        std::vector<double> localTimeMatrix = multiDijkstra(src, keyIntersections, turn_penalty);
        
        #pragma omp critical
        {
            timeMatrix[src] = localTimeMatrix;
        }
    }
    //now that we have our list of key intersections as well as the corresponding travel times, we can start our greedy heuristic and define our results
    std::vector<keyIntersection> pathOutline;
    std::vector<CourierSubPath> path;

/*GREEDY HEURISTIC*/
    
    //first heuristic: choosing starting depo based on which has the closest pickup location
    IntersectionIdx startDepo = -1;
    IntersectionIdx startDelivery = -1;

    //we will use this variable to determine which depo has the shortest travel time to any pickup location
    double currentLowestTravelTime = infinity;
    int startIndex;

    //loop through depots and pickups, getting shortest time; (p1)
    for(int depotIndex = 0 ; depotIndex < depots.size() ; depotIndex++){
        for(int pickUpIndex = 0 ; pickUpIndex < pickUps.size() ; pickUpIndex++){
            if(timeMatrix[depots[depotIndex]].size() > pickUps[pickUpIndex] && 
               timeMatrix[depots[depotIndex]][pickUps[pickUpIndex]] < currentLowestTravelTime){
                startDepo = depots[depotIndex];
                startDelivery = pickUps[pickUpIndex];
                startIndex = pickUpIndex;
                currentLowestTravelTime = timeMatrix[depots[depotIndex]][pickUps[pickUpIndex]];
            }
        }
    }
    
    //add the start depo and first pickup point to result strcutures
    if(startDepo != -1 && startDelivery != -1){
        pathOutline.push_back({startDepo,false,false});
        pathOutline.push_back({startDelivery,true,false});
        
        std::vector<StreetSegmentIdx> initialPath = findPathBetweenIntersections(turn_penalty, std::make_pair(startDepo, startDelivery));
        if(initialPath.empty()) {
            std::cout << "No path found from depot to first pickup" << std::endl;
            cleanDataStructures();
            return {};
        }
        
        path.push_back({std::make_pair(startDepo,startDelivery), initialPath});
    }
    else{
        std::cout<<"error with initial conditions"<<std::endl;
        cleanDataStructures();
        return {};
    }
    
    //now, starting at the current delivery intersection, navigate through the rest prioritizing travel time, until there are no packages left to deliver,
    //we will use a priority queue to manage times
    IntersectionIdx currentIntersection = startDelivery;
    IntersectionIdx nextIntersection = -1;
    bool legal = false;
    bool pickup = false;
    bool dropoff = false;
    bool removeKeyIntersection = false; 
    bool thing = false;
    
    // Maximum iterations to prevent infinite loop
    int maxIterations = deliveries.size() * 10;
    int currentIteration = 0;

    while(dropOffCount < deliveries.size() && currentIteration < maxIterations){
        currentIteration++;
        //first we should handle the edge case where we just started the loop and the current intersection is the starting pickup
        if(startDelivery==currentIntersection){
            std::vector<IntersectionIdx> currentDrops = pickUpMap[currentIntersection];
            //add all the corresponding drops into the drop vector
            dropOffs.insert(dropOffs.end(),currentDrops.begin(),currentDrops.end());
            //now clear this pickup location off the list of pickups and key intersections;
            pickedUp.push_back(currentIntersection);
            auto it = std::find(keyIntersections.begin(),keyIntersections.end(),currentIntersection);
            if(it != keyIntersections.end()) {
                keyIntersections.erase(it);
            }
        }
        std::priority_queue<std::pair<double, IntersectionIdx>, std::vector<std::pair<double, IntersectionIdx>>, std::greater<std::pair<double, IntersectionIdx>>> timeQueue;
        //now, we loop through each key Intersection and input its time into the minQueue, which will ensure the fastest one is at the top of the queue
        for(int intersectionIndex = 0; intersectionIndex < keyIntersections.size(); intersectionIndex++){
            if(currentIntersection < timeMatrix.size() && 
               keyIntersections[intersectionIndex] < timeMatrix[currentIntersection].size() &&
               timeMatrix[currentIntersection][keyIntersections[intersectionIndex]] != infinity) {
                timeQueue.push(std::make_pair(timeMatrix[currentIntersection][keyIntersections[intersectionIndex]],keyIntersections[intersectionIndex]));
            }
        }
        //now, the minqeue is filled and we can now verify the legality of the top of the qeue, and pop it off if its illegal
        legal = false;
        pickup = false;
        dropoff = false;
        removeKeyIntersection = false;
        
        while(!timeQueue.empty() && !legal){
            nextIntersection = timeQueue.top().second;
            timeQueue.pop();
            
            //check if its a pickup (if so then its legal)
            for(int pickUpIndex = 0; pickUpIndex < pickUps.size(); pickUpIndex++){
                if(pickUps[pickUpIndex] == nextIntersection){
                    std::vector<IntersectionIdx>& currentDrops = pickUpMap[nextIntersection];
                    //add all the corresponding drops into the drop vector
                    dropOffs.insert(dropOffs.end(), currentDrops.begin(), currentDrops.end());
                    //now clear this pickup location off the list of pickups and key intersections;
                    pickedUp.push_back(nextIntersection);
                    legal = true;
                    pickup = true;
                    removeKeyIntersection = true;
                    break;
                }
            }
            
            //if its legal we can end here
            if(legal){
                break;
            }

            //resize drop off vector to get rid of all duplicates
            std::sort(dropOffs.begin(), dropOffs.end());
            dropOffs.erase(std::unique(dropOffs.begin(), dropOffs.end()), dropOffs.end());

            removeKeyIntersection = false;

            //if its not a fresh pick up, the next option would be a legal drop off, so we loop through all the drop offs
            for(int dropOffIndex = 0; dropOffIndex < dropOffs.size(); dropOffIndex++){
                //first, check that the nextIntersection is a dropOff
                if(dropOffs[dropOffIndex] == nextIntersection){
                    //now we can check which of the dropoffs feeder intersections have been picked up
                    if(dropOffMap.find(nextIntersection) != dropOffMap.end()) {
                        std::vector<IntersectionIdx>& currentPicks = dropOffMap[nextIntersection];
                        for(size_t i = 0; i < currentPicks.size(); i++) {
                            if(std::find(pickedUp.begin(), pickedUp.end(), currentPicks[i]) != pickedUp.end()) {
                                legal = true;
                                dropoff = true;
                                dropOffCount++;
                                
                                //erase the now finished pickup
                                auto it = std::find(dropOffMap[nextIntersection].begin(), dropOffMap[nextIntersection].end(), currentPicks[i]);
                                if(it != dropOffMap[nextIntersection].end()) {
                                    dropOffMap[nextIntersection].erase(it);
                                    i--; // Adjust for the removed element
                                }
                            }
                        }
                        
                        if(dropOffMap[nextIntersection].empty()){
                            removeKeyIntersection = true;
                            dropOffs.erase(dropOffs.begin() + dropOffIndex);
                            break;
                        }
                    }
                }
            }
            
            //if its legal we can end the loop
            if(legal){
                break;
            }
        }
        
        if(removeKeyIntersection){
            auto it = std::find(keyIntersections.begin(), keyIntersections.end(), nextIntersection);
            if(it != keyIntersections.end()) {
                keyIntersections.erase(it);
            }
        }

        // Check if we have a valid next intersection
        if(timeQueue.empty() && !legal){
            std::cout << "failure in greedy algo loop " << std::endl;
            cleanDataStructures();
            return {};
        }
        
        //we can now add the path between current and next into the resulting path, making sure the two intersections aren't somehow the same
        if(currentIntersection != nextIntersection){
            std::vector<StreetSegmentIdx> segmentPath = findPathBetweenIntersections(turn_penalty, std::make_pair(currentIntersection, nextIntersection));
            
            if(segmentPath.empty()) {
                std::cout << "No path found between intersections" << std::endl;
                cleanDataStructures();
                return {};
            }
            
            path.push_back({std::make_pair(currentIntersection, nextIntersection), segmentPath});
            pathOutline.push_back({nextIntersection, pickup, dropoff});
        }
        
        //if we made it, advance to the next intersection and do it all over again
        currentIntersection = nextIntersection;
    }
    
    // Check if we exceeded max iterations
    if(currentIteration >= maxIterations) {
        std::cout << "Max iterations reached in greedy algorithm" << std::endl;
        cleanDataStructures();
        return {};
    }
    
    // Check if we delivered everything
    if(dropOffCount < deliveries.size()) {
        std::cout << "Failed to deliver all packages" << std::endl;
        cleanDataStructures();
        return {};
    }
    
    //by this point we would have just dropped off our last package so now its time to go to the nearest depo
    IntersectionIdx endDepot = -1;
    currentLowestTravelTime = infinity;
    for(int depotIndex = 0; depotIndex < depots.size(); depotIndex++){
        if(currentIntersection < timeMatrix.size() && 
           depots[depotIndex] < timeMatrix[currentIntersection].size() &&
           timeMatrix[currentIntersection][depots[depotIndex]] < currentLowestTravelTime){
            endDepot = depots[depotIndex];
            currentLowestTravelTime = timeMatrix[currentIntersection][depots[depotIndex]];
        }
    }
    
    if(endDepot == -1) {
        std::cout << "No valid end depot found" << std::endl;
        cleanDataStructures();
        return {};
    }
    
    std::vector<StreetSegmentIdx> finalPath = findPathBetweenIntersections(turn_penalty, std::make_pair(currentIntersection, endDepot));
    if(finalPath.empty()) {
        std::cout << "No path found to end depot" << std::endl;
        cleanDataStructures();
        return {};
    }
    
    path.push_back({std::make_pair(currentIntersection, endDepot), finalPath});
    pathOutline.push_back({endDepot, false, false});

    // Apply 2-opt optimization if path is valid
    if(legalPathCheck(path)) {
        path = perform2Opt(turn_penalty, path);
    }
    
    // Clean up global data structures
    cleanDataStructures();
    
    //for greedy testing
    return path;
}

std::vector<double> multiDijkstra(IntersectionIdx& start, std::vector<IntersectionIdx>& intersectionList, double turn_penalty){
        //first create the vector we will return and the vector we will use to measure whether or not we have computed times for all intersections
        std::vector<double> travelTimes(getNumIntersections(), std::numeric_limits<double>::infinity());
        std::vector<IntersectionIdx> visitedKeyIntersections(0);

        //then, set the starting point, there is no endpoint because this version of the algo leaves when you either run out of intersections in the city or you get the times
        //for all the key Intersections
        IntersectionIdx from = start;

        //We want a variable for the number of intersections to simplify code
        int numIntersections = getNumIntersections();

        //final path vector to pass to compute PathTravelTime
        std::vector<StreetSegmentIdx> path;

        //arguments of priority queue: object type being stored, container of objects, comparison
        //uses operator> to compare the f(n) of both WaveElements
        std::priority_queue<WaveElemDijkstra, std::vector<WaveElemDijkstra>, std::greater<WaveElemDijkstra>> minQueue;

        //vector for holding travel times of each node
        std::vector<double> gofn(numIntersections, std::numeric_limits<double>::infinity());

        //vector of nodes already visited
        std::vector<int> nodesVisited(numIntersections, -1);

        //doesn't take any time to get to the start of the path
        gofn.at(from) = 0.0;

        //Add start node into minQueue with (intersection position, streetseg reaching, travel time)
        minQueue.push({from, -1, gofn[from],});
        while(!minQueue.empty()) {
            //look at node at top of queue, which is the current best and pop() so that you don look at it twice
            WaveElemDijkstra temporaryWave = minQueue.top();
            minQueue.pop();

            //store the current ID
            int currentId = temporaryWave.nodeId;

            // Skip if we've found a better path since this was queued
            if (temporaryWave.travelTime > gofn[currentId]) continue;
            
            //loop through all the street segments of the intersection
            for(StreetSegmentIdx segmentId: findStreetSegmentsOfIntersection(currentId)){
                StreetSegmentInfo segmentInfo = getStreetSegmentInfo(segmentId);
                double segTravelTime = findStreetSegmentTravelTime(segmentId);
                double penalty = 0.0;
                double tempG;

                //skip one ways if we are starting at the wrong side
                if(segmentInfo.oneWay && segmentInfo.to == currentId){
                    continue;
                }
                        
                //check if current location is the to or the from for the street segment and set next node accordingly
                IntersectionIdx nextNode;
                if(segmentInfo.from == currentId) {
                    nextNode = segmentInfo.to;
                }else{
                    nextNode = segmentInfo.from;
                }

                // Boundary check
                if(nextNode >= numIntersections) {
                    continue;
                }
                        
                if(temporaryWave.edgeId != -1){
                    StreetSegmentInfo lastInfo = getStreetSegmentInfo(temporaryWave.edgeId);
                    if(lastInfo.streetID != segmentInfo.streetID){
                        penalty = turn_penalty;
                    }
                }

                bool case1 = false;
                        
                tempG = temporaryWave.travelTime + segTravelTime + penalty;

                //is the path we found to the node is faster than previous ones                
                if(gofn.at(nextNode) > tempG){
                    //update info for this node
                    gofn.at(nextNode) = tempG;
                    nodesVisited[nextNode] = segmentId;
                    case1 = true;
                    
                    //now loop through all key intersections and check if this one is in the list
                    for(int checkIndex = 0 ; checkIndex < intersectionList.size() ; checkIndex++){
                        if(nextNode == intersectionList[checkIndex]){
                            travelTimes[nextNode] = tempG;      
                            //check if this is a new intersection
                            if(std::find(visitedKeyIntersections.begin(), visitedKeyIntersections.end(), nextNode) == visitedKeyIntersections.end()){
                                visitedKeyIntersections.push_back(nextNode);
                            }
                            //check if we have hit all key intersections, if we have, return our current times
                            if(visitedKeyIntersections.size() == intersectionList.size()){
                                return travelTimes;
                            }
                        }
                    }
                }
                //if our current g calculation is faster than either of the above cases update the min queue
                if(case1){
                    minQueue.push({nextNode, segmentId, tempG});
                }
            }
        }
        return travelTimes;
}

std::vector<CourierSubPath> perform2Opt(const float turn_penalty, const std::vector<CourierSubPath>& currentPath) {
    std::vector<CourierSubPath> perturbedPath = currentPath;

    // Extract the delivery portion (excluding depot segments)
    std::vector<CourierSubPath> deliveryPath(perturbedPath.begin() + 1, perturbedPath.end() - 1);
    int deliveryEdges = deliveryPath.size();

    if (deliveryEdges < 5) return perturbedPath; // Too small to meaningfully 2-opt

    // Seed randomness
    srand(static_cast<unsigned int>(time(0)));

    // Choose two distinct partition indices
    int maxStart = deliveryEdges - 3; // ensures at least 2 elements after
    if(maxStart <= 0) return perturbedPath; // Not enough elements
    
    int partition1 = 1 + rand() % maxStart;
    int partition2 = partition1 + 1 + rand() % (deliveryEdges - partition1 - 1);

    // Divide the deliveryPath into 3 parts, covering the full range
    std::vector<CourierSubPath> subpath1(deliveryPath.begin(), deliveryPath.begin() + partition1);
    std::vector<CourierSubPath> subpath2(deliveryPath.begin() + partition1, deliveryPath.begin() + partition2);
    std::vector<CourierSubPath> subpath3(deliveryPath.begin() + partition2, deliveryPath.end());

    // Identify the smallest subpath to reverse
    int size1 = subpath1.size();
    int size2 = subpath2.size();
    int size3 = subpath3.size();

    int smallest = std::min({size1, size2, size3});
    int toReverse = (smallest == size1) ? 1 : (smallest == size2) ? 2 : 3;

    std::vector<CourierSubPath>* targetSubpath = (toReverse == 1) ? &subpath1 : (toReverse == 2) ? &subpath2 : &subpath3;

    // Reverse and rebuild the selected subpath
    std::reverse(targetSubpath->begin(), targetSubpath->end());
    for (auto& sub : *targetSubpath) {
        std::swap(sub.intersections.first, sub.intersections.second);
        std::vector<StreetSegmentIdx> newPath = findPathBetweenIntersections(turn_penalty, sub.intersections);
        if(!newPath.empty()) {
            sub.subpath = newPath;
        } else {
            // If path finding fails, revert
            return perturbedPath;
        }
    }

    // Build connecting bridges
    CourierSubPath bridge1;
    bridge1.intersections.first = subpath1.empty() ? subpath2.front().intersections.first : 
                                  subpath1.back().intersections.second;
    bridge1.intersections.second = subpath2.empty() ? subpath3.front().intersections.first : 
                                   subpath2.front().intersections.first;
    
    std::vector<StreetSegmentIdx> bridge1Path = findPathBetweenIntersections(turn_penalty, bridge1.intersections);
    if(bridge1Path.empty() && subpath1.size() > 0 && subpath2.size() > 0) {
        return perturbedPath; // Revert if bridge can't be created
    }
    bridge1.subpath = bridge1Path;

    CourierSubPath bridge2;
    bridge2.intersections.first = subpath2.empty() ? subpath1.back().intersections.second : 
                                 subpath2.back().intersections.second;
    bridge2.intersections.second = subpath3.empty() ? perturbedPath.back().intersections.first : 
                                  subpath3.front().intersections.first;
    
    std::vector<StreetSegmentIdx> bridge2Path = findPathBetweenIntersections(turn_penalty, bridge2.intersections);
    if(bridge2Path.empty() && subpath2.size() > 0 && subpath3.size() > 0) {
        return perturbedPath; // Revert if bridge can't be created
    }
    bridge2.subpath = bridge2Path;

    // Recombine the new path
    std::vector<CourierSubPath> updatedDelivery;
    if(!subpath1.empty()) {
        updatedDelivery.insert(updatedDelivery.end(), subpath1.begin(), subpath1.end());
    }
    
    if(!subpath1.empty() && !subpath2.empty() && !bridge1Path.empty()) {
        updatedDelivery.push_back(bridge1);
    }
    
    if(!subpath2.empty()) {
        updatedDelivery.insert(updatedDelivery.end(), subpath2.begin(), subpath2.end());
    }
    
    if(!subpath2.empty() && !subpath3.empty() && !bridge2Path.empty()) {
        updatedDelivery.push_back(bridge2);
    }
    
    if(!subpath3.empty()) {
        updatedDelivery.insert(updatedDelivery.end(), subpath3.begin(), subpath3.end());
    }

    // Wrap delivery with depot paths
    std::vector<CourierSubPath> finalPath;
    finalPath.push_back(perturbedPath.front());
    finalPath.insert(finalPath.end(), updatedDelivery.begin(), updatedDelivery.end());
    finalPath.push_back(perturbedPath.back());

    // Final sanity check — all intersections must connect
    for (size_t i = 1; i < finalPath.size(); ++i) {
        if (finalPath[i - 1].intersections.second != finalPath[i].intersections.first) {
            std::cerr << "ERROR: Broken connection between subpath " << i - 1 << " and " << i << std::endl;
            return perturbedPath; // Revert if broken
        }
    }

    return finalPath;
}

bool legalPathCheck(const std::vector<CourierSubPath>& path){
    // Reset all delivery items at the beginning of check
    for (auto& item : deliveryItems) {
        item.pickedUp = false;
        item.droppedOff = false;
    }
    
    bool pathLegal = true;

    // Check all intersections that are not depots and simulate the path
    // Idea: if it is a drop off, check if already picked up and update
    //       if it is a pick up, update the data and continue.

    for(int subPath = 1; subPath <  path.size(); subPath++){
        IntersectionIdx currentIntersection = path[subPath].intersections.first;
        
        // Boundary check
        if(currentIntersection >= deliveryLocations.size()) {
            continue;
        }
        
        // Check all deliverys to be made at the current intersection.
        for(int delivery = 0; delivery < deliveryLocations[currentIntersection].deliveryOrderProfile.size(); delivery++){
            
            bool pickupRole = deliveryLocations[currentIntersection].deliveryOrderProfile[delivery].second;
            int deliveryItemNumber = deliveryLocations[currentIntersection].deliveryOrderProfile[delivery].first;
            
            // Boundary check
            if(deliveryItemNumber >= deliveryItems.size()) {
                continue;
            }
            
            if(pickupRole == true){ // Pick up condition
                // Picked up delivery item
                deliveryItems[deliveryItemNumber].pickedUp = true;
            }else if(pickupRole == false){ // Drop off condition reached, check if already picked up
                if(deliveryItems[deliveryItemNumber].pickedUp == false){
                    pathLegal = false;
                    return pathLegal;
                }else{
                    deliveryItems[deliveryItemNumber].droppedOff = true;
                }
            }
        }
    }

    return pathLegal;
}

double calculatePathCost(const float turn_penalty, const std::vector<CourierSubPath>& path){
    double cost = 0.0;
    
    for(int i = 0; i < path.size(); i++){
        cost += computePathTravelTime(turn_penalty, path[i].subpath);
    }
    
    return cost;
}

void cleanDataStructures(){
    // Clear timeMatrix
    for (auto& row : timeMatrix) {
        row.clear();
    }
    timeMatrix.clear();

    // Clear delivery locations properly
    for (auto& location : deliveryLocations) {
        location.deliveryOrderProfile.clear();
    }
    deliveryLocations.clear();
    
    // Clear delivery items
    deliveryItems.clear();
}