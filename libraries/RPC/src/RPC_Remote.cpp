#include "RPC_internal.h"

#ifdef CORE_CM4

static struct rpmsg_endpoint rp_endpoints[4];

enum endpoints_t {
  ENDPOINT_RPC_CLIENT = 0,
  ENDPOINT_RPC_SERVER,
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
    osSignalSet(rpc->dispatcherThreadId, 0x1);
  } else {
    osSignalSet(rpc->dispatcherThreadId, 0x2);
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

osThreadId eventHandlerThreadId;

void eventHandler() {
  eventHandlerThreadId = osThreadGetId();
  while (1) {
    osEvent v = osSignalWait(0, osWaitForever);
    delay(1);
    OPENAMP_check_for_message();
  }
}

int RPC::begin() {

  pac_.reserve_buffer(1024);

  eventThread = new rtos::Thread(osPriorityHigh);
  eventThread->start(&eventHandler);

  /* Inilitize OpenAmp and libmetal libraries */
  if (MX_OPENAMP_Init(RPMSG_REMOTE, NULL) !=  0) {
    return 0;
  }

  rp_endpoints[0].priv = this;
  rp_endpoints[1].priv = this;
  rp_endpoints[2].priv = this;

  /* create a endpoint for rmpsg communication */
  int status = OPENAMP_create_endpoint(&rp_endpoints[ENDPOINT_RPC_CLIENT], "cm4_rpc_client", RPMSG_ADDR_ANY,
                                   rpmsg_recv_rpc_callback, NULL);
  if (status < 0)
  {
    return 0;
  }

  status = OPENAMP_create_endpoint(&rp_endpoints[ENDPOINT_RPC_SERVER], "cm4_rpc_server", RPMSG_ADDR_ANY,
                                   rpmsg_recv_rpc_callback, NULL);
  if (status < 0)
  {
    return 0;
  }

  /* create a endpoint for raw rmpsg communication */
  status = OPENAMP_create_endpoint(&rp_endpoints[ENDPOINT_RAW], "raw", RPMSG_ADDR_ANY,
                                   rpmsg_recv_raw_callback, NULL);
  if (status < 0)
  {
    return 0;
  }

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