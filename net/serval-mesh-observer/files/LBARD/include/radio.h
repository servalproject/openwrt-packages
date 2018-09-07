#ifndef RADIO_H
#define RADIO_H

//#include "lbard.h"

// Set packet TX interval details (all in ms)
#define INITIAL_AVG_PACKET_TX_INTERVAL 1000
#define INITIAL_PACKET_TX_INTERVAL_RANDOMNESS 250


extern int message_update_interval;
extern int message_update_interval_randomness;

extern int serial_errors;
#endif