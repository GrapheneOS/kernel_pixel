// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA NEP CMD RPC Client
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#include "nep_cmd_rpc_client.h"

#include <linux/completion.h>
#include <linux/mutex.h>

#include "../public/net/map_def.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/nep_cmd_rpc_service_client.nanopb.h"

typedef struct NepCmdRpcClient {
  struct mutex mutex;
  struct completion compl;
  PwRpcClient* rpc_client;
  uint8_t saved_config_msg[128];
  uint32_t saved_config_len;
  bool has_saved_config;
} NepCmdRpcClient;

// The NepCmdRpcClient instance we use throughout the RPC.
static NepCmdRpcClient client;
static noa_service_nep_cmd_service_Response response = {
    .result = noa_service_nep_cmd_service_CmdResult_SUCCESS};

void NepCmdRpcClientOnCompleteCallback(struct PwRpcCallStruct* __attribute__((__unused__))call,
                                       const uint8_t* payload, size_t payload_size,
                                       PwStatus status) {
  if (status != kPwStatusOk) {
    pr_info("%s: NEP cmd failed.", __func__);
    return;
  }

  NepCmdRpcServiceCommandDeserializeResponse(payload, payload_size, &response);
  pr_info("%s: Received NEP response: %d", __func__, response.result);
  complete(&client.compl);
}

void LargeNepCmdRpcClientOnCompleteCallback(struct PwRpcCallStruct*
                                       __attribute__((__unused__))call,
                                       const uint8_t* payload, size_t payload_size,
                                       PwStatus status) {
  if (status != kPwStatusOk) {
    pr_info("%s: Large NEP cmd failed.", __func__);
    return;
  }

  NepCmdRpcServiceLargeCommandDeserializeResponse(payload, payload_size, &response);
  pr_info("%s: Received NEP response: %d", __func__, response.result);
  complete(&client.compl);
}

// Handle on error rpc command.
void NepCmdRpcClientOnErrorCallback(struct PwRpcCallStruct*
                                    __attribute__((__unused__))call, PwStatus error) {
  pr_info("%s: NEP cmd received error", __func__);
}


typedef struct NepCmdInfo {
  uint32_t cmd;
  const char *name;
  const char *driver_type;
} NepCmdInfo;

static const NepCmdInfo kNepCmdTable[] = {
  {0,   "CMD_UPSTREAM_4MAP",            "TETHERING_IPV4"},
  {1,   "CMD_DOWNSTREAM_4MAP",          "TETHERING_IPV4"},
  {2,   "CMD_REMOVE_UPSTREAM_4MAP",     "TETHERING_IPV4"},
  {3,   "CMD_REMOVE_DOWNSTREAM_4MAP",   "TETHERING_IPV4"},
  {4,   "CMD_PUT_UPSTREAM_6MAP",        "TETHERING_IPV6"},
  {5,   "CMD_REMOVE_UPSTREAM_6MAP",     "TETHERING_IPV6"},
  {6,   "CMD_PUT_DOWNSTREAM_6MAP",      "TETHERING_IPV6"},
  {7,   "CMD_REMOVE_DOWNSTREAM_6MAP",   "TETHERING_IPV6"},
  {14,  "CMD_SET_CONFIG",               "TETHERING_CONFIG"},
  {15,  "CMD_REMOVE_CONFIG",            "TETHERING_CONFIG"},
  {16,  "CMD_GET_STATS",                "TETHERING_STATS"},
  {29,  "CMD_REQUEST_BASE_ADDR",        "SYS_BASE_ADDR"},
  {100, "CMD_VPN_ADD_SA",               "VPN_IPSEC_OFFLOAD"},
  {101, "CMD_VPN_DEL_SA",               "VPN_IPSEC_OFFLOAD"},
  {102, "CMD_VPN_UPDATE_SA",            "VPN_IPSEC_OFFLOAD"},
  {103, "CMD_VPN_FREE_SA",              "VPN_IPSEC_OFFLOAD"},
  {201, "CMD_PUT_NETLINK_CONF",         "NETLINK_NEIGHBOR_SYNC"},
};

