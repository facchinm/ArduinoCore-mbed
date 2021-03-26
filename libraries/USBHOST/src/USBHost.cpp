#include "USBHost.h"
#include "USB251xB.h"

static rtos::Thread t(osPriorityHigh);

void USBHost::supplyPowerOnVBUS(bool enable){
  mbed::DigitalOut otg(PJ_6, enable ? 0 : 1);
}

void USBHost::InternalTask() {
  while (1) {
    tusbh_msg_loop(mq);
  }
}

uint32_t USBHost::Init(uint8_t id, const tusbh_class_reg_t class_table[]) {

  mq = tusbh_mq_create();
  tusbh_mq_init(mq);

  if (id == USB_CORE_ID_FS) {
    host = tusb_get_host(USB_CORE_ID_FS);
    HOST_PORT_POWER_ON_FS();
    root.mq = mq;
    root.id = "FS";
    root.support_classes = class_table;
    tusb_host_init(host, &root);
    tusb_open_host(host);
    start_hub();
  }

  if (id == USB_CORE_ID_HS) {

    get_usb_phy()->deinit();
    mbed::DigitalOut otg(PJ_6, 1);

    host = tusb_get_host(USB_CORE_ID_HS);
    HOST_PORT_POWER_ON_HS();
    root.mq = mq;
    root.id = "HS";
    root.support_classes = class_table;
    tusb_host_init(host, &root);
    tusb_open_host(host);
  }

  t.start(mbed::callback(this, &USBHost::InternalTask));
}

std::vector<std::pair<uint16_t, uint16_t>> USBHost::lsusb() {
  std::vector<std::pair<uint16_t, uint16_t>> collection;
  tusbh_root_hub_t* root = (tusbh_root_hub_t*)host->user_data;
  TUSB_PRINTF("Device of %s root hub\n", root->id);
  for(int i=0;i<TUSHH_ROOT_CHILD_COUNT;i++){
    tusbh_device_t* dev = root->children[i];
    if(dev){
      std::pair<uint16_t, uint16_t> descriptor;
      descriptor.first = dev->device_desc.idVendor;
      descriptor.second = dev->device_desc.idProduct;
      collection.push_back(descriptor);
    }
  }
  return collection;
}

uint32_t USBHost::Task() {

}

static HardwareSerial* DebugSerial = NULL;

void USBHost::debug(HardwareSerial& s) {
  DebugSerial = &s;
}

extern "C" {
  // host need accurate delay
  void tusb_delay_ms(uint32_t ms)
  {
    delay(ms);
  }

  void tusb_printf(const char* fmt, ...) {
    if (DebugSerial != NULL) {
      char buffer[256];
      va_list args;
      va_start(args, fmt);
      vsnprintf(buffer, 256, fmt, args);
      DebugSerial->print(buffer);
      va_end(args);
    }
  }
}
