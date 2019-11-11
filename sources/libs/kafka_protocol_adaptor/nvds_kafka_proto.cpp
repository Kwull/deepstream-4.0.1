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
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <glib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <netdb.h>
#include "nvds_logger.h"
#include "nvds_msgapi.h"
#include "kafka_client.h"


#define MAX_FIELD_LEN 255 //maximum topic length supported by kafka is 255

#define NVDS_MSGAPI_VERSION "1.0"

#define CONFIG_GROUP_MSG_BROKER "message-broker"
#define CONFIG_GROUP_MSG_BROKER_RDKAFKA_CFG "proto-cfg"
#define CONFIG_GROUP_MSG_BROKER_PARTITION_KEY "partition-key"


int json_get_key_value(const char *msg, int msglen, const char *key, char *value, int nbuf);

typedef struct {
  void *kh;
  char topic[MAX_FIELD_LEN];
  char partition_key_field[MAX_FIELD_LEN];
} NvDsKafkaProtoConn;

/**
 * internal function to read settings from config file
 * Documentation needs to indicate that kafka config parameters are:
  (1) located within application level config file passed to connect
  (2) within the message broker group of the config file
  (3) specified based on 'rdkafka-cfg' key
  (4) the various options to rdkafka are specified based on 'key=value' format, within various entries semi-colon separated
Eg:
[message-broker]
enable=1
broker-proto-lib=/opt/nvidia/deepstream/deepstream-<version>/lib/libnvds_kafka_proto.so
broker-conn-str=kafka1.data.nvidiagrid.net;9092;metromind-test-1
rdkafka-cfg="message.timeout.ms=2000"

 */
static void nvds_kafka_read_config(void *kh, char *config_path, char *partition_key_field, int field_len)
{
  //iterate over the config params to set one by one
  //finally call into launch function passing topic
  GKeyFile *key_file = g_key_file_new ();
  gchar **keys = NULL;
  gchar **key = NULL;
  GError *error = NULL;
  char *confptr = NULL, *curptr = NULL;

  if (!g_key_file_load_from_file (key_file, config_path, G_KEY_FILE_NONE,
            &error)) {
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR,  "unable to load config file at path %s; error message = %s\n", config_path, error->message);
    return;
  }

  keys = g_key_file_get_keys(key_file, CONFIG_GROUP_MSG_BROKER, NULL, &error);
  if (error) {
     nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR,  "Error parsing config file. %s\n", error->message);
     return;
  }
  for (key = keys; *key; key++) {
    gchar *setvalquote;

    // check if this is one of adaptor settings
    if (!g_strcmp0(*key, CONFIG_GROUP_MSG_BROKER_RDKAFKA_CFG))
	{
           setvalquote = g_key_file_get_string (key_file, CONFIG_GROUP_MSG_BROKER,
               CONFIG_GROUP_MSG_BROKER_RDKAFKA_CFG, &error);

	   if (error) {
             nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR,  "Error parsing config file\n");
	     return;
	   }

	   confptr = setvalquote;
	   size_t conflen = strlen(confptr);

	   //remove "". (string length needs to be at least 2)
	   //Could use g_shell_unquote but it might have other side effects
	   if ((conflen <3) || (confptr[0] != '"') || (confptr[conflen-1] != '"')) {
             nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR,  "invalid format for rdkafa \
                               config entry. Start and end with \"\"\n");
	     return;
           }
           confptr[conflen-1] = '\0'; //remove ending quote
           confptr = confptr + 1; //remove starting quote
	   nvds_log(NVDS_KAFKA_LOG_CAT, LOG_INFO,  "kafka setting %s = %s\n", *key, confptr);
	}

       // check if this entry specifies the partition key field
    if (!g_strcmp0(*key, CONFIG_GROUP_MSG_BROKER_PARTITION_KEY))
        {
           gchar *key_name_conf;
           key_name_conf = g_key_file_get_string (key_file, CONFIG_GROUP_MSG_BROKER,
                CONFIG_GROUP_MSG_BROKER_PARTITION_KEY, &error);
           if (error) {
             nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR,  "Error parsing config file\n");
             g_error_free(error);
             return;
           }
           strncpy(partition_key_field, (char *)key_name_conf, field_len);
           nvds_log(NVDS_KAFKA_LOG_CAT, LOG_INFO,  "kafka partition key field name = %s\n", partition_key_field);
           g_free(key_name_conf);
        }
  }

  if (!confptr) {
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_DEBUG,  "No " CONFIG_GROUP_MSG_BROKER_RDKAFKA_CFG " entry found in config file.\n");
    return;
 }
  
  char *equalptr, *semiptr;
  int keylen, vallen, conflen = strlen(confptr);
  char confkey[MAX_FIELD_LEN], confval[MAX_FIELD_LEN];
  curptr = confptr;

  while (((equalptr = strchr(curptr, '=')) != NULL) && ((curptr - confptr) < conflen)) {
     keylen = (equalptr - curptr);
     if (keylen >= MAX_FIELD_LEN)
       keylen = MAX_FIELD_LEN - 1;

     memcpy(confkey, curptr, keylen);
     confkey[keylen] = '\0';

     if (equalptr >= (confptr + conflen)) //no more string; dangling key
       return;

     semiptr = strchr(equalptr+1, ';');

     if (!semiptr) {
       vallen = (confptr + conflen - equalptr - 1);//end of strng case
       curptr = (confptr + conflen);
     }
     else {
       curptr = semiptr + 1;
       vallen = (semiptr - equalptr - 1);
     }

     if (vallen >= MAX_FIELD_LEN)
       vallen = MAX_FIELD_LEN - 1;

     memcpy(confval, (equalptr + 1), vallen);
     confval[vallen] = '\0';

     nvds_kafka_client_setconf(kh, confkey, confval);

  }
}


