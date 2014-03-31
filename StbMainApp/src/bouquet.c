#include "bouquet.h"

#include "dvbChannel.h"
#include "list.h"
#include "debug.h"
#include "off_air.h"

#define BOUGET_NAME                     "bouquets"
#define BOUGET_SERVICES_FILENAME_TV     CONFIG_DIR "/" BOUGET_NAME ".tv"
#define BOUGET_SERVICES_FILENAME_RADIO  CONFIG_DIR "/" BOUGET_NAME ".radio"
#define BOUGET_LAMEDB_FILENAME          CONFIG_DIR "/" "lamedb"

#define BOUGET_NAME_SIZE                64
#define CHANNEL_BUFFER_NAME             64

typedef enum
{
    None = 0,
    TV,
    Radio,
} bouquet_type;

typedef struct _transpounder_id_t
{
    uint32_t ts_id;         //Transpounder_ID
    uint32_t n_id;          //Network_ID
    uint32_t name_space;    //Namespace(media_ID -?)
} transpounder_id_t;

typedef struct _transpounder_t
{
    transpounder_id_t transpounder_id;
    char type;                      // dvb-c/t/
    uint32_t freq;                  //Frequency
    uint32_t sym_rate;              //symbol rate
    uint32_t mod;                   //Modulation
} transpounder_t;

typedef struct _services_t
{
    uint32_t s_id;                   //Services_ID
    transpounder_id_t transpounder_id;
    char channel_name[CHANNEL_BUFFER_NAME]; //channels_name
    char provider_name[CHANNEL_BUFFER_NAME]; //provider_name
    uint32_t v_pid;                 //video pid
    uint32_t a_pid;                 //audio pid
    uint32_t t_pid;                 //teletext pid
    uint32_t p_pid;                 //PCR
    uint32_t ac_pid;                //AC3
    uint32_t f;                     //f - ?

} services_t;

list_element_t *full_bouquet_list = NULL;
list_element_t *head_ts_list = NULL;
list_element_t *head_services_list = NULL;

char bouquet_name_tv[BOUGET_NAME_SIZE];
//int number_of_bouquets_tv = 0;
char bouquet_name_radio[BOUGET_NAME_SIZE];
//int number_of_bouquets_radio = 0;


void get_bouquets_file_name(char *bouquet_name,char *bouquet_file){
    char buf[BUFFER_SIZE];
    FILE* fd;
    memset(bouquet_name,'\0',sizeof(bouquet_name));

    fd = fopen(bouquet_file, "r");
    if(fd == NULL) {
        dprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
        return;
    }
    // check head file name
    while ( fgets(buf, BUFFER_SIZE, fd) != NULL ){
        if ( strncasecmp(buf, "#SERVICE", 8) !=0 )
            continue;
    char * ptr;
        int    ch = '"';
        ptr = strchr( buf, ch ) + 1;
        sscanf(ptr,"%s \n",bouquet_name); //get bouquet_name type: name" (with ")
        bouquet_name[strlen(bouquet_name) - 1] = '\0'; //get bouquet_name type: name
        dprintf("Get bouquet file name: %s\n", bouquet_name);
        break;
    }
        fclose(fd);
}


