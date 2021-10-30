#include "RPC_internal.h"

static struct rpmsg_endpoint rp_endpoints[4];

enum endpoints_t {
  ENDPOINT_RAW = 0,
  ENDPOINT_RESPONSE = 1
};

void rpc::client::send_msgpack(RPCLIB_MSGPACK::sbuffer *buffer) {
  OPENAMP_send(&rp_endpoints[ENDPOINT_RAW], (const uint8_t*)buffer->data(), buffer->size());
}

static uint8_t intermediate_buffer[1024];
static uint8_t intermediate_buffer_resp[1024];

int RPC::rpmsg_recv_callback(struct rpmsg_endpoint *ept, void *data,
                                       size_t len, uint32_t src, void *priv)
{
  RPC* rpc = (RPC*)priv;

  memcpy(intermediate_buffer, data, len);

  osSignalSet(rpc->dispatcherThreadId, len);

  return 0;
}

int RPC::rpmsg_recv_response_callback(struct rpmsg_endpoint *ept, void *data,
                                       size_t len, uint32_t src, void *priv)
{
  RPC* rpc = (RPC*)priv;

  memcpy(intermediate_buffer_resp, data, len);

  osSignalSet(rpc->responseThreadId, len);

  return 0;
}

void RPC::new_service_cb(struct rpmsg_device *rdev, const char *name, uint32_t dest)
{
  if (strcmp(name, "raw") == 0) {
    OPENAMP_create_endpoint(&rp_endpoints[ENDPOINT_RAW], name, dest, rpmsg_recv_callback, NULL);
  }
  if (strcmp(name, "response") == 0) {
    OPENAMP_create_endpoint(&rp_endpoints[ENDPOINT_RESPONSE], name, dest, rpmsg_recv_response_callback, NULL);
  }
}

osThreadId eventHandlerThreadId;

void eventHandler() {
  eventHandlerThreadId = osThreadGetId();
  while (1) {
    osSignalWait(0, osWaitForever);
    OPENAMP_check_for_message();
  }
}

#ifdef CORE_CM7

