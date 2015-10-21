
#include <stdio.h>
#include <libbacnet/address.h>
#include <libbacnet/device.h>
#include <libbacnet/handlers.h>
#include <libbacnet/datalink.h>
#include <libbacnet/bvlc.h>
#include <libbacnet/client.h>
#include <libbacnet/txbuf.h>
#include <libbacnet/tsm.h>
#include <libbacnet/ai.h>
#include "bacnet_namespace.h"
#include <modbus-tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>

/*Define names for the arguments in the modbus_new_tcp() function*/
#define SERVER_ADDRESS "140.159.153.159"
#define SERVER_ADDRESS "127.0.0.1"
/*Changed to the Loopback address for testing*/
#define SERVER_PORT 502
#define BACNET_DEVICE_NO            52
#define BACNET_PORT                 0xBAC1
#define BACNET_INTERFACE            "lo"
#define BACNET_DATALINK_TYPE        "bvlc"
#define BACNET_SELECT_TIMEOUT_MS    1	/* ms */

#define RUN_AS_BBMD_CLIENT          1

#if RUN_AS_BBMD_CLIENT
#define BACNET_BBMD_PORT            0xBAC0
#define BACNET_BBMD_ADDRESS         "127.0.0.1"
#define BACNET_BBMD_TTL             90
#endif

/* If you are trying out the test suite from home, this data matches the data
 * stored in RANDOM_DATA_POOL for device number 12
 * BACnet client will print "Successful match" whenever it is able to receive
 * this set of data. Note that you will not have access to the RANDOM_DATA_POOL
 * for your final submitted application. */
//Variables used for reading the registers

uint16_t tab_reg[64];		/*Memory for storing the registers*/
static uint16_t test_data[] = {
    0xA4EC, 0x6E39, 0x8740, 0x1065, 0x9134, 0xFC8C
};
/*Linked List Object*/
typedef struct s_list_object list_object;
struct s_list_object{
	int number;
	list_object *next;
};

/*List is shared between Modbus and BACnet so need list lock*/
static list_object *list_head;
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t list_data_flush = PTHREAD_COND_INITIALIZER;

#define NUM_TEST_DATA (sizeof(test_data)/sizeof(test_data[0]))

static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;
static int Update_Analog_Input_Read_Property(BACNET_READ_PROPERTY_DATA *
					     rpdata){

    static int index;
    int instance_no =
	bacnet_Analog_Input_Instance_To_Index(rpdata->object_instance);

    if (rpdata->object_property != bacnet_PROP_PRESENT_VALUE)
	goto not_pv;

    printf("AI_Present_Value request for instance %i\n", instance_no);
    /* Update the values to be sent to the BACnet client here.
     * The data should be read from the head of a linked list. You are required
     * to implement this list functionality.
     *
     * bacnet_Analog_Input_Present_Value_Set() 
     *     First argument: Instance No
     *     Second argument: data to be sent
     *
     * Without reconfiguring libbacnet, a maximum of 4 values may be sent */
    bacnet_Analog_Input_Present_Value_Set(0, test_data[index++]);
    /* bacnet_Analog_Input_Present_Value_Set(1, test_data[index++]); */
    /* bacnet_Analog_Input_Present_Value_Set(2, test_data[index++]); */

    if (index == NUM_TEST_DATA)
	index = 0;

  not_pv:
    return bacnet_Analog_Input_Read_Property(rpdata);
}

static bacnet_object_functions_t server_objects[] = {
    {bacnet_OBJECT_DEVICE,
     NULL,
     bacnet_Device_Count,
     bacnet_Device_Index_To_Instance,
     bacnet_Device_Valid_Object_Instance_Number,
     bacnet_Device_Object_Name,
     bacnet_Device_Read_Property_Local,
     bacnet_Device_Write_Property_Local,
     bacnet_Device_Property_Lists,
     bacnet_DeviceGetRRInfo,
     NULL,			/* Iterator */
     NULL,			/* Value_Lists */
     NULL,			/* COV */
     NULL,			/* COV Clear */
     NULL			/* Intrinsic Reporting */
     },
    {bacnet_OBJECT_ANALOG_INPUT,
     bacnet_Analog_Input_Init,
     bacnet_Analog_Input_Count,
     bacnet_Analog_Input_Index_To_Instance,
     bacnet_Analog_Input_Valid_Instance,
     bacnet_Analog_Input_Object_Name,
     Update_Analog_Input_Read_Property,
     bacnet_Analog_Input_Write_Property,
     bacnet_Analog_Input_Property_Lists,
     NULL /* ReadRangeInfo */ ,
     NULL /* Iterator */ ,
     bacnet_Analog_Input_Encode_Value_List,
     bacnet_Analog_Input_Change_Of_Value,
     bacnet_Analog_Input_Change_Of_Value_Clear,
     bacnet_Analog_Input_Intrinsic_Reporting},
    {MAX_BACNET_OBJECT_TYPE}
};