static const NepCmdInfo* nep_cmd_lookup(uint32_t cmd) {
  size_t i;
  for (i = 0; i < sizeof(kNepCmdTable) / sizeof(kNepCmdTable[0]); i++) {
    if (kNepCmdTable[i].cmd == cmd) {
      return &kNepCmdTable[i];
    }
  }
  return NULL;
}

int NepCmdRpcClientReplaySetConfig(void) {
  if (!client.has_saved_config || client.saved_config_len == 0) {
    pr_info("%s: No stored CMD_SET_CONFIG to replay", __func__);
    return -ENOENT;
  }
  pr_info("%s: Replaying latest stored CMD_SET_CONFIG (len %u)", __func__, client.saved_config_len);
  return NepCmdRpcClientSendCommand(CMD_SET_CONFIG,
                                     client.saved_config_msg,
                                     client.saved_config_len,
                                     NULL, NULL);
}

PwStatus NepCmdRpcClientSendCommand(uint32_t cmd, const void* msg,
                                    uint32_t msg_len, void* res_buf, uint32_t* res_code) {
  const NepCmdInfo* info = nep_cmd_lookup(cmd);
  pr_info("%s: Call to NEP, cmd %u [%s] driver_type: %s, msg_len: %u, client.rpc_client(%p)",
          __func__, cmd, info ? info->name : "CMD_UNKNOWN",
          info ? info->driver_type : "UNKNOWN", msg_len, client.rpc_client);

  if (client.rpc_client == NULL) {
    if (cmd == CMD_SET_CONFIG && msg != NULL && msg_len > 0 && msg_len <= sizeof(client.saved_config_msg)) {
      memcpy(client.saved_config_msg, msg, msg_len);
      client.saved_config_len = msg_len;
      client.has_saved_config = true;
      pr_info("%s: Stored CMD_SET_CONFIG payload when client.rpc_client is NULL (len %u)", __func__, msg_len);
    }
    pr_info("%s: client not initialized!", __func__);
    return kPwStatusFailedPrecondition;
  }

  if (mutex_lock_interruptible(&client.mutex)) {
    return kPwStatusAborted;
  }

  PwStatus status;
  if (msg_len > 128) {
    noa_service_nep_cmd_service_LargeRequest large_request;
    large_request.cmd = cmd;
    large_request.msg.size = msg_len;
    if (msg_len > 0) memcpy(large_request.msg.bytes, msg, msg_len);

    status = NepCmdRpcServiceLargeCommand(
        client.rpc_client, &large_request, LargeNepCmdRpcClientOnCompleteCallback,
        NepCmdRpcClientOnErrorCallback, NULL, NULL);
  }
  else {
    noa_service_nep_cmd_service_Request request;
    request.cmd = cmd;
    request.msg.size = msg_len;
    if (msg_len > 0) memcpy(request.msg.bytes, msg, msg_len);

    status = NepCmdRpcServiceCommand(
        client.rpc_client, &request, NepCmdRpcClientOnCompleteCallback,
        NepCmdRpcClientOnErrorCallback, NULL, NULL);
  }

  if (status == kPwStatusOk) {
    wait_for_completion_interruptible(&client.compl);
    pr_info("%s: done, res_size %d", __func__, response.msg.size);
    if (response.msg.size > 0 && res_buf != NULL) {
      memcpy(res_buf, response.msg.bytes, response.msg.size);
    }
    if (res_code != NULL) {
      *res_code = response.result;
    }
  } else {
    pr_info("%s: NEP cmd failed.", __func__);
  }

  mutex_unlock(&client.mutex);
  return status;
}

void NepCmdRpcClientInit(PwRpcClient* rpc_client) {
  init_completion(&client.compl);
  mutex_init(&client.mutex);
  if (mutex_lock_interruptible(&client.mutex)) {
    return;
  }

  client.rpc_client = rpc_client;
  mutex_unlock(&client.mutex);

  pr_info("%s: nep cmd rpc client init done", __func__);
}

PwStatus NepCmdRpcClientDeinit(void) {
  if (mutex_lock_interruptible(&client.mutex)) {
		return kPwStatusAborted;
  }
  client.rpc_client = NULL;
  complete(&client.compl);
	mutex_unlock(&client.mutex);

  return kPwStatusOk;
}