/**
 * connects to a broker based on given url and port to check if address is valid
 * Returns 0 if valid, and non-zero if invalid.
 * Also returns 0 if there is trouble resolving  the address or creating connection
 */
static int test_kafka_broker_endpoint(char *burl, char *bport) {
  int sockid;
  int port = atoi(bport);
  int flags;
  fd_set wfds;
  int error;
  struct addrinfo *res, hints;

  if (!port)
    return -1;

  memset(&hints, 0, sizeof (hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  //resolve the given url
  if ((error = getaddrinfo(burl, bport, &hints, &res))) {
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "getaddrinfo returned error %d\n", error);

    if ((error == EAI_FAIL) || (error == EAI_NONAME) ||  (error == EAI_NODATA)){
      nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "count not resolve addr - permanent failure\n");
      return error; //permanent failure to resolve
    }
    else 
      return 0; //unknown error during resolve; can't invalidate address
  }

  //iterate through all ip addresses resolved for the url
  for(; res != NULL; res = res->ai_next) {
    sockid = socket(AF_INET, SOCK_STREAM, 0); //tcp socket

    //make socket non-blocking
    flags = fcntl(sockid, F_GETFL);
    if (fcntl(sockid, F_SETFL, flags | O_NONBLOCK) == -1)
      /* having trouble making socket non-blocking;
        can't check network address, and so assume it is valid
      */
     return 0;

    if (!connect(sockid, (struct sockaddr *)res->ai_addr, res->ai_addrlen)) {
      return 0; //connection succeeded right away
    }
    else {
      if (errno == EINPROGRESS) { //normal for non-blocking socker
        struct timeval conn_timeout;
        int optval;
        socklen_t optlen;

        conn_timeout.tv_sec = 5; //give 5 sec for connection to go through
        conn_timeout.tv_usec = 0;
        FD_ZERO(&wfds);
        FD_SET(sockid, &wfds);

        int err = select(sockid+1, NULL, & wfds, NULL, &conn_timeout);
        switch(err) {
          case 0: //timeout
            return ETIMEDOUT;

          case 1: //socket unblocked; now figure out why
            optval = -1;
            optlen = sizeof(optval);
            if (getsockopt(sockid, SOL_SOCKET, SO_ERROR, &optval, &optlen) == -1) {
              /* error getting socket options; can't invalidate address */
               return 0;
            }
            if (optval == 0)
              return 0; //no error; connection succeeded
            else
              return optval;	 //connection failed; something wrong with address

          case -1: //error in select, can't invalidate address
            return 0;
        }
      } else
        return 0; // error in connect; can't invalidate address
    } // non-blocking connect did not succeed
  }
  return 0; //if we got here then can't invalidate
}


