/*
 * Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 */

#include "nvmsgconv.h"
#include <json-glib/json-glib.h>
#include <uuid.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>
#include <unordered_map>

using namespace std;


#define CONFIG_GROUP_SENSOR "sensor"
#define CONFIG_GROUP_PLACE "place"
#define CONFIG_GROUP_ANALYTICS "analytics"

#define CONFIG_KEY_COORDINATE "coordinate"
#define CONFIG_KEY_DESCRIPTION "description"
#define CONFIG_KEY_ENABLE  "enable"
#define CONFIG_KEY_ID "id"
#define CONFIG_KEY_LANE "lane"
#define CONFIG_KEY_LEVEL "level"
#define CONFIG_KEY_LOCATION "location"
#define CONFIG_KEY_NAME "name"
#define CONFIG_KEY_SOURCE "source"
#define CONFIG_KEY_TYPE "type"
#define CONFIG_KEY_VERSION "version"


#define CONFIG_KEY_PLACE_SUB_FIELD1 "place-sub-field1"
#define CONFIG_KEY_PLACE_SUB_FIELD2 "place-sub-field2"
#define CONFIG_KEY_PLACE_SUB_FIELD3 "place-sub-field3"

#define DEFAULT_CSV_FIELDS 10


#define CHECK_ERROR(error) \
    if (error) { \
      cout << "Error: " << error->message << endl; \
      goto done; \
    }


/**
 * Based on place type field of this object will have different meaning.
 * e.g. field1 will be 'id' and 'name' for spot and entrance respectively.
 */
struct NvDsPlaceSubObject {
  string field1;
  string field2;
  string field3;
};

struct NvDsSensorObject {
  string id;
  string type;
  string desc;
  gdouble location[3];
  gdouble coordinate[3];
};

struct NvDsPlaceObject {
  string id;
  string name;
  string type;
  gdouble location[3];
  gdouble coordinate[3];
  NvDsPlaceSubObject subObj;
};

struct NvDsAnalyticsObject {
  string id;
  string desc;
  string source;
  string version;
};

struct NvDsPayloadPriv {
  unordered_map<int, NvDsSensorObject> sensorObj;
  unordered_map<int, NvDsPlaceObject> placeObj;
  unordered_map<int, NvDsAnalyticsObject> analyticsObj;
};

static void
get_csv_tokens (const string &text, vector<string> &tokens)
{
  /* This is based on assumption that fields and their locations
   * are fixed in CSV file. This should be updated accordingly if
   * that is not the case.
   */
  gint count = 0;

  gchar **csv_tokens = g_strsplit (text.c_str(), ",", -1);
  gchar **temp = csv_tokens;
  gchar *token;

  while (*temp && count < DEFAULT_CSV_FIELDS) {
    token = *temp++;
    tokens.push_back (string(g_strstrip(token)));
    count++;
  }
  g_strfreev (csv_tokens);
}

static JsonObject*
generate_place_object (NvDsMsg2pCtx *ctx, NvDsEventMsgMeta *meta)
{
  NvDsPayloadPriv *privObj = NULL;
  NvDsPlaceObject *dsPlaceObj = NULL;
  JsonObject *placeObj;
  JsonObject *jobject;
  JsonObject *jobject2;

  privObj = (NvDsPayloadPriv *) ctx->privData;
  auto idMap = privObj->placeObj.find (meta->placeId);

  if (idMap != privObj->placeObj.end()) {
    dsPlaceObj = &idMap->second;
  } else {
    cout << "No entry for " CONFIG_GROUP_PLACE << meta->placeId
        << " in configuration file" << endl;
    return NULL;
  }

  /* place object
   * "place":
     {
       "id": "string",
       "name": "endeavor",
       “type”: “garage”,
       "location": {
         "lat": 30.333,
         "lon": -40.555,
         "alt": 100.00
       },
       "entrance/aisle": {
         "name": "walsh",
         "lane": "lane1",
         "level": "P2",
         "coordinate": {
           "x": 1.0,
           "y": 2.0,
           "z": 3.0
         }
       }
     }
   */

  placeObj = json_object_new ();
  json_object_set_string_member (placeObj, "id", dsPlaceObj->id.c_str());
  json_object_set_string_member (placeObj, "name", dsPlaceObj->name.c_str());
  json_object_set_string_member (placeObj, "type", dsPlaceObj->type.c_str());

  // location sub object
  jobject = json_object_new ();
  json_object_set_double_member (jobject, "lat", dsPlaceObj->location[0]);
  json_object_set_double_member (jobject, "lon", dsPlaceObj->location[1]);
  json_object_set_double_member (jobject, "alt", dsPlaceObj->location[2]);
  json_object_set_object_member (placeObj, "location", jobject);

  // parkingSpot / aisle /entrance sub object
  jobject = json_object_new ();

  switch (meta->type) {
    case NVDS_EVENT_MOVING:
    case NVDS_EVENT_STOPPED:
      json_object_set_string_member (jobject, "id", dsPlaceObj->subObj.field1.c_str());
      json_object_set_string_member (jobject, "name", dsPlaceObj->subObj.field2.c_str());
      json_object_set_string_member (jobject, "level", dsPlaceObj->subObj.field3.c_str());
      json_object_set_object_member (placeObj, "aisle", jobject);
      break;
    case NVDS_EVENT_EMPTY:
    case NVDS_EVENT_PARKED:
      json_object_set_string_member (jobject, "id", dsPlaceObj->subObj.field1.c_str());
      json_object_set_string_member (jobject, "type", dsPlaceObj->subObj.field2.c_str());
      json_object_set_string_member (jobject, "level", dsPlaceObj->subObj.field3.c_str());
      json_object_set_object_member (placeObj, "parkingSpot", jobject);
      break;
    case NVDS_EVENT_ENTRY:
    case NVDS_EVENT_EXIT:
      if (meta->objType == NVDS_OBJECT_TYPE_VEHICLE) {
        json_object_set_string_member (jobject, "id", dsPlaceObj->subObj.field1.c_str());
        json_object_set_string_member (jobject, "name", dsPlaceObj->subObj.field2.c_str());
        json_object_set_string_member (jobject, "level", dsPlaceObj->subObj.field3.c_str());
        json_object_set_object_member (placeObj, "aisle", jobject);
      } else {
        json_object_set_string_member (jobject, "name", dsPlaceObj->subObj.field1.c_str());
        json_object_set_string_member (jobject, "lane", dsPlaceObj->subObj.field2.c_str());
        json_object_set_string_member (jobject, "level", dsPlaceObj->subObj.field3.c_str());
        json_object_set_object_member (placeObj, "entrance", jobject);
      }
      break;
    default:
      cout << "Event type not implemented " << endl;
      break;
  }

  // coordinate sub sub object
  jobject2 = json_object_new ();
  json_object_set_double_member (jobject2, "x", dsPlaceObj->coordinate[0]);
  json_object_set_double_member (jobject2, "y", dsPlaceObj->coordinate[1]);
  json_object_set_double_member (jobject2, "z", dsPlaceObj->coordinate[2]);
  json_object_set_object_member (jobject, "coordinate", jobject2);

  return placeObj;
}

