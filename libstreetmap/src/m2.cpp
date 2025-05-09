/* 
 * Copyright 2025 University of Toronto
 *
 * Permission is hereby granted, to use this software and associated 
 * documentation files (the "Software") in course work at the University 
 * of Toronto, or for personal use. Other uses are prohibited, in 
 * particular the distribution of the Software either publicly or to third 
 * parties.
 *
 * The above copyright notice and this permission notice shall be included in 
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "m1.h"
#include "m2.h"
#include "m3.h"
#include "m4.h"
#include "ezgl/application.hpp"
#include "ezgl/graphics.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <curl/curl.h>
#include <sstream>
#include <unordered_map>
#include <dirent.h>

int mapNum = 0;
int directionInstructionNumber = 0;
bool reload = false;
bool teleportForIntersection = false;
ezgl::rectangle firstScope;

// Lists to store city street names for autocomplete for our GtkEntries
GtkListStore *listOne;
GtkListStore *listTwo;

// Lists to store city intersection names for autocomplete for our GtkEntries
GtkListStore *intersectionListOne;
GtkListStore *intersectionListTwo;

// API related global variables to make API calls in the program
CURLcode initResult;
CURL *weatherHandle;

CURL *chatbotHandle;

// Stack to show instructions
GtkStack* directionsStack = nullptr;

std::unordered_map<std::string, ezgl::point2d> featureMidpoints;
std::vector<std::string> mapCodes = {};
std::vector<std::string> directionInstructionsVec;
std::unordered_map<std::string, IntersectionIdx> intersectionNames;

std::vector<CourierSubPath> pathz = {};

// Basic Function Declarations
// Helper functions prototypes for cartesian form Latitude and Longitude
float latAverage;
float lonToX(float lon);
float latToY(float lat);
float xToLon(float x);
float yToLat(float y);

void roadSegmentProcessing();
void act_on_mouse_click(ezgl::application* app, GdkEventButton* /*event*/, double x, double y);
void toggle_darkMode(GtkWidget* /*widget*/, ezgl::application* application);
void toggle_icons(GtkWidget* /*widget*/, ezgl::application* application);
void initial_setup(ezgl::application* application, bool /*newWindow*/);
void draw_main_canvas(ezgl::renderer *g);
void preprocess_poi_midpoints();
void preprocess_feature_midpoints();
void label_features(ezgl::renderer *g);
void loadMapCodes(const char* path);

size_t webScrapeProcessor(void* scrapedContent, size_t contentSize, size_t dataSizePerN, void* contentDataStructPtr);
size_t fetchChatbotResponse(void* response, size_t responseSize, size_t dataSizePerN, void* contentDataStructPtr);

void updateWeatherCall(ezgl::point2d center);
void displayChatbotQueryInfo(GtkWidget */*widget*/, ezgl::application *application);
void map_select(GtkComboBoxText* self, ezgl::application* app);
void findIntersectionsOfEntryStreets(GtkWidget */*widget*/, ezgl::application *application);
void setStartIntersection(GtkWidget */*widget*/, ezgl::application *application);
void setEndIntersection(GtkWidget */*widget*/, ezgl::application *application);
void helpDialogCBK(GtkWidget */*widget*/, ezgl::application *application);
void changeClickMode(GtkWidget */*widget*/, ezgl::application *application);
void getDirections(GtkWidget */*widget*/, ezgl::application *application);
std::stringstream getDetailedDirections(std::vector<StreetSegmentIdx> path);
void clearDirections(GtkWidget */*widget*/, ezgl::application *application);
void displayDirectionsOnStack(std::string& directionsStr);

bool clickMode = false;
bool startClick = false;
bool endClick = false;
bool tripMessage = false;
int checkTurn(StreetSegmentIdx incoming, StreetSegmentIdx outgoing);

struct drawSettings{
   double iconSize;
   bool enableRoads;
   bool enableHighways;
   bool enableMajorRoads;
   bool enableMinorRoads;
   bool enableFeatures;
   bool enableWaterFeature;
   bool enableGreeneryFeature;
   bool enableIsland;
   bool enableBuilding;
   bool enableUnknown;
   bool enableIntersections;
   bool enableFeatureLabels;
   bool enableStreetlabels;
   bool enableBuildingIcon;
   bool enableGreeneryIcon;
   bool enableIslandIcon;
   bool enableWaterIcon;
};

void setDrawSettings(drawSettings* settings,double scale);
void preprocess_street_name();

enum RoadType {
   HIGHWAY,
   MAJOR_ROAD,
   MINOR_ROAD
};

struct RoadSegmentByType {
   RoadType roadType;
   StreetSegmentInfo segInfo;
   bool isOneWay;
   bool isCurved;
   std::vector<ezgl::point2d> allPoints;
   ezgl::point2d midPoint;
   ezgl::point2d tangentAtMidpoint;
   std::string streetName;
   std::vector<ezgl::point2d> labelPositions;
   std::vector<double> labelAngles;


   RoadSegmentByType(StreetSegmentIdx streetIdx){
      
      segInfo = getStreetSegmentInfo(streetIdx);

      if (segInfo.speedLimit*3.6 >= 90) {
         roadType = HIGHWAY;
      }
      else if (segInfo.speedLimit*3.6 >= 50 &&  segInfo.speedLimit*3.6 < 90){
         roadType = MAJOR_ROAD;
      }
      else if (segInfo.speedLimit*3.6 < 50){
         roadType = MINOR_ROAD;
      }
      
      isOneWay = segInfo.oneWay;

       // Drawing the street segments (hence, streets in the long run)
      LatLon fromPoint = getIntersectionPosition(segInfo.from);
      LatLon toPoint = getIntersectionPosition(segInfo.to);
      
      allPoints.push_back(ezgl::point2d(lonToX(fromPoint.longitude()), latToY(fromPoint.latitude())));

      isCurved = segInfo.numCurvePoints > 0;

      for(int i = 0; i < segInfo.numCurvePoints; i++){
         LatLon curvePoint = getStreetSegmentCurvePoint(streetIdx,i);
         allPoints.push_back(ezgl::point2d(lonToX(curvePoint.longitude()), latToY(curvePoint.latitude())));
      }
      allPoints.push_back(ezgl::point2d(lonToX(toPoint.longitude()), latToY(toPoint.latitude())));

      midPoint = {(allPoints.front().x + allPoints.back().x) * 0.5, (allPoints.front().y + allPoints.back().y) * 0.5};


      if (isCurved){
         int middleCurve;
      
         int numCurvePoints = segInfo.numCurvePoints;
         bool isOdd = numCurvePoints % 2;
         if(isOdd){
            int makeEven = numCurvePoints - 1;
            middleCurve = (makeEven/2) + 1;
         
            midPoint = allPoints.at(middleCurve);
            tangentAtMidpoint = allPoints.at(middleCurve+1) - allPoints.at(middleCurve-1);

         }else{
            middleCurve = numCurvePoints/2;
            
            midPoint = allPoints.at(middleCurve);
            tangentAtMidpoint = allPoints.at(middleCurve+1) - allPoints.at(middleCurve);
         }

      } else{
        tangentAtMidpoint = allPoints.back() - allPoints.front();
      }
      streetName = getStreetName(segInfo.streetID);
   

      // Used ChatGPT for debugging purposes. Prompt Used: "The struct is not 
      //properly computing the angle, comment by line and help me debug". 
      //Date Accessed: March 2nd. (lines 170 - 198)
      int numLabels = static_cast<int>(allPoints.size() / 6);
      if (numLabels < 1){                                    
         numLabels = 1;
       }

      for (int i = 1; i <= numLabels; i++) {

         //splits into the num of labels and equally spaces each of them
         int pointIndex = (i * allPoints.size()) / (numLabels + 1);
         
         ezgl::point2d labelPos = allPoints.at(pointIndex);

         //keeps from pasting at very end
         if (pointIndex + 1 >= allPoints.size()) {
            continue; 
         }

         ezgl::point2d tangentVector = allPoints.at(pointIndex + 1) - allPoints.at(pointIndex);
         double angle = atan2(tangentVector.y, tangentVector.x) / kDegreeToRadian;

         // Flip text if upside-down
         if (angle > 90 || angle < -90) {
            angle += 180;
         }

         labelPositions.push_back(labelPos);
         labelAngles.push_back(angle);
      }
   }
};

void one_way_street_arrows(ezgl::renderer *g, const RoadSegmentByType &roadSegment);

struct Intersection_data {
   ezgl::point2d xy_loc;
   std::string name;
   bool highlighted = false;
   int intersectionId;
   bool starting = false;
   bool ending = false;
};


struct FeatureData {
   std::string name;
   FeatureType type;
   std::vector<ezgl::point2d> pointsOfFeature;
};

struct WeatherLabelsData{
   GtkLabel *descriptionLabel;
   GtkLabel *temperatureLabel;
   GtkLabel *feelLikeLabel;
   GtkLabel *humidityLabel;
   GtkLabel *windSpeed;
   GtkLabel *cloudiness;
};

struct POIData {
    std::string name;
    std::vector<ezgl::point2d> pointsOfPOI;
};
// Global Labels struct object to store GTK objects and then set text later
WeatherLabelsData weatherLabels;

// Clear these later
//intersection global variables
std::vector<Intersection_data> intersections;
std::vector<Intersection_data*> highlightedIntersections = {};
Intersection_data* startIntersection;
Intersection_data* endIntersection;
std::vector<StreetSegmentInfo> streetSegments;
std::vector<std::string> streets;
std::vector<FeatureData> features;
std::vector<RoadSegmentByType> roadSegmentsByType;
std::unordered_map<std::string, ezgl::point2d> poiMidpoints;
std::vector<POIData> pois;
std::vector<StreetSegmentIdx>directions;

