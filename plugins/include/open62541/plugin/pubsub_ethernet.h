/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information.
 *
 *   Copyright 2018 (c) Kontron Europe GmbH (Author: Rudolf Hoyler)
 *   Copyright (c) 2019-2020 Kalycito Infotech Private Limited
 */

#ifndef UA_NETWORK_PUBSUB_ETHERNET_H_
#define UA_NETWORK_PUBSUB_ETHERNET_H_

#include <open62541/plugin/pubsub.h>
#ifdef UA_ENABLE_PUBSUB_CUSTOM_PUBLISH_HANDLING
#include <linux/if_packet.h>
#include <netinet/ether.h>
#include <open62541/plugin/pubsub_ethernet.h>
#endif

_UA_BEGIN_DECLS

UA_PubSubTransportLayer UA_EXPORT
UA_PubSubTransportLayerEthernet(void);

#ifdef UA_ENABLE_PUBSUB_CUSTOM_PUBLISH_HANDLING
/* Function for real time pubsub - txtime calculation*/
UA_StatusCode
txtimecalc_ethernet(UA_PubSubChannel *channel, UA_ExtensionObject *transportSettigns, void *bufSend, int lenBuf, struct sockaddr_ll sll);
#endif
_UA_END_DECLS

#endif /* UA_NETWORK_PUBSUB_ETHERNET_H_ */
