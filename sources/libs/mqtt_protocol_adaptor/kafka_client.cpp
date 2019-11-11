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
/*
 * librdkafka - Apache Kafka C library
 *
 * Copyright (c) 2017, Magnus Edenhill
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * kafka client based on Simple Apache Kafka producer from librdkafka
 * (https://github.com/edenhill/librdkafka)
 */

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include "rdkafka.h"
#include "nvds_logger.h"
#include "kafka_client.h"

/**
 * @brief Message delivery report callback.
 *
 * This callback is called exactly once per message, indicating if
 * the message was succesfully delivered
 * (rkmessage->err == RD_KAFKA_RESP_ERR_NO_ERROR) or permanently
 * failed delivery (rkmessage->err != RD_KAFKA_RESP_ERR_NO_ERROR).
 *
 * The callback is triggered from rd_kafka_poll() and executes on
 * the application's thread.
 */
static void dr_msg_cb (rd_kafka_t *rk,
                       const rd_kafka_message_t *rkmessage, void *opaque) {
  NvDsMsgApiErrorType dserr;
  if (rkmessage->err) {
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "Message delivery failed: %s\n", \
	     rd_kafka_err2str(rkmessage->err));
  }
  else
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_DEBUG, "Message delivered (%zd bytes, " \
                        "partition %d)\n", rkmessage->len, rkmessage->partition);

  switch (rkmessage->err) {
    case RD_KAFKA_RESP_ERR_NO_ERROR:
        dserr = NVDS_MSGAPI_OK;
        break;

   case RD_KAFKA_RESP_ERR_UNKNOWN_TOPIC_OR_PART:
        dserr = NVDS_MSGAPI_UNKNOWN_TOPIC;
        break;

    default:
        dserr = NVDS_MSGAPI_ERR;
        break;
  };
  ((NvDsKafkaSendCompl *)(rkmessage->_private))->sendcomplete(dserr);

  delete ((NvDsKafkaSendCompl *)(rkmessage->_private));

}

typedef struct {
   rd_kafka_t *producer;         /* Producer instance handle */
   rd_kafka_topic_t *topic;  /* Topic object */
   rd_kafka_conf_t *conf;  /* Temporary configuration object */
   char topic_name[255];
} NvDsKafkaClientHandle;

NvDsKafkaSyncSendCompl::NvDsKafkaSyncSendCompl(uint8_t *cflag) {
  compl_flag = cflag;
}

/**
 * Method that gets invoked when sync send operation is completed
 */
void NvDsKafkaSyncSendCompl::sendcomplete(NvDsMsgApiErrorType senderr) {
  *compl_flag = 1;
  err = senderr;
}


void NvDsKafkaSendCompl::sendcomplete(NvDsMsgApiErrorType senderr) {
  printf("wrong class\n");
}

NvDsMsgApiErrorType NvDsKafkaSendCompl::get_err() {
  return NVDS_MSGAPI_OK;
}

/**
 * Method that gets invoked when sync send operation is completed
 */
NvDsMsgApiErrorType NvDsKafkaSyncSendCompl::get_err() {
  return err;
}


NvDsKafkaAsyncSendCompl::NvDsKafkaAsyncSendCompl(void *ctx, nvds_msgapi_send_cb_t cb) {
  user_ptr = ctx;
  async_send_cb = cb;
}

/**
 * Method that gets invoked when async send operation is completed
 */
void NvDsKafkaAsyncSendCompl::sendcomplete(NvDsMsgApiErrorType senderr) {
  // simply call any registered callback
  if (async_send_cb)
    async_send_cb(user_ptr, senderr);
}

/*
 * The kafka protocol adaptor expects the client to manage handle usage and retirement.
 * Specifically, client has to ensure that once a handle is retired through disconnect,
 * that it does not get used for either send or do_work.
 * While the library iplements a best effort mechanism to ensure that calling into these
 * functions with retired handles is  gracefully handled, this is not done in a thread-safe
 * manner
 *
 * Also, note thatrdkafka is inherently thread safe and therefore there is no need to implement separate
 * locking mechanisms in the kafka protocol adaptor for methods calling directly in rdkafka
 *
 */
