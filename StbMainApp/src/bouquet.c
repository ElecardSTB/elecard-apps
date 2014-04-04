#include "bouquet.h"

#include "dvbChannel.h"
#include "list.h"
#include "debug.h"
#include "off_air.h"

#ifdef ENABLE_DVB
#define BOUGET_CONFIG_DIR               "/var/etc/elecard/StbMainApp/bouquet"
#define BOUGET_NAME                     "bouquets"
#define BOUGET_SERVICES_FILENAME_TV     BOUGET_CONFIG_DIR "/" BOUGET_NAME ".tv"
#define BOUGET_SERVICES_FILENAME_RADIO  BOUGET_CONFIG_DIR "/" BOUGET_NAME ".radio"
#define BOUGET_LAMEDB_FILENAME          BOUGET_CONFIG_DIR "/" "lamedb"

#define BOUGET_NAME_SIZE                64
#define CHANNEL_BUFFER_NAME             64
#define MAX_TEXT	512

#define GARB_DIR					"/var/etc/garb"
#define GARB_CONFIG_JSON			GARB_DIR "/config.json"
#define GARB_CONFIG			GARB_DIR "/config"

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

list_element_t *head_ts_list = NULL;
list_element_t *bouquet_name_tv;
list_element_t *bouquet_name_radio;

list_element_t *list_getElement(int count, list_element_t **head)
{
	list_element_t *cur_element;
	int cur_count = 0;

    for(cur_element = *head; cur_element != NULL; cur_element = cur_element->next) {
		cur_count++;
		if (cur_count == count)
			return cur_element;
	}
	return NULL;
}

void get_bouquets_file_name(list_element_t **bouquet_name,char *bouquet_file)
{
	char buf[BUFFER_SIZE];
    FILE* fd;
    list_element_t *cur_element;
	char *element_data;
	free_elements(&*bouquet_name);

    fd = fopen(bouquet_file, "r");
    if(fd == NULL) {
		eprintf("%s: Failed to open '%s'\n", __FUNCTION__, bouquet_file);
        return;
    }
    // check head file name
    while ( fgets(buf, BUFFER_SIZE, fd) != NULL ) {
        if ( strncasecmp(buf, "#SERVICE", 8) !=0 )
            continue;

        if (*bouquet_name == NULL) {
            cur_element = *bouquet_name = allocate_element(BOUGET_NAME_SIZE);
        } else {
            cur_element = append_new_element(*bouquet_name, BOUGET_NAME_SIZE);
        }
        if (!cur_element)
            break;

        element_data = (char *)cur_element->data;

		char * ptr;
        int    ch = '"';
        ptr = strchr( buf, ch ) + 1;
        sscanf(ptr,"%s \n",element_data); //get bouquet_name type: name" (with ")
        element_data[strlen(element_data) - 1] = '\0'; //get bouquet_name type: name
        dprintf("Get bouquet file name: %s\n", element_data);
    }
    fclose(fd);
}


void get_bouquets_list(char *bouquet_file)
{
    char buf[BUFFER_SIZE];
    char path[BOUGET_NAME_SIZE * 2];
    FILE* fd;
    uint32_t count;
    uint32_t type;
    uint32_t flags;
    uint32_t serviceType;
    uint32_t service_id;
    uint32_t transport_stream_id;
    uint32_t network_id;
    uint32_t name_space;
    uint32_t index_8;
    uint32_t index_9;
    uint32_t index_10;

    sprintf(path, "%s/%s",BOUGET_CONFIG_DIR, bouquet_file);
    fd = fopen(path, "r");
    if(fd == NULL) {
        eprintf("%s: Failed to open '%s'\n", __FUNCTION__, path);
        return;
    }
    count = 0;
    while(fgets(buf, BUFFER_SIZE, fd) != NULL) {
        if ( sscanf(buf, "#SERVICE %x:%x:%x:%04x:%04x:%x:%x:%x:%x:%x:\n",   &type,
                                                                            &flags,
                                                                            &serviceType,
                                                                            &service_id,
                                                                            &transport_stream_id,
                                                                            &network_id,
                                                                            &name_space,
                                                                            &index_8,
                                                                            &index_9,
                                                                            &index_10) != 10) {
            continue;
        }
        bouquet_data_t bouquet_data;
        EIT_common_t	common;
        bouquet_data.serviceType = serviceType;
        bouquet_data.network_id = network_id;
        bouquet_data.channel_number = count;
        count++;
        common.service_id = service_id;
        common.transport_stream_id = transport_stream_id;
        common.media_id = network_id;
        dvbChannel_addBouquetData(&common, &bouquet_data);
    }
    fclose(fd);
}