void get_bouquets_list(char *bouquet_file){
    printf("%s[%d]\n",__func__,__LINE__);
    char buf[BUFFER_SIZE];
    FILE* fd;

    char path[BOUGET_NAME_SIZE * 2];
    sprintf(path, "%s/%s",CONFIG_DIR, bouquet_file);


    fd = fopen(path, "r");
    if(fd == NULL) {
        dprintf("%s: Failed to open '%s'\n", __FUNCTION__, path);
        return;
    }
    /*
     #NAME Blizoo
     #SERVICE 1:0:1:1:1:C8:FFFF0000:0:0:0:
     ---      1 2 3 4 5 6   7       8 9 10
     */
    while(fgets(buf, BUFFER_SIZE, fd) != NULL) {
        printf("%s[%d]\n",__func__,__LINE__);
        uint32_t index_1 = 0;
        uint32_t index_2 = 0;
        uint32_t index_3 = 0;
        uint32_t service_id = 0; //4
        uint32_t media_id = 0;
        uint32_t transport_stream_id = 0; //5
        uint32_t network_id = 0;    //6
        uint32_t index_8 = 0;
        uint32_t index_9 = 0;
        uint32_t index_10 = 0;

      if ( sscanf(buf, "#SERVICE %x:%x:%x:%x:%x:%x:%x:%x:%x:%x:\n",&index_1, &index_2, &index_3, &service_id, &transport_stream_id, &network_id, &media_id, &index_8, &index_9, &index_10) != 10)
          continue;
         //   printf("#SERVICE %x:%x:%x:%x:%x:%x:%x:%x:%x:%x:\n",index_1, index_2, index_3, service_id, transport_stream_id, network_id, media_id, index_8, index_9, index_10);
              EIT_common_t common;

              common.media_id = network_id;
              common.service_id = service_id;
              common.transport_stream_id = transport_stream_id;
              dvbChannel_addCommon(&common, 0);
              /*
               *get add servise/// not done
               *
               */

      }
    fclose(fd);
}

void  bouquet_dump(char *filename){
    dump_services(full_bouquet_list, filename );
}

EIT_service_t *bouquet_findService(EIT_common_t *header) {
    printf("%s[%d]\n",__func__, __LINE__);
    list_element_t *cur_element;
    EIT_service_t *element;

   for( cur_element = full_bouquet_list; cur_element != NULL; cur_element = cur_element->next ){
        element = (EIT_service_t*)cur_element->data;

        if(element == NULL) {
            continue;
        }

        if(memcmp(&(element->common), header, sizeof(EIT_common_t)) == 0) {
            return element;
        }

    }
   return NULL;
}

void get_transponder_data(){
   // printf("%s[%d]\n",__func__, __LINE__);
    list_element_t *cur_element;
    EIT_service_t *element;

    uint32_t old_count;
    old_count = dvbChannel_getCount();




    struct list_head *pos;

    extern dvb_channels_t g_dvb_channels;

   list_for_each(pos, &g_dvb_channels.orderNoneHead) {
        service_index_t *srv = list_entry(pos, service_index_t, orderNone);
        // printf("%s[%d]\n",__func__, __LINE__);
        //printf("%d  ==  %d\n",srv->common.transport_stream_id , header->transpounder_id.ts_id);
       element = bouquet_findService(&srv->common);
       if (element != NULL) {
              srv->service = element;
       } else {
       }

/*        if( srv->common.transport_stream_id == header->transpounder_id.ts_id){
        //if(memcmp(&(srv->common), header, sizeof(EIT_common_t)) == 0) {
            printf("%s[%d]\n",__func__, __LINE__);
            return srv;
        }
*/
       if ( srv->service != NULL)
           printf("add\n");
    }
    return;

}


/*





    for( cur_element = full_bouquet_list; cur_element != NULL; cur_element = cur_element->next ){
        service_index_t *p_srvIdx;
        element = (EIT_service_t*)cur_element->data;

        if(element == NULL) {
            continue;
        }

        p_srvIdx = bouquet_findService(element, old_count);
        if(p_srvIdx) {
            if ( element->type == 'c' )
                //printf("%d = \n",&p_srvIdx->service->media.dvb_c.frequency);
                &p_srvIdx->service->media.dvb_c.frequency == element->freq;
                &p_srvIdx->service->media.dvb_c.modulation == element->mod;
                &p_srvIdx->service->media.dvb_c.symbol_rate == element->sym_rate;

            /*curService->media.dvb_c.frequency == element->freq;
                curService->media.dvb_c.modulation == element->mod;
                curService->media.dvb_c.symbol_rate == element->sym_rate;
*/
      //          p_srvIdx->service == curService;
                        //->media.dvb_c.frequency = element->freq;*/
/*
        } else  {
            //not find transponder data
        }

    }
/*
            printf("type = %c\n", ts_element->type);
            printf("freq = %d\n", ts_element->freq);
            printf("sym_rate = %d\n", ts_element->sym_rate);
            printf("mod = %d\n", ts_element->mod);


*/