static void OpenAMP_MPU_Config(void)
{
	MPU_Region_InitTypeDef MPU_InitStruct;

	/* Disable the MPU */
	HAL_MPU_Disable();

	/* Configure the MPU attributes as WT for SDRAM */
	MPU_InitStruct.Enable = MPU_REGION_ENABLE;
	MPU_InitStruct.BaseAddress = D3_SRAM_BASE;
	MPU_InitStruct.Size = MPU_REGION_SIZE_64KB;
	MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
	MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;
	MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
	MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
	MPU_InitStruct.Number = MPU_REGION_NUMBER7;
	MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
	MPU_InitStruct.SubRegionDisable = 0x00;
	MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;

	HAL_MPU_ConfigRegion(&MPU_InitStruct);

	/* Enable the MPU */
	HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

int RPC::begin() {

	OpenAMP_MPU_Config();

	//resource_table_load_from_flash();
	//HAL_SYSCFG_EnableCM4BOOT();

	eventThread = new rtos::Thread(osPriorityHigh);
	eventThread->start(&eventHandler);

  dispatcherThread = new rtos::Thread(osPriorityNormal);
  dispatcherThread->start(mbed::callback(this, &RPC::dispatch));

  responseThread = new rtos::Thread(osPriorityNormal);
  responseThread->start(mbed::callback(this, &RPC::response));

	/* Initialize OpenAmp and libmetal libraries */
	if (MX_OPENAMP_Init(RPMSG_MASTER, new_service_cb) !=  HAL_OK) {
	 return 0;
	}

  //metal_set_log_handler(metal_default_log_handler);

	/* Initialize the rpmsg endpoint to set default addresses to RPMSG_ADDR_ANY */
  rpmsg_init_ept(&rp_endpoints[ENDPOINT_RAW], "raw", RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, NULL, NULL);
  rpmsg_init_ept(&rp_endpoints[ENDPOINT_RESPONSE], "response", RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, NULL, NULL);

  rp_endpoints[ENDPOINT_RAW].priv = this;
  rp_endpoints[ENDPOINT_RESPONSE].priv = this;

  bootM4();

	/*
	* The rpmsg service is initiate by the remote processor, on H7 new_service_cb
	* callback is received on service creation. Wait for the callback
	*/
  OPENAMP_Wait_EndPointready(&rp_endpoints[ENDPOINT_RAW], HAL_GetTick() + 500);
  OPENAMP_Wait_EndPointready(&rp_endpoints[ENDPOINT_RESPONSE], HAL_GetTick() + 500);

	// Send first dummy message to enable the channel
	uint8_t message = 0x00;
  write(ENDPOINT_RAW, &message, sizeof(message));
  write(ENDPOINT_RESPONSE, &message, sizeof(message));

	return 1;
}

#endif


#ifdef CORE_CM4

int RPC::begin() {

  eventThread = new rtos::Thread(osPriorityHigh);
  eventThread->start(&eventHandler);

  dispatcherThread = new rtos::Thread(osPriorityNormal);
  dispatcherThread->start(mbed::callback(this, &RPC::dispatch));

  responseThread = new rtos::Thread(osPriorityNormal);
  responseThread->start(mbed::callback(this, &RPC::response));

  /* Initialize OpenAmp and libmetal libraries */
  if (MX_OPENAMP_Init(RPMSG_REMOTE, NULL) !=  0) {
    return 0;
  }

  rp_endpoints[ENDPOINT_RAW].priv = this;
  rp_endpoints[ENDPOINT_RESPONSE].priv = this;

  /* create a endpoint for raw rmpsg communication */
  int status = OPENAMP_create_endpoint(&rp_endpoints[ENDPOINT_RAW], "raw", RPMSG_ADDR_ANY,
                                   rpmsg_recv_callback, NULL);
  if (status < 0) {
    return 0;
  }

  status = OPENAMP_create_endpoint(&rp_endpoints[ENDPOINT_RESPONSE], "response", RPMSG_ADDR_ANY,
                                   rpmsg_recv_response_callback, NULL);
  if (status < 0) {
    return 0;
  }

  return 1;
}

#endif

using raw_call_t = std::tuple<RPCLIB_MSGPACK::object>;

void RPC::response() {
  responseThreadId = osThreadGetId();

  for (int i = 0; i< 10; i++) {
    clients[i] = NULL;
  }

  while (true) {
    osEvent v = osSignalWait(0, osWaitForever);

{
      RPCLIB_MSGPACK::unpacker pac;
      memcpy(pac.buffer(), intermediate_buffer_resp, v.value.signals);
      pac.buffer_consumed(v.value.signals);

      for (int i = 0; i< v.value.signals; i++) {
        printf("%02x ", intermediate_buffer_resp[i]);
      }
      printf("\n");

      RPCLIB_MSGPACK::unpacked result;
      while (pac.next(result)) {
        printf("result is ok\n");
        auto r = rpc::detail::response(std::move(result));
        printf("response is ok\n");
        auto id = r.get_id();
        printf("get_id is ok\n");
        // fill the correct client stuff
        int i;
        for (i = 0; i<10; i++) {
          printf("finding thread\n");
          if (clients[i] != NULL) {
            if ((uint)clients[i]->callThreadId == id) {
              printf("id: %x id: %x\n", clients[i]->callThreadId, id);
              break;
            }
          }
        }
        if (i == 10) {
          continue;
        }
        clients[i]->result = std::move(*r.get_result());
        printf("unlocking caller thread\n");
        // Unlock callThreadId thread
        osSignalSet(clients[i]->callThreadId, 0x1);
      }
    }
  }
}

void RPC::dispatch() {

  dispatcherThreadId = osThreadGetId();

  while (true) {
    osEvent v = osSignalWait(0, osWaitForever);

{
    RPCLIB_MSGPACK::unpacker pac;
    memcpy(pac.buffer(), intermediate_buffer, v.value.signals);
    pac.buffer_consumed(v.value.signals);

    RPCLIB_MSGPACK::unpacked result;
    while (pac.next(result)) {
      auto msg = result.get();
      if (msg.via.array.size == 1) {
        // raw array
        raw_call_t arr;
        msg.convert(arr);

        std::vector<uint8_t> buf;
        std::get<0>(arr).convert(buf);

        for (size_t i = 0; i < buf.size(); i++) {
          rx_buffer.store_char(buf[i]);
        }
        // call attached function
        if (_rx) {
          _rx.call();
        }
      }

      if (msg.via.array.size > 2) {
        auto resp = rpc::detail::dispatcher::dispatch(msg, true);
        auto data = resp.get_data();
        if (resp.is_empty()) {
          //printf("no response\n");
        } else {
          OPENAMP_send(&rp_endpoints[ENDPOINT_RESPONSE], (const uint8_t*)data.data(), data.size());
        }
      }
    }
  }
}
}


size_t RPC::write(uint8_t c) {
  write(&c, 1);
  return 1;
}

size_t RPC::write(const uint8_t* buf, size_t len) {
  return write(ENDPOINT_RAW, buf, len);
}

size_t RPC::write(uint8_t ep, const uint8_t* buf, size_t len) {

  std::vector<uint8_t> tx_buffer;
  for (size_t i = 0; i < len; i++) {
    tx_buffer.push_back(buf[i]);
  }
  auto call_obj = std::make_tuple(tx_buffer);

  auto buffer = new RPCLIB_MSGPACK::sbuffer;
  RPCLIB_MSGPACK::pack(*buffer, call_obj);

  OPENAMP_send(&rp_endpoints[ep], (const uint8_t*)buffer->data(), buffer->size());
  delete buffer;
  return len;
}

arduino::RPC RPC1;