//images
ezgl::surface* image = ezgl::renderer::load_png("libstreetmap/resources/temp_pinpoint.png");
ezgl::surface* imageLight = ezgl::renderer::load_png("libstreetmap/resources/temp_pinpointLight.png");
ezgl::surface* BUILDINGicon = ezgl::renderer::load_png("libstreetmap/resources/buildingIcon.png");
ezgl::surface* PARKicon = ezgl::renderer::load_png("libstreetmap/resources/greeneryIcon.png");
ezgl::surface* GREENSPACEicon = ezgl::renderer::load_png("libstreetmap/resources/greeneryIcon.png");
ezgl::surface* ISLANDicon = ezgl::renderer::load_png("libstreetmap/resources/islandIcon.png");
ezgl::surface* BEACHicon = ezgl::renderer::load_png("libstreetmap/resources/waterIcon.png");
ezgl::surface* LAKEicon = ezgl::renderer::load_png("libstreetmap/resources/waterIcon.png");
ezgl::surface* RIVERicon = ezgl::renderer::load_png("libstreetmap/resources/waterIcon.png");
ezgl::surface* startIcon = ezgl::renderer::load_png("libstreetmap/resources/startingIcon.png");
ezgl::surface* endIcon = ezgl::renderer::load_png("libstreetmap/resources/endingIcon.png");


bool darkMode = false;
bool iconEnable = false;
bool newMap = false;

void preprocess_feature_midpoints() {

   for (const auto &feature : features) {
      //skip over features that dont have names
      if (feature.name.empty() || feature.name == "<unknown>") {
         continue;
      }
      
      // Compute the centroid of the feature
      double sumOfX = 0, sumOfY = 0;
      int numPoints = feature.pointsOfFeature.size();

      //Sum points in x and points in Y
      for (const auto &point : feature.pointsOfFeature) {
         sumOfX += point.x;
         sumOfY += point.y;
      }

      //average of X and Y coordinates to find centroid position
      ezgl::point2d centroid(sumOfX / numPoints, sumOfY / numPoints);

      //stores centroid of feature by name
      featureMidpoints[feature.name] = centroid;
   }
}

void preprocess_poi_midpoints() {

    for (POIIdx poiIdx = 0; poiIdx < getNumPointsOfInterest(); poiIdx++) {

        std::string poiName = getPOIName(poiIdx);

        //skip over if no name
        if (poiName.empty() || poiName == "<unknown>") {
            continue;
        }

        //get position
        LatLon position = getPOIPosition(poiIdx);

        //store point by name in midpoints vector
        poiMidpoints[poiName] = ezgl::point2d(lonToX(position.longitude()), latToY(position.latitude()));
    }
}


void roadSegmentProcessing(){ 

   //store struct of each road Segment in vector
   for (StreetSegmentIdx currentId = 0; currentId < getNumStreetSegments(); currentId++){
      roadSegmentsByType.push_back(RoadSegmentByType(currentId));
   }
}

// Helper functions prototypes for cartesian form Latitude and Longitude
void deleteImages();
void clearAll();
void loadDataStructures();
void label_streets(ezgl::renderer *g);
void label_major_and_highway(ezgl::renderer *g);
void label_minor_roads(ezgl::renderer *g);
void label_pois(ezgl::renderer *g);
void drawDirections(std::vector<StreetSegmentIdx> path, ezgl::renderer *g);
void openChatbotCBK(GtkWidget *, ezgl::application *);
gboolean hideChatWindow(GtkWidget *, GdkEvent *, gpointer);
gboolean hideHelpWindow(GtkWidget *, GdkEvent *, gpointer);


void label_major_and_highway(ezgl::renderer *g) {
    
   if (darkMode){
      g ->set_color(255, 255, 255);
   }else{
      g->set_color(50, 50, 50);
   }
    int segmentCounter = 0;
    int skipBy = 25;  

    for (const auto& roadSegment : roadSegmentsByType) {

      //only print major roads and highways that have names
        if (roadSegment.roadType == MINOR_ROAD || roadSegment.streetName.empty() || roadSegment.streetName == "<unknown>") {
            continue;
        }
      //skip every (skipBy) segment to avoid clutter and incriment segment you have gone through
      //to keep track
        if (segmentCounter % skipBy != 0) {
            segmentCounter++;
            continue;
        }

        for (size_t i = 0; i < roadSegment.labelPositions.size(); i++) {

           //check if in visible world
            ezgl::rectangle visualBox = g->get_visible_world();
            ezgl::point2d position = roadSegment.labelPositions[i];

            if(position.y > visualBox.bottom() && position.y < visualBox.top() && position.x > visualBox.left() && position.x < visualBox.right()){
               if((g->get_visible_world().area()<pow(10,8)&&roadSegment.roadType==HIGHWAY)||(g->get_visible_world().area()<pow(10,6)&&roadSegment.roadType==MAJOR_ROAD)){
                  g->set_text_rotation(roadSegment.labelAngles.at(i));
                  g->draw_text(roadSegment.labelPositions.at(i), roadSegment.streetName);
               }
            }
        }
        //increment segement you are on
        segmentCounter++; 
    }

    g->set_text_rotation(0);
}

void label_minor_roads(ezgl::renderer *g) {
   //colout - dark yellow

    if (darkMode){
      g ->set_color(255, 255, 255);
   }else{
      g->set_color(20, 20, 7); 
   }
    

    int segmentCounter = 0;
    int skipBy = 10;  

   for (const auto& roadSegment : roadSegmentsByType) {
      
      //only print minor roads with a name
      if(roadSegment.roadType != MINOR_ROAD || roadSegment.streetName.empty() || roadSegment.streetName == "<unknown>"){
         continue;
      }

      //skip by skipByamount 
      if (segmentCounter % skipBy != 0) {
            segmentCounter++;
            continue;
        }

      //
        for (size_t i = 0; i < roadSegment.labelPositions.size(); ++i) {

            //check if in visual world
            ezgl::rectangle visualBox = g->get_visible_world();
            ezgl::point2d position = roadSegment.labelPositions[i];
            if(position.y > visualBox.bottom() && position.y < visualBox.top() && position.x > visualBox.left() && position.x < visualBox.right()&&g->get_visible_world().area()<pow(10,6)){
               g->set_text_rotation(roadSegment.labelAngles.at(i));
               g->draw_text(roadSegment.labelPositions.at(i), roadSegment.streetName);
            }
        }
        segmentCounter++; 
    }
}

void label_pois(ezgl::renderer *g) {

   if (darkMode){
      g ->set_color(255, 255, 255);
   }else{
      g->set_color(0, 0, 0);
   }
   g->set_font_size(8);
   
   for (const auto &[poiName, centroid] : poiMidpoints) {
        if (poiName == "<noname>" || poiName == " ") {
            return;
        }

         //check if in visible world
         ezgl::point2d label_position = {centroid.x, centroid.y};
         ezgl::rectangle visualBox = g->get_visible_world();

         if(label_position.y > visualBox.bottom() && label_position.y < visualBox.top() && label_position.x > visualBox.left() && label_position.x < visualBox.right()){
            g->draw_text(label_position, poiName);
         }
    }
}

void label_features(ezgl::renderer *g) {
   if (darkMode){
      g ->set_color(255, 255, 255);
   }else{
      g->set_color(0, 0, 0);
   }
   g->set_font_size(10);

   for (const auto &[featureName, centroid] : featureMidpoints) {

      if (featureName == "<noname>" || featureName == " "){
         return;
      }
         //check if in visible world
         ezgl::point2d label_position = {centroid.x, centroid.y};  
         ezgl::rectangle visualBox = g->get_visible_world();
         if(label_position.y > visualBox.bottom() && label_position.y < visualBox.top() && label_position.x > visualBox.left() && label_position.x < visualBox.right()){
            g->draw_text(label_position, featureName);
         }
     }
 }

void drawDirections(std::vector<StreetSegmentIdx> path, ezgl::renderer *g){

   std::vector<ezgl::point2d> pathPoints;

   for(const StreetSegmentIdx& segmentId : path){
      
      RoadSegmentByType& segment = roadSegmentsByType[segmentId];
      pathPoints.insert(pathPoints.end(), segment.allPoints.begin(), segment.allPoints.end());
   }
   g->set_color(0,0,255);
   g->set_line_width(6);

   for(int i =0; i<=pathPoints.size(); i++){
      g->draw_line(pathPoints[i], pathPoints[i + 1]);
   }
}

void act_on_mouse_click(ezgl::application* app, GdkEventButton* /*event*/, double x, double y){   
   //get the nearest intersection the user clicekd
   LatLon interPos = LatLon(yToLat(y), xToLon(x));
   int interId = findClosestIntersection(interPos);
   if(clickMode){
      if(sqrt(pow(x-lonToX(getIntersectionPosition(interId).longitude()),2) + pow(y-latToY(getIntersectionPosition(interId).latitude()),2))<=20){
         if(startClick){
            GtkWindow *directionOutputWindow = GTK_WINDOW(app->get_object("directionOutputWindow"));
            gtk_widget_hide(GTK_WIDGET(directionOutputWindow));

            GtkGrid* directionsGrid = GTK_GRID(app->get_object("directionOutput"));
            gtk_widget_hide(GTK_WIDGET(directionsGrid));
            startClick = false;
            endClick = true;
            //here i will get rid of current directions
            directions = {};
            if(startIntersection!=nullptr){
               startIntersection->starting = false;
            }
            if(endIntersection!=nullptr){
               endIntersection->ending = false;
            }
            startIntersection = nullptr;
            endIntersection = nullptr;
            startIntersection = &intersections[interId];
            startIntersection->starting = true;
            app->update_message("");
         }
         else if(endClick){
            startClick = true;
            endClick = false;
            endIntersection = &intersections[interId];
            endIntersection->ending = true;
            if(startIntersection != nullptr && endIntersection != nullptr){
               directions = findPathBetweenIntersections(15,{startIntersection->intersectionId,endIntersection->intersectionId});
            }
            std::stringstream pathMessage;
            pathMessage << "From: " << startIntersection->name << " To: " << endIntersection->name << ", Estimated travel time: " << computePathTravelTime(15,directions)/60 << " Mins\n";
            app->update_message(pathMessage.str());
            tripMessage = true;
            GtkWindow *directionOutputWindow = GTK_WINDOW(app->get_object("directionOutputWindow"));
            gtk_widget_show_all(GTK_WIDGET(directionOutputWindow));
            GtkGrid* directionsGrid = GTK_GRID(app->get_object("directionOutput"));
            gtk_widget_show(GTK_WIDGET(directionsGrid));
            std::string directions_str = getDetailedDirections(directions).str();
            std::cout << getDetailedDirections(directions).str() << std::endl;//keep for now
            displayDirectionsOnStack(directions_str);
         }
         app->refresh_drawing();
      }
   }
   else{
      //if the clicked point is within a radius of 20 from the intersection, highlight the intersection
      if(sqrt(pow(x-lonToX(getIntersectionPosition(interId).longitude()),2) + pow(y-latToY(getIntersectionPosition(interId).latitude()),2))<=20){
         //if highlighted intersections arent empty
         tripMessage = false;
         if(highlightedIntersections.size()>1){
            for(int i = 0 ; i < highlightedIntersections.size() ; i++){
               highlightedIntersections[i]->highlighted = false;
            }
            highlightedIntersections.clear();
            highlightedIntersections.resize(0);
            intersections[interId].highlighted = true;
            highlightedIntersections.push_back(&intersections[interId]);
         }
         //if highlighted intersections are empty
         else if(highlightedIntersections.size()==1){
            if(highlightedIntersections[0] == &intersections[interId]){
               highlightedIntersections[0]->highlighted=false;
               highlightedIntersections.clear();
               highlightedIntersections.resize(0);
            }
            else{
               highlightedIntersections[0]->highlighted=false;
               highlightedIntersections[0] = &intersections[interId];
               intersections[interId].highlighted=true;
            }
         }
         else if(highlightedIntersections.size() == 0){
            intersections[interId].highlighted = true;
            highlightedIntersections.push_back(&intersections[interId]);
         }
         std::stringstream ss;
         ss << "Intersection Name: " << intersections[interId].name << "\n";
         if(intersections[interId].highlighted){
            app->update_message(ss.str());
         }
         else{
            app->update_message("");
         }
         app->refresh_drawing();
      }
   }
}