/*

    for(service_element = dvb_services; service_element != NULL; service_element = service_element->next) {
        service_index_t *p_srvIdx;
        EIT_service_t *curService = (EIT_service_t *)service_element->data;

        if(curService == NULL) {
            continue;
        }
        printf("%s[%d]\n",__func__, __LINE__);
        p_srvIdx = dvbChannel_findServiceLimit(&curService->common, old_count);
        if(p_srvIdx) {
            p_srvIdx->service = curService;
        } else {
/*			if( dvb_hasMedia(curService) &&
                (appControlInfo.offairInfo.dvbShowScrambled || (dvb_getScrambled(curService) == 0))
            )*/
 /*           if(dvb_hasMedia(curService)) {
                //dvbChannel_addService(curService);
            }
        }
    }

*/

//}

void get_servises_data(){


}

void bouquets_free_serveces()
{
    free_elements(&full_bouquet_list);
}
int bouquets_compare(list_element_t **services){
    uint32_t old_count;
    list_element_t	*service_element;
    old_count = dvbChannel_getCount();

    printf("%d = %d \n", old_count, dvb_getNumber_Services());


    if ( old_count !=  dvb_getNumber_Services() )
        return false;


    for(service_element = *services; service_element != NULL; service_element = service_element->next) {
        EIT_service_t *curService = (EIT_service_t *)service_element->data;

        printf("element->common.transport_stream_id = %d \n",curService->common.transport_stream_id);
        printf("element->common.media_id = %d \n",curService->common.media_id);
        printf("element->common.original_network_id = %d \n",curService->original_network_id);
        printf("element->common.service_id = %d \n",curService->common.service_id);

        if(curService == NULL) {
            continue;
        }


        if (dvbChannel_findServiceLimit(&curService->common, old_count) == NULL) {
            return false;
        };
    }
    return true;
}

void load_bouquets() {
    get_bouquets_file_name(bouquet_name_tv, BOUGET_SERVICES_FILENAME_TV);
    get_bouquets_file_name(bouquet_name_radio, BOUGET_SERVICES_FILENAME_RADIO);
    get_bouquets_list(bouquet_name_tv);
 //   get_bouquets_list(bouquet_name_radio);
  //  load_lamedb();
}

void get_bouquet(int n, char *name) {


}

list_element_t *get_bouquet_list(){
    return full_bouquet_list;
}

