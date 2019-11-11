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

#include "nvds_msgapi.h"

class NvDsKafkaSendCompl {
 public:
  virtual void sendcomplete(NvDsMsgApiErrorType);
  NvDsMsgApiErrorType get_err();
  virtual ~NvDsKafkaSendCompl() = default;
};

class NvDsKafkaSyncSendCompl: public NvDsKafkaSendCompl {
 private:
  uint8_t *compl_flag;
  NvDsMsgApiErrorType err;

 public:
  NvDsKafkaSyncSendCompl(uint8_t *);
  void sendcomplete(NvDsMsgApiErrorType);
  NvDsMsgApiErrorType get_err();
};

class NvDsKafkaAsyncSendCompl: public NvDsKafkaSendCompl {
 private:
  void *user_ptr;
  nvds_msgapi_send_cb_t async_send_cb;

 public:
  NvDsKafkaAsyncSendCompl(void *ctx, nvds_msgapi_send_cb_t cb);
  void sendcomplete(NvDsMsgApiErrorType);
};

void *nvds_kafka_client_init(char *brokers, char *topic);
NvDsMsgApiErrorType nvds_kafka_client_launch(void *kh);
NvDsMsgApiErrorType nvds_kafka_client_send(void *kh, const uint8_t *payload, int len, int sync, void *ctx, nvds_msgapi_send_cb_t cb,  char *key, int keylen);
NvDsMsgApiErrorType nvds_kafka_client_setconf(void *kh, char *key, char *val);
void nvds_kafka_client_poll(void *kv);
void nvds_kafka_client_finish(void *kv);

#define NVDS_KAFKA_LOG_CAT "NVDS_KAFKA_PROTO"