void toggle_darkMode(GtkWidget* /*widget*/, ezgl::application* application){
   darkMode = !darkMode;
   application->refresh_drawing();
}

void toggle_icons(GtkWidget* /*widget*/, ezgl::application* application){
   iconEnable = !iconEnable;
   application->refresh_drawing();
}

void map_select(GtkComboBoxText* self, ezgl::application* app){
   gint mapClicked = gtk_combo_box_get_active(GTK_COMBO_BOX(self));
   //if a new map is selected execute
   if(mapClicked != 0){
      //make pop up message
      GtkWidget* popUpMessage = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, 
                                               GTK_MESSAGE_INFO, 
                                               GTK_BUTTONS_OK, 
                                               "loading new map... please press OK and wait a few seconds");
      gtk_window_set_title(GTK_WINDOW(popUpMessage), "Notification");
      //output the widget
      gtk_dialog_run(GTK_DIALOG(popUpMessage)); //draw widget
      gtk_widget_destroy(popUpMessage); //destroy it afte user clicks ok
      mapNum = mapClicked -1;
      newMap = true; 
   }
   app->update_message("");
   app->refresh_drawing();
}

void changeClickMode(GtkWidget */*widget*/, ezgl::application *application){
   clickMode = !clickMode;
   if(clickMode){
      application->update_message("Click mode on. Click intersections to get directions between them.");
      startClick = true;
      endClick = false;
   }
   else{
      application->update_message("Click mode off. To get directions, insert intersections into search bar.");
      startClick = false;
      endClick = false;
   }
}
void setStartIntersection(GtkWidget */*widget*/, ezgl::application *application){
   GtkEntry *firstStreetEntry = GTK_ENTRY(application->get_object("FirstStreetEntry"));
   GtkEntry *secondStreetEntry = GTK_ENTRY(application->get_object("SecondStreetEntry"));
    
   
   // Convert the text in the entries from gchar* to string  for our functions in M1.
   std::string  firstStreetText(gtk_entry_get_text(firstStreetEntry)); // text from first entry
   std::string secondStreetText(gtk_entry_get_text(secondStreetEntry)); // text from second entry

   // Find Ids of streets entered
   std::vector<StreetIdx> firstStreet = findStreetIdsFromPartialStreetName(firstStreetText);
   std::vector<StreetIdx> secondStreet = findStreetIdsFromPartialStreetName(secondStreetText);

   std::cout << "First Street: " << firstStreetText << "\n";
   std::cout << "Second Street: " << secondStreetText << "\n";

   std::pair<StreetIdx, StreetIdx> streetIds;
   if(firstStreet.empty() || secondStreet.empty()){;
     std::cout << "One or more street(s) not found" << "\n";
     return;
   }

   std::cout << "Matches for first street: " << firstStreet.size() << "\n";
   std::cout << "Matches for second street: " << secondStreet.size() << "\n";
   std::vector<IntersectionIdx> intersectionsOfStreetEntries = {};

   for(int firstIndex = 0 ; firstIndex < firstStreet.size() ; firstIndex++){
      for(int secondIndex = 0 ; secondIndex < secondStreet.size() ; secondIndex++){
         streetIds.first = firstStreet[firstIndex];
         streetIds.second = secondStreet[secondIndex];
         std::vector<IntersectionIdx> tempIntersectionsOfStreetEntries = findIntersectionsOfTwoStreets(streetIds);  
         if(tempIntersectionsOfStreetEntries.size() > 0){
            intersectionsOfStreetEntries.insert(intersectionsOfStreetEntries.end(),tempIntersectionsOfStreetEntries.begin(),tempIntersectionsOfStreetEntries.end());
         } 
      }
   }
   std::cout << "Number of intersections found: " << intersectionsOfStreetEntries.size() << "\n";
   if(clickMode){
      application->update_message("You are in click mode. Reclick the click mode toggle button to insert.");
   }
   if(intersectionsOfStreetEntries.size() > 0 && !clickMode){
      //highlight set intersection
      if(startIntersection != nullptr){
         startIntersection->starting = false;
      }
      application->update_message(getIntersectionName(intersectionsOfStreetEntries[0]));
      for(int i = 0; i < intersectionsOfStreetEntries.size(); i++){
         std::cout << getIntersectionName(intersectionsOfStreetEntries[i]) << "\n";
         startIntersection = &intersections[intersectionsOfStreetEntries[i]];
         application->update_message(getIntersectionName(intersectionsOfStreetEntries[i]));
      }
      startIntersection->starting = true;
   }
   application->refresh_drawing();
}

void setEndIntersection(GtkWidget */*widget*/, ezgl::application *application){
   GtkEntry *firstStreetEntry = GTK_ENTRY(application->get_object("FirstStreetEntry"));
   GtkEntry *secondStreetEntry = GTK_ENTRY(application->get_object("SecondStreetEntry"));
    
   
   // Convert the text in the entries from gchar* to string  for our functions in M1.
   std::string  firstStreetText(gtk_entry_get_text(firstStreetEntry)); // text from first entry
   std::string secondStreetText(gtk_entry_get_text(secondStreetEntry)); // text from second entry

   // Find Ids of streets entered
   std::vector<StreetIdx> firstStreet = findStreetIdsFromPartialStreetName(firstStreetText);
   std::vector<StreetIdx> secondStreet = findStreetIdsFromPartialStreetName(secondStreetText);

   std::cout << "First Street: " << firstStreetText << "\n";
   std::cout << "Second Street: " << secondStreetText << "\n";

   std::pair<StreetIdx, StreetIdx> streetIds;
   if(firstStreet.empty() || secondStreet.empty()){;
     std::cout << "One or more street(s) not found" << "\n";
     return;
   }

   std::cout << "Matches for first street: " << firstStreet.size() << "\n";
   std::cout << "Matches for second street: " << secondStreet.size() << "\n";
   std::vector<IntersectionIdx> intersectionsOfStreetEntries = {};

   for(int firstIndex = 0 ; firstIndex < firstStreet.size() ; firstIndex++){
      for(int secondIndex = 0 ; secondIndex < secondStreet.size() ; secondIndex++){
         streetIds.first = firstStreet[firstIndex];
         streetIds.second = secondStreet[secondIndex];
         std::vector<IntersectionIdx> tempIntersectionsOfStreetEntries = findIntersectionsOfTwoStreets(streetIds);  
         if(tempIntersectionsOfStreetEntries.size() > 0){
            intersectionsOfStreetEntries.insert(intersectionsOfStreetEntries.end(),tempIntersectionsOfStreetEntries.begin(),tempIntersectionsOfStreetEntries.end());
         } 
      }
   }
   std::cout << "Number of intersections found: " << intersectionsOfStreetEntries.size() << "\n";
   if(clickMode){
      application->update_message("You are in click mode. Reclick the click mode toggle button to insert.");
   }
   if(intersectionsOfStreetEntries.size() > 0&&!clickMode){
      //highlight set intersection
      if(endIntersection != nullptr){
         endIntersection->ending = false;
      }
      application->update_message(getIntersectionName(intersectionsOfStreetEntries[0]));
      for(int i = 0; i < intersectionsOfStreetEntries.size(); i++){
         std::cout << getIntersectionName(intersectionsOfStreetEntries[i]) << "\n";
         endIntersection = &intersections[intersectionsOfStreetEntries[i]];
         application->update_message(getIntersectionName(intersectionsOfStreetEntries[i]));
      }
      endIntersection->ending = true;
   }
   application->refresh_drawing();
}


void getDirections(GtkWidget */*widget*/, ezgl::application *application){

   if(startIntersection != nullptr && endIntersection != nullptr && !clickMode){
      GtkWindow *directionOutputWindow = GTK_WINDOW(application->get_object("directionOutputWindow"));
      gtk_widget_show_all(GTK_WIDGET(directionOutputWindow));
      GtkGrid* directionsGrid = GTK_GRID(application->get_object("directionOutput"));
      gtk_widget_show(GTK_WIDGET(directionsGrid));
      directions = findPathBetweenIntersections(15,{startIntersection->intersectionId,endIntersection->intersectionId});
      std::stringstream pathMessage;
      pathMessage << "From: " << startIntersection->name << " To: " << endIntersection->name << ", Estimated travel time: " << computePathTravelTime(15,directions)/60 << " Mins\n";
      application->update_message(pathMessage.str());
      tripMessage = true;
      std::string directions_str = getDetailedDirections(directions).str();
      std::cout << getDetailedDirections(directions).str() << std::endl;//keep for now
      displayDirectionsOnStack(directions_str);
   }
   application->refresh_drawing();
}