void *nvds_kafka_client_init(char *brokers, char *topic) {
   NvDsKafkaClientHandle *kh = NULL;
   rd_kafka_conf_t *conf;  /* Temporary configuration object */
   char errstr[512];

   nvds_log(NVDS_KAFKA_LOG_CAT, LOG_INFO, "Connecting to kafka broker: \
                            %s on topic %s\n", brokers, topic);

   if ((brokers == NULL) || (topic == NULL)) {
     nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "Broker and/or topic is null. init failed\n");
     return NULL;
   }


   /*
     Create Kafka client configuration place-holder
   */
   conf = rd_kafka_conf_new();

   /* Set bootstrap broker(s) as a comma-separated list of
          host or host:port (default port 9092).
    * librdkafka will use the bootstrap brokers to acquire the full
          set of brokers from the cluster. */
   if (rd_kafka_conf_set(conf, "bootstrap.servers", brokers,
                              errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
     nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "Error connecting kafka broker: %s\n",errstr);
       return NULL;
    }
   //else
   //    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_INFO, "Connection to broker succeeded\n");

    /* Set the delivery report callback.
     * This callback will be called once per message to inform
         the application if delivery succeeded or failed.

     * See dr_msg_cb() above. */
    rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);

    kh = (NvDsKafkaClientHandle *)(malloc(sizeof(NvDsKafkaClientHandle)));
     if (kh == NULL) {
       //free rkt, rk, conf
       return NULL;
     }

     kh->producer = NULL;
     kh->topic = NULL;
     kh->conf = conf;
     snprintf(kh->topic_name, sizeof(kh->topic_name), "%s",topic);
     return (void *)kh;
}

//There could be several synchronous and asychronous send operations in flight.
//Once a send operation callback is received the course of action  depends on if it's sync or async
// -- if it's sync then the associated completion flag should  be set
// -- if it's asynchronous then completion callback from the user should be called along with context
NvDsMsgApiErrorType nvds_kafka_client_send(void *kv,  const uint8_t *payload, int len, int sync, void *ctx, nvds_msgapi_send_cb_t cb, char *key, int keylen)
{
  NvDsKafkaClientHandle *kh = (NvDsKafkaClientHandle *)kv;
  uint8_t done = 0;

  NvDsKafkaSendCompl *scd;
  if (sync) {
    NvDsKafkaSyncSendCompl *sc= new NvDsKafkaSyncSendCompl(&done);
    scd = sc;
  } else {
    NvDsKafkaAsyncSendCompl *sc= new NvDsKafkaAsyncSendCompl(ctx, cb);
    scd = sc;
  }

  if (!kh) {
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "send called on NULL handle \n");
    return NVDS_MSGAPI_ERR;
  }

  if (rd_kafka_produce(
          /* Topic object */
          kh->topic,
          /* Use builtin partitioner to select partition*/
          RD_KAFKA_PARTITION_UA,
          /* Make a copy of the payload. */
          RD_KAFKA_MSG_F_COPY,
          /* Message payload (value) and length */
          (void *)payload, len,
          /* Optional key and its length */
          key, keylen,
          /* Message opaque, provided in
           * delivery report callback as
           * msg_opaque. */
          scd) == -1) {

      if (rd_kafka_last_error() ==
           RD_KAFKA_RESP_ERR__QUEUE_FULL) {
           /* If the internal queue is full, discard the
	    * message and log the error.
            * The internal queue represents both
            * messages to be sent and messages that have
            * been sent or failed, awaiting their
            * delivery report callback to be called.
            *
            * The internal queue is limited by the
            * configuration property
            * queue.buffering.max.messages */
           nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR,"rd_kafka_produce: Internal queue is full, discarding payload\n");
	   nvds_log(NVDS_KAFKA_LOG_CAT, LOG_DEBUG, "rd_kafkaproduce: Discarding payload=%.*s \n topic = %s\n",len, payload, rd_kafka_topic_name(kh->topic));
       }
       else
       {
           /**
             * Failed to *enqueue* message for producing.
             */
          nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR,"Failed to schedule kafka send: %s on topic <%s>\n", rd_kafka_err2str(rd_kafka_last_error()), rd_kafka_topic_name(kh->topic));			 
       }
       return NVDS_MSGAPI_ERR;

     }
     else
     {
       NvDsMsgApiErrorType err;
       if  (!sync)
          return NVDS_MSGAPI_OK;
       else
       {
          while (sync && !done) {
            usleep(1000);
            rd_kafka_poll(kh->producer, 0/*non-blocking*/);
          }
          err = (scd)->get_err();
	  return err;
        }
     }
}

