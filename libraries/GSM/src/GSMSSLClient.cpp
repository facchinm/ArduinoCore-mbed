#include "GSMSSLClient.h"

arduino::GSMSSLClient::GSMSSLClient() {
  onBeforeConnect(mbed::callback(this, &GSMSSLClient::setRootCA));
};
