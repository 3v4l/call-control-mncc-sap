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
#define CALL_CMD "call\n"
#define EXIT_CMD "exit\n"
int mncc_sock_fd = -1;
int new_callref = 1;
int send_voice = 0;
signed short samples[160]; /* last received audi packet */
signed short rxdata[160]; /* receive audio buffer */
int rxpos = 0; /* position in audio buffer 0..159 */

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
		//printf("recieved voice from the caller\n");

		/** Write voice to socket */
		if(send_voice)
		{
			send_voice = 0;
			send_voice_sample(mncc_prim->callref);
			printf("Done sending voice sample\n");			
		}
		
		return 0;
	}
	return record_count;
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

		case MNCC_SETUP_IND:
		printf("MNCC setup indication for incoming call.\n");
		struct gsm_mncc *proceeding = create_mncc(MNCC_CALL_CONF_REQ, callref);
		if (!(mncc->fields & MNCC_F_BEARER_CAP)) {
		proceeding->fields |= MNCC_F_BEARER_CAP;
		proceeding->bearer_cap.coding = 0;
		proceeding->bearer_cap.radio = 1;
		proceeding->bearer_cap.speech_ctm = 0;
		proceeding->bearer_cap.speech_ver[0] = 0;
		proceeding->bearer_cap.speech_ver[1] = -1; /* end of list */

		proceeding->bearer_cap.transfer = 0;
		proceeding->bearer_cap.mode = 0;
		}
		/* DTMF supported */
		proceeding->fields |= MNCC_F_CCCAP;
		proceeding->cccap.dtmf = 1;
		send_and_free_mncc(proceeding->msg_type, proceeding);

		printf("MNCC sending frame\n");
		struct gsm_mncc *frame = create_mncc(MNCC_FRAME_RECV, callref);
		send_and_free_mncc(frame->msg_type, frame);

		printf("MNCC call alerting.\n");
		struct gsm_mncc *call_alert = create_mncc(MNCC_ALERT_IND, callref);
		send_and_free_mncc(call_alert->msg_type, call_alert);

		printf("MNCC setup complete request.\n");
		struct gsm_mncc *call_setup_compl = create_mncc(MNCC_SETUP_RSP, callref);
		send_and_free_mncc(call_setup_compl->msg_type, call_setup_compl);
		break;

		case MNCC_SETUP_COMPL_IND:
		printf("MNCC connection acknowledgement indication.\n");
		struct gsm_mncc *call_setup_compl_ack = create_mncc(MNCC_SETUP_COMPL_REQ, callref);
		send_and_free_mncc(call_setup_compl_ack->msg_type, call_setup_compl_ack);
		send_voice = 1;
		break;

		case MNCC_DISC_IND:
		printf("MNCC disconnet indication\n");
		struct gsm_mncc *disconnet = create_mncc(MNCC_REL_REQ, callref);
		send_and_free_mncc(disconnet->msg_type, disconnet);
		break;

		case MNCC_REL_IND:
		printf("MNCC call control release indication - MNCC_REL_IND.\n");
		break;

		case MNCC_REL_CNF:
		printf("MNCC call control release indication - MNCC_REL_CNF.\n");
		break;

		case MNCC_REJ_IND:
		printf("MNCC call control release indication - MNCC_REJ_IND.\n");
		break;

		case MNCC_NOTIFY_IND:
		printf("MNC notification indication.\n");
		break;

		case MNCC_START_DTMF_RSP:
		case MNCC_START_DTMF_REJ:
		case MNCC_STOP_DTMF_RSP:
		printf("MNCC DTMF statement machine.\n");
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
	int i = 0;
	int call_ref = new_callref++;
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

		mncc->bearer_cap.radio = 3; /** Support TCH/H also*/
		/** Supported rates **/
		mncc->bearer_cap.speech_ver[i++] = 2; /* support full rate v2 */
		mncc->bearer_cap.speech_ver[i++] = 0; /* support full rate v1 */
		mncc->bearer_cap.speech_ver[i++] = 1; /* support half rate v2 */
		mncc->bearer_cap.speech_ver[i] = -1; /* end of list */
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

	return send_and_free_mncc(mncc->msg_type, mncc);
}

int recvfd(int unix_domain_socket_fd) {
	int res;
	char dummy_char;
	char buf[CMSG_SPACE(sizeof(int))];
	struct msghdr msg;
	struct iovec dummy_iov;
	struct cmsghdr *cmsg;
	memset(&msg, 0, sizeof msg);
	dummy_iov.iov_base = &dummy_char;
	dummy_iov.iov_len = 1;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	msg.msg_iov = &dummy_iov;
	msg.msg_iovlen = 1;
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	msg.msg_controllen = cmsg->cmsg_len;
	res = recvmsg(unix_domain_socket_fd, &msg, 0);
	if (res == 0) return -2;  /* EOF. */
	if (res < 0) return -1;  /* I/O error. */
	memcpy(&res, CMSG_DATA(cmsg), sizeof res);  /* int. */
	return res;  /* OK, new file descriptor is returned. */
}

/**
* Main application begins here 
*/
int main(int argc, char *argv[]) {
	/*
	unlink(UNIX_CC_SOCKFD);
	

	int sock, msgsock;
	struct sockaddr_un server;
	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("opening stream socket");
		exit(1);
	}
	
	server.sun_family = AF_UNIX;
	strcpy(server.sun_path, UNIX_CC_SOCKFD);

	if (bind(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un))) {
	  perror("binding stream socket");
	 exit(1);
	}

	listen(sock, 1);
	msgsock = accept(sock, 0, 0);
	mncc_sock_fd = recvfd(msgsock);
	printf("SOCKET FD: %d\n", mncc_sock_fd);
	*/
	connect_mncc();
	if (mncc_sock_fd < 0) {
		exit(-1);
	}
	char *number = "4804650123";
	mncc_call(number);
	while(1) {
		read_mncc_sock();
	}
	//char input[25];
	//printf("\nEnter commands to OsmocomBB:\n>>");
	//fgets(input, 25, stdin);
	//if(strcmp(input, CALL_CMD) == 0) 
	//{
	//	char number[10];
	//	printf("Enter number to call: ");
	//	fgets(number, 25, stdin);
	//	mncc_call(number);
	//} 
	//else if(strcmp(input, EXIT_CMD) == 0) 
	//{
	//	printf("Safe close and exit application\n");
	//	close(mncc_sock_fd);
	//}
	//else
	//{
	//	printf("Invalid Command\n");
	//}
	return 0;
}