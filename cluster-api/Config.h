// No Copyright. Vladislav Aleinik 2020            
//=================================================
// Config File                                     
//=================================================
// - All system-wide configuration is defined here 
//=================================================
#ifndef COMPUTING_CLUSTER_CONFIG_HPP_INCLUDED
#define COMPUTING_CLUSTER_CONFIG_HPP_INCLUDED

#include <string.h>

//-------------------
// Discovery process 
//-------------------

const int DISCOVERY_SERVER_PORT = 9798;
const int DISCOVERY_CLIENT_PORT = 9799;
const int DISCOVERY_REPEAT_INTERVAL  =    2; // sec

const char   CLIENTS_DISCOVERY_DATAGRAM[]    = "Where are you, CLUSTER-SERVER!";
const size_t CLIENTS_DISCOVERY_DATAGRAM_SIZE = sizeof(CLIENTS_DISCOVERY_DATAGRAM)/sizeof(char);

const char   SERVERS_DISCOVERY_DATAGRAM[]    = "I am here, CLUSTER-CLIENT!";
const size_t SERVERS_DISCOVERY_DATAGRAM_SIZE = sizeof(SERVERS_DISCOVERY_DATAGRAM)/sizeof(char);

//-----------------------
// Connection Management 
//-----------------------

const int   CONNECTION_PORT            =   9798;
const char* CONNECTION_PORT_STR        = "9798";
const int MAX_SIMULTANEOUS_CONNECTIONS =    100;
const int LISTEN_CONNECTION_BACKLOG    =     10;

// TCP-keepalive attributes:
const int TCP_KEEPALIVE_IDLE_TIME  = 1; // sec
const int TCP_KEEPALIVE_INTERVAL   = 1; // sec
const int TCP_KEEPALIVE_NUM_PROBES = 4;


#endif // COMPUTING_CLUSTER_CONFIG_HPP_INCLUDED