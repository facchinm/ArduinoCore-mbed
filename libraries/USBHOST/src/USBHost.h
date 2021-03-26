extern "C" {
#include "teeny_usb.h"
#include "class/host/tusbh.h"
#include "class/host/tusbh_vendor.h"
#include "class/host/tusbh_hub.h"
#include "class/host/tusbh_hid.h"
#include "class/host/tusbh_msc.h"
#include "class/host/tusbh_cdc_acm.h"
#include "class/host/tusbh_cdc_rndis.h"
#include "string.h"
#include "class/host/usb_key_code.h"
}

#include "mbed.h"
#include "usb_phy_api.h"

#include <vector>

class USBHost {
public:
	uint32_t Init(uint8_t id, const tusbh_class_reg_t class_table[]);
	uint32_t Task();
	void debug(HardwareSerial& s);
	std::vector<std::pair<uint16_t, uint16_t>> lsusb();

	/**
	 * Allows to provide power to USB devices on VBUS when powered through VIN.
	 * @param enable A flag to indicate whether power is supplied on VBUS.
	*/
	void supplyPowerOnVBUS(bool enable);

private:

	void InternalTask();
	tusbh_msg_q_t* mq;
	tusb_host_t* host;
	tusbh_root_hub_t root;
};