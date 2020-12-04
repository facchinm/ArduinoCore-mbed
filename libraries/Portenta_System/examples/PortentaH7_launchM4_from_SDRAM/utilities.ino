#include "QSPIFBlockDevice.h"
#include "FATFileSystem.h"

static QSPIFBlockDevice bd(PD_11, PD_12, PF_7, PD_13,  PF_10, PG_6, QSPIF_POLARITY_MODE_1, 40000000);
void USBMSD::begin()
{
  int err = getFileSystem().mount(&bd);
  if (err) {
    err = getFileSystem().reformat(&bd);
  }
}

mbed::FATFileSystem& USBMSD::getFileSystem()
{
  static mbed::FATFileSystem fs("fs");
  return fs;
}
USBMSD MassStorage(&bd);

// Mass storage in Portenta is flaxible so we don't provide a singleton
void enableMassStorage() {
  MassStorage.begin();
}

long getFileSize(FILE *fp) {
  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  return size;
}
