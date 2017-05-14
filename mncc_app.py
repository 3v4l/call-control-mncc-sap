#!/usr/bin/env python

import sys
import os
import ctypes
import mncc
import socket
import time
import _multiprocessing

new_callref = 1
connected = False
mncc_sock = None
GSM_AUDIO_FILE = "sample.gsm"
connection = None
UNIX_CC_SOCKFD = "/tmp/cc_sockfd"

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
		return 'mncc_msg(type=0x%04x, callref=%u)' % (self.msg_type, self.callref)

	def __unicode__(self):
		return u'mncc_msg(type=0x%04x, callref=%u)' % (self.msg_type, self.callref)
		

class MnccSocket(object):
	"""docstring for MNCCSocket"""
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
	global connection
	print "MNCC: Calling number %s \n" % number
	connection.send("Call number %s\n" % number)
	msg = mncc_msg(msg_type = mncc.MNCC_SETUP_REQ, callref = callref,
                       fields = mncc.MNCC_F_CALLED | mncc.MNCC_F_BEARER_CAP | mncc.MNCC_F_CCCAP,
                       called = mncc_number(number),
                       bearer_cap = mncc_bearer_cap(),
                       cccap = mncc_cccap(),
                       clir = mncc_clir())
	send_mncc(msg)

def send_voice(callref):
	global connected
	if connected:
		connected = False
		print "Sending audio"
		send_audio(callref)
		print "Done sending audio"

def mncc_call_proc_ind(msg):
	global connection
	connection.send("Call proceeding")
	print "Call proceeding \n"
	cc_msg = mncc_msg(msg_type = mncc.MNCC_FRAME_RECV, callref = msg.callref)
	return cc_msg

def mncc_call_alert_ind(msg):
	global connection
	connection.send("Call alerting")
	print "Call alerting \n"
	cc_msg = mncc_msg(msg_type = mncc.MNCC_ALERT_IND, callref = msg.callref)
	return cc_msg

def mncc_call_setup_cnf(msg):
	global connection
	connection.send("Call setup confirmation ")
	global connected
	print "Call setup confirmation \n"
	cc_msg = mncc_msg(msg_type = mncc.MNCC_SETUP_COMPL_REQ, callref = msg.callref)
	connected = True
	return cc_msg

def mncc_call_setup_compl_ind(msg):	
	global connection
	connection.send("Call setup complete")
	print "Call setup complete \n"
	cc_msg = mncc_msg(msg_type = mncc.MNCC_SETUP_COMPL_REQ, callref = msg.callref)	
	return cc_msg

def mncc_call_disc_ind(msg):
	global connection
	connection.send("Call disconnet")
	print "Call disconnet \n"
	cc_msg = mncc_msg(msg_type = mncc.MNCC_REL_REQ, callref = msg.callref)
	return cc_msg

def mncc_other_ind(msg):
	global connection
	connection.send("Call other indication recieved with message %s" % msg.msg_type)
	print "Other indication recieved with message type %s" % msg.msg_type
	return None

def mncc_indication(msg):
	global connection
	global call_indicators
	if msg.msg_type == mncc.GSM_TCHF_FRAME or msg.msg_type == mncc.GSM_BAD_FRAME:
		x = 5
	else:
		cc_msg = call_indicators.get(str(msg.msg_type))(msg)
		if cc_msg is None:
			print "Ignoring command for MNCC indication"
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
		data = audio_file.read(index)
		while data:
			cc_msg = mncc_data(msg_type = mncc.GSM_TCHF_FRAME, callref = callref)
			ar = bytearray(data)
			mytype = ctypes.c_ubyte * 33			
			voice = mytype.from_buffer(ar)
			cc_msg.data = voice
			time.sleep(0.02)
			send_mncc(cc_msg)
			index = index + 33
			audio_file.seek(index)
			data = audio_file.read(33)
	except Exception as error:
		print "\nError in reading GSM audio file, error message:\n[%s]\n" % error

call_indicators = {
	str(mncc.MNCC_CALL_PROC_IND) : mncc_call_proc_ind,
	str(mncc.MNCC_ALERT_IND) : mncc_call_alert_ind,
	str(mncc.MNCC_SETUP_CNF) : mncc_call_setup_cnf,
	str(mncc.MNCC_SETUP_COMPL_IND) : mncc_call_setup_compl_ind,
	str(mncc.MNCC_DISC_IND) : mncc_call_disc_ind,
	str(mncc.MNCC_REL_IND) : mncc_other_ind,
	str(mncc.MNCC_REJ_IND) : mncc_other_ind,
	str(mncc.MNCC_NOTIFY_IND) : mncc_other_ind,
	str(mncc.MNCC_START_DTMF_RSP) : mncc_other_ind,
	str(mncc.MNCC_START_DTMF_REJ) : mncc_other_ind,
	str(mncc.MNCC_STOP_DTMF_RSP) : mncc_other_ind,
}

def init(number, recv_connection):
	global connection
	global mncc_sock
	connection = recv_connection
	mncc_sock = MnccSocket()
	time.sleep(3)
	mncc_call(number, new_callref)
	mncc_rx_thread()