void displayDirectionsOnStack(std::string& strDirections) {

   // Clear older instructions if any
   directionInstructionsVec.clear();


   // populate current instructions vector with items in sstream that way given
   std::istringstream iss(strDirections);
   std::string instruction;
   while (std::getline(iss, instruction)) {
      if (!instruction.empty()) {
         directionInstructionsVec.push_back(instruction);
      }
   }

   // Citation: The below for loop snipets were drawn from ChatGPT for syntax, functions and learning how to use the stack toplevel.
   // Retrieved on March 24th, 2025
   // Still commented for clarity

   // Removes the older labels in the stack 
   GList* children = gtk_container_get_children(GTK_CONTAINER(directionsStack));
   for (GList* iter = children; iter != NULL; iter = iter->next) {
      gtk_widget_destroy(GTK_WIDGET(iter->data));
   }
   g_list_free(children);

   // Create new instructions labels and add to stack
   for (size_t i = 0; i < directionInstructionsVec.size(); ++i) {
      GtkWidget* instructionLbl = gtk_label_new(directionInstructionsVec[i].c_str());
      gtk_widget_show(instructionLbl);

      std::string pageName = "instruction" + std::to_string(i);
      gtk_stack_add_named(GTK_STACK(directionsStack), instructionLbl, pageName.c_str());
   }


   // show first item on the stack to begin (first direction instruction)
   if (!directionInstructionsVec.empty()) {
      std::string firstPage = "instruction0";
      gtk_stack_set_visible_child_name(GTK_STACK(directionsStack), firstPage.c_str());
   }
}


void findIntersectionsOfEntryStreets(GtkWidget */*widget*/, ezgl::application *application){
   GtkEntry *firstStreetEntry = GTK_ENTRY(application->get_object("FirstStreetEntry"));
   GtkEntry *secondStreetEntry = GTK_ENTRY(application->get_object("SecondStreetEntry"));
   
   // Convert the text in the entries from gchar* to string  for our functions in M1.
   std::string  firstStreetText(gtk_entry_get_text(firstStreetEntry)); // text from first entry
   std::string secondStreetText(gtk_entry_get_text(secondStreetEntry)); // text from second entry

   // Find Ids of streets entered
   std::vector<StreetIdx> firstStreet = findStreetIdsFromPartialStreetName(firstStreetText);
   std::vector<StreetIdx> secondStreet = findStreetIdsFromPartialStreetName(secondStreetText);

   std::cout << "First Street: " << firstStreetText << "\n";
   std::cout << "Second Street: " << secondStreetText << "\n";

   std::pair<StreetIdx, StreetIdx> streetIds;
   if(firstStreet.empty() || secondStreet.empty()){;
     std::cout << "One or more street(s) not found" << "\n";
     return;
   }

   std::cout << "Matches for first street: " << firstStreet.size() << "\n";
   std::cout << "Matches for second street: " << secondStreet.size() << "\n";
   std::vector<IntersectionIdx> intersectionsOfStreetEntries = {};

   for(int firstIndex = 0 ; firstIndex < firstStreet.size() ; firstIndex++){
      for(int secondIndex = 0 ; secondIndex < secondStreet.size() ; secondIndex++){
         streetIds.first = firstStreet[firstIndex];
         streetIds.second = secondStreet[secondIndex];
         std::vector<IntersectionIdx> tempIntersectionsOfStreetEntries = findIntersectionsOfTwoStreets(streetIds);  
         if(tempIntersectionsOfStreetEntries.size() > 0){
            intersectionsOfStreetEntries.insert(intersectionsOfStreetEntries.end(),tempIntersectionsOfStreetEntries.begin(),tempIntersectionsOfStreetEntries.end());
         } 
      }
   }
   std::cout << "Number of intersections found: " << intersectionsOfStreetEntries.size() << "\n";


   if(intersectionsOfStreetEntries.size() > 0){
      //cancel all currently highlighted intersections
      for(int i = 0 ; i < highlightedIntersections.size() ; i++){
         highlightedIntersections[i]->highlighted = false;
      }
      highlightedIntersections.resize(0);
      application->update_message(getIntersectionName(intersectionsOfStreetEntries[0]));
      for(int i = 0; i < intersectionsOfStreetEntries.size(); i++){
         std::cout << getIntersectionName(intersectionsOfStreetEntries[i]) << "\n";
         highlightedIntersections.push_back(&intersections[intersectionsOfStreetEntries[i]]);
         intersections[intersectionsOfStreetEntries[i]].highlighted=true;
         application->update_message(getIntersectionName(intersectionsOfStreetEntries[i]));
         teleportForIntersection = true;
         tripMessage = false; //the trip message just got cleared out
      }

   }else{
      application->update_message("No intersections found between given streets");
   }
   
   application->refresh_drawing();
}



void openChatbotCBK(GtkWidget */*widget*/, ezgl::application *application){
   GtkWindow *chatbotWindow = GTK_WINDOW(application->get_object("chatbotWindow"));

   gtk_widget_show_all(GTK_WIDGET(chatbotWindow));
}

gboolean hideChatWindow(GtkWidget *widget, GdkEvent */*event*/, gpointer user_data) {
   gtk_widget_hide(widget);
   return TRUE; 
}


void helpDialogCBK(GtkWidget */*widget*/, ezgl::application *application){
   // casting gpoint into ezgl::application to access widgets with it
   GtkDialog *dialog = GTK_DIALOG(application->get_object("helpDialog"));
   gtk_widget_show_all(GTK_WIDGET(dialog));
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_hide(GTK_WIDGET(dialog));
}

gboolean hideHelpWindow(GtkWidget */*widget*/, GdkEvent */*event*/, gpointer user_data) {
   GtkWidget *helpDialog = GTK_WIDGET(user_data);
   gtk_widget_hide(helpDialog);
   return TRUE;
}

void clearDirections(GtkWidget */*widget*/, ezgl::application *application){
   
   GtkWindow *directionOutputWindow = GTK_WINDOW(application->get_object("directionOutputWindow"));
   gtk_widget_hide(GTK_WIDGET(directionOutputWindow));

   GtkGrid* directionsGrid = GTK_GRID(application->get_object("directionOutput"));
   gtk_widget_hide(GTK_WIDGET(directionsGrid));

   if(clickMode){
      startClick = true;
      endClick = false;
   }
   
   directions = {};
   if(startIntersection!=nullptr){
      startIntersection->starting = false;
   }
   if(endIntersection!=nullptr){
      endIntersection->ending = false;
   }
   if(tripMessage){
      application->update_message("");
      tripMessage = false;
   }
   startIntersection = nullptr;
   endIntersection = nullptr;
   application->refresh_drawing();
}

void prevDirectionCBK(GtkWidget */*widget*/, ezgl::application *application){
   // Check if you are not at the start of the stack, if in bounds, then print the earlier instruction
   if (directionInstructionNumber > 0) {
        directionInstructionNumber--;
        std::string prevPage = "instruction" + std::to_string(directionInstructionNumber);
        gtk_stack_set_visible_child_name(GTK_STACK(directionsStack), prevPage.c_str());
   }
}

void nextDirectionCBK(GtkWidget */*widget*/, ezgl::application *application){
   // Check if you are not at the end of the stack, if in bounds, then print the later instruction
   if (directionInstructionNumber < directionInstructionsVec.size() - 1) {
        directionInstructionNumber++;
        std::string nextPage = "instruction" + std::to_string(directionInstructionNumber);
        gtk_stack_set_visible_child_name(GTK_STACK(directionsStack), nextPage.c_str());
    }
}

