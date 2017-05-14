#!/usr/bin/env python

import sys
import os
import ctypes
import mncc
import socket
import time
import array
import _multiprocessing

new_callref = 1
connected = False
mncc_sock = None
GSM_AUDIO_FILE = "sample.gsm"

class mncc_msg(mncc.gsm_mncc):
	def send(self):
		return buffer(self)[:]

	def receive(self, bytes):
		fit = min(len(bytes), ctypes.sizeof(self))
		ctypes.memmove(ctypes.addressof(self), bytes, fit)

	def __str__(self):
		return 'mncc_msg(type=0x%04x, callref=%u, fields=0x%04x)' % (self.msg_type, self.callref, self.fields)

	def __unicode__(self):
		return u'mncc_msg(type=0x%04x, callref=%u, fields=0x%04x)' % (self.msg_type, self.callref, self.fields)

class mncc_data(mncc.gsm_data_frame):
	def send(self):
		return buffer(self)[:]

	def receive(self, bytes):
		fit = min(len(bytes), ctypes.sizeof(self))
		ctypes.memmove(ctypes.addressof(self), bytes, fit)

	def __str__(self):
		return 'mncc_data(type=0x%04x, callref=%u)' % (self.msg_type, self.callref)

	def __unicode__(self):
		return u'mncc_data(type=0x%04x, callref=%u)' % (self.msg_type, self.callref)
		

class MnccSocket(object):
	def __init__(self, address = '/tmp/ms_mncc_1'):
		self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
		print 'connecting to %s' % address
		try:
			self.sock.connect(address)
		except socket.error, errmsg:
			print >>sys.stderr, errmsg
			sys.exit(1)

	def send(self, msg):
		return self.sock.sendall(msg.send())

	def recv(self):
		data = self.sock.recv(1500)
		ms = mncc_msg()
		ms.receive(data)
		mncc_indication(ms)

	def getFileNo(self):
		return self.sock.fileno()

def send_mncc(msg):
	global mncc_sock
	mncc_sock.send(msg)

def mncc_number(number, num_type = 0, num_plan = 1, num_present = 1, num_screen = 0):
	return mncc.gsm_mncc_number(number = number, type = num_type,
                                plan = num_plan, present = num_present,
                                screen = num_screen)

def mncc_bearer_cap():
	EightIntegers = ctypes.c_int * 8
	ii = EightIntegers(2, 0, 1, -1, 0, 0, 0, 0)
	return mncc.gsm_mncc_bearer_cap(coding = 0, speech_ctm=0, radio = 3, speech_ver = ii, transfer = 0, mode = 0)

def mncc_cccap():
	return mncc.gsm_mncc_cccap(dtmf = 1)

def mncc_clir():
	return mncc.N8gsm_mncc3DOT_2E(inv = 1, sup=1)

def mncc_call(number, callref):
	print "MNCC: Calling number %s \n" % number
	msg = mncc_msg(msg_type = mncc.MNCC_SETUP_REQ, callref = callref,
                       fields = mncc.MNCC_F_CALLED | mncc.MNCC_F_BEARER_CAP | mncc.MNCC_F_CCCAP,
                       called = mncc_number(number),
                       bearer_cap = mncc_bearer_cap(),
                       cccap = mncc_cccap(),
                       clir = mncc_clir())
	send_mncc(msg)

	audio_tx_msg = mncc_msg(msg_type = mncc.MNCC_FRAME_RECV, callref = callref)
	send_mncc(audio_tx_msg)

def send_voice(callref):
	#change connected to False
	global connected
	if connected:
		connected = False
		time.sleep(2)
		print "Sending audio"
		send_audio(callref)
		print "Done sending audio"

#MT call setup indication when MO party sends MNCC_SETUP_REQ
def mncc_call_setup_ind(msg):
	print "MNCC: Setup indicating incoming call. \n"
	#MT call sends call confirmation MNCC_CALL_CONF_REQ to BTS which then forwards to MO as MNCC_CALL_CONF_IND
	call_conf_req = mncc_msg(msg_type = mncc.MNCC_SETUP_COMPL_REQ, callref = msg.callref, 
		fields = mncc.MNCC_F_BEARER_CAP | mncc.MNCC_F_CCCAP,
		bearer_cap = mncc_bearer_cap(),
        cccap = mncc_cccap())
	send_mncc(call_conf_req)

	#Change audio mode for voice transmission
	##audio_mode = mncc_msg(msg_type = mncc.MNCC_FRAME_RECV, callref = msg.callref)
	#send_mncc(call_alert_req)
	
	#MT call request alerting MNCC_ALERT_REQ to BTS which internally forwards to MO as MNCC_ALERT_IND
	print "MNCC: Call alerting.\nCall will be auto picked after 4seconds.\n"
	call_alert_req = mncc_msg(msg_type = mncc.MNCC_ALERT_REQ, callref = msg.callref)
	send_mncc(call_alert_req)

	time.sleep(4)

	#MT call requests to send connect message MNCC_SETUP_RSP to BTS which internall forwards to MO as MNCC_SETUP_CNF
	print "MNCC: Call setup complete request.\n"
	call_conn_req = mncc_msg(msg_type = mncc.MNCC_SETUP_RSP, callref = msg.callref)
	send_mncc(call_conn_req)