NvDsMsgApiErrorType nvds_kafka_client_setconf(void *kv, char *key, char *val)
{
  char errstr[512];
  NvDsKafkaClientHandle *kh = (NvDsKafkaClientHandle *)kv;
  
  if (rd_kafka_conf_set(kh->conf, key, val , errstr, sizeof(errstr)) \
           != RD_KAFKA_CONF_OK) {
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "Error setting config setting %s; %s\n", key, errstr );
    return NVDS_MSGAPI_ERR;
  } else {
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_INFO, "set config setting %s to %s\n", key, val);
    return NVDS_MSGAPI_OK;
  }
}

/**
  Instantiates the rd_kafka_t object, which initializes the protocol
 */
NvDsMsgApiErrorType nvds_kafka_client_launch(void *kv)
{
   rd_kafka_t *rk;         /* Producer instance handle */
   rd_kafka_topic_t *rkt;  /* Topic object */
   NvDsKafkaClientHandle *kh = (NvDsKafkaClientHandle *)kv;
   char errstr[512];

   /*
      * Create producer instance.
      * NOTE: rd_kafka_new() takes ownership of the conf object
      *       and the application must not reference it again after
      *       this call.
    */
   rk = rd_kafka_new(RD_KAFKA_PRODUCER, kh->conf, errstr, sizeof(errstr));
   if (!rk) {
      nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "Failed to create new producer: %s\n", errstr);
      return NVDS_MSGAPI_ERR;
   }

    /* Create topic object that will be reused for each message
         * produced.
         *
         * Both the producer instance (rd_kafka_t) and topic objects (topic_t)
         * are long-lived objects that should be reused as much as possible.
    */
   rkt = rd_kafka_topic_new(rk, kh->topic_name, NULL);
   if (!rkt) {
        nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "Failed to create topic object: %s\n", \
           rd_kafka_err2str(rd_kafka_last_error()));
        rd_kafka_destroy(rk);
        return NVDS_MSGAPI_ERR;
   }
   kh->producer = rk;
   kh->topic = rkt;
   return NVDS_MSGAPI_OK;

}

void nvds_kafka_client_finish(void *kv)
{
  NvDsKafkaClientHandle *kh = (NvDsKafkaClientHandle *)kv;

  if (!kh) {
    nvds_log(NVDS_KAFKA_LOG_CAT, LOG_ERR, "finish called on NULL handle\n");
    return;
  }
    
  rd_kafka_flush (kh->producer, 10000);

  /* Destroy topic object */
  rd_kafka_topic_destroy(kh->topic);

  /* Destroy the producer instance */
  rd_kafka_destroy( kh->producer );

}

void nvds_kafka_client_poll(void *kv)
{
  NvDsKafkaClientHandle *kh = (NvDsKafkaClientHandle *)kv;
  if (kh)
    rd_kafka_poll(kh->producer, 0/*non-blocking*/);
}