static JsonObject*
generate_sensor_object (NvDsMsg2pCtx *ctx, NvDsEventMsgMeta *meta)
{
  NvDsPayloadPriv *privObj = NULL;
  NvDsSensorObject *dsSensorObj = NULL;
  JsonObject *sensorObj;
  JsonObject *jobject;

  privObj = (NvDsPayloadPriv *) ctx->privData;
  auto idMap = privObj->sensorObj.find (meta->sensorId);

  if (idMap != privObj->sensorObj.end()) {
    dsSensorObj = &idMap->second;
  } else {
    cout << "No entry for " CONFIG_GROUP_SENSOR << meta->sensorId
         << " in configuration file" << endl;
    return NULL;
  }

  /* sensor object
   * "sensor": {
       "id": "string",
       "type": "Camera/Puck",
       "location": {
         "lat": 45.99,
         "lon": 35.54,
         "alt": 79.03
       },
       "coordinate": {
         "x": 5.2,
         "y": 10.1,
         "z": 11.2
       },
       "description": "Entrance of Endeavor Garage Right Lane"
     }
   */

  // sensor object
  sensorObj = json_object_new ();
  json_object_set_string_member (sensorObj, "id", dsSensorObj->id.c_str());
  json_object_set_string_member (sensorObj, "type", dsSensorObj->type.c_str());
  json_object_set_string_member (sensorObj, "description", dsSensorObj->desc.c_str());

  // location sub object
  jobject = json_object_new ();
  json_object_set_double_member (jobject, "lat", dsSensorObj->location[0]);
  json_object_set_double_member (jobject, "lon", dsSensorObj->location[1]);
  json_object_set_double_member (jobject, "alt", dsSensorObj->location[2]);
  json_object_set_object_member (sensorObj, "location", jobject);

  // coordinate sub object
  jobject = json_object_new ();
  json_object_set_double_member (jobject, "x", dsSensorObj->coordinate[0]);
  json_object_set_double_member (jobject, "y", dsSensorObj->coordinate[1]);
  json_object_set_double_member (jobject, "z", dsSensorObj->coordinate[2]);
  json_object_set_object_member (sensorObj, "coordinate", jobject);

  return sensorObj;
}

static JsonObject*
generate_analytics_module_object (NvDsMsg2pCtx *ctx, NvDsEventMsgMeta *meta)
{
  NvDsPayloadPriv *privObj = NULL;
  NvDsAnalyticsObject *dsObj = NULL;
  JsonObject *analyticsObj;

  privObj = (NvDsPayloadPriv *) ctx->privData;

  auto idMap = privObj->analyticsObj.find (meta->moduleId);

  if (idMap != privObj->analyticsObj.end()) {
    dsObj = &idMap->second;
  } else {
    cout << "No entry for " CONFIG_GROUP_ANALYTICS << meta->moduleId
        << " in configuration file" << endl;
    return NULL;
  }

  /* analytics object
   * "analyticsModule": {
       "id": "string",
       "description": "Vehicle Detection and License Plate Recognition",
       "confidence": 97.79,
       "source": "OpenALR",
       "version": "string"
     }
   */

  // analytics object
  analyticsObj = json_object_new ();
  json_object_set_string_member (analyticsObj, "id", dsObj->id.c_str());
  json_object_set_string_member (analyticsObj, "description", dsObj->desc.c_str());
  json_object_set_string_member (analyticsObj, "source", dsObj->source.c_str());
  json_object_set_string_member (analyticsObj, "version", dsObj->version.c_str());
  json_object_set_double_member (analyticsObj, "confidence", meta->confidence);

  return analyticsObj;
}

static JsonObject*
generate_event_object (NvDsMsg2pCtx *ctx, NvDsEventMsgMeta *meta)
{
  JsonObject *eventObj;
  uuid_t uuid;
  gchar uuidStr[37];

  /*
   * "event": {
       "id": "event-id",
       "type": "entry / exit"
     }
   */

  uuid_generate_random (uuid);
  uuid_unparse_lower(uuid, uuidStr);

  eventObj = json_object_new ();
  json_object_set_string_member (eventObj, "id", uuidStr);

  switch (meta->type) {
    case NVDS_EVENT_ENTRY:
      json_object_set_string_member (eventObj, "type", "entry");
      break;
    case NVDS_EVENT_EXIT:
      json_object_set_string_member (eventObj, "type", "exit");
      break;
    case NVDS_EVENT_MOVING:
      json_object_set_string_member (eventObj, "type", "moving");
      break;
    case NVDS_EVENT_STOPPED:
      json_object_set_string_member (eventObj, "type", "stopped");
      break;
    case NVDS_EVENT_PARKED:
      json_object_set_string_member (eventObj, "type", "parked");
      break;
    case NVDS_EVENT_EMPTY:
      json_object_set_string_member (eventObj, "type", "empty");
      break;
    case NVDS_EVENT_RESET:
      json_object_set_string_member (eventObj, "type", "reset");
      break;
    default:
      cout << "Unknown event type " << endl;
      break;
  }

  return eventObj;
}

static JsonObject*
generate_object_object (NvDsMsg2pCtx *ctx, NvDsEventMsgMeta *meta)
{
  JsonObject *objectObj;
  JsonObject *jobject;
  guint i;
  gchar tracking_id[64];

  // object object
  objectObj = json_object_new ();
  if (snprintf (tracking_id, sizeof(tracking_id), "%d", meta->trackingId)
      >= (int) sizeof(tracking_id))
    g_warning("Not enough space to copy trackingId");
  json_object_set_string_member (objectObj, "id", tracking_id);
  json_object_set_double_member (objectObj, "speed", 0);
  json_object_set_double_member (objectObj, "direction", 0);
  json_object_set_double_member (objectObj, "orientation", 0);

  switch (meta->objType) {
    case NVDS_OBJECT_TYPE_VEHICLE:
      // vehicle sub object
      jobject = json_object_new ();

      if (meta->extMsgSize) {
        NvDsVehicleObject *dsObj = (NvDsVehicleObject *) meta->extMsg;
        if (dsObj) {
          json_object_set_string_member (jobject, "type", dsObj->type);
          json_object_set_string_member (jobject, "make", dsObj->make);
          json_object_set_string_member (jobject, "model", dsObj->model);
          json_object_set_string_member (jobject, "color", dsObj->color);
          json_object_set_string_member (jobject, "licenseState", dsObj->region);
          json_object_set_string_member (jobject, "license", dsObj->license);
          json_object_set_double_member (jobject, "confidence", meta->confidence);
        }
      } else {
        // No vehicle object in meta data. Attach empty vehicle sub object.
        json_object_set_string_member (jobject, "type", "");
        json_object_set_string_member (jobject, "make", "");
        json_object_set_string_member (jobject, "model", "");
        json_object_set_string_member (jobject, "color", "");
        json_object_set_string_member (jobject, "licenseState", "");
        json_object_set_string_member (jobject, "license", "");
        json_object_set_double_member (jobject, "confidence", 1.0);
      }
      json_object_set_object_member (objectObj, "vehicle", jobject);
      break;
    case NVDS_OBJECT_TYPE_PERSON:
      // person sub object
      jobject = json_object_new ();

      if (meta->extMsgSize) {
        NvDsPersonObject *dsObj = (NvDsPersonObject *) meta->extMsg;
        if (dsObj) {
          json_object_set_int_member (jobject, "age", dsObj->age);
          json_object_set_string_member (jobject, "gender", dsObj->gender);
          json_object_set_string_member (jobject, "hair", dsObj->hair);
          json_object_set_string_member (jobject, "cap", dsObj->cap);
          json_object_set_string_member (jobject, "apparel", dsObj->apparel);
          json_object_set_double_member (jobject, "confidence", meta->confidence);
        }
      } else {
        // No person object in meta data. Attach empty person sub object.
        json_object_set_int_member (jobject, "age", 0);
        json_object_set_string_member (jobject, "gender", "");
        json_object_set_string_member (jobject, "hair", "");
        json_object_set_string_member (jobject, "cap", "");
        json_object_set_string_member (jobject, "apparel", "");
        json_object_set_double_member (jobject, "confidence", 1.0);
      }
      json_object_set_object_member (objectObj, "person", jobject);
      break;
    case NVDS_OBJECT_TYPE_FACE:
      // face sub object
      jobject = json_object_new ();

      if (meta->extMsgSize) {
        NvDsFaceObject *dsObj = (NvDsFaceObject *) meta->extMsg;
        if (dsObj) {
          json_object_set_int_member (jobject, "age", dsObj->age);
          json_object_set_string_member (jobject, "gender", dsObj->gender);
          json_object_set_string_member (jobject, "hair", dsObj->hair);
          json_object_set_string_member (jobject, "cap", dsObj->cap);
          json_object_set_string_member (jobject, "glasses", dsObj->glasses);
          json_object_set_string_member (jobject, "facialhair", dsObj->facialhair);
          json_object_set_string_member (jobject, "name", dsObj->name);
          json_object_set_string_member (jobject, "eyecolor", dsObj->eyecolor);
          json_object_set_double_member (jobject, "confidence", meta->confidence);
        }
      } else {
        // No face object in meta data. Attach empty face sub object.
        json_object_set_int_member (jobject, "age", 0);
        json_object_set_string_member (jobject, "gender", "");
        json_object_set_string_member (jobject, "hair", "");
        json_object_set_string_member (jobject, "cap", "");
        json_object_set_string_member (jobject, "glasses", "");
        json_object_set_string_member (jobject, "facialhair", "");
        json_object_set_string_member (jobject, "name", "");
        json_object_set_string_member (jobject, "eyecolor", "");
        json_object_set_double_member (jobject, "confidence", 1.0);
      }
      json_object_set_object_member (objectObj, "face", jobject);
      break;
    case NVDS_OBJECT_TYPE_UNKNOWN:
      if(!meta->objectId) {
        break;
      }
      /** No information to add; object type unknown within NvDsEventMsgMeta */
      jobject = json_object_new ();
      json_object_set_object_member (objectObj, meta->objectId, jobject);
      break;
    default:
      cout << "Object type not implemented" << endl;
  }

  // bbox sub object
  jobject = json_object_new ();
  json_object_set_int_member (jobject, "topleftx", meta->bbox.left);
  json_object_set_int_member (jobject, "toplefty", meta->bbox.top);
  json_object_set_int_member (jobject, "bottomrightx", meta->bbox.left + meta->bbox.width);
  json_object_set_int_member (jobject, "bottomrighty", meta->bbox.top + meta->bbox.height);
  json_object_set_object_member (objectObj, "bbox", jobject);

  // signature sub array
  if (meta->objSignature.size) {
    JsonArray *jArray = json_array_sized_new (meta->objSignature.size);

    for (i = 0; i < meta->objSignature.size; i++) {
      json_array_add_double_element (jArray, meta->objSignature.signature[i]);
    }
    json_object_set_array_member (objectObj, "signature", jArray);
  }

  // location sub object
  jobject = json_object_new ();
  json_object_set_double_member (jobject, "lat", meta->location.lat);
  json_object_set_double_member (jobject, "lon", meta->location.lon);
  json_object_set_double_member (jobject, "alt", meta->location.alt);
  json_object_set_object_member (objectObj, "location", jobject);

  // coordinate sub object
  jobject = json_object_new ();
  json_object_set_double_member (jobject, "x", meta->coordinate.x);
  json_object_set_double_member (jobject, "y", meta->coordinate.y);
  json_object_set_double_member (jobject, "z", meta->coordinate.z);
  json_object_set_object_member (objectObj, "coordinate", jobject);

  return objectObj;
}