#MT call setup confirmation when MO party sends MNCC_SETUP_COMPL_REQ
def mncc_call_setup_compl_ind(msg):	
	print "MNCC: Call setup completion indication.\n"
	time.sleep(2)
	connected = True

#MO call alert indication when MT party sends MNCC_ALERT_REQ
def mncc_call_alert_ind(msg):
	print "MNCC: Call is alerting.\n"

#MO call setup confirmation when MT party sends MNCC_PROC_REQ
def mncc_call_proc_ind(msg):
	print "MNCC: Call is proceeding.\n"

#MO call setup confirmation when MT party sends MNCC_SETUP_RSP
def mncc_call_setup_cnf(msg):
	print "MNCC: Call is answered.\n"
	time.sleep(1)
	connected = True

#MO call disconnect request MNCC_DISC_REQ to BTS which internally forwards to MT as MNCC_DISC_IND
def mnnc_call_disc_req(msg):
	print "MNCC: Call disconnect release request.\n"
	call_disc_req = mncc_msg(msg_type = mncc.MNCC_DISC_REQ, callref = msg.callref)
	send_mncc(call_disc_req)

#MO call disconnect response indication MNCC_REL_IND from BTS when call has be disconnected with MNCC_DISC_REQ
def mncc_rel_ind(msg):
	printf("MNCC: Call release indication.\n");
	#Respond To BTS to release channel which forwards to MT as MNCC_REL_CNF
	call_disc_req = mncc_msg(msg_type = mncc.MNCC_REL_REQ, callref = msg.callref)
	send_mncc(call_disc_req)

#Mobile call termination indication MNCC_DISC_IND
def mncc_call_disc_ind(msg):
	print "MNCC: Disconnet indication from end-to-end communication.\n"
	#Which then forwards to BTS to release channel as MNCC_REL_REQ
	disconnet = mncc_msg(msg_type = mncc.MNCC_REL_REQ, callref = msg.callref)
	send_mncc(disconnet)

def mncc_rel_conf(msg):
	print "MNCC: call control release confirmation - MNCC_REL_CNF.\n"

def mncc_rej_ind(msg):
	print "MNCC: call control release confirmation - MNCC_REJ_IND.\n"

def mncc_notify_ind(msg):
	print "MNCC: notification indication.\n"

def mncc_other_dtmf_ind(msg):
	print "Other indication recieved with message type %s" % msg.msg_type
	return None

def mncc_indication(msg):
	global call_indicators
	if msg.msg_type == mncc.GSM_TCHF_FRAME or msg.msg_type == mncc.GSM_BAD_FRAME:
		send_voice(msg.callref)
	else:
		print "Message Type %s\n" % msg.msg_type
		indicator = call_indicators.get(str(msg.msg_type))
		if indicator is None:
			print "Oops! Message Type not implementated in indicators"
		else:
			cc_msg = call_indicators.get(str(msg.msg_type))(msg)
			if cc_msg is None:
				print "Ignoring command for MNCC message type"
			else:
				send_mncc(cc_msg)

def mncc_rx_thread():
	global mncc_sock
	while True:
		mncc_sock.recv()

def send_audio(callref):
	global mncc_sock 
	audio_file = None
	try:
		index = 33
		audio_file = open(GSM_AUDIO_FILE, 'rb')
		data = audio_file.read(33)
		while data:	
			mytype = ctypes.c_ubyte * 33
			voice = mytype.from_buffer_copy(data)
			cc_msg = mncc_data(msg_type = mncc.GSM_TCHF_FRAME, callref = callref, data=voice)			
			send_mncc(cc_msg)
			time.sleep(0.02)
			audio_file.seek(index)
			index = index + 33			
			data = audio_file.read(33)
	except Exception as error:
		print "\nError in reading GSM audio file, error message:\n[%s]\n" % error

call_indicators = {
	str(mncc.MNCC_SETUP_IND) : mncc_call_setup_ind,
	str(mncc.MNCC_SETUP_COMPL_IND) : mncc_call_setup_compl_ind,
	str(mncc.MNCC_ALERT_IND) : mncc_call_alert_ind,
	str(mncc.MNCC_CALL_PROC_IND) : mncc_call_proc_ind,
	str(mncc.MNCC_SETUP_CNF) : mncc_call_setup_cnf,
	str(mncc.MNCC_DISC_REQ) : mnnc_call_disc_req,
	str(mncc.MNCC_REL_IND) : mncc_rel_ind,
	str(mncc.MNCC_DISC_IND) : mncc_call_disc_ind,
	str(mncc.MNCC_REL_CNF) : mncc_rel_conf,
	str(mncc.MNCC_REJ_IND) : mncc_rej_ind,
	str(mncc.MNCC_REJ_IND) : mncc_rej_ind,
	str(mncc.MNCC_NOTIFY_IND) : mncc_notify_ind,
	str(mncc.MNCC_START_DTMF_RSP) : mncc_other_dtmf_ind,
	str(mncc.MNCC_START_DTMF_REJ) : mncc_other_dtmf_ind,
	str(mncc.MNCC_STOP_DTMF_RSP) : mncc_other_dtmf_ind,
}

if __name__== "__main__":
	mncc_sock = MnccSocket()
	time.sleep(2)
	calling_proc = True
	if calling_proc:
		number = "4804650123"
		mncc_call(number, new_callref)
	else:
		mncc_rx_thread()