void initial_setup(ezgl::application* application, bool /*newWindow*/){
   application->create_button("Dark Mode", 6, toggle_darkMode);
   application->create_button("Location Icons", 7, toggle_icons);
   std::vector<std::string> maps = {};
   maps.push_back("Map select");
   for(int mapNameIndex = 0 ; mapNameIndex < mapCodes.size() ; mapNameIndex++){
      std::string newName = mapCodes[mapNameIndex].substr(26,mapCodes[mapNameIndex].length()-26-12);
      maps.push_back(newName);
   }
   
   // Code for CSS loading from Piazza, Posted by Prof. Abed Yassine.
   GtkCssProvider* css = gtk_css_provider_new();
   gtk_css_provider_load_from_path(css, "libstreetmap/src/style.css", NULL); //Replace the filepath string with your .css filepath
   GdkDisplay *display = gdk_display_get_default();
   GdkScreen *screen = gdk_display_get_default_screen(display);
   gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(css);

   // access chatbot button and window objects
   GtkButton *openChatbotButton = GTK_BUTTON(application->get_object("openChatbotButton"));
   GtkWindow *chatbotWindow = GTK_WINDOW(application->get_object("chatbotWindow"));
   GtkButton *chatbotSendButton = GTK_BUTTON(application->get_object("chatbotSendButton"));
   GtkEntry *chatTextField = GTK_ENTRY(application->get_object("chatTextFieldEntry"));
   GtkTextView *chatTextResponse = GTK_TEXT_VIEW(application->get_object("chatTextResponse"));

   // hide chatbot window initially
   gtk_widget_hide(GTK_WIDGET(chatbotWindow));

   // Gtk Objects pointers for the widgets needed for searching two streets with autocomplete
   GtkEntry *firstStreetEntry = GTK_ENTRY(application->get_object("FirstStreetEntry")); // First street entry widget
   GtkEntry *secondStreetEntry = GTK_ENTRY(application->get_object("SecondStreetEntry")); // Second street entry widget

   // Gtk Objects pointers for the widgets needed for searching two streets with autocomplete
   GtkEntry *firstIntersectionEntry = GTK_ENTRY(application->get_object("firstIntersectionEntry"));  // First intersection entry widget
   GtkEntry *secondIntersectionEntry = GTK_ENTRY(application->get_object("secondIntersectionEntry")); //  // second intersection entry widget
   
   // GTK objects pointers stored in weatherLabels struct identified.
   weatherLabels.descriptionLabel = GTK_LABEL(application->get_object("descriptionLabel"));
   weatherLabels.temperatureLabel = GTK_LABEL(application->get_object("temperatureLabel"));
   weatherLabels.feelLikeLabel = GTK_LABEL(application->get_object("feelLikeLabel"));
   weatherLabels.humidityLabel = GTK_LABEL(application->get_object("humidityLabel"));
   weatherLabels.windSpeed = GTK_LABEL(application->get_object("windSpeedLabel"));
   weatherLabels.cloudiness = GTK_LABEL(application->get_object("cloudinessLabel"));

   listOne = GTK_LIST_STORE(application->get_object("liststore1"));
   listTwo = GTK_LIST_STORE(application->get_object("liststore2"));

   intersectionListOne = GTK_LIST_STORE(application->get_object("liststore3"));
   intersectionListTwo = GTK_LIST_STORE(application->get_object("liststore4"));
   
   //navigation buttons
   GtkButton *findStreetInterButton = GTK_BUTTON(application->get_object("FindIntersectionsButton")); // find button
   GtkButton *findDirectionsButton = GTK_BUTTON(application->get_object("findDirectionsButton")); // find button
   GtkButton *setStartIntersectionButton = GTK_BUTTON(application->get_object("setInter1"));
   GtkButton *setEndIntersectionButton = GTK_BUTTON(application->get_object("setInter2"));
   GtkButton *clearDirectionsButton = GTK_BUTTON(application->get_object("clearDirections"));
   GtkButton *clickModeSwitch = GTK_BUTTON(application->get_object("quickDirectionSwitch"));

   // Direction output widgets
   directionsStack = GTK_STACK(application->get_object("directionStack"));
   GtkButton* prevButton = GTK_BUTTON(application->get_object("prevDirectionButton"));
   GtkButton* nextButton = GTK_BUTTON(application->get_object("nextDirectionButton"));

   // Connect button signals to the callbacks.
   g_signal_connect(prevButton, "clicked", G_CALLBACK(prevDirectionCBK), NULL);
   g_signal_connect(nextButton, "clicked", G_CALLBACK(nextDirectionCBK), NULL);

   // Help button and diaglog widgets
   GtkDialog *helpDialog = GTK_DIALOG(application->get_object("helpDialog"));
   GtkButton *helpButton = GTK_BUTTON(application->get_object("helpbutton"));
   GtkButton *okayButton = GTK_BUTTON(application->get_object("okayButton"));

   for(int id = 0; id < streets.size(); id++){
      GtkTreeIter currData1;
      GtkTreeIter currData2;

      gtk_list_store_append(listOne, &currData1);
      gtk_list_store_append(listTwo, &currData2);
      gtk_list_store_set(listOne, &currData1, 0, streets[id].c_str(),-1);
      gtk_list_store_set(listTwo, &currData2, 0, streets[id].c_str(),-1);
   }

   for(int id = 0; id < intersections.size(); id++){
      GtkTreeIter currData1;
      GtkTreeIter currData2;

      gtk_list_store_append(intersectionListOne, &currData1);
      gtk_list_store_append(intersectionListTwo, &currData2);
      gtk_list_store_set(intersectionListOne, &currData1, 0, intersections[id].name.c_str(),-1);
      gtk_list_store_set(intersectionListTwo, &currData2, 0, intersections[id].name.c_str(),-1);
   }

   // Signal Calls:
   g_signal_connect(clearDirectionsButton,"clicked",G_CALLBACK(clearDirections),application);
   g_signal_connect(findDirectionsButton,"clicked",G_CALLBACK(getDirections),application);
   g_signal_connect(
   findStreetInterButton, // pointer to the UI widget
   "clicked", // "click the button" is the signal to lookout for.
   G_CALLBACK(findIntersectionsOfEntryStreets), // name of callback function (you write this function)
   application
   );

   g_signal_connect(setStartIntersectionButton,"clicked",G_CALLBACK(setStartIntersection),application);
   g_signal_connect(setEndIntersectionButton,"clicked",G_CALLBACK(setEndIntersection),application);

   g_signal_connect(
   openChatbotButton, // pointer to the UI widget
   "clicked", // "click the button" is the signal to lookout for.
   G_CALLBACK(openChatbotCBK), // name of callback function (you write this function)
   application
   );

   g_signal_connect(
   chatbotSendButton, // pointer to the UI widget
   "clicked", // "click the button" is the signal to lookout for.
   G_CALLBACK(displayChatbotQueryInfo), // name of callback function (you write this function)
   application
   );

   g_signal_connect(
   chatTextField, // pointer to the UI widget
   "activate", // "click the button" is the signal to lookout for.
   G_CALLBACK(displayChatbotQueryInfo), // name of callback function (you write this function)
   application
   );

   // Hide chatbot window if closed.
   g_signal_connect(
   chatbotWindow,          // pointer to the chatbot window widget
   "delete-event",         // signal to intercept when closing
   G_CALLBACK(hideChatWindow), // our callback function
   NULL                    // no user data in this case
   );

   g_signal_connect(
   okayButton,          // pointer to the okay button widget
   "clicked",         // signal to intercept when closing
   G_CALLBACK(hideHelpWindow), // our callback function
   helpDialog                    // no user data in this case
   );
   
   g_signal_connect(
   helpButton, // point to the UI widget (help button)
   "clicked", // click signal
   G_CALLBACK(helpDialogCBK), // our callback function
   application
   );

   //signal connect for click mode
   g_signal_connect(
   clickModeSwitch, // point to the UI widget (help button)
   "clicked", // click signal
   G_CALLBACK(changeClickMode), // our callback function
   application
   );


   application->create_combo_box_text("Map select", 7, map_select, maps);
   
   // Weather API CALL FIRST HERE FOR FIRST CITY
   // API CODE (private): d3e5d851b860df19b1a5f0d7439c3ac6
   updateWeatherCall(firstScope.center());
}

