# gbnudp2way
Go-back-N method for 2 way UDP communication for reliable channel
Use:
$make

$./server <server-port-number>
$./client <server-ip-address> <server-port-number>

The client and server can send messages at each other. The messages seems lost in output messages, but if you type specific messages you will see them reach on the other side. 
The only missing parts are multiple attempts/FINACK/and assembling the received packets into one buffer at the end. 


