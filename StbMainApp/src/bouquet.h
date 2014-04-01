#ifndef BOUQUET_H
#define BOUQUET_H

#include "interface.h"
#include "defines.h"
#include "dvbChannel.h"

#ifdef ENABLE_DVB

void load_bouquets();
void  bouquet_dump(char *filename);
list_element_t *get_bouquet_list();
void load_lamedb(list_element_t **);
void bouquets_free_serveces();
int bouquets_compare(list_element_t **);
EIT_service_t *bouquet_findService(EIT_common_t *header);

#endif // ENABLE_DVB
#endif // BOUQUET_H
