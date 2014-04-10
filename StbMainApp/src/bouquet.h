#ifndef BOUQUET_H
#define BOUQUET_H

#include "interface.h"
#include "defines.h"
#include "dvbChannel.h"

#ifdef ENABLE_DVB
#define BOUQUET_FULL_LIST	"/var/etc/elecard/StbMainApp/"

void load_bouquets();
int bouquet_file();
void bouquet_downloadFileWithServices(char *filename);
void  bouquet_dump(char *filename);
list_element_t *get_bouquet_list();
void load_lamedb(list_element_t **);
void bouquets_free_serveces();
int bouquets_compare(list_element_t **);
EIT_service_t *bouquet_findService(EIT_common_t *header);

#endif // ENABLE_DVB
#endif // BOUQUET_H