int bouquets_compare(list_element_t **services)
{
    uint32_t old_count;
    list_element_t	*service_element;
    old_count = dvbChannel_getCount();

    if (old_count != (uint32_t)dvb_getCountOfServices())
        return false;

    for(service_element = *services; service_element != NULL; service_element = service_element->next) {
        EIT_service_t *curService = (EIT_service_t *)service_element->data;
        if (dvbChannel_findServiceLimit(&curService->common, old_count) == NULL)
            return false;
    }
    return true;
}

void load_bouquets()
{
    get_bouquets_file_name(&bouquet_name_tv, BOUGET_SERVICES_FILENAME_TV);
    get_bouquets_file_name(&bouquet_name_radio, BOUGET_SERVICES_FILENAME_RADIO);
    list_element_t *NameElement;
    NameElement = list_getElement(1, &bouquet_name_tv); //get first element TV
    if (NameElement != NULL)
        get_bouquets_list((char *)NameElement->data);
}

void load_lamedb(list_element_t **services)
{
    dprintf("Load services list from lamedb\n");
    char buf[BUFFER_SIZE];
    FILE* fd;
    fd = fopen(BOUGET_LAMEDB_FILENAME, "r");
    if(fd == NULL) {
        eprintf("%s: Failed to open '%s'\n", __FUNCTION__, BOUGET_LAMEDB_FILENAME);
        return;
    }
    do {
        if (fgets(buf, BUFFER_SIZE, fd) == NULL)
            break;
        //parse head file name
        /*
         *  not done
         */
        if (fgets(buf, BUFFER_SIZE, fd) == NULL)
            break;
        if (strncasecmp(buf, "transponders", 12) != 0)
            break;
        //------ start parse transponders list -----//
        if (fgets(buf, BUFFER_SIZE, fd) == NULL)
            break;
        do {
            uint32_t name_space;
            uint32_t ts_id;
            uint32_t n_id;
            if (sscanf(buf, "%x:%x:%x\n",&name_space, &ts_id, &n_id) != 3)
                break;

            list_element_t *cur_element;
            transpounder_t *element;

            if (head_ts_list == NULL) {
                cur_element = head_ts_list = allocate_element(sizeof(transpounder_t));
            } else {
                cur_element = append_new_element(head_ts_list, sizeof(transpounder_t));
            }
            if (!cur_element)
                break;

            element = (transpounder_t *)cur_element->data;
            element->transpounder_id.name_space = name_space;
            element->transpounder_id.ts_id = ts_id;
            element->transpounder_id.n_id = n_id;

            char type;
            uint32_t freq;
            uint32_t sym_rate;
            uint32_t inversion; //0 - auto, 1 - on, 2 - off
            uint32_t mod;
            uint32_t fec_inner;
            uint32_t flag;
            uint32_t system; // 0 - DVB-C, 1 DVB-C ANNEX C

            if (fgets(buf, BUFFER_SIZE, fd) == NULL)
                break;
            if (sscanf(buf, " %c %d:%d:%d:%d:%d:%d:%d\n",&type, &freq, &sym_rate, &inversion, &mod, &fec_inner, &flag, &system) != 8)
                break;
            element->type = type;
            element->freq = freq;
            element->sym_rate = sym_rate;
            element->mod = mod;

            if (fgets(buf, BUFFER_SIZE, fd) == NULL)
                break;
            if (strncasecmp(buf, "/", 1) != 0 )
                break;
            if (fgets(buf, BUFFER_SIZE, fd) == NULL)
                break;
        } while (strncasecmp(buf, "end", 3) != 0);
        //------ start parse services list -----//
        if (fgets(buf, BUFFER_SIZE, fd) == NULL)
            break;
        if (strncasecmp(buf, "services", 8) != 0)
            break;
        if (fgets(buf, BUFFER_SIZE, fd) == NULL)
            break;
        do {
            uint32_t service_id;
            uint32_t name_space;
            uint32_t transport_stream_id;
            uint32_t original_network_id;
            uint32_t serviceType;
            uint32_t hmm;

            if (sscanf(buf, "%x:%x:%x:%x:%x:%x\n",&service_id, &name_space, &transport_stream_id, &original_network_id, &serviceType, &hmm) != 6)
                break;

            list_element_t *cur_element = NULL;
            EIT_service_t *element;
            if (*services == NULL) {
                *services = cur_element = allocate_element(sizeof(EIT_service_t));
            } else {
                cur_element = append_new_element(*services, sizeof(EIT_service_t));
            }
            if (!cur_element)
                break;

            element = (EIT_service_t *)cur_element->data;
            element->common.service_id = service_id;
            element->common.transport_stream_id = transport_stream_id;
            element->common.media_id  = original_network_id;
            element->original_network_id = original_network_id;

            char service_name[MAX_TEXT];
            if (fgets(service_name, BUFFER_SIZE, fd) == NULL)
                break;
            service_name[strlen(service_name) - 1] = '\0';

            memcpy(&element->service_descriptor.service_name, &service_name, strlen(service_name));
            if (fgets(buf, BUFFER_SIZE, fd) == NULL)
                break;

            // add transpounder data
            list_element_t *cur_tr_element;
            transpounder_t *element_tr;
            for(cur_tr_element = head_ts_list; cur_tr_element != NULL; cur_tr_element = cur_tr_element->next) {
                element_tr = (transpounder_t*)cur_tr_element->data;

                if(element_tr == NULL) {
                    continue;
                }
                if (name_space == element_tr->transpounder_id.name_space &&
                     element->common.transport_stream_id == element_tr->transpounder_id.ts_id &&
                     element->original_network_id == element_tr->transpounder_id.n_id) {
                    if (element_tr->type == 'c') {
                        element->media.type = serviceMediaDVBC;
                        element->media.dvb_c.frequency = element_tr->freq * 1000;
                        element->media.dvb_c.symbol_rate = element_tr->sym_rate;
                        element->media.dvb_c.modulation = element_tr->mod;
                        element->common.media_id = element->common.media_id;
                    }
                }
            }
            if (fgets(buf, BUFFER_SIZE, fd) == NULL)
                break;
        } while (strncasecmp(buf, "end", 3) != 0);
    } while(0);

    free_elements(&head_ts_list);
    fclose(fd);
}
int32_t getParam(const char *path, const char *param, const char *defaultValue, char *output)
{
    char buf[MENU_ENTRY_INFO_LENGTH];
    FILE *fd;
    int32_t found = 0;
    int32_t plen, vlen;

    fd = fopen(path, "r");
    if(fd != NULL) {
        while(fgets(buf, sizeof(buf), fd) != NULL) {
            plen = strlen(param);
            vlen = strlen(buf)-1;
            if(strncmp(buf, param, plen) == 0 && buf[plen] == '=') {
                while(buf[vlen] == '\r' || buf[vlen] == '\n' || buf[vlen] == ' ') {
                    buf[vlen] = 0;
                    vlen--;
                }
                if(vlen-plen > 0) {
                    if(output != NULL) {
                        strcpy(output, &buf[plen+1]);
                    }
                    found = 1;
                }
                break;
            }
        }
        fclose(fd);
    }

    if(!found && defaultValue && output) {
        strcpy(output, defaultValue);
    }
    return found;
}


void bouquet_downloadFileFromServer(char *shortname, char *fullname){
    int ret = 0;
    char serverName[16];
    char serverDir[256];
    char loginName[32];
    char cmd[1024];
    getParam(GARB_CONFIG, "SERVER_DIR", "-spool/input", serverDir);
    getParam(GARB_CONFIG, "SERVER_USER", "", loginName);
    getParam(GARB_CONFIG, "SERVER_IP", "", serverName);
    snprintf(cmd, sizeof(cmd), "scp -i " GARB_DIR "/.ssh/id_rsa -r %s@%s:%s/../channels/%s %s", loginName, serverName, serverDir, shortname, fullname);
    printf("cmd: %s\n",cmd);
    ret = system(cmd);
}

void bouquet_downloadFileWithServices(char *filename){
    printf("%s[%d]\n", __func__, __LINE__);
//    struct stat sb;
  //  if(stat(filename, &sb) != 0) {
  //      printf("%s[%d]\n", __func__, __LINE__);
        bouquet_downloadFileFromServer("bouquet", filename);
  //  }
}

#endif // ENABLE_DVB