static void register_with_bbmd(void)
{
#if RUN_AS_BBMD_CLIENT
    /* Thread safety: Shares data with datalink_send_pdu */
    bacnet_bvlc_register_with_bbmd(bacnet_bip_getaddrbyname
				   (BACNET_BBMD_ADDRESS),
				   htons(BACNET_BBMD_PORT),
				   BACNET_BBMD_TTL);
#endif
}

static void *minute_tick(void *arg)
{
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Expire addresses once the TTL has expired */
	bacnet_address_cache_timer(60);

	/* Re-register with BBMD once BBMD TTL has expired */
	register_with_bbmd();

	/* Update addresses for notification class recipient list 
	 * Requred for INTRINSIC_REPORTING
	 * bacnet_Notification_Class_find_recipient(); */

	/* Sleep for 1 minute */
	pthread_mutex_unlock(&timer_lock);
	sleep(60);
    }
    return arg;
}

static void *second_tick(void *arg)
{
    while (1) {
	pthread_mutex_lock(&timer_lock);

	/* Invalidates stale BBMD foreign device table entries */
	bacnet_bvlc_maintenance_timer(1);

	/* Transaction state machine: Responsible for retransmissions and ack
	 * checking for confirmed services */
	bacnet_tsm_timer_milliseconds(1000);

	/* Re-enables communications after DCC_Time_Duration_Seconds
	 * Required for SERVICE_CONFIRMED_DEVICE_COMMUNICATION_CONTROL
	 * bacnet_dcc_timer_seconds(1); */

	/* State machine for load control object
	 * Required for OBJECT_LOAD_CONTROL
	 * bacnet_Load_Control_State_Machine_Handler(); */

	/* Expires any COV subscribers that have finite lifetimes
	 * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
	 * bacnet_handler_cov_timer_seconds(1); */

	/* Monitor Trend Log uLogIntervals and fetch properties
	 * Required for OBJECT_TRENDLOG
	 * bacnet_trend_log_timer(1); */

	/* Run [Object_Type]_Intrinsic_Reporting() for all objects in device
	 * Required for INTRINSIC_REPORTING
	 * bacnet_Device_local_reporting(); */

	/* Sleep for 1 second */
	pthread_mutex_unlock(&timer_lock);
	sleep(1);
    }
    return arg;
}

/* What I need to do
 * Create add to list (done)
 * Find the last object in the list
 * Place the next object in the list after the last object
 * Retrieve the first object from list
 * Print this object
 * Free object and memory
 * Flush the list
 * Set up Modbus connection */

/*Add to list*/
static void add_to_list(list_object **list_head, int number){
	list_object *last_object, *temp_object;/**/
	int temp_number;
	temp_number = malloc(sizeof(list_object));/*Allocate memory for each number*/

	temp_object -> number = temp_number;
	temp_object -> next = NULL;

	pthread_mutex_lock(&list_lock);

		if (*list_head == NULL){/*make the first number in list*/
	*list_head = temp_object;/*point to the first number*/
}
		else{
	last_object = *list_head;
	while(last_object -> next){
		last_object -> next;
}
	last_object -> next = temp_object;
	}
	pthread_mutex_unlock(&list_lock);
	pthread_cond_signal(&list_data_ready);
}

static list_object *list_get_first(void){
	list_object *first_object;
	first_object = list_head;
	list_head = list_head -> next;
	return first_object;
}

static void *print_func(void *arg){
	list_object  *current_object;
	fprintf(stderr, "Print thread starting\n");
	while(1){
		pthread_mutex_lock(&list_lock);
		while(list_head == NULL){
			pthread_cond_wait(&list_data_ready, &list_lock);
		}
	current_object = list_get_first;
	pthread_mutex_unlock(&list_lock);
	printf("Print thread: %s\n", current_object -> number);
	free(current_object -> number);
	free(current_object);

	pthread_cond_signal(&list_data_flush);
	}
	return arg;
}