/**
 * Connects to a remote kafka broker based on connection string.
 */
NvDsMsgApiHandle nvds_msgapi_connect(char *connection_str,  nvds_msgapi_connect_cb_t connect_cb, char *config_path)
{
  NvDsKafkaProtoConn *conn_ptr = (NvDsKafkaProtoConn *)malloc(sizeof(NvDsKafkaProtoConn));
  char burl[MAX_FIELD_LEN], bport[MAX_FIELD_LEN], btopic[MAX_FIELD_LEN], brokerurl[MAX_FIELD_LEN];
  int urllen, portlen, topiclen;
  urllen = portlen = topiclen = MAX_FIELD_LEN;
  char *portptr, *topicptr;

  nvds_log_open();
  nvds_log(NVDS_KAFKA_LOG_CAT, LOG_INFO, "nvds_msgapi_connect:connection_str = %s\n", connection_str);

  portptr = strchr(connection_str, ';');

  if (conn_ptr == NULL) { //malloc failed
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "Unable to allocate memory for kafka connection handle.\
                                                      Can't create connection\n");
    return NULL;
  }

  if (portptr == NULL) { //invalid format
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "invalid connection string format. Can't create connection\n");
    return NULL;
  }

  topicptr = strchr(portptr+1, ';');

  if (topicptr == NULL) { //invalid format
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "invalid connection string format. Can't create connection\n");
    return NULL;
  }

  urllen = (portptr - connection_str);
  if (urllen >= MAX_FIELD_LEN)
    urllen = MAX_FIELD_LEN - 1;

  memcpy(burl, connection_str, urllen);
  burl[urllen] = '\0';
  
  portlen = (topicptr - portptr - 1);
  if (portlen >= MAX_FIELD_LEN)
    portlen = MAX_FIELD_LEN - 1;
  memcpy(bport, (portptr + 1), portlen);
  bport[portlen] = '\0';

  topiclen = (strlen(connection_str) - urllen - portlen - 2);
  if (topiclen >=  MAX_FIELD_LEN)
    topiclen = MAX_FIELD_LEN;
  memcpy(btopic, (topicptr + 1), topiclen);
  btopic[topiclen] = '\0';

  nvds_log(NVDS_KAFKA_LOG_CAT, LOG_INFO, "kafka broker url = %s; port = %s; topic = %s", burl, bport, btopic);

  snprintf(brokerurl, sizeof(brokerurl), "%s:%s", burl, bport);

  if (test_kafka_broker_endpoint(burl, bport)) {
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "Invalid address or network endpoint down. kafka connect failed\n");
    free(conn_ptr);
    return NULL;
  }

  conn_ptr->kh = nvds_kafka_client_init(brokerurl, btopic);
  if (!conn_ptr->kh) {
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "Unable to init kafka client.\n");
    free(conn_ptr);
    return NULL;
  }
  strncpy(conn_ptr->topic, btopic, MAX_FIELD_LEN);

  /* set key field name to default value of sensor.id */
  strncpy(conn_ptr->partition_key_field, "sensor.id", sizeof(conn_ptr->partition_key_field));

  if (config_path)
    nvds_kafka_read_config(conn_ptr->kh, config_path, conn_ptr->partition_key_field, \
                        sizeof(conn_ptr->partition_key_field));

  nvds_kafka_client_launch(conn_ptr->kh);

  return (NvDsMsgApiHandle)(conn_ptr);
}

