/** Header files **/
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdint.h>
#include "mncc.h"

#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <unistd.h>


/** END  **/

/** Constant and variables used in the app */
#define GSM_AUDIO_FILE "sample.gsm"
#define MNCC_SOCK_PATH "/tmp/ms_mncc_1"
int mncc_sock_fd = -1;
int new_callref = 1;
int send_voice = 0;
int calling_proc = 1; // 1 indicates MO and 0 indicates MT calling procedures

#define UNIX_CC_SOCKFD "/tmp/cc_sockfd"
/** END  **/

int mncc_setup_ind(int msg_type, void *arg);
int send_voice_sample(int callref);

/**
* Connect to OsmocomBB exposed MNCC socket
*/
static int connect_mncc()
{
	struct sockaddr_un addr;
	
	mncc_sock_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (mncc_sock_fd < 0) {
		printf("Error in creating unix domain socket\n");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
  	addr.sun_family = AF_UNIX;
  	if (*MNCC_SOCK_PATH == '\0') {
    	*addr.sun_path = '\0';
    	strncpy(addr.sun_path + 1, MNCC_SOCK_PATH + 1, sizeof(addr.sun_path) - 2);
  	} else {
    	strncpy(addr.sun_path, MNCC_SOCK_PATH, sizeof(addr.sun_path) - 1);
	}

	if (connect(mncc_sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    	printf("Error in connecting to socket path %s", MNCC_SOCK_PATH);
   		return -1;
	}
	return 0;
}



/**
* Write from MNNC socket 
*/
static int write_osmo_mncc_sock(void *data, int len)
{
	int record_count = write(mncc_sock_fd, data, len);
	if(record_count <= 0) {
		perror("ERROR writing to socket");
		printf("Error in writing to socket %s %d\n", MNCC_SOCK_PATH, mncc_sock_fd);
	}	
	return record_count;
}


/*
 * create and send mncc message
 */
static struct gsm_mncc *create_mncc(int msg_type, unsigned int callref)
{
	struct gsm_mncc *mncc;

	mncc = (struct gsm_mncc *)malloc(sizeof(struct gsm_mncc));
	memset(mncc, 0, sizeof(struct gsm_mncc));
	mncc->msg_type = msg_type;
	mncc->callref = callref;
	return (mncc);
}

/*
* send mncc message over mncc socket
*/
static int send_mncc(int msg_type, void *data)
{
	int len = 0;

	switch (msg_type) {
	case GSM_TCHF_FRAME:
		len = sizeof(struct gsm_data_frame) + 33;
		break;
	default:
		len = sizeof(struct gsm_mncc);
		break;
	}
	write_osmo_mncc_sock(data, len);
	return 0;
}

/*
* send and free mncc
*/
static int send_and_free_mncc(unsigned int msg_type, void *data)
{
	int ret = 0;
	ret = send_mncc(msg_type, data);
	free(data);
	return 0;
}

/**
* Read from MNNC socket 
*/
static int read_mncc_sock()
{
	char buf[sizeof(struct gsm_mncc)+1024];
	struct gsm_mncc *mncc_prim = (struct gsm_mncc *) buf;
	memset(buf, 0, sizeof(buf));
	int record_count = recv(mncc_sock_fd, buf, sizeof(buf), 0);
	mncc_setup_ind(mncc_prim->msg_type, buf);

	if (mncc_prim->msg_type == GSM_TCHF_FRAME
	 || mncc_prim->msg_type == GSM_BAD_FRAME) {

		/** Write voice to socket */
		if(send_voice)
		{
			printf("MNCC: Change audio mode\n");
			struct gsm_mncc *audio_mode = create_mncc(MNCC_FRAME_RECV, mncc_prim->callref);
			send_and_free_mncc(audio_mode->msg_type, audio_mode);
			send_voice = 0;
			send_voice_sample(mncc_prim->callref);
			printf("Done sending voice sample\n");			
		}
		
		return 0;
	}
	return record_count;
}

/**
* Send GSM voice frame after constructing the gsm_data_frame - struct
*/
static int send_frame(void *_frame, int len, int msg_type, int callref)
{	
	unsigned char buffer[sizeof(struct gsm_data_frame) + len];
	struct gsm_data_frame *mncc_voice = (struct gsm_data_frame *)buffer;
	
	mncc_voice->msg_type = msg_type;
	mncc_voice->callref = callref;

	memcpy(mncc_voice->data, _frame, len);
	send_mncc(mncc_voice->msg_type, mncc_voice);
}

int send_voice_sample(int callref)
{
	int fd;
	int buflen = 33;
	uint8_t buf[33];
	buf[33] = '\0';
	fd = open(GSM_AUDIO_FILE, O_RDONLY);
	while (read(fd, buf, buflen) == buflen) {
		usleep(20000);
		send_frame(buf, buflen, GSM_TCHF_FRAME, callref);
	}

	close(fd);
	
	return 0;
}

/** MNCC setup indicator respods to messages from RR/ MM
* Parameters msg_type and *mncc
*/
int mncc_setup_ind(int msg_type, void *arg)
{
	struct gsm_mncc *mncc = (struct gsm_mncc *)arg;
	unsigned int callref = mncc->callref;
	switch(msg_type) {
		/*MT call setup indication when MO party sends MNCC_SETUP_REQ*/
		case MNCC_SETUP_IND:
		printf("MNCC: Setup indicatiing incoming call.\n");

		/*MT call sends call confirmation MNCC_CALL_CONF_REQ to BTS which then forwards to MO as MNCC_CALL_CONF_IND*/
		struct gsm_mncc *call_conf_req = create_mncc(MNCC_CALL_CONF_REQ, callref);
		
		if (!(mncc->fields & MNCC_F_BEARER_CAP)) {
		call_conf_req->fields |= MNCC_F_BEARER_CAP;

		call_conf_req->bearer_cap.coding = 0;
		call_conf_req->bearer_cap.radio = 1;
		call_conf_req->bearer_cap.speech_ctm = 0;
		call_conf_req->bearer_cap.speech_ver[0] = 0;
		call_conf_req->bearer_cap.speech_ver[1] = -1; /* end of list */

		call_conf_req->bearer_cap.transfer = 0;
		call_conf_req->bearer_cap.mode = 0;
		}
		
		call_conf_req->fields |= MNCC_F_CCCAP;
		call_conf_req->cccap.dtmf = 1;
		send_and_free_mncc(call_conf_req->msg_type, call_conf_req);
		
		printf("MNCC: Change audio mode\n");
		struct gsm_mncc *audio_mode = create_mncc(MNCC_FRAME_RECV, callref);
		send_and_free_mncc(audio_mode->msg_type, audio_mode);

		/*MT call request alerting MNCC_ALERT_REQ to BTS which internally forwards to MO as MNCC_ALERT_IND */
		printf("MNCC: Call alerting.\nCall will be auto picked after 4seconds.\n");
		struct gsm_mncc *call_alert_req = create_mncc(MNCC_ALERT_REQ, callref);
		send_and_free_mncc(call_alert_req->msg_type, call_alert_req);	

		sleep(4);	

		/*MT call requests to send connect message MNCC_SETUP_RSP to BTS which internall forwards to MO as MNCC_SETUP_CNF*/
		printf("MNCC: Call setup complete request.\n");
		struct gsm_mncc *call_conn_req = create_mncc(MNCC_SETUP_RSP, callref);
		send_and_free_mncc(call_conn_req->msg_type, call_conn_req);
		break;

		/*MT call setup confirmation when MO party sends MNCC_SETUP_COMPL_REQ*/
		case MNCC_SETUP_COMPL_IND:
		printf("MNCC: Call setup completion indication.\n");
		sleep(2);
		send_voice = 1;
		break;

		/*MO call alert indication when MT party sends MNCC_ALERT_REQ*/
		case MNCC_ALERT_IND:
		printf("MNCC: Call is alerting.\n");
		break;

		/*MO call proceeding indication when MT party sends MNCC_PROC_REQ*/
		case MNCC_CALL_PROC_IND:
		printf("MNCC: Call is proceeding.\n");
		break;

		/*MO call setup confirmation when MT party sends MNCC_SETUP_RSP*/
		case MNCC_SETUP_CNF:
		printf("MNCC: Call is answered.\n");
		sleep(1);
		send_voice = 1;
		break;

		/*MO call disconnect request MNCC_DISC_REQ to BTS which internally forwards to MT as MNCC_DISC_IND*/
		case MNCC_DISC_REQ:
		printf("MNCC: Call disconnect release request.\n");
		struct gsm_mncc *call_disc_req = create_mncc(MNCC_DISC_REQ, callref);
		send_and_free_mncc(call_disc_req->msg_type, call_disc_req);
		break;

		/*MO call disconnect response indication MNCC_REL_IND from BTS when call has be disconnected with MNCC_DISC_REQ*/
		case MNCC_REL_IND:
		printf("MNCC: Call release indication.\n");
		/*Respond To BTS to release channel which forwards to MT as MNCC_REL_CNF*/
		struct gsm_mncc *call_rel_req = create_mncc(MNCC_REL_REQ, callref);
		send_and_free_mncc(call_rel_req->msg_type, call_rel_req);
		break;

		/*Mobile call termination indication MNCC_DISC_IND*/
		case MNCC_DISC_IND:
		printf("MNCC: Disconnet indication from end-to-end communication.\n");
		/* Which then forwards to BTS to release channel as MNCC_REL_REQ*/
		struct gsm_mncc *disconnet = create_mncc(MNCC_REL_REQ, callref);
		send_and_free_mncc(disconnet->msg_type, disconnet);
		break;

		case MNCC_REL_CNF:
		printf("MNCC: call control release confirmation - MNCC_REL_CNF.\n");
		break;

		case MNCC_REJ_IND:
		printf("MNCC: call control release indication - MNCC_REJ_IND.\n");
		break;

		case MNCC_NOTIFY_IND:
		printf("MNCC: notification indication.\n");
		break;

		case MNCC_START_DTMF_RSP:
		case MNCC_START_DTMF_REJ:
		case MNCC_STOP_DTMF_RSP:
		printf("MNCC: DTMF statement machine.\n");
		break;

		default:
		;
	}
	return(0);
}

/**
* Initializes the MNCC caller application, fills in the necessary fields for osmocomBB 
* to build the structure and initiate the call
* Parameters: char* number dialling number
*/
static int mncc_call(char *number)
{
	struct gsm_mncc *mncc;
	int call_ref = new_callref++;
	printf("MNCC: Call setup request\n");
	mncc = create_mncc(MNCC_SETUP_REQ, call_ref);

	if (!strncasecmp(number, "emerg", 5)) {
		printf("Make emergency call\n");
		mncc->emergency = 1;
	} else {
		printf("make call to %s\n", number);
		mncc->fields |= MNCC_F_CALLED;

		if(number[0] == '+') {
			number++;
			mncc->called.type = 1; /*international*/
		} else {
			mncc->called.type = 0; /*auto-unknown -prefix must be used*/
		}
		mncc->called.plan = 1;
		strncpy(mncc->called.number, number,
		sizeof(mncc->called.number) - 1);
		printf("Calling number %s\n", mncc->called.number);

		/* bearer capability (mandatory) */
		mncc->fields |= MNCC_F_BEARER_CAP;
		mncc->bearer_cap.coding = 0;
		mncc->bearer_cap.speech_ctm = 0;

		mncc->bearer_cap.radio = 1; /** Support TCH/F only*/
		/** Supported rates **/
		mncc->bearer_cap.speech_ver[0] = 0;
		mncc->bearer_cap.speech_ver[1] = -1; /* end of list */
		/** END **/
		
		mncc->bearer_cap.transfer = 0;
		mncc->bearer_cap.mode = 0;

		/* CC capabilities (optional) DTMF */
		mncc->fields |= MNCC_F_CCCAP;
		mncc->cccap.dtmf = 1;

		mncc->fields |= MNCC_F_CCCAP;
		mncc->clir.inv = 1;
		mncc->clir.sup = 1;
	}

	send_and_free_mncc(mncc->msg_type, mncc);
	printf("MNCC: Change audio mode\n");
	struct gsm_mncc *audio_mode = create_mncc(MNCC_FRAME_RECV, call_ref);
	return send_and_free_mncc(audio_mode->msg_type, audio_mode);
}

/**
* Main application begins here 
*/
int main(int argc, char *argv[]) {
	
	connect_mncc();
	if (mncc_sock_fd < 0) {
		exit(-1);
	}
	if (calling_proc) {
		char *number = "4804650123";
		mncc_call(number);
	}
	
	while(1) {
		read_mncc_sock();
	}
	
	return 0;
}