#include "RPC_internal.h"

#ifdef CORE_CM4

static struct rpmsg_endpoint rp_endpoints[4];

enum endpoints_t {
  ENDPOINT_RAW = 0
};

void rpc::client::send_msgpack(RPCLIB_MSGPACK::sbuffer *buffer) {
  OPENAMP_send(&rp_endpoints[ENDPOINT_RAW], (const uint8_t*)buffer->data(), buffer->size());
}

int RPC::rpmsg_recv_callback(struct rpmsg_endpoint *ept, void *data,
                                       size_t len, uint32_t src, void *priv)
{
  RPC* rpc = (RPC*)priv;
  memcpy(rpc->pac_.buffer(), (const void*)data, len);
  rpc->pac_.buffer_consumed(len);

/*
  printf("remote: got message: ");
  for (int i = 0; i < len; i++) {
    printf("%02x ", ((uint8_t*)data)[i]);
  }
  printf("\n");
*/

  osSignalSet(rpc->dispatcherThreadId, 0x1);

  return 0;
}

osThreadId eventHandlerThreadId;

void eventHandler() {
  eventHandlerThreadId = osThreadGetId();
  while (1) {
    osEvent v = osSignalWait(0, osWaitForever);
    //delay(50);
    OPENAMP_check_for_message();
  }
}

int RPC::begin() {

  pac_.reserve_buffer(1024);

  eventThread = new rtos::Thread(osPriorityHigh);
  eventThread->start(&eventHandler);

  dispatcherThread = new rtos::Thread(osPriorityNormal);
  dispatcherThread->start(mbed::callback(this, &RPC::dispatch));

  /* Initialize OpenAmp and libmetal libraries */
  if (MX_OPENAMP_Init(RPMSG_REMOTE, NULL) !=  0) {
    return 0;
  }

  rp_endpoints[0].priv = this;

  /* create a endpoint for raw rmpsg communication */
  int status = OPENAMP_create_endpoint(&rp_endpoints[ENDPOINT_RAW], "raw", RPMSG_ADDR_ANY,
                                   rpmsg_recv_callback, NULL);
  if (status < 0)
  {
    return 0;
  }

  initialized = true;

  return 1;
}

using raw_call_t = std::tuple<RPCLIB_MSGPACK::object>;

void RPC::dispatch() {

  dispatcherThreadId = osThreadGetId();

  while (true) {
    osEvent v = osSignalWait(0, osWaitForever);

    RPCLIB_MSGPACK::unpacked result;
    while (pac_.next(result)) {
      auto msg = result.get();

      if (msg.via.array.size == 1) {

        // raw array
        raw_call_t arr;
        msg.convert(arr);

        std::vector<uint8_t> buf;
        std::get<0>(arr).convert(buf);

        for (int i=0; i < buf.size(); i++) {
          rx_buffer.store_char(buf[i]);
        }
        // call attached function
        if (_rx) {
          _rx.call();
        }
      }

      if (msg.via.array.size == 2) {
        // response
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

      if (msg.via.array.size > 2) {
        auto resp = rpc::detail::dispatcher::dispatch(msg, true);
        auto data = resp.get_data();
        if (resp.is_empty()) {
          //printf("no response\n");
        } else {
          OPENAMP_send(&rp_endpoints[ENDPOINT_RAW], (const uint8_t*)data.data(), data.size());
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

  std::vector<uint8_t> tx_buffer;
  for (int i = 0; i < len; i++) {
    tx_buffer.push_back(buf[i]);
  }
  auto call_obj = std::make_tuple(tx_buffer);

  auto buffer = new RPCLIB_MSGPACK::sbuffer;
  RPCLIB_MSGPACK::pack(*buffer, call_obj);

  OPENAMP_send(&rp_endpoints[ENDPOINT_RAW], (const uint8_t*)buffer->data(), buffer->size());
  delete buffer;
  return len;
}

arduino::RPC RPC1;

#endif