void draw_main_canvas (ezgl::renderer *g){

   //deal with map changes
   if(newMap){
      clearAll();
      closeMap();
      bool loaded = loadMap(mapCodes[mapNum]);
      if(loaded){
         loadDataStructures();
         //reload street drop down options for intersection search
         gtk_list_store_clear(listOne);
         gtk_list_store_clear(listTwo);
         for(int id = 0; id < streets.size(); id++){
            GtkTreeIter currData1;
            GtkTreeIter currData2;

            gtk_list_store_append(listOne, &currData1);
            gtk_list_store_append(listTwo, &currData2);
            gtk_list_store_set(listOne, &currData1, 0, streets[id].c_str(),-1);
            gtk_list_store_set(listTwo, &currData2, 0, streets[id].c_str(),-1);
         }
         //set new "spawn" location based on new data structure info
         double maxLat = getIntersectionPosition(0).latitude();
         double minLat = maxLat;
         double maxLon = getIntersectionPosition(0).longitude();
         double minLon = maxLon;
         for(int id = 0; id < getNumIntersections(); id++){
            LatLon interPos = getIntersectionPosition(id);

            maxLat = std::max(maxLat, interPos.latitude());
            minLat = std::min(minLat, interPos.latitude());
            maxLon = std::max(maxLon, interPos.longitude());
            minLon = std::min(minLon, interPos.longitude());
         }
         ezgl::rectangle temp({lonToX(minLon), latToY(minLat)}, {lonToX(maxLon), latToY(maxLat)});
         firstScope = temp;
         g->set_visible_world(firstScope);

         // UPDATE WEATHER API INFO HERE AGAIN
         updateWeatherCall(g->get_visible_world().center());
      }
      newMap = false;
   }

   //teleporting to new intersection
   if(teleportForIntersection){
      double maxLat = getIntersectionPosition(highlightedIntersections[0]->intersectionId).latitude();
      double minLat = maxLat;
      double maxLon = getIntersectionPosition(highlightedIntersections[0]->intersectionId).longitude();
      double minLon = maxLon;
      for(int id = 0; id < highlightedIntersections.size(); id++){
         LatLon interPos = getIntersectionPosition(highlightedIntersections[id]->intersectionId);

         maxLat = std::max(maxLat, interPos.latitude());
         minLat = std::min(minLat, interPos.latitude());
         maxLon = std::max(maxLon, interPos.longitude());
         minLon = std::min(minLon, interPos.longitude());
      }
      ezgl::rectangle initial_world({lonToX(minLon)-100, latToY(minLat)-100}, {lonToX(maxLon)+100, latToY(maxLat)+100});
      g->set_visible_world(initial_world);
      std::cout<<g->get_visible_world().area()<<std::endl;
      teleportForIntersection = false;
   }

   //check zoom scale
   ezgl::rectangle zoomscale = g->get_visible_world();
   double scale = zoomscale.area();//used to represent how zoomed the user is
   if(scale > firstScope.area()){
      g->set_visible_world(firstScope);
   }

   //load in draw settings
   drawSettings settings;
   setDrawSettings(&settings,scale);

   //actually draw everything

      // Background for Dark Mode
      if(darkMode){g->set_color(50,71,92);}
      else{g->set_color(255,255,204);}
      ezgl::rectangle visible_world = g->get_visible_world();
      g->fill_rectangle(visible_world);

      //features
      if(settings.enableFeatures){
         for(size_t i = 0; i < features.size(); i++){
            FeatureData feature = features[i];

            if(feature.pointsOfFeature.size() > 2){
               if(settings.enableWaterFeature){
                  if(feature.type == BEACH || feature.type == LAKE || feature.type == RIVER ){
                  // #0C2B52
                  if(darkMode){g->set_color(12,43,82);}
                  else{g->set_color(179, 205, 224);}
                  g->fill_poly(feature.pointsOfFeature);
                  }
               }
               
               if(settings.enableGreeneryFeature){
                  if(feature.type == PARK || feature.type == GREENSPACE){
                     // #092211
                     if(darkMode){g->set_color(9,34,17);}
                     else{g->set_color(143, 190, 117);}
                     g->fill_poly(feature.pointsOfFeature);
                  }
               }

               if(settings.enableIsland){
                  if(feature.type == ISLAND){
                     if(darkMode){g->set_color(23,26,33);}
                     else{g->set_color(143, 190, 117);}
                     g->fill_poly(feature.pointsOfFeature);
                  }
               }

               if(settings.enableBuilding){
                  if(feature.type == BUILDING){
                     // # 33394A
                     if(darkMode){g->set_color(3, 35, 41);}
                     else{g->set_color(194,178,128);}

                     //check if building is entirely out of scope
                     ezgl::rectangle scope = g->get_visible_world();
                     int scopeStatus = 0;
                     for(int scopeIndex = 0 ; scopeIndex < feature.pointsOfFeature.size() ; scopeIndex++){
                        ezgl::point2d buildingPoint = feature.pointsOfFeature[scopeIndex];
                        if(buildingPoint.x < scope.right()+100 && buildingPoint.x > scope.left()-100 && buildingPoint.y > scope.bottom()-100 && buildingPoint.y < scope.top()+100){
                           scopeStatus++;
                        }
                     }
                     if(scopeStatus == feature.pointsOfFeature.size()){
                        g->fill_poly(feature.pointsOfFeature);             
                     }
                  }
               }                  

               if(settings.enableUnknown){
                  if(feature.type == UNKNOWN){
                     // # 5A5A5A
                     if(darkMode){g->set_color(90,90,90);}
                     else{g->set_color(90,90,90);}
                     g->fill_poly(feature.pointsOfFeature);
                  }
               }
            }
         }
      }

      double roadScale = g->get_visible_world().area();
      double highwayWidth, majorRoadWidth, minorRoadWidth;

      if (roadScale > 1e8) {
         highwayWidth = 6;
         majorRoadWidth = 2;
         minorRoadWidth = 2;
      } else if (roadScale > 1e7) {
         highwayWidth = 10;
         majorRoadWidth = 3;
         minorRoadWidth = 2;
      } else if (roadScale > 1e6) {
         highwayWidth = 12;
         majorRoadWidth = 5;
         minorRoadWidth = 4;
      } else if (roadScale > 1e5) {
         highwayWidth = 14;
         majorRoadWidth = 8;
         minorRoadWidth = 7;
      } else if (roadScale > 1e4) {
         highwayWidth = 25;
         majorRoadWidth = 25;
         minorRoadWidth = 10;
      } else if (roadScale > 1e3) {
         highwayWidth = 35;
         majorRoadWidth = 25;
         minorRoadWidth = 20;
      } else if (roadScale > 1e2) {
         highwayWidth = 50;
         majorRoadWidth = 45;
         minorRoadWidth = 35;
      } else {
         highwayWidth = 54;
         majorRoadWidth = 52;
         minorRoadWidth = 50;
      }
      //roads
      if(settings.enableRoads){
         for(const auto& roadSegment : roadSegmentsByType) {
            if (roadSegment.roadType == HIGHWAY && settings.enableHighways) {
               if(darkMode){g->set_color(150, 75, 10);}
                     else{g->set_color(224, 160, 80);}
               g->set_line_width(highwayWidth);
            } else if (roadSegment.roadType == MAJOR_ROAD && settings.enableMajorRoads) {
               if(darkMode){g->set_color(15, 15, 15);}
                     else{g->set_color(168, 149, 125);}
               g->set_line_width(majorRoadWidth);
            } else if (roadSegment.roadType == MINOR_ROAD && settings.enableMinorRoads) {
               // Color code: #60574B
               if(darkMode){g->set_color(40, 40, 40);}
                     else{g->set_color(194,181,155);}
               g->set_line_width(minorRoadWidth);
            }
            
            for (size_t i = 0; i < roadSegment.allPoints.size() - 1; ++i) {
               if((roadSegment.roadType==HIGHWAY&&settings.enableHighways)||
               (roadSegment.roadType==MAJOR_ROAD&&settings.enableMajorRoads)||
               (roadSegment.roadType==MINOR_ROAD&&settings.enableMinorRoads)){
                  g->draw_line(roadSegment.allPoints[i], roadSegment.allPoints[i + 1]);
               }
            }
            if (roadSegment.isOneWay) {
               if((roadSegment.roadType==HIGHWAY&&settings.enableHighways)||
               (roadSegment.roadType==MAJOR_ROAD&&settings.enableMajorRoads)||
               (roadSegment.roadType==MINOR_ROAD&&settings.enableMinorRoads)){
                  one_way_street_arrows(g, roadSegment);
               }
            }
         }
      }
      //drawing path
      for(int directionsDrawIndex = 0 ; directionsDrawIndex < directions.size() ; directionsDrawIndex++){
         if(darkMode){
            g->set_color(255,255,255);
         }
         else{
            g->set_color(0,0,0);
         }
         g->set_line_width(majorRoadWidth);
         ezgl::point2d startPoint = {lonToX(getIntersectionPosition(getStreetSegmentInfo(directions[directionsDrawIndex]).from).longitude()),latToY(getIntersectionPosition(getStreetSegmentInfo(directions[directionsDrawIndex]).from).latitude())};
         for(int segmentIndex = 0 ; segmentIndex < getStreetSegmentInfo(directions[directionsDrawIndex]).numCurvePoints ; segmentIndex++){
            ezgl::point2d endPoint = {lonToX(getStreetSegmentCurvePoint(directions[directionsDrawIndex],segmentIndex).longitude()),latToY(getStreetSegmentCurvePoint(directions[directionsDrawIndex],segmentIndex).latitude())};
            g->draw_line(startPoint,endPoint);
            startPoint = endPoint;
         }
         ezgl::point2d endPoint = {lonToX(getIntersectionPosition(getStreetSegmentInfo(directions[directionsDrawIndex]).to).longitude()),latToY(getIntersectionPosition(getStreetSegmentInfo(directions[directionsDrawIndex]).to).latitude())};
         g->draw_line(startPoint,endPoint);
      }
      
      //drawing feature labels
      if(settings.enableFeatureLabels){
         label_features(g);
         label_pois(g);
      }

      //drawing major roads and highways
      if(settings.enableMinorRoads){
         if (roadScale > 1e7) {
            g->set_font_size(8); 
         } else if (roadScale > 1e6) {
            g->set_font_size(10); 
         } else {
             g->set_font_size(12);
         }
         label_minor_roads(g);
      }

      if(settings.enableMajorRoads){

         if (roadScale > 1e7) {
            g->set_font_size(8); 
         } else if (roadScale > 1e6) {
            g->set_font_size(10); 
         } else {
             g->set_font_size(12);
         }
         label_major_and_highway(g);
      }

      //drawing icons
      if(iconEnable){
         for(size_t i = 0; i < features.size(); i++){
            FeatureData feature = features[i];
            if(feature.pointsOfFeature.size() > 2){
               if(settings.enableWaterIcon){
                  if(feature.type == BEACH || feature.type == LAKE || feature.type == RIVER ){
                     ezgl::rectangle scope = g->get_visible_world();
                     ezgl::point2d location = featureMidpoints[feature.name];
                     if(location.x < scope.right()+100 && location.x > scope.left()-100 && location.y > scope.bottom()-100 && location.y < scope.top()+100){
                        g->draw_surface(BEACHicon,location,settings.iconSize); 
                     }
                  }
               }
               
               if(settings.enableGreeneryIcon){
                  if(feature.type == PARK || feature.type == GREENSPACE){
                     ezgl::rectangle scope = g->get_visible_world();
                     ezgl::point2d location = featureMidpoints[feature.name];
                     if(location.x < scope.right()+100 && location.x > scope.left()-100 && location.y > scope.bottom()-100 && location.y < scope.top()+100){
                        g->draw_surface(PARKicon,location,settings.iconSize); 
                     }
                  }
               }

               if(settings.enableIslandIcon){
                  if(feature.type == ISLAND){
                     ezgl::rectangle scope = g->get_visible_world();
                     ezgl::point2d location = featureMidpoints[feature.name];
                     if(location.x < scope.right()+100 && location.x > scope.left()-100 && location.y > scope.bottom()-100 && location.y < scope.top()+100){
                        g->draw_surface(ISLANDicon,location,settings.iconSize); 
                     }
                  }
               }

               if(settings.enableBuildingIcon){
                  if(feature.type == BUILDING){

                     //check if building is entirely out of scope
                     ezgl::rectangle scope = g->get_visible_world();
                     int scopeStatus = 0;
                     for(int scopeIndex = 0 ; scopeIndex < feature.pointsOfFeature.size() ; scopeIndex++){
                        ezgl::point2d buildingPoint = feature.pointsOfFeature[scopeIndex];
                        if(buildingPoint.x < scope.right()+100 && buildingPoint.x > scope.left()-100 && buildingPoint.y > scope.bottom()-100 && buildingPoint.y < scope.top()+100){
                           scopeStatus++;
                        }
                     }
                     if(scopeStatus == feature.pointsOfFeature.size()){
                        ezgl::point2d location = featureMidpoints[feature.name];
                        g->draw_surface(BUILDINGicon,location,settings.iconSize);
                     }
                  }
               }                  
            }
         }
      }
      //drawing intersection info
      if(settings.enableIntersections){
         for(size_t i = 0; i < intersections.size(); i++){
            ezgl::point2d locationTarget = intersections[i].xy_loc;
            if(intersections[i].highlighted){
               if(!darkMode){
                  g->draw_surface(image,locationTarget,0.75);
               }
               else{
                  ezgl::point2d locationTarget = intersections[i].xy_loc;
                  g->draw_surface(imageLight,locationTarget,0.75);
               }
            }
            if(intersections[i].starting){
               g->draw_surface(startIcon,locationTarget,0.75);
            }
            if(intersections[i].ending){
               g->draw_surface(endIcon,locationTarget,0.75);
            }
         }
      }
}

void one_way_street_arrows(ezgl::renderer *g, const RoadSegmentByType &roadSegment) {
    
   // get midpoint from struct
   ezgl::point2d midPoint = roadSegment.midPoint;

   //get tangent from struct
   ezgl::point2d directionVector = roadSegment.tangentAtMidpoint;
   double arrowDirectionLength = std::sqrt(directionVector.x * directionVector.x + directionVector.y * directionVector.y);

   if (arrowDirectionLength < 1.0){
       arrowDirectionLength = 1.0;
   }
   //red arrows
   g->set_color(255, 0, 0);
   g->set_line_width(2);

   //keeps arrow a specific size
   double arrowSize = std::clamp(arrowDirectionLength * 0.2, 2.0, 4.0);

   //get unit direction, perpendicular vector and arrow multiplied by size
   ezgl::point2d unitDirection = {directionVector.x / arrowDirectionLength, directionVector.y / arrowDirectionLength};
   ezgl::point2d perpVector = {unitDirection.y, -unitDirection.x};
   ezgl::point2d arrowWithLength = {unitDirection.x * arrowSize, unitDirection.y * arrowSize};

   //draw back from midpoint of segment
   ezgl::point2d arrowStart = midPoint - arrowWithLength;
   
   //find half way of arrow stem to draw wings
   ezgl::point2d findHalfWay = {arrowWithLength.x * 0.5, arrowWithLength.y * 0.5};
   ezgl::point2d halfWay = midPoint - findHalfWay;

   //get wings on either side using vector math
   ezgl::point2d wings = {perpVector.x * arrowSize * 0.35, perpVector.y * arrowSize * 0.35};
   ezgl::point2d lWing = halfWay - wings;
   ezgl::point2d rWing = halfWay + wings;

   //draw arrows
   g->draw_line(arrowStart, midPoint);
   g->draw_line(midPoint, lWing);
   g->draw_line(midPoint, rWing);

}

