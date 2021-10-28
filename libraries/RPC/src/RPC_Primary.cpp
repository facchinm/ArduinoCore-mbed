#include "RPC_internal.h"

#ifdef CORE_CM7

static struct rpmsg_endpoint rp_endpoints[4];

enum endpoints_t {
  ENDPOINT_RPC_SERVER = 0,
  ENDPOINT_RPC_CLIENT,
  ENDPOINT_RAW
};

void rpc::client::post(RPCLIB_MSGPACK::sbuffer *buffer) {
  RPC1.write(ENDPOINT_RPC_SERVER, (const uint8_t*)buffer->data(), buffer->size());
}

int RPC::rpmsg_recv_rpc_callback(struct rpmsg_endpoint *ept, void *data,
                                       size_t len, uint32_t src, void *priv)
{
  RPC* rpc = (RPC*)priv;
  memcpy(rpc->pac_.buffer(), (const void*)data, len);
  rpc->pac_.buffer_consumed(len);

  if (strcmp(ept->name, "cm4_rpc_server") == 0) {
    osSignalSet(rpc->dispatcherThreadId, 0x2);
  } else {
    osSignalSet(rpc->dispatcherThreadId, 0x1);
  }

  return 0;
}

int RPC::rpmsg_recv_raw_callback(struct rpmsg_endpoint *ept, void *data,
                                       size_t len, uint32_t src, void *priv)
{
  RPC* rpc = (RPC*)priv;
  uint8_t* buf = (uint8_t*)data;
  for (int i=0; i<len; i++) {
    rpc->rx_buffer.store_char(buf[i]);
  }
  // call attached function
  if (rpc->_rx) {
    rpc->_rx.call();
  }

  return 0;
}

void RPC::new_service_cb(struct rpmsg_device *rdev, const char *name, uint32_t dest)
{
  int idx = -1;
  if (strcmp(name, "cm4_rpc_server") == 0) {
    OPENAMP_create_endpoint(&rp_endpoints[ENDPOINT_RPC_CLIENT], name, dest, rpmsg_recv_rpc_callback, NULL);
  }
  if (strcmp(name, "cm4_rpc_client") == 0) {
    OPENAMP_create_endpoint(&rp_endpoints[ENDPOINT_RPC_SERVER], name, dest, rpmsg_recv_rpc_callback, NULL);
  }
  if (strcmp(name, "raw") == 0) {
    OPENAMP_create_endpoint(&rp_endpoints[ENDPOINT_RAW], name, dest, rpmsg_recv_raw_callback, NULL);
  }
}

osThreadId eventHandlerThreadId;

void eventHandler() {
  eventHandlerThreadId = osThreadGetId();
  while (1) {
    osEvent v = osSignalWait(0, osWaitForever);
    OPENAMP_check_for_message();
  }
}

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
	MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
	MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
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
	bootM4();

  pac_.reserve_buffer(1024);

	eventThread = new rtos::Thread(osPriorityHigh);
	eventThread->start(&eventHandler);

	/* Initialize OpenAmp and libmetal libraries */
	if (MX_OPENAMP_Init(RPMSG_MASTER, new_service_cb) !=  HAL_OK) {
	printf("openAMP init failed\n\rNo RPC is available\n\r");
	return 0;
	}

	/* Initialize the rpmsg endpoint to set default addresses to RPMSG_ADDR_ANY */
	rpmsg_init_ept(&rp_endpoints[ENDPOINT_RPC_SERVER], "cm4_rpc_client", RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, NULL, NULL);

	rpmsg_init_ept(&rp_endpoints[ENDPOINT_RPC_CLIENT], "cm4_rpc_server", RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, NULL, NULL);

	rpmsg_init_ept(&rp_endpoints[ENDPOINT_RAW], "raw", RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, NULL, NULL);

	rp_endpoints[ENDPOINT_RPC_SERVER].priv = this;
	rp_endpoints[ENDPOINT_RPC_CLIENT].priv = this;
	rp_endpoints[ENDPOINT_RAW].priv = this;

	/*
	* The rpmsg service is initiate by the remote processor, on H7 new_service_cb
	* callback is received on service creation. Wait for the callback
	*/
	OPENAMP_Wait_EndPointready(&rp_endpoints[ENDPOINT_RPC_SERVER], HAL_GetTick() + 500);
	OPENAMP_Wait_EndPointready(&rp_endpoints[ENDPOINT_RPC_CLIENT], HAL_GetTick() + 500);
	OPENAMP_Wait_EndPointready(&rp_endpoints[ENDPOINT_RAW], HAL_GetTick() + 500);

	// Send first dummy message to enable the channel
	uint8_t message = 0x00;
	OPENAMP_send(&rp_endpoints[ENDPOINT_RPC_SERVER], &message, sizeof(message));
	OPENAMP_send(&rp_endpoints[ENDPOINT_RPC_CLIENT], &message, sizeof(message));
	OPENAMP_send(&rp_endpoints[ENDPOINT_RAW], &message, sizeof(message));

	dispatcherThread = new rtos::Thread(osPriorityNormal);
	dispatcherThread->start(mbed::callback(this, &RPC::dispatch));

	initialized = true;
	return 1;
}

void RPC::dispatch() {

  dispatcherThreadId = osThreadGetId();

  while (true) {
    osEvent v = osSignalWait(0, osWaitForever);

    if (v.status == osEventSignal) {
       if (v.value.signals & 0x1) {
        RPCLIB_MSGPACK::unpacked result;
        while (pac_.next(result)) {
          auto msg = result.get();
          auto resp = rpc::detail::dispatcher::dispatch(msg, true);
          auto data = resp.get_data();
          if (resp.is_empty()) {
            //printf("no response\n");
          } else {
            write(ENDPOINT_RPC_SERVER, (const uint8_t*)data.data(), data.size());
          }
        }
      }
      if (v.value.signals & 0x2) {
        RPCLIB_MSGPACK::unpacked result;
        while (pac_.next(result)) {
          auto r = rpc::detail::response(std::move(result));
          auto id = r.get_id();
          // fill the correct client stuff
          int i = 0;
          for (i = 0; i<10; i++) {
            if (clients[i] != NULL && (int)clients[i]->callThreadId == id) {
              break;
            }
          }
          clients[i]->result = std::move(*r.get_result());
          // Unlock callThreadId thread
          osSignalSet(clients[i]->callThreadId, 0x1);
        }
      }
    }
  }
}

size_t RPC::write(const uint8_t* buf, size_t len) {
  OPENAMP_send(&rp_endpoints[ENDPOINT_RAW], buf, len);
  return len;
}

size_t RPC::write(uint8_t c) {
  OPENAMP_send(&rp_endpoints[ENDPOINT_RAW], &c, 1);
  return 1;
}

size_t RPC::write(uint8_t ep, const uint8_t* buf, size_t len) {
  int ret = OPENAMP_send(&rp_endpoints[ep], buf, len);
  return len;
}

arduino::RPC RPC1;

#endif