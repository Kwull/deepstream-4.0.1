/*
 * Copyright (c) 2018 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 */
#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "nvds_logger.h"
#include "nvds_msgapi.h"

/* MODIFY: to reflect your own path */
#define NVDS_VERSION 4.0
#define SO_PATH "/opt/nvidia/deepstream/deepstream-NVDS_VERSION/lib/"

#define PROTO_SO "libnvds_kafka_proto.so"
#define KAFKA_PROTO_PATH SO_PATH PROTO_SO
#define CFG_FILE "./config.txt"

void sample_msgapi_connect_cb(NvDsMsgApiHandle *h_ptr, NvDsMsgApiEventType ds_evt)
{}

int main()
{
   NvDsMsgApiHandle conn_handle;
   NvDsMsgApiHandle (*msgapi_connect_ptr)(char *connection_str, nvds_msgapi_connect_cb_t connect_cb, char *config_path);
   NvDsMsgApiErrorType (*msgapi_send_ptr)(NvDsMsgApiHandle conn, char *topic, const uint8_t *payload, size_t nbuf);
   NvDsMsgApiErrorType (*msgapi_disconnect_ptr)(NvDsMsgApiHandle h_ptr);
   void *so_handle = dlopen(KAFKA_PROTO_PATH, RTLD_LAZY);
   char *error;
   //   const char SEND_MSG[]="Hello World";
   const char SEND_MSG[]= "{ \
   \"messageid\" : \"84a3a0ad-7eb8-49a2-9aa7-104ded6764d0_c788ea9efa50\", \
   \"mdsversion\" : \"1.0\", \
   \"@timestamp\" : \"\", \
   \"place\" : { \
    \"id\" : \"1\", \
    \"name\" : \"HQ\", \
    \"type\" : \"building/garage\", \
    \"location\" : { \
      \"lat\" : 0, \
      \"lon\" : 0, \
      \"alt\" : 0 \
    }, \
    \"aisle\" : { \
      \"id\" : \"C_126_135\", \
      \"name\" : \"Lane 1\", \
      \"level\" : \"P1\", \
      \"coordinate\" : { \
        \"x\" : 1, \
        \"y\" : 2, \
        \"z\" : 3 \
      } \
     }\
    },\
   \"sensor\" : { \
    \"id\" : \"10_110_126_135_A0\", \
    \"type\" : \"Camera\", \
    \"description\" : \"Aisle Camera\", \
    \"location\" : { \
      \"lat\" : 0, \
      \"lon\" : 0, \
      \"alt\" : 0 \
    }, \
    \"coordinate\" : { \
      \"x\" : 0, \
      \"y\" : 0, \
      \"z\" : 0 \
     } \
    } \
   }";

   printf("Refer to nvds log file for log output\n");   
   char display_str[5][100];
   nvds_log_open();

   for(int i = 0; i < 5; i++) 
     snprintf(&(display_str[i][0]), 100, "Async send [%d] complete", i);

   if (!so_handle) {
       error = dlerror();
       fprintf(stderr, "%s\n", error);

     printf("unable to open shared library\n");
     exit(-1);
   }


      
   *(void **) (&msgapi_connect_ptr) = dlsym(so_handle, "nvds_msgapi_connect");
   *(void **) (&msgapi_send_ptr) = dlsym(so_handle, "nvds_msgapi_send");
   *(void **) (&msgapi_disconnect_ptr) = dlsym(so_handle, "nvds_msgapi_disconnect"); 
   
   if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", error);
	exit(-1);
    }

    // set kafka broker appropriately
   conn_handle = msgapi_connect_ptr((char *)"yourserver.yourdomain.net;9092;yourtopic",(nvds_msgapi_connect_cb_t) sample_msgapi_connect_cb, (char *)CFG_FILE);
    
    if (!conn_handle) {
      printf("Connect failed. Exiting\n");
      exit(-1);
    }
   
    
    for(int i = 0; i < 5; i++) {
      if (msgapi_send_ptr(conn_handle, (char *)"yourtopic", (const uint8_t*) SEND_MSG, \
			                                              strlen(SEND_MSG)) != NVDS_MSGAPI_OK)
	printf("send [%d] failed\n", i);
      else {
	printf("send [%d] completed\n", i);
	nvds_log("TEST_KAFKA_PROTO", LOG_ERR, "send [%d] completed\n", i);
      sleep(1);
      }
    }
    nvds_log_close();
    msgapi_disconnect_ptr(conn_handle);
}