void drawMap() {
   // Resize the intersections vector and initialize min/max variables for positions

   double maxLat = getIntersectionPosition(0).latitude();
   double minLat = maxLat;
   double maxLon = getIntersectionPosition(0).longitude();
   double minLon = maxLon;

   intersections.resize(getNumIntersections());

   // Pre-process intersection information to draw later and find map bounds
   for(int id = 0; id < getNumIntersections(); id++){
      LatLon interPos = getIntersectionPosition(id);

      maxLat = std::max(maxLat, interPos.latitude());
      minLat = std::min(minLat, interPos.latitude());
      maxLon = std::max(maxLon, interPos.longitude());
      minLon = std::min(minLon, interPos.longitude());

   }

   loadDataStructures();
   
   // Set up the ezgl graphics window and hand control to it, as shown in the 
   // ezgl example program. 
   ezgl::application::settings settings;
   settings.main_ui_resource = "libstreetmap/resources/main.ui";
   settings.window_identifier = "MainWindow";
   settings.canvas_identifier = "MainCanvas";

   // Create the EZGL Application with given settings details above.
   ezgl::application application(settings);
   ezgl::rectangle temp({lonToX(minLon), latToY(minLat)}, {lonToX(maxLon), latToY(maxLat)});
   firstScope = temp;
   application.add_canvas("MainCanvas", draw_main_canvas, firstScope);
   
   // Initialize the Curl Library and create a handler for weather
   initResult = curl_global_init(CURL_GLOBAL_ALL);
   weatherHandle = curl_easy_init();

   // create a handler for chatbot
   chatbotHandle = curl_easy_init();

   // Run the application until the user quits
   application.run(initial_setup, act_on_mouse_click, nullptr, nullptr);

   // After program exits, clean up the resources used.
   curl_easy_cleanup(weatherHandle);
   curl_easy_cleanup(chatbotHandle);

   weatherHandle = nullptr;
   chatbotHandle = nullptr;
   curl_global_cleanup();

   clearAll();
   // This function will be called by both the unit tests (ece297exercise) 
   // and your main() function in main/src/main.cpp.
   // The unit tests always call loadMap() before calling this function
   // and call closeMap() after this function returns.
}

void loadDataStructures(){
   //load in all the maps 
   const char* filePath = "/cad2/ece297s/public/maps/";
   loadMapCodes(filePath);

   //intersections
   double maxLat = getIntersectionPosition(0).latitude();
   double minLat = maxLat;
   double maxLon = getIntersectionPosition(0).longitude();
   double minLon = maxLon;

   intersections.resize(getNumIntersections());

   // Pre-process intersection information to draw later and find map bounds
   for(int id = 0; id < getNumIntersections(); id++){
      LatLon interPos = getIntersectionPosition(id);

      maxLat = std::max(maxLat, interPos.latitude());
      minLat = std::min(minLat, interPos.latitude());
      maxLon = std::max(maxLon, interPos.longitude());
      minLon = std::min(minLon, interPos.longitude());

   } 
   // Establish the latitude average for the mapping area for conversions
   latAverage = (minLat+maxLat)/2;

   for(int id = 0; id < getNumIntersections(); id++){
      LatLon interPos = getIntersectionPosition(id);

      intersections[id].xy_loc.x = lonToX(interPos.longitude());
      intersections[id].xy_loc.y = latToY(interPos.latitude());
      intersections[id].name = getIntersectionName(id);
      intersectionNames[intersections[id].name] = id;
      intersections[id].intersectionId = id;
   }

   //street segments
   streetSegments.resize(getNumStreetSegments());
   for(int id = 0; id < getNumStreetSegments(); id++){
      streetSegments[id] = getStreetSegmentInfo(id);
   }

   // streets
   streets.resize(getNumStreets());
   for(int id = 0; id < getNumStreets(); id++){
      streets[id] = getStreetName(id);
   }

   //features
   features.resize(getNumFeatures());
   for(int id = 0; id < getNumFeatures(); id++){
      features[id].name = getFeatureName(id);
      features[id].type = getFeatureType(id);
      
      int numPoints = getNumFeaturePoints(id);
      // Non zero area features will be stored
      if(numPoints > 2){
         for(int point = 0; point < numPoints; point++){
            LatLon currPoint = getFeaturePoint(id, point);
            // Convert LatLon point to ezgl::point2D for storing
            ezgl::point2d currPoint2D{lonToX(currPoint.longitude()), latToY(currPoint.latitude())};
            features[id].pointsOfFeature.push_back(currPoint2D);
         }
      }
   }

   //road segments
   roadSegmentProcessing();
   preprocess_feature_midpoints();
   preprocess_poi_midpoints();

}



void clearAll(){
   //maybe need to clear mapcodes here
   intersections.clear();
   for(int clearIndex = 0 ; clearIndex<highlightedIntersections.size();clearIndex++){
      highlightedIntersections[clearIndex]->highlighted = false;
   }
   highlightedIntersections.clear();
   streets.clear();
   streetSegments.clear();
   for(int i = 0 ; i < features.size();i++){
      features[i].pointsOfFeature.clear();
   }
   features.clear();
   for(int i = 0 ; i < roadSegmentsByType.size() ; i++){
      roadSegmentsByType[i].allPoints.clear();
   }

   directionInstructionsVec.clear();
   roadSegmentsByType.clear();

   featureMidpoints.clear();

   intersectionNames.clear(); 
}

void setDrawSettings(drawSettings* settings, double scale){
   if(scale>=pow(10,8)){
      settings->iconSize = 0.25;
      settings->enableRoads = true;
      settings->enableHighways = true;
      settings->enableMajorRoads = false;
      settings->enableMinorRoads = false;
      settings->enableFeatures = true;
      settings->enableWaterFeature = true;
      settings->enableGreeneryFeature = true;
      settings->enableIsland = true;
      settings->enableBuilding = false;
      settings->enableUnknown = true;
      settings->enableIntersections = true;
      settings->enableFeatureLabels = false;
     settings->enableBuildingIcon = false;
      settings->enableGreeneryIcon = true;
      settings->enableIslandIcon = true;
      settings->enableWaterIcon = true;
   }
   else if(scale<pow(10,8)&&scale>=pow(10,7)){
      settings->iconSize = 0.35;
      settings->enableRoads = true;
      settings->enableHighways = true;
      settings->enableMajorRoads = true;
      settings->enableMinorRoads = false;
      settings->enableFeatures = true;
      settings->enableWaterFeature = true;
      settings->enableGreeneryFeature = true;
      settings->enableIsland = true;
      settings->enableBuilding = false;
      settings->enableUnknown = true;
      settings->enableIntersections = true;
      settings->enableFeatureLabels = false;
      settings->enableBuildingIcon = false;
      settings->enableGreeneryIcon = true;
      settings->enableIslandIcon = true;
      settings->enableWaterIcon = true;
   }
   else if(scale<pow(10,7)&&scale>=pow(10,6)){
      settings->iconSize = 0.5;
      settings->enableRoads = true;
      settings->enableHighways = true;
      settings->enableMajorRoads = true;
      settings->enableMinorRoads = true;
      settings->enableFeatures = true;
      settings->enableWaterFeature = true;
      settings->enableGreeneryFeature = true;
      settings->enableIsland = true;
      settings->enableBuilding = false;
      settings->enableUnknown = true;
      settings->enableIntersections = true;
      settings->enableFeatureLabels = false;
      settings->enableBuildingIcon = false;
      settings->enableGreeneryIcon = true;
      settings->enableIslandIcon = true;
      settings->enableWaterIcon = true;
   }
   else if(scale<pow(10,6)&&scale>=pow(10,5)){
      settings->iconSize = 1;
      settings->enableRoads = true;
      settings->enableHighways = true;
      settings->enableMajorRoads = true;
      settings->enableMinorRoads = true;
      settings->enableFeatures = true;
      settings->enableWaterFeature = true;
      settings->enableGreeneryFeature = true;
      settings->enableIsland = true;
      settings->enableBuilding = true;
      settings->enableUnknown = true;
      settings->enableIntersections = true;
      settings->enableFeatureLabels = false;
      settings->enableBuildingIcon = true;
      settings->enableGreeneryIcon = true;
      settings->enableIslandIcon = true;
      settings->enableWaterIcon = true;
   }
   else if(scale<pow(10,5)){
      settings->iconSize = 1;
      settings->enableRoads = true;
      settings->enableHighways = true;
      settings->enableMajorRoads = true;
      settings->enableMinorRoads = true;
      settings->enableFeatures = true;
      settings->enableWaterFeature = true;
      settings->enableGreeneryFeature = true;
      settings->enableIsland = true;
      settings->enableBuilding = true;
      settings->enableUnknown = true;
      settings->enableIntersections = true;
      settings->enableFeatureLabels = true;
      settings->enableBuildingIcon = true;
      settings->enableGreeneryIcon = true;
      settings->enableIslandIcon = true;
      settings->enableWaterIcon = true;
   }
}

size_t webScrapeProcessor(void* scrapedContent, size_t contentSize, size_t dataSizePerN, void* contentDataStructPtr){
   size_t totalContentSize = contentSize*dataSizePerN;//get memory size of scraped content by multiplying the amount of scraped content by its size per amount
   std::string* totalContentString = static_cast<std::string*>(contentDataStructPtr);//create the string to hold the scraped content and set it to the location of the inputted pointer
   totalContentString->append(static_cast<char*>(scrapedContent),totalContentSize);//add the scraped content into the total content string
   return totalContentSize;
}

