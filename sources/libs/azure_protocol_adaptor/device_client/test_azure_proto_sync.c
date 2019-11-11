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

//This is a test program to perform connect, disconnect , send messages to Azure Iothub
//Use main thread to connect and multiple threads to perform synchronous send

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
const char * AZURE_PROTO_SO = "./libnvds_azure_proto.so";
//static const char* connection_str = "url;port;deviceId";

NvDsMsgApiHandle (*nvds_msgapi_connect_ptr)(char *connection_str, nvds_msgapi_connect_cb_t connect_cb, char *config_path);
NvDsMsgApiErrorType (*nvds_msgapi_send_ptr)(NvDsMsgApiHandle conn, char *topic, const uint8_t *payload, size_t nbuf);
NvDsMsgApiErrorType (*nvds_msgapi_disconnect_ptr)(NvDsMsgApiHandle h_ptr);

struct send_info_t {
    pid_t tid;
    int num;
};

void connect_cb(NvDsMsgApiHandle h_ptr, NvDsMsgApiEventType evt) {
    if(evt == NVDS_MSGAPI_EVT_DISCONNECT)
        printf("In sample prog: connect failed \n");
    else
        printf("In sample prog: connect success \n");
}


void *func(void *ptr) {
    NvDsMsgApiHandle ah = (NvDsMsgApiHandle) ptr;
    const char *msg = "Hello world";
    pid_t myid = syscall(SYS_gettid);
    for(int i=0; i < 200; i++) {
        if(nvds_msgapi_send_ptr(ah, NULL, (const uint8_t *)msg, strlen(msg)) == NVDS_MSGAPI_OK) {
            printf("Thread [%d] , Message num %d : send success\n", myid, i);
        }
        else {
            printf("Thread [%d] , Message num %d : send failed\n", myid, i);
        }
        sleep(1);
    }
}

int main(int argc, char **argv) {
    void *so_handle;
    char *error;
    if(argc < 2)
        so_handle = dlopen(AZURE_PROTO_SO, RTLD_LAZY);
    else if(argc == 2)
        so_handle = dlopen(argv[1], RTLD_LAZY);
    else {
        printf("Invalid arguments to sample applicaiton\n");
        printf("Usage: \n\t./test_async [optional path_to_so_lib] \n\n");
        exit(1);
    }
    if (!so_handle) {
        printf("unable to open shared library\n");
        exit(-1);
    }
    nvds_log_open();
    *(void **) (&nvds_msgapi_connect_ptr) = dlsym(so_handle, "nvds_msgapi_connect");
    *(void **) (&nvds_msgapi_send_ptr) = dlsym(so_handle, "nvds_msgapi_send");
    *(void **) (&nvds_msgapi_disconnect_ptr) = dlsym(so_handle, "nvds_msgapi_disconnect");

    if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", error);
        exit(-1);
    }

    //There are 2 options to provide connection string
    //option 1: if part of connection params provided in nvds_msgapi_connect()
    //          with format url;port;device-id and SAS key provided in separate config file
    //option 2: The full device connection string is provided in config file.
    //          ex: HostName=<my-hub>.azure-devices.net;DeviceId=<device_id>;SharedAccessKey=<my-policy-key>

    //In this example: option 2 is used. full connection string should be provided in cfg_azure.txt

    NvDsMsgApiHandle ah = nvds_msgapi_connect_ptr(NULL, connect_cb, "cfg_azure.txt");
    if(ah == NULL) {
        printf("COnnect to Azure failed\n");
        exit(0);
    }
    printf("main: after connect\n");
    pthread_t tid[NUM_THREADS];
    for(int i=0; i<NUM_THREADS; i++)
        pthread_create(&tid[i], NULL, &func, ah);

    for(int i=0; i<NUM_THREADS; i++)
        pthread_join(tid[i] , NULL);
    nvds_log_close();
    nvds_msgapi_disconnect_ptr(ah);
}
