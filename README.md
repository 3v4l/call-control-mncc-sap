# call-control-mncc-sap
Call control services (MS side) in Layer3 (CM) partial protocol stack implementation for MNCC-SAP access points
MNCC stands for Mobile Network Call Control
The repository contains an implementation of elementary procedures for Circuit Switched (CS) Call Control (CC) in 3GPP. <br>
<h3><b><i> Inspiration </i></b></h3>
This repository has been inspired by <i> Harald Welte et al. and osmocom projects.</i><br>
It is a reverse engineering project by: <br><br>
<ul>
  <li>
  	<a href="http://www.etsi.org/deliver/etsi_ts/124000_124099/124007/14.00.00_60/ts_124007v140000p.pdf"> 3GPP specification of MNCC-SAP </a>
  </li>
  <li>
  	<a href="http://laforge.gnumonks.org/blog/20151202-mncc-python/"> Call Conrol in Python for OpenNITB MNCC Interface (not OsmocomBB ~ although interface more or less is the same)</a>
  </li>
  <li><a href="http://www.linux-call-router.de/">Linux Call Router has an implementation for bridging calls (MNCC socket interface)</a></li>
</ul>
<h3>Description:</h3>
The project has been tested with OsmcomBB MNCC socket interface (/tmp/ms_mncc_). This external socket allows you to build L3 Messages that acts as an external call control application and send it to the OsmocomBB compatible phone. <br>
It has two implementation, one is C and the other in Python.


 
