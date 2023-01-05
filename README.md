# gbnudp2way

METU - CENG 435 
Fall 2022/2023 - 2nd Assignment


Go-back-N method for 2 way UDP communication for reliable channel
Use:

$make

$./server <server-port-number>

$./client <server-ip-address> <server-port-number>

The client and server can send messages at each other. The messages seems lost in output messages, but if you type specific messages you will see them reach on the other side. 
There are few parts missing. Sequence numbers fail at longer messages sent by server side. FINACK is there but not functional in this scenario but can play around by uncommenting FIN_SENT part in gbnRcv at client. No multiple attempts to resend a timeout message. 

This was prepared according to the homework scenario. Since this is bidiractional chatting this can all be implemented in one code file and roles could be assigned at runtime.  