void load_lamedb(list_element_t **services) {
    dprintf("Load services list from lamedb\n");

    char buf[BUFFER_SIZE];
    FILE* fd;
    fd = fopen(BOUGET_LAMEDB_FILENAME, "r");
    if(fd == NULL) {
        dprintf("%s: Failed to open '%s'\n", __FUNCTION__, BOUGET_LAMEDB_FILENAME);
        return;
    }
    do {
        if (fgets(buf, BUFFER_SIZE, fd) == NULL)
            break;
        //parse head file name
        // ---
        // ---
        // ---
        if (fgets(buf, BUFFER_SIZE, fd) == NULL)
            break;
        if (strncasecmp(buf, "transponders", 12) != 0)
            break;
        // start parse transponders list

        if (fgets(buf, BUFFER_SIZE, fd) == NULL)
            break;
        do{
            list_element_t *cur_element;
            transpounder_t *element;
            uint32_t index_1 = 0;
            uint32_t index_2 = 0;
            uint32_t index_3 = 0;
            uint32_t index_4 = 0;

            //printf("%s[%d]\n",__func__, __LINE__);

            if (head_ts_list == NULL)
            {
                cur_element = head_ts_list = allocate_element(sizeof(transpounder_t));
            } else
            {
                cur_element = append_new_element(head_ts_list, sizeof(transpounder_t));
            }
            if ( !cur_element )
                break;

            element = (transpounder_t *)cur_element->data;

            if (sscanf(buf, "%x:%x:%x\n",&element->transpounder_id.name_space, &element->transpounder_id.ts_id, &element->transpounder_id.n_id) != 3)
                break;
            if (fgets(buf, BUFFER_SIZE, fd) == NULL)
                break;
            if (sscanf(buf, " %c %d:%d:%d:%d:%d:%d:%d\n",&element->type, &element->freq, &element->sym_rate, &index_1, &element->mod, &index_2, &index_3,&index_4) != 8)
                break;
            if (fgets(buf, BUFFER_SIZE, fd) == NULL)
                break;
            if (strncasecmp(buf, "/", 1) != 0 )
                break;
            if ( fgets(buf, BUFFER_SIZE, fd) == NULL )
                break;
        }while (strncasecmp(buf, "end", 3) != 0);
//////------------------------------------------------------------------------------////////////
        if (fgets(buf, BUFFER_SIZE, fd) == NULL)
            break;
        if (strncasecmp(buf, "services", 8) != 0 )
            break;
        if (fgets(buf, BUFFER_SIZE, fd) == NULL)
            break;
        do{

            list_element_t *cur_element = NULL;
            EIT_service_t *element;

            uint32_t service_id = 0;
            uint32_t name_space = 0;
            uint32_t transport_stream_id = 0;
            uint32_t original_network_id = 0;
            uint32_t index_1 = 0;
            uint32_t index_2 = 0;

            if ( sscanf(buf, "%x:%x:%x:%x:%x:%x\n",&service_id, &name_space, &transport_stream_id, &original_network_id, &index_1, &index_1) != 6)
                break;

            if (*services == NULL)
            {
                *services = cur_element = allocate_element(sizeof(EIT_service_t));
            } else
            {
                cur_element = append_new_element(*services, sizeof(EIT_service_t));
            }
            if ( !cur_element )
                break;
            element = (EIT_service_t *)cur_element->data;

            element->common.service_id = service_id;
            element->common.transport_stream_id = transport_stream_id;
            element->common.media_id  = original_network_id;
            element->original_network_id = original_network_id;


            if ( fgets(&element->service_descriptor.service_name, BUFFER_SIZE, fd) == NULL)
                break;
             element->service_descriptor.service_name[strlen(element->service_descriptor.service_name) - 1] = '\0';

             if ( fgets(buf, BUFFER_SIZE, fd) == NULL)
                break;
        /*    if (sscanf(buf, "p:%s,c:%x,c:%x,c:%x,c:%x,f:%d\n",&element->provider_name, &element->v_pid, &element->a_pid, &element->t_pid, &element->p_pid,&element->f) < 1)   //not done
                break;
          */
            ///////////////------------------------////////////

            list_element_t *cur_tr_element;
            transpounder_t *element_tr;
            for( cur_tr_element = head_ts_list; cur_tr_element != NULL; cur_tr_element = cur_tr_element->next ){
                element_tr = (transpounder_t*)cur_tr_element->data;

                if(element_tr == NULL) {
                    continue;
                }

                if ( name_space == element_tr->transpounder_id.name_space &&
                     element->common.transport_stream_id == element_tr->transpounder_id.ts_id &&
                     element->original_network_id == element_tr->transpounder_id.n_id ){
                    if (element_tr->type == 'c') {
                        element->media.type = serviceMediaDVBC;
                        element->media.dvb_c.frequency = element_tr->freq * 1000;
                        element->media.dvb_c.symbol_rate = element_tr->sym_rate;
                        element->media.dvb_c.modulation = element_tr->mod;
                        element->common.media_id = element->common.media_id;
                    }

                }

            }
           ///////////////------------------------////////////

            if ( fgets(buf, BUFFER_SIZE, fd) == NULL )
                break;
        }while (strncasecmp(buf, "end", 3) != 0);

    }while(0);



    free_elements(&head_ts_list);
    fclose(fd);
}

void update_bouquet() {

}

void update_lamedb() {

}

void add_dvb_audio_track() {

}

void add_setting_lamedb() {

}

void add_setting_channels() {

}

void get_PID() {

}

void save_bouquet() {

}

void save_lamedb() {

}