size_t fetchChatbotResponse(void* response, size_t responseSize, size_t dataSizePerN, void* contentDataStructPtr) {
    ((std::string*)contentDataStructPtr)->append((char*)response, responseSize * dataSizePerN);
    return responseSize * dataSizePerN;
}
void updateWeatherCall(ezgl::point2d center){
   std::string weatherData;
   std::string apiKey = "d3e5d851b860df19b1a5f0d7439c3ac6";//keep private
   std::string url =  "https://api.openweathermap.org/data/2.5/weather?lat=" + std::to_string(yToLat(center.y)) + "&lon=" + std::to_string(xToLon(center.x)) +"&appid=" + apiKey + "&units=metric";
   curl_easy_setopt(weatherHandle, CURLOPT_URL, url.c_str());
   curl_easy_setopt(weatherHandle, CURLOPT_WRITEFUNCTION, webScrapeProcessor);
   curl_easy_setopt(weatherHandle, CURLOPT_WRITEDATA, &weatherData);

   CURLcode success = curl_easy_perform(weatherHandle);
   std::stringstream ss(weatherData);
   boost::property_tree::ptree dataRoot;
   boost::property_tree::read_json(ss, dataRoot);

   // Parse to get the parameters from the root:
   auto firstWeather = dataRoot.get_child("weather").begin();
   std::string description = firstWeather->second.get<std::string>("description", "N/A");
   double temp = dataRoot.get<double>("main.temp");
   double feelsLike = dataRoot.get<double>("main.feels_like", 0.0);
   double humidity = dataRoot.get<double>("main.humidity", 0.0);
   double windSpeed = dataRoot.get<double>("wind.speed", 0.0);
   double cloudiness = dataRoot.get<double>("clouds.all", 0.0);

   // Conversions to string  to concatentate to display on label
   std::string strTemp = std::to_string(temp) + "°C";
   std::string strFeelsLike = std::to_string(feelsLike) + "°C";
   std::string strHumidity = std::to_string(humidity) + "%";
   std::string strWindSpeed = std::to_string(windSpeed) + "m/s";
   std::string strCloudiness = std::to_string(cloudiness) + "%";
   
   // Set text for labels
   gtk_label_set_text(weatherLabels.descriptionLabel, description.c_str());
   gtk_label_set_text(weatherLabels.temperatureLabel, strTemp.c_str());
   gtk_label_set_text(weatherLabels.feelLikeLabel, strFeelsLike.c_str());
   gtk_label_set_text(weatherLabels.humidityLabel, strHumidity.c_str());
   gtk_label_set_text(weatherLabels.windSpeed, strWindSpeed.c_str());
   gtk_label_set_text(weatherLabels.cloudiness, strCloudiness.c_str());

}

void displayChatbotQueryInfo(GtkWidget */*widget*/, ezgl::application *application){
   GtkEntry *chatTextField = GTK_ENTRY(application->get_object("chatTextFieldEntry"));
   GtkTextView *chatTextResponse = GTK_TEXT_VIEW(application->get_object("chatTextResponse"));
   std::string apiKey = "."; // keep private 
   std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=" + apiKey;
   
   // Preparing query to access Gemini API
   std::string query = gtk_entry_get_text(chatTextField);
   std::string payload = R"({"contents":[{"parts":[{"text":")" + query + R"("}]}]})";

   std::string apiResponse;
   
   struct curl_slist *headers = NULL;
   headers = curl_slist_append(headers, "Content-Type: application/json");
   headers = curl_slist_append(headers, "Accept: application/json");
   curl_easy_setopt(chatbotHandle, CURLOPT_URL, url.c_str());

   // Set the HTTP request type to POST
   curl_easy_setopt(chatbotHandle, CURLOPT_HTTPHEADER, headers);
   curl_easy_setopt(chatbotHandle, CURLOPT_POST, 1L);

   // Set the payload data
   curl_easy_setopt(chatbotHandle, CURLOPT_POSTFIELDS, payload.c_str());

   // Set the callback to handle the response
   curl_easy_setopt(chatbotHandle, CURLOPT_WRITEFUNCTION, fetchChatbotResponse);
   curl_easy_setopt(chatbotHandle, CURLOPT_WRITEDATA, &apiResponse);

   CURLcode res = curl_easy_perform(chatbotHandle);

   std::string chatbotReply;
   try{
      std::istringstream responseStream(apiResponse);
      boost::property_tree::ptree responseTree;
      boost::property_tree::read_json(responseStream, responseTree);
      auto candidates = responseTree.get_child("candidates");
      if (!candidates.empty()) {
         auto firstCandidate = candidates.begin()->second;
         auto content = firstCandidate.get_child("content");
         auto parts = content.get_child("parts");
         if (!parts.empty()) {
            chatbotReply = parts.begin()->second.get<std::string>("text");
         }
      }
   }catch (const std::exception &e){
      chatbotReply = "error parsing response";
      std::cerr <<"JSON parsing error: " <<e.what() <<std::endl;
   }

   GtkTextBuffer *responseBuffer = gtk_text_view_get_buffer(chatTextResponse);
   gtk_text_buffer_set_text(responseBuffer, chatbotReply.c_str(), -1); // Update the TextView with the API response

   curl_slist_free_all(headers);
}



void loadMapCodes(const char* directoryPath){
   DIR* directory = opendir(directoryPath);
   if(directory == nullptr){
      std::cout<<"failure to open direcroty"<<std::endl;
      return;
   }
   struct dirent* entry;
   while((entry = readdir(directory))!=nullptr){
      std::string fileName = entry->d_name;
      if(fileName.length()>12){
         if((fileName.substr(fileName.length()-12,12)==".streets.bin")){
            mapCodes.push_back(directoryPath + fileName);
         }
      }
   }
   closedir(directory);
   return;
}

float lonToX(float lon){
   // x = R * lon * cos(lat_avg)
   return kEarthRadiusInMeters*(lon*kDegreeToRadian) * std::cos(latAverage*kDegreeToRadian);
}

float xToLon(float x){
   // lon = x/(R * cos(lat_avg))
   return x/(kDegreeToRadian*kEarthRadiusInMeters*std::cos(latAverage*kDegreeToRadian));
}

float latToY(float lat){
   // y = R * lat
   return kEarthRadiusInMeters*(lat*kDegreeToRadian);
}

float yToLat(float y){
   // lat = y/R
   return y/(kEarthRadiusInMeters*kDegreeToRadian);
}

std::stringstream getDetailedDirections(std::vector<StreetSegmentIdx> path){
   //format: how long you will be on each street for, then new line for when turning occurs
   std::stringstream detailedDirections;
   double length = 0;
   for(int pathSegIndex = 0 ; pathSegIndex < path.size() ; pathSegIndex++){
      //next, find out how long they will be going in the direction for 
      double segmentLength = findStreetSegmentLength(path[pathSegIndex]);
      //check for turns
      if(pathSegIndex != 0 && getStreetSegmentInfo(path[pathSegIndex]).streetID != getStreetSegmentInfo(path[pathSegIndex-1]).streetID){
         detailedDirections << length << " meters. \n";
         length = segmentLength;
         //turn is true, now check turn angle to determine left or right
         std::string turnType;
         if(checkTurn(path[pathSegIndex-1],path[pathSegIndex]) == 1){
            turnType = "left turn";
         }
         else if(checkTurn(path[pathSegIndex-1],path[pathSegIndex]) == 2){
            turnType = "right turn";
         }
         else{
            turnType = "very slight turn";
         }
         detailedDirections << "Next, make a " << turnType << " onto " << getStreetName(getStreetSegmentInfo(path[pathSegIndex]).streetID) << " and head straight for ";
      }
      else if(pathSegIndex == 0){
         detailedDirections << "First, head straight along " <<  getStreetName(getStreetSegmentInfo(path[pathSegIndex]).streetID) << " for ";
         length += segmentLength;
      }
      else{
         length+=segmentLength;
      }
   }
   detailedDirections << length << " meters.\n";
   detailedDirections << "At this point, you should have reached your destination!\n";
   return detailedDirections;
}

int checkTurn(StreetSegmentIdx incoming, StreetSegmentIdx outgoing){
   //its assumed that they are connected
   //first, establish which ends they are connected at
   IntersectionIdx inFrom = getStreetSegmentInfo(incoming).from;
   IntersectionIdx inTo = getStreetSegmentInfo(incoming).to;
   IntersectionIdx outFrom = getStreetSegmentInfo(outgoing).from;
   IntersectionIdx outTo = getStreetSegmentInfo(outgoing).to;
   IntersectionIdx furthest,shared,closest;
   float incomingXdif,incomingYdif,outgoingXdif,outgoingYdif;
   if(inFrom == outFrom){
      shared = outFrom;
      furthest = outTo;
      closest = inTo;
   }
   else if(inFrom == outTo){
      furthest = outFrom;
      shared = outTo;
      closest = inTo;
   }
   else if(inTo == outFrom){
      furthest = outTo;
      shared = outFrom;
      closest = inFrom;
   }
   else if(inTo == outTo){
      furthest = outFrom;
      shared = outTo;
      closest = inFrom;
   }
   //now, use this info to get direction of incoming street

   double latAvg = getIntersectionPosition(shared).latitude();

   double srcFinishX = kEarthRadiusInMeters * getIntersectionPosition(shared).longitude() * std::cos(latAvg);
   double srcFinishY = kEarthRadiusInMeters * getIntersectionPosition(shared).latitude();
   double dstStartX = srcFinishX;
   double dstStartY = srcFinishY;
   double srcStartX = kEarthRadiusInMeters * getIntersectionPosition(closest).longitude() * std::cos(latAvg);
   double srcStartY = kEarthRadiusInMeters * getIntersectionPosition(closest).latitude();
   double dstFinishX = kEarthRadiusInMeters * getIntersectionPosition(furthest).longitude() * std::cos(latAvg);
   double dstFinishY = kEarthRadiusInMeters * getIntersectionPosition(furthest).latitude();

   // first, get the x values
   double srcXdifference = srcFinishX - srcStartX;
   double dstXdifference = dstFinishX - dstStartX;

   // then, get the Y values
   double srcYdifference = srcFinishY - srcStartY;
   double dstYdifference = dstFinishY - dstStartY;

   double crossProduct = srcXdifference*dstYdifference-srcYdifference*dstXdifference;
   if(crossProduct > 0){
      return 1;
   }
   else if(crossProduct < 0){
      return 2;
   }
   else{
      return 3;
   }
}
