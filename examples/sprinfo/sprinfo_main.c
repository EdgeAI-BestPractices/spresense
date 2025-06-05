#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spresense_version.h"

int main(int argc, FAR char *argv[])
{
  char json_output[128];
  if (argc > 1) {
    snprintf(json_output, sizeof(json_output),
             "{\"status\":{\"code\":-1,\"msg\":\"Unknown option: %s\"}}",
             argv[1]);
    printf("%s\n", json_output);
    return 1;
  }

  snprintf(json_output, sizeof(json_output),
           "{\"status\":{\"code\":0,\"msg\":\"Success\"},\"data\":{\"spresense\":\"%s\",\"bootloader\":\"%s\"}}",
           SPRESENSE_VERSION, BOOTLOADER_VERSION);

  printf("%s\n", json_output);
  return 0;
}