static gchar*
generate_schema_message (NvDsMsg2pCtx *ctx, NvDsEventMsgMeta *meta)
{
  JsonNode *rootNode;
  JsonObject *rootObj;
  JsonObject *placeObj;
  JsonObject *sensorObj;
  JsonObject *analyticsObj;
  JsonObject *eventObj;
  JsonObject *objectObj;
  gchar *message;

  uuid_t msgId;
  gchar msgIdStr[37];

  uuid_generate_random (msgId);
  uuid_unparse_lower(msgId, msgIdStr);

  // place object
  placeObj = generate_place_object (ctx, meta);

  // sensor object
  sensorObj = generate_sensor_object (ctx, meta);

  // analytics object
  analyticsObj = generate_analytics_module_object (ctx, meta);

  // object object
  objectObj = generate_object_object (ctx, meta);

  // event object
  eventObj = generate_event_object (ctx, meta);

  // root object
  rootObj = json_object_new ();
  json_object_set_string_member (rootObj, "messageid", msgIdStr);
  json_object_set_string_member (rootObj, "mdsversion", "1.0");
  json_object_set_string_member (rootObj, "@timestamp", meta->ts);
  json_object_set_object_member (rootObj, "place", placeObj);
  json_object_set_object_member (rootObj, "sensor", sensorObj);
  json_object_set_object_member (rootObj, "analyticsModule", analyticsObj);
  json_object_set_object_member (rootObj, "object", objectObj);
  json_object_set_object_member (rootObj, "event", eventObj);

  if (meta->videoPath)
    json_object_set_string_member (rootObj, "videoPath", meta->videoPath);
  else
    json_object_set_string_member (rootObj, "videoPath", "");

  rootNode = json_node_new (JSON_NODE_OBJECT);
  json_node_set_object (rootNode, rootObj);

  message = json_to_string (rootNode, TRUE);
  json_node_free (rootNode);
  json_object_unref (rootObj);

  return message;
}

static const gchar*
object_enum_to_str (NvDsObjectType type, gchar* objectId)
{
  switch (type) {
    case NVDS_OBJECT_TYPE_VEHICLE:
      return "Vehicle";
    case NVDS_OBJECT_TYPE_FACE:
      return "Face";
    case NVDS_OBJECT_TYPE_PERSON:
      return "Person";
    case NVDS_OBJECT_TYPE_BAG:
      return "Bag";
    case NVDS_OBJECT_TYPE_BICYCLE:
      return "Bicycle";
    case NVDS_OBJECT_TYPE_ROADSIGN:
      return "RoadSign";
    case NVDS_OBJECT_TYPE_CUSTOM:
      return "Custom";
    case NVDS_OBJECT_TYPE_UNKNOWN:
      return objectId ? objectId : "Unknown";
    default:
      return "Unknown";
  }
}

static const gchar*
to_str (gchar* cstr)
{
    return reinterpret_cast<const gchar*>(cstr) ? cstr : "";
}

static const gchar *
sensor_id_to_str (NvDsMsg2pCtx *ctx, gint sensorId)
{
  NvDsPayloadPriv *privObj = NULL;
  NvDsSensorObject *dsObj = NULL;

  g_return_val_if_fail (ctx, NULL);
  g_return_val_if_fail (ctx->privData, NULL);

  privObj = (NvDsPayloadPriv *) ctx->privData;

  auto idMap = privObj->sensorObj.find (sensorId);
  if (idMap != privObj->sensorObj.end()) {
    dsObj = &idMap->second;
    return dsObj->id.c_str();
  } else {
    cout << "No entry for " CONFIG_GROUP_SENSOR << sensorId
        << " in configuration file" << endl;
    return NULL;
  }
}

static gchar*
generate_deepstream_message_minimal (NvDsMsg2pCtx *ctx, NvDsEvent *events, guint size)
{
  /*
  The JSON structure of the frame
  {
   "version": "4.0",
   "id": "frame-id",
   "@timestamp": "2018-04-11T04:59:59.828Z",
   "sensorId": "sensor-id",
   "objects": [
      ".......object-1 attributes...........",
      ".......object-2 attributes...........",
      ".......object-3 attributes..........."
    ]
  }
  */

  /*
  An example object with Vehicle object-type
  {
    "version": "4.0",
    "id": "frame-id",
    "@timestamp": "2018-04-11T04:59:59.828Z",
    "sensorId": "sensor-id",
    "objects": [
        "957|1834|150|1918|215|Vehicle|#|sedan|Bugatti|M|blue|CA 444|California|0.8",
        "..........."
    ]
  }
   */

  JsonNode *rootNode;
  JsonObject *jobject;
  JsonArray *jArray;
  guint i;
  stringstream ss;
  gchar *message = NULL;

  jArray = json_array_new ();
  for (i = 0; i < size; i++) {
    ss.str("");
    ss.clear();

    NvDsEventMsgMeta *meta = events[i].metadata;
    ss << meta->trackingId << "|" << meta->bbox.left << "|" << meta->bbox.top
        << "|" << meta->bbox.left + meta->bbox.width << "|" << meta->bbox.top + meta->bbox.height
        << "|" << object_enum_to_str (meta->objType, meta->objectId);

    if (meta->extMsg && meta->extMsgSize) {
      // Attach secondary inference attributes.
      switch (meta->objType) {
        case NVDS_OBJECT_TYPE_VEHICLE: {
          NvDsVehicleObject *dsObj = (NvDsVehicleObject *) meta->extMsg;
          if (dsObj) {
            ss << "|#|" << to_str(dsObj->type) << "|" << to_str(dsObj->make) << "|"
               << to_str(dsObj->model) << "|" << to_str(dsObj->color) << "|" << to_str(dsObj->license)
               << "|" << to_str(dsObj->region) << "|" << meta->confidence;
          }
        }
          break;
        case NVDS_OBJECT_TYPE_PERSON: {
          NvDsPersonObject *dsObj = (NvDsPersonObject *) meta->extMsg;
          if (dsObj) {
            ss << "|#|" << to_str(dsObj->gender) << "|" << dsObj->age << "|"
                << to_str(dsObj->hair) << "|" << to_str(dsObj->cap) << "|" << to_str(dsObj->apparel)
                << "|" << meta->confidence;
          }
        }
          break;
        case NVDS_OBJECT_TYPE_FACE: {
          NvDsFaceObject *dsObj = (NvDsFaceObject *) meta->extMsg;
          if (dsObj) {
            ss << "|#|" << to_str(dsObj->gender) << "|" << dsObj->age << "|"
                << to_str(dsObj->hair) << "|" << to_str(dsObj->cap) << "|" << to_str(dsObj->glasses)
                << "|" << to_str(dsObj->facialhair) << "|" << to_str(dsObj->name) << "|"
                << "|" << to_str(dsObj->eyecolor) << "|" << meta->confidence;
          }
        }
          break;
        default:
          cout << "Object type (" << meta->objType << ") not implemented" << endl;
          break;
      }
    }

    json_array_add_string_element (jArray, ss.str().c_str());
  }

  // It is assumed that all events / objects are associated with same frame.
  // Therefore ts / sensorId / frameId of first object can be used.

  jobject = json_object_new ();
  json_object_set_string_member (jobject, "version", "4.0");
  json_object_set_int_member (jobject, "id", events[0].metadata->frameId);
  json_object_set_string_member (jobject, "@timestamp", events[0].metadata->ts);
  if (events[0].metadata->sensorStr) {
    json_object_set_string_member (jobject, "sensorId", events[0].metadata->sensorStr);
  } else if (ctx->privData) {
    json_object_set_string_member (jobject, "sensorId",
        to_str((gchar *) sensor_id_to_str (ctx, events[0].metadata->sensorId)));
  } else {
    json_object_set_string_member (jobject, "sensorId", "0");
  }

  json_object_set_array_member (jobject, "objects", jArray);

  rootNode = json_node_new (JSON_NODE_OBJECT);
  json_node_set_object (rootNode, jobject);

  message = json_to_string (rootNode, TRUE);
  json_node_free (rootNode);
  json_object_unref (jobject);

  return message;
}

static bool
nvds_msg2p_parse_sensor (NvDsMsg2pCtx *ctx, GKeyFile *key_file, gchar *group)
{
  bool ret = false;
  bool isEnabled = false;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;
  NvDsPayloadPriv *privObj = NULL;
  NvDsSensorObject sensorObj;
  gint sensorId;
  gchar *keyVal;


  if (sscanf (group, CONFIG_GROUP_SENSOR "%u", &sensorId) < 1) {
    cout << "Wrong sensor group name " << group << endl;
    return ret;
  }

  privObj = (NvDsPayloadPriv *) ctx->privData;

  auto idMap = privObj->sensorObj.find (sensorId);
  if (idMap != privObj->sensorObj.end()) {
    cout << "Duplicate entries for " << group << endl;
    return ret;
  }

  isEnabled = g_key_file_get_boolean (key_file, group, CONFIG_KEY_ENABLE,
                                      &error);
  if (!isEnabled) {
    // Not enabled, skip the parsing of keys.
    ret = true;
    goto done;
  } else {
    g_key_file_remove_key (key_file, group, CONFIG_KEY_ENABLE,
                           &error);
    CHECK_ERROR (error);
  }

  keys = g_key_file_get_keys (key_file, group, NULL, &error);
  CHECK_ERROR (error);

  for (key = keys; *key; key++) {
    keyVal = NULL;
    if (!g_strcmp0 (*key, CONFIG_KEY_ID)) {
      keyVal = g_key_file_get_string (key_file, group,
                                      CONFIG_KEY_ID, &error);
      sensorObj.id = keyVal;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_TYPE)) {
      keyVal = g_key_file_get_string (key_file, group,
                                      CONFIG_KEY_TYPE, &error);
      sensorObj.type = keyVal;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_DESCRIPTION)) {
      keyVal = g_key_file_get_string (key_file, group,
                                      CONFIG_KEY_DESCRIPTION, &error);
      sensorObj.desc = keyVal;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_LOCATION)) {
      gsize length;
      gdouble *location = g_key_file_get_double_list (key_file, group,
                                                      CONFIG_KEY_LOCATION,
                                                      &length, &error);
      if (length != 3) {
        cout << "Wrong values provided, it should be like lat;lon;alt" << endl;
        g_free (location);
        goto done;
      }

      memcpy (sensorObj.location, location, length * sizeof (gdouble));
      g_free (location);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_COORDINATE)) {
      gsize length;
      gdouble *coordinate = g_key_file_get_double_list (key_file, group,
                                                      CONFIG_KEY_COORDINATE,
                                                      &length, &error);
      if (length != 3) {
        cout << "Wrong values provided, it should be like x;y;z" << endl;
        g_free (coordinate);
        goto done;
      }

      memcpy (sensorObj.coordinate, coordinate, length * sizeof (gdouble));
      g_free (coordinate);
      CHECK_ERROR (error);
    } else {
      cout << "Unknown key " << *key << " for group [" << group <<"]\n";
    }

    if (keyVal)
      g_free (keyVal);
  }

  privObj->sensorObj.insert (make_pair (sensorId, sensorObj));

  ret = true;

done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }

  return ret;
}

static bool
nvds_msg2p_parse_place (NvDsMsg2pCtx *ctx, GKeyFile *key_file, gchar *group)
{
  bool ret = false;
  bool isEnabled = false;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;
  NvDsPayloadPriv *privObj = NULL;
  NvDsPlaceObject placeObj;
  gint placeId;
  gchar *keyVal;

  if (sscanf (group, CONFIG_GROUP_PLACE "%u", &placeId) < 1) {
    cout << "Wrong place group name " << group << endl;
    return ret;
  }

  privObj = (NvDsPayloadPriv *) ctx->privData;

  auto idMap = privObj->placeObj.find (placeId);
  if (idMap != privObj->placeObj.end()) {
    cout << "Duplicate entries for " << group << endl;
    return ret;
  }

  isEnabled = g_key_file_get_boolean (key_file, group, CONFIG_KEY_ENABLE,
                                      &error);
  if (!isEnabled) {
    // Not enabled, skip the parsing of keys.
    ret = true;
    goto done;
  } else {
    g_key_file_remove_key (key_file, group, CONFIG_KEY_ENABLE,
                           &error);
    CHECK_ERROR (error);
  }

  keys = g_key_file_get_keys (key_file, group, NULL, &error);
  CHECK_ERROR (error);

  for (key = keys; *key; key++) {
    if (!g_strcmp0 (*key, CONFIG_KEY_ID)) {
      keyVal = g_key_file_get_string (key_file, group,
                                      CONFIG_KEY_ID, &error);
      placeObj.id = keyVal;
      g_free (keyVal);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_TYPE)) {
      keyVal = g_key_file_get_string (key_file, group,
                                      CONFIG_KEY_TYPE, &error);
      placeObj.type = keyVal;
      g_free (keyVal);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_NAME)) {
      keyVal = g_key_file_get_string (key_file, group,
                                      CONFIG_KEY_NAME, &error);
      placeObj.name = keyVal;
      g_free (keyVal);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_LOCATION)) {
      gsize length;
      gdouble *location = g_key_file_get_double_list (key_file, group,
                                                      CONFIG_KEY_LOCATION,
                                                      &length, &error);
      if (length != 3) {
        cout << "Wrong values provided, it should be like lat;lon;alt" << endl;
        g_free (location);
        goto done;
      }

      memcpy (placeObj.location, location, length * sizeof (gdouble));
      g_free (location);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_COORDINATE)) {
      gsize length;
      gdouble *coordinate = g_key_file_get_double_list (key_file, group,
                                                      CONFIG_KEY_COORDINATE,
                                                      &length, &error);
      if (length != 3) {
        cout << "Wrong values provided, it should be like x;y;z" << endl;
        g_free (coordinate);
        goto done;
      }

      memcpy (placeObj.coordinate, coordinate, length * sizeof (gdouble));
      g_free (coordinate);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_PLACE_SUB_FIELD1)) {
      keyVal = g_key_file_get_string (key_file, group,
                                        CONFIG_KEY_PLACE_SUB_FIELD1, &error);
      placeObj.subObj.field1 = keyVal;
      g_free (keyVal);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_PLACE_SUB_FIELD2)) {
      keyVal = g_key_file_get_string (key_file, group,
                                        CONFIG_KEY_PLACE_SUB_FIELD2, &error);
      placeObj.subObj.field2 = keyVal;
      g_free (keyVal);
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_PLACE_SUB_FIELD3)) {
      keyVal = g_key_file_get_string (key_file, group,
                                        CONFIG_KEY_PLACE_SUB_FIELD3, &error);
      placeObj.subObj.field3 = keyVal;
      g_free (keyVal);
      CHECK_ERROR (error);
    } else {
      cout << "Unknown key " << *key << " for group [" << group <<"]\n";
    }
  }

  privObj->placeObj.insert (pair<int, NvDsPlaceObject> (placeId, placeObj));

  ret = true;

done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }

  return ret;
}

static bool
nvds_msg2p_parse_analytics (NvDsMsg2pCtx *ctx, GKeyFile *key_file, gchar *group)
{
  bool ret = false;
  bool isEnabled = false;
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;
  NvDsPayloadPriv *privObj = NULL;
  NvDsAnalyticsObject analyticsObj;
  gint moduleId;
  gchar *keyVal;

  if (sscanf (group, CONFIG_GROUP_ANALYTICS "%u", &moduleId) < 1) {
    cout << "Wrong analytics module group name " << group << endl;
    return ret;
  }

  privObj = (NvDsPayloadPriv *) ctx->privData;

  auto idMap = privObj->analyticsObj.find (moduleId);
  if (idMap != privObj->analyticsObj.end()) {
    cout << "Duplicate entries for " << group << endl;
    return ret;
  }

  isEnabled = g_key_file_get_boolean (key_file, group, CONFIG_KEY_ENABLE,
                                      &error);
  if (!isEnabled) {
    // Not enabled, skip the parsing of keys.
    ret = true;
    goto done;
  } else {
    g_key_file_remove_key (key_file, group, CONFIG_KEY_ENABLE,
                           &error);
    CHECK_ERROR (error);
  }

  keys = g_key_file_get_keys (key_file, group, NULL, &error);
  CHECK_ERROR (error);

  for (key = keys; *key; key++) {
    keyVal = NULL;
    if (!g_strcmp0 (*key, CONFIG_KEY_ID)) {
      keyVal = g_key_file_get_string (key_file, group,
                                      CONFIG_KEY_ID, &error);
      analyticsObj.id = keyVal;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_SOURCE)) {
      keyVal = g_key_file_get_string (key_file, group,
                                      CONFIG_KEY_SOURCE, &error);
      analyticsObj.source = keyVal;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_DESCRIPTION)) {
      keyVal = g_key_file_get_string (key_file, group,
                                      CONFIG_KEY_DESCRIPTION, &error);
      analyticsObj.desc = keyVal;
      CHECK_ERROR (error);
    } else if (!g_strcmp0 (*key, CONFIG_KEY_VERSION)) {
      keyVal = g_key_file_get_string (key_file, group,
                                      CONFIG_KEY_VERSION, &error);
      analyticsObj.version = keyVal;
      CHECK_ERROR (error);
    } else {
      cout << "Unknown key " << *key << " for group [" << group <<"]\n";
    }

    if (keyVal)
      g_free (keyVal);
  }

  privObj->analyticsObj.insert (make_pair (moduleId, analyticsObj));

  ret = true;

done:
  if (error) {
    g_error_free (error);
  }
  if (keys) {
    g_strfreev (keys);
  }

  return ret;
}

static bool
nvds_msg2p_parse_csv (NvDsMsg2pCtx *ctx, const gchar *file)
{
  NvDsPayloadPriv *privObj = NULL;
  NvDsAnalyticsObject analyticsObj;
  NvDsSensorObject sensorObj;
  NvDsPlaceObject placeObj;
  bool retVal = true;
  bool firstRow = true;
  string line;
  gint i, index = 0;

  ifstream inputFile (file);
  if (!inputFile.is_open()) {
    cout << "Couldn't open CSV file " << file << endl;
    return false;
  }

  privObj = (NvDsPayloadPriv *) ctx->privData;

  try {

    while (getline (inputFile, line)) {

      if (firstRow) {
        // Discard first row as it will have header fields.
        firstRow = false;
        continue;
      }

      vector<string> tokens;
      get_csv_tokens (line, tokens);
      // Ignore first cameraId field.
      i = 1;

      // sensor object fields
      sensorObj.id = tokens.at(i++);
      sensorObj.type = "Camera";
      sensorObj.desc = tokens.at(i++);

      //Hard coded values but can be read from CSV file.
      sensorObj.location[0] = 0; //atof (tokens.at(i++).c_str ());
      sensorObj.location[1] = 0;
      sensorObj.location[2] = 0;
      sensorObj.coordinate[0] = 0;
      sensorObj.coordinate[1] = 0;
      sensorObj.coordinate[2] = 0;

      // place object fields
      placeObj.id = "Id";
      placeObj.type = "building/garage";
      placeObj.name = "endeavor";
      placeObj.location[0] = 0;
      placeObj.location[1] = 0;
      placeObj.location[2] = 0;
      placeObj.coordinate[0] = 0;
      placeObj.coordinate[1] = 0;
      placeObj.coordinate[2] = 0;
      //Ignore cameraIDstring
      i++;
      placeObj.subObj.field1 = tokens.at(i++);
      placeObj.subObj.field2 = tokens.at(i++);
      placeObj.subObj.field3 = tokens.at(i++);

      // analytics object fields
      // hard coded values but can be read from CSV file.
      analyticsObj.id = "";
      analyticsObj.source = "";
      analyticsObj.desc = "";
      analyticsObj.version = "1.0";

      privObj->sensorObj.insert (make_pair (index, sensorObj));
      privObj->placeObj.insert (make_pair (index, placeObj));
      privObj->analyticsObj.insert (make_pair (index, analyticsObj));

      index++;
    }
  } catch (const std::out_of_range& oor) {
    std::cerr << "Out of Range error: " << oor.what() << '\n';
    retVal = false;
  }

  inputFile.close ();
  return retVal;
}

static bool
nvds_msg2p_parse_key_value (NvDsMsg2pCtx *ctx, const gchar *file)
{
  bool retVal = true;
  GKeyFile *cfgFile = NULL;
  GError *error = NULL;
  gchar **groups = NULL;
  gchar **group;

  cfgFile = g_key_file_new ();
  if (!g_key_file_load_from_file (cfgFile, file, G_KEY_FILE_NONE, &error)) {
    g_message ("Failed to load file: %s", error->message);
    retVal = false;
    goto done;
  }

  groups = g_key_file_get_groups (cfgFile, NULL);

  for (group = groups; *group; group++) {
    if (!strncmp (*group, CONFIG_GROUP_SENSOR, strlen (CONFIG_GROUP_SENSOR))) {
      retVal = nvds_msg2p_parse_sensor (ctx, cfgFile, *group);
    } else if (!strncmp (*group, CONFIG_GROUP_PLACE, strlen (CONFIG_GROUP_PLACE))) {
      retVal = nvds_msg2p_parse_place (ctx, cfgFile, *group);
    } else if (!strncmp (*group, CONFIG_GROUP_ANALYTICS, strlen (CONFIG_GROUP_ANALYTICS))) {
      retVal = nvds_msg2p_parse_analytics (ctx, cfgFile, *group);
    } else {
      cout << "Unknown group " << *group << endl;
    }

    if (!retVal) {
      cout << "Failed to parse group " << *group << endl;
      goto done;
    }
  }

done:
  if (groups)
    g_strfreev (groups);

  if (cfgFile)
    g_key_file_free (cfgFile);

  return retVal;
}

NvDsMsg2pCtx* nvds_msg2p_ctx_create (const gchar *file, NvDsPayloadType type)
{
  NvDsMsg2pCtx *ctx = NULL;
  string str;
  bool retVal = true;

  /*
   * Need to parse configuration / CSV files to get static properties of
   * components (e.g. sensor, place etc.) in case of full deepstream schema.
   */
  if (type == NVDS_PAYLOAD_DEEPSTREAM) {
    g_return_val_if_fail (file, NULL);

    ctx = new NvDsMsg2pCtx;
    ctx->privData = (void *) new NvDsPayloadPriv;

    if (g_str_has_suffix (file, ".csv")) {
      retVal = nvds_msg2p_parse_csv (ctx, file);
    } else {
      retVal = nvds_msg2p_parse_key_value (ctx, file);
    }
  } else {
    ctx = new NvDsMsg2pCtx;
    /* If configuration file is provided for minimal schema,
     * parse it for static values.
     */
    if (file) {
      ctx->privData = (void *) new NvDsPayloadPriv;
      retVal = nvds_msg2p_parse_key_value (ctx, file);
    } else {
      ctx->privData = nullptr;
      retVal = true;
    }
  }

  ctx->payloadType = type;

  if (!retVal) {
    cout << "Error in creating instance" << endl;

    if (ctx && ctx->privData)
      delete (NvDsPayloadPriv *) ctx->privData;

    if (ctx) {
      delete ctx;
      ctx = NULL;
    }
  }
  return ctx;
}

void nvds_msg2p_ctx_destroy (NvDsMsg2pCtx *ctx)
{
  delete (NvDsPayloadPriv *) ctx->privData;
  ctx->privData = nullptr;
  delete ctx;
}

NvDsPayload*
nvds_msg2p_generate (NvDsMsg2pCtx *ctx, NvDsEvent *events, guint size)
{
  gchar *message = NULL;
  gint len = 0;
  NvDsPayload *payload = (NvDsPayload *) g_malloc0 (sizeof (NvDsPayload));
  if (ctx->payloadType == NVDS_PAYLOAD_DEEPSTREAM) {
    message = generate_schema_message (ctx, events->metadata);
    if (message) {
      len = strlen (message);
      // Remove '\0' character at the end of string and just copy the content.
      payload->payload = g_memdup (message, len);
      payload->payloadSize = len;
      g_free (message);
    }
  } else if (ctx->payloadType == NVDS_PAYLOAD_DEEPSTREAM_MINIMAL) {
    message = generate_deepstream_message_minimal (ctx, events, size);
    if (message) {
      len = strlen (message);
      // Remove '\0' character at the end of string and just copy the content.
      payload->payload = g_memdup (message, len);
      payload->payloadSize = len;
      g_free (message);
    }
  } else if (ctx->payloadType == NVDS_PAYLOAD_CUSTOM) {
    payload->payload = (gpointer) g_strdup ("CUSTOM Schema");
    payload->payloadSize = strlen ((char *)payload->payload) + 1;
  } else
    payload->payload = NULL;

  return payload;
}

void
nvds_msg2p_release (NvDsMsg2pCtx *ctx, NvDsPayload *payload)
{
  g_free (payload->payload);
  g_free (payload);
}
