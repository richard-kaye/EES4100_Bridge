//Libraries to include
#include <stdio.h>
#include <modbus-tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>

// file name changed to modbus_client.c
//Define names for the arguments in the modbus_new_tcp() function
#define SERVER_ADDRESS "140.159.153.159"
#define SERVER_PORT 502

//Variables used for reading the registers
uint16_t tab_reg[64];//Memory for storing the registers
int rc;              //An integer to store the number of registers read from the server
int i;               //An integer to keep count of the registers printed to the screen


int main(void){
//Allocate and initialise a new modbus_t structure
	modbus_t *ctx;	
	ctx = modbus_new_tcp(SERVER_ADDRESS,SERVER_PORT);//Arguments to function
	if (ctx == NULL) {  //This NULL is returned on error
		fprintf(stderr, " Allocation and Initialisation unsucseful\n");//Display message on 								 //screen to indicate failure
	return -1;
}
//Establish a connection using the modbus_t structure       
	if(modbus_connect(ctx) == -1){
		fprintf(stderr,"Connenction to server unsuccesful:%s\n",modbus_strerror(errno));
			//Detect return value from function to confirm connection
		modbus_free(ctx);//This function shall free an allocated modbus_t structure.
		return -1;
}
	else{
		fprintf(stderr,"Connection to server succesful\n");//If the return value from
							           //the function is not -1
								   //connection was succesful
}

//Read the registers
	rc = modbus_read_registers(ctx, 12, 3, tab_reg);
	if (rc == -1){
		fprintf(stderr, "Reading of the registers has failed:%s\n", modbus_strerror(errno));
	                                                    //Detects that an error has occured
		return -1;
}

	for (i = 0; i < rc; i++){
	printf("reg[%d]=%d (0x%X)\n", i, tab_reg[i], tab_reg[i]);
	//Display the contents of registers up to the rc value, the number of registers read
}
//Close the connection to the server and free the modbus_t structure
	modbus_close(ctx);
	modbus_free(ctx);
        return 0;

}
