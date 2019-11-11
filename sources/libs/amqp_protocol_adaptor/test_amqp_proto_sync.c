/*
################################################################################
# Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#
################################################################################
*/

//This is a test program to perform connect, disconnect , send messages to amqp broker
//Use a single thread to connect and perform synchronous send

#include<stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <dlfcn.h>
#include "nvds_msgapi.h"
#include "nvds_logger.h"

#define NUM_THREADS 5
const char * AMQP_PROTO_SO = "./libnvds_amqp_proto.so";
//static const char* connection_str = "url;port;username";

NvDsMsgApiHandle (*nvds_msgapi_connect_ptr)(char *connection_str, nvds_msgapi_connect_cb_t connect_cb, char *config_path);
NvDsMsgApiErrorType (*nvds_msgapi_send_sync_ptr)(NvDsMsgApiHandle conn, char *topic, const uint8_t *payload, size_t nbuf);
NvDsMsgApiErrorType (*nvds_msgapi_disconnect_ptr)(NvDsMsgApiHandle h_ptr);


void connect_cb(NvDsMsgApiHandle h_ptr, NvDsMsgApiEventType evt) {
    if(evt == NVDS_MSGAPI_EVT_DISCONNECT)
        printf("In sample prog: connect failed \n");
    else
        printf("In sample prog: connect success \n");
}

int main(int argc, char **argv) {
    void *so_handle;
    if(argc < 2)
        so_handle = dlopen(AMQP_PROTO_SO, RTLD_LAZY);
    else if(argc == 2)
        so_handle = dlopen(argv[1], RTLD_LAZY);
    else {
        printf("Invalid arguments to sample applicaiton\n");
        printf("Usage: \n\t./test_sync [optional path_to_so_lib] \n\n");
        exit(1);
    }
    char *error;
    if (!so_handle) {
        printf("unable to open shared library\n");
        exit(-1);
    }
    nvds_log_open();
    *(void **) (&nvds_msgapi_connect_ptr) = dlsym(so_handle, "nvds_msgapi_connect");
    *(void **) (&nvds_msgapi_send_sync_ptr) = dlsym(so_handle, "nvds_msgapi_send");
    *(void **) (&nvds_msgapi_disconnect_ptr) = dlsym(so_handle, "nvds_msgapi_disconnect");

    if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", error);
        exit(-1);
    }

    //There are 2 options to provide connection string
    //option 1: if part of connection params provided in nvds_msgapi_connect()
    //          with format url;port;username with password provided in config file
    //option 2: The full connection details in config file and connection params provided in nvds_msgapi_connect() as NULL

    //In this example: option 2 is used. full connection string should be provided in cfg_amqp.txt

    NvDsMsgApiHandle ah = nvds_msgapi_connect_ptr(NULL, connect_cb, "cfg_amqp.txt");
    if(ah == NULL) {
        printf("Connect to amqp broker failed\n");
        exit(0);
    }
    const char *msg = "Hello world";
    for(int i=0; i < 1000; i++) {
        nvds_msgapi_send_sync_ptr(ah, "person.event.fr_id", (const uint8_t *)msg, strlen(msg));
        printf("Successfully sent msg[%d] : %s\n", i, msg);
    }
    sleep(1);
    nvds_log_close();
    nvds_msgapi_disconnect_ptr(ah);
}
