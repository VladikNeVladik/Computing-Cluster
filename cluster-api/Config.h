// No Copyright. Vladislav Aleinik 2020            
//=================================================
// Config File                                     
//=================================================
// - All system-wide configuration is defined here 
//=================================================
#ifndef COMPUTING_CLUSTER_CONFIG_HPP_INCLUDED
#define COMPUTING_CLUSTER_CONFIG_HPP_INCLUDED

//-------------------
// Discovery process 
//-------------------

#define DISCOVERY_PORT 9787
#define DISCOVERY_REPEAT_TIME 2 // sec
#define DISCOVERY_DATAGRAM_SIZE 16

//-----------------
// Server Tracking 
//-----------------

#define TIMEOUT_NO_DISCOVERY_DATAGRAMS_FROM_SERVER 10 // sec

//-----------------------
// Connection Management 
//-----------------------

#define CONNECTION_PORT 9798
#define LISTEN_CONNECTION_BACKLOG 10
#define MAX_SIMULTANEOUS_CONNECTIONS 100

// TCP-keepalive attributes:
#define TCP_KEEPALIVE_IDLE_TIME  1 // sec
#define TCP_KEEPALIVE_INTERVAL   1 // sec
#define TCP_KEEPALIVE_NUM_PROBES 4


#endif // COMPUTING_CLUSTER_CONFIG_HPP_INCLUDED