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
//Use a single thread to connect and perform asynchronous send

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
static const char* connection_str = "localhost;5672;guest";

NvDsMsgApiHandle (*nvds_msgapi_connect_ptr)(char *connection_str, nvds_msgapi_connect_cb_t connect_cb, char *config_path);
NvDsMsgApiErrorType (*nvds_msgapi_send_async_ptr)(NvDsMsgApiHandle conn, char *topic, const uint8_t *payload, size_t nbuf, nvds_msgapi_send_cb_t send_callback, void *user_ptr);
NvDsMsgApiErrorType (*nvds_msgapi_disconnect_ptr)(NvDsMsgApiHandle h_ptr);
void (*nvds_msgapi_do_work_ptr)(NvDsMsgApiHandle h_ptr);

void connect_cb(NvDsMsgApiHandle h_ptr, NvDsMsgApiEventType evt) {
    if(evt == NVDS_MSGAPI_EVT_DISCONNECT)
        printf("In sample prog: connect failed \n");
    else
        printf("In sample prog: connect success \n");
}

void send_callback (void *user_ptr, NvDsMsgApiErrorType completion_flag) {
    if(completion_flag == NVDS_MSGAPI_OK)
        printf("Message num %d : send success\n", *((int *) user_ptr));
    else
        printf("Message num %d : send failed\n", *((int *) user_ptr));
}

int main(int argc, char **argv) {
    void *so_handle;
    if(argc < 2)
        so_handle = dlopen(AMQP_PROTO_SO, RTLD_LAZY);
    else if(argc == 2)
        so_handle = dlopen(argv[1], RTLD_LAZY);
    else {
        printf("Invalid arguments to sample applicaiton\n");
        printf("Usage: \n\t./test_async [optional path_to_so_lib] \n\n");
        exit(1);
    }
    char *error;
    if (!so_handle) {
        printf("unable to open shared library\n");
        exit(-1);
    }
    nvds_log_open();
    *(void **) (&nvds_msgapi_connect_ptr) = dlsym(so_handle, "nvds_msgapi_connect");
    *(void **) (&nvds_msgapi_send_async_ptr) = dlsym(so_handle, "nvds_msgapi_send_async");
    *(void **) (&nvds_msgapi_disconnect_ptr) = dlsym(so_handle, "nvds_msgapi_disconnect");
    *(void **) (&nvds_msgapi_do_work_ptr) = dlsym(so_handle, "nvds_msgapi_do_work");

    if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", error);
        exit(-1);
    }

    //There are 2 options to provide connection string
    //option 1: if part of connection params provided in nvds_msgapi_connect()
    //          with format url;port;username with password provided in config file
    //option 2: The full connection details in config file and connection params provided in nvds_msgapi_connect() as NULL

    //In this example: option 1 is used. Part of connection string is provided within (char *)connection_str. password provided in cfg_amqp.txt

    NvDsMsgApiHandle ah = nvds_msgapi_connect_ptr((char *)connection_str, connect_cb, "cfg_amqp.txt");
    if(ah == NULL) {
        printf("Connect to amqp broker failed\n");
        exit(0);
    }
    printf("Connect Success\n");
    for(int i=0; i < 1000; i++) {
        char msg[100];
        sprintf(msg, "Hello%d\n", i);
        if(nvds_msgapi_send_async_ptr(ah, NULL, (const uint8_t *)msg, strlen(msg), send_callback, &i) == NVDS_MSGAPI_OK)
            printf("Message sent successfully\n");
        nvds_msgapi_do_work_ptr(ah);
    }
    sleep(1);
    nvds_log_close();
    nvds_msgapi_disconnect_ptr(ah);
}