//There could be several synchronous and asychronous send operations in flight.
//Once a send operation callback is received the course of action  depends on if it's synch or async
// -- if it's sync then the associated complletion flag should  be set
// -- if it's asynchronous then completion callback from the user should be called
NvDsMsgApiErrorType nvds_msgapi_send(NvDsMsgApiHandle h_ptr, char *topic, const uint8_t *payload, size_t nbuf)
{
  char idval[100];
  int retval;

  nvds_log(NVDS_KAFKA_LOG_CAT, LOG_DEBUG, \
    "nvds_msgapi_send: payload=%.*s, \n topic = %s, h->topic = %s\n"\
           , nbuf, payload, topic, (((NvDsKafkaProtoConn *) h_ptr)->topic));

  if (strcmp(topic, (((NvDsKafkaProtoConn *) h_ptr)->topic))) {
     nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "nvds_msgapi_send: send topic has \
                                             to match topic defined at connect.\n");
     return NVDS_MSGAPI_ERR;
  }

  // parition key retrieved from config file
  char *partition_key_field = ((NvDsKafkaProtoConn *) h_ptr)->partition_key_field;
  retval = json_get_key_value((const char *)payload, nbuf, partition_key_field , idval, sizeof(idval));


  if (retval)
    return nvds_kafka_client_send(((NvDsKafkaProtoConn *) h_ptr)->kh, payload, nbuf, 1, NULL, NULL, idval, strlen(idval));
  else {
      nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "nvds_msgapi_send: \
                  no matching json field found based on kafka key config; \
                  using default partition\n");

       return nvds_kafka_client_send(((NvDsKafkaProtoConn *) h_ptr)->kh, payload, nbuf, 1, NULL, NULL, NULL, 0);
  }
}

NvDsMsgApiErrorType nvds_msgapi_send_async(NvDsMsgApiHandle h_ptr, char *topic, const uint8_t *payload, size_t nbuf,  nvds_msgapi_send_cb_t send_callback, void *user_ptr)
{
  char idval[100];
  int retval;

  nvds_log(NVDS_KAFKA_LOG_CAT, LOG_DEBUG, "nvds_msgapi_send_async: payload=%.*s, \
      \n topic = %s, h->topic = %s\n", nbuf, payload, topic, \
       (((NvDsKafkaProtoConn *) h_ptr)->topic));

  if (strcmp(topic, (((NvDsKafkaProtoConn *) h_ptr)->topic))) {
     nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "nvds_msgapi_send_async: \
        send topic has to match topic defined at connect.\n");
     return NVDS_MSGAPI_ERR;
  }

  // parition key retrieved from config file
  char *partition_key_field = ((NvDsKafkaProtoConn *) h_ptr)->partition_key_field;
  retval = json_get_key_value((const char *)payload, nbuf, partition_key_field , idval, sizeof(idval));

  if (retval)
    return nvds_kafka_client_send(((NvDsKafkaProtoConn *) h_ptr)->kh, payload, nbuf, 0, user_ptr, \
             send_callback, idval, strlen(idval));
  else {
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "no matching json field found \
        based on kafka key config; using default partition\n");
    return nvds_kafka_client_send(((NvDsKafkaProtoConn *) h_ptr)->kh, payload, nbuf, 0, user_ptr, \
                  send_callback, NULL, 0);

  }

}

void nvds_msgapi_do_work(NvDsMsgApiHandle h_ptr)
{
  nvds_log(NVDS_KAFKA_LOG_CAT, LOG_DEBUG, "nvds_msgapi_do_work\n");
  nvds_kafka_client_poll(((NvDsKafkaProtoConn *) h_ptr)->kh);
}

NvDsMsgApiErrorType nvds_msgapi_disconnect(NvDsMsgApiHandle h_ptr)
{
  if (!h_ptr) {
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_DEBUG, "nvds_msgapi_disconnect called with null handle\n");
    return NVDS_MSGAPI_OK;
  }

  nvds_kafka_client_finish(((NvDsKafkaProtoConn *) h_ptr)->kh);
  (((NvDsKafkaProtoConn *) h_ptr)->kh) = NULL;
  nvds_log_close();
  return NVDS_MSGAPI_OK;
}

/**
  * Returns version of API supported byh this adaptor
  */
char *nvds_msgapi_getversion()
{
  return (char *)NVDS_MSGAPI_VERSION;
}


