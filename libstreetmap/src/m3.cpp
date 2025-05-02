#include "m1.h"
#include "m2.h"
#include "m3.h"
#include <vector>
#include <queue>
#include <unordered_map>
#include "StreetsDatabaseAPI.h"
#include <limits>

//Global Variables


//Data Structures
struct WaveElemAstar {
        IntersectionIdx nodeId;
        StreetSegmentIdx edgeId;
        double travelTime; 
        double fofn; //adding travel time and heuristics

        bool operator>(const WaveElemAstar& otherf)const{
                return fofn > otherf.fofn;
        }
};

//Implement Data Structures

std::vector<StreetSegmentIdx> findPathBetweenIntersections(const double turn_penalty,
        const std::pair<IntersectionIdx, IntersectionIdx> intersect_ids){

        IntersectionIdx from = intersect_ids.first;
        IntersectionIdx to = intersect_ids.second;

        //already at destination - exit function
        if(from == to){
                return {};
        }

        //finding heuristic travel time - birds eye view path from point a to point b speed
        int numIntersections = getNumIntersections();
        std::vector<double>heuristic(numIntersections, -1.0);

        for(int temp = 0; temp < numIntersections; temp++){
                if(heuristic[temp] < 0){

                        LatLon tempFrom = getIntersectionPosition(temp);
                        LatLon tempTo = getIntersectionPosition(to);

                        double distanceBetween = findDistanceBetweenTwoPoints(tempFrom, tempTo);
                        heuristic[temp] = distanceBetween / (100/3.6);
                }
        }

        //final path vector to pass to compute PathTravelTime
        std::vector<StreetSegmentIdx> path;

        //arguments of priority queue: object type being stored, container of objects, comparison
        //uses operator> to compare the f(n) of both WaveElements
        std::priority_queue<WaveElemAstar, std::vector<WaveElemAstar>, std::greater<WaveElemAstar>> minQueue;

        //vector for g(n)
        std::vector<double> gofn(numIntersections, std::numeric_limits<double>::infinity());

        //vector of nodes already visited
        std::vector<int> nodesVisited(numIntersections, -1);

        //doesn't take any time to get to the start of the path
        gofn.at(from) = 0.0;

        //Add start node into minQueue with (intersection position, streetseg, travel time, f(n))
        minQueue.push({from, -1, gofn[from], heuristic[from]});

        while(!minQueue.empty()) {

                //look at node at top of queue, which is the cutrent best and pop() so that you don look at it twice
                WaveElemAstar temporaryWave = minQueue.top();
                minQueue.pop();

                IntersectionIdx currentId = temporaryWave.nodeId;

                if (currentId == to){
                        break;
                }

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
                                gofn.at(nextNode) = tempG;
                                nodesVisited[nextNode] = segmentId;
                                case1 = true;
                        }

                        
                        //if our current g calculation is faster than either of the above cases update the min queue
                        if(case1){
                                minQueue.push({nextNode, segmentId, tempG, tempG + heuristic.at(nextNode)});
                        }
                
                }
        }
        
        IntersectionIdx temp = to;

        while((nodesVisited[temp] != -1) && (temp != from)){
                StreetSegmentIdx prev = nodesVisited[temp];
                path.push_back(prev);
                StreetSegmentInfo segmentInfo = getStreetSegmentInfo(prev);
                if(segmentInfo.to == temp){
                        temp = segmentInfo.from;
                }else{
                        temp = segmentInfo.to;
                }
        }
        if(temp != from){
                return {};
        }

        std::reverse(path.begin(), path.end());
        return path;
}

double computePathTravelTime(const double turn_penalty, const std::vector<StreetSegmentIdx>& path){
        double travelTime = 0.0;
        //initialize first "previous" streetId to -1 since its not a valid streetId
        StreetIdx previous = -1;
        //loop through every street segment in the path
        for(int pathIndex = 0 ; pathIndex < path.size() ; pathIndex++){
                //check if the current streetId has changed
                if(previous != getStreetSegmentInfo(path[pathIndex]).streetID && previous != -1){
                        travelTime += turn_penalty;
                        
                }

                travelTime += findStreetSegmentTravelTime(path[pathIndex]);
                previous = getStreetSegmentInfo(path[pathIndex]).streetID;
        }

        return travelTime;
}