/*Modbus*/
static void *modbus_start(void *arg){ /*Allocate and initialise a new modbus_t*/
							/*structure*/
    int rc;
    int i;
    modbus_t *ctx;
    restart:
    ctx = modbus_new_tcp(SERVER_ADDRESS, SERVER_PORT);/*Arguments to function*/
    if (ctx == NULL) {	
	fprintf(stderr, " Allocation and Initialisation unsucseful\n");                       
	sleep(1);
	return -1;
	goto restart;
    }
/*Establish a connection using the modbus_t structure*/       
    if (modbus_connect(ctx) == -1) {
	fprintf(stderr, "Connenction to server unsuccesful:%s\n",
		modbus_strerror(errno));
	/*Detect return value from function to confirm connection*/
	modbus_free(ctx);/*This function shall free an allocated modbus_t structure*/
	sleep(1);
	return -1;
	goto restart;
    }    
    else {
	fprintf(stderr, "Connection to server succesful\n");
    } 

/*Read the registers*/
    rc = modbus_read_registers(ctx, 52, 2, tab_reg);/* I have been assigned Modbus address*/
                                                                 /*52 and 53*/
    if (rc == -1) {
	fprintf(stderr, "Reading of the registers has failed:%s\n",
		                 modbus_strerror(errno));
	return -1;
	goto restart;
    }
    for (i = 0; i < rc; i++) {
	printf("reg[%d]=%d (0x%X)\n", i, tab_reg[i], tab_reg[i]);
    /*Display the contents of registers up to the rc value, the number of registers read*/
    }
/*Close the connection to the server and free the modbus_t structure*/
    modbus_close(ctx);
    modbus_free(ctx);
    sleep(0.1);//100ms sleep
    return arg;
} /* End of Modbus*/



static void ms_tick(void)
{
    /* Updates change of value COV subscribers.
     * Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
     * bacnet_handler_cov_task(); */
}

#define BN_UNC(service, handler) \
    bacnet_apdu_set_unconfirmed_handler(                \
                    SERVICE_UNCONFIRMED_##service,      \
                    bacnet_handler_##handler)
#define BN_CON(service, handler) \
    bacnet_apdu_set_confirmed_handler(                  \
                    SERVICE_CONFIRMED_##service,        \
                    bacnet_handler_##handler)

int main(int argc, char **argv)
{
    uint8_t rx_buf[bacnet_MAX_MPDU];
    uint16_t pdu_len;
    BACNET_ADDRESS src;
    pthread_t minute_tick_id, second_tick_id;
    pthread_t modbus_start_id;
    bacnet_Device_Set_Object_Instance_Number(BACNET_DEVICE_NO);
    bacnet_address_init();

    /* Setup device objects */
    bacnet_Device_Init(server_objects);
    BN_UNC(WHO_IS, who_is);
    BN_CON(READ_PROPERTY, read_property);

    bacnet_BIP_Debug = true;
    bacnet_bip_set_port(htons(BACNET_PORT));
    bacnet_datalink_set(BACNET_DATALINK_TYPE);
    bacnet_datalink_init(BACNET_INTERFACE);
    atexit(bacnet_datalink_cleanup);
    memset(&src, 0, sizeof(src));

    register_with_bbmd();

    bacnet_Send_I_Am(bacnet_Handler_Transmit_Buffer);

    pthread_create(&minute_tick_id, 0, minute_tick, NULL);
    pthread_create(&second_tick_id, 0, second_tick, NULL);
    pthread_create(&modbus_start_id, 0, modbus_start, NULL);/*Modbus*/
    
    //modbus_start();
    /* Start another thread here to retrieve your allocated registers from the
     * modbus server. This thread should have the following structure (in a
     * separate function):
     *
     * Initialise:
     *      Connect to the modbus server
     *
     * Loop:
     *      Read the required number of registers from the modbus server
     *      Store the register data into the tail of a linked list 
     */

    while (1) {
	pdu_len =
	    bacnet_datalink_receive(&src, rx_buf, bacnet_MAX_MPDU,
				    BACNET_SELECT_TIMEOUT_MS);

	if (pdu_len) {
	    /* May call any registered handler.
	     * Thread safety: May block, however we still need to guarantee
	     * atomicity with the timers, so hold the lock anyway */
	    pthread_mutex_lock(&timer_lock);
	    bacnet_npdu_handler(&src, rx_buf, pdu_len);
	    pthread_mutex_unlock(&timer_lock);
	}

	ms_tick();
    }

    return 0;
}
