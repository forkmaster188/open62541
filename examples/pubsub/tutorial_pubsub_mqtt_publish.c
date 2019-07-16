/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information.

 * Copyright (c) 2019 Kalycito Infotech Private Limited */
/**
 * .. _pubsub-tutorial:
 *
 * Working with Publish/Subscribe
 * ------------------------------
 *
 * Work in progress:
 * This Tutorial will be continuously extended during the next PubSub batches. More details about
 * the PubSub extension and corresponding open62541 API are located here: :ref:`pubsub`.
 *
 * Publishing Fields
 * ^^^^^^^^^^^^^^^^^
 * The PubSub mqtt publish example demonstrate the simplest way to publish
 * informations from the information model over MQTT using
 * the UADP (or later JSON) encoding.
 * To receive information the subscribe functionality of mqtt is used.
 * A periodical call to yield is necessary to update the mqtt stack.
 *
 * **Connection handling**
 * PubSubConnections can be created and deleted on runtime. More details about the system preconfiguration and
 * connection can be found in ``tutorial_pubsub_connection.c``.
 */

#include "open62541/server.h"
#include "open62541/server_config_default.h"
#include "ua_pubsub.h"
#include "ua_network_pubsub_mqtt.h"
#include "open62541/plugin/log_stdout.h"

#include <signal.h>

static UA_Boolean useJson = false;
static UA_NodeId connectionIdent;
static UA_NodeId publishedDataSetIdent;
static UA_NodeId writerGroupIdent;
UA_NodeId counterVariable; /* Variable to write data into information model */
UA_NodeId currentNodeId;

UA_Variant variablePointer;
UA_Int32 counterValue = 0;  /* Initialize counter value to zero */

static void
addPubSubConnection(UA_Server *server, char *addressUrl) {
    /* Details about the connection configuration and handling are located
     * in the pubsub connection tutorial */
    UA_PubSubConnectionConfig connectionConfig;
    memset(&connectionConfig, 0, sizeof(connectionConfig));
    connectionConfig.name = UA_STRING("MQTT Publisher Connection");
    connectionConfig.transportProfileUri = UA_STRING("http://opcfoundation.org/UA-Profile/Transport/pubsub-mqtt");
    connectionConfig.enabled = UA_TRUE;

    /* configure address of the mqtt broker (local on default port) */
    UA_NetworkAddressUrlDataType networkAddressUrl = {UA_STRING_NULL , UA_STRING(addressUrl)};
    UA_Variant_setScalar(&connectionConfig.address, &networkAddressUrl, &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);
    connectionConfig.publisherId.numeric = UA_UInt32_random();

    /* configure options, set mqtt client id */
    UA_KeyValuePair connectionOptions[1];
    connectionOptions[0].key = UA_QUALIFIEDNAME(0, "mqttClientId");
    UA_String mqttClientId = UA_STRING("TESTCLIENTPUBSUBMQTTPUBLISHER");
    UA_Variant_setScalar(&connectionOptions[0].value, &mqttClientId, &UA_TYPES[UA_TYPES_STRING]);
    connectionConfig.connectionProperties = connectionOptions;
    connectionConfig.connectionPropertiesSize = 1;

    UA_Server_addPubSubConnection(server, &connectionConfig, &connectionIdent);
}

/**
 * **PublishedDataSet handling**
 * The PublishedDataSet (PDS) and PubSubConnection are the toplevel entities and can exist alone. The PDS contains
 * the collection of the published fields.
 * All other PubSub elements are directly or indirectly linked with the PDS or connection.
 */
static void
addPublishedDataSet(UA_Server *server) {
    /* The PublishedDataSetConfig contains all necessary public
    * informations for the creation of a new PublishedDataSet */
    UA_PublishedDataSetConfig publishedDataSetConfig;
    memset(&publishedDataSetConfig, 0, sizeof(UA_PublishedDataSetConfig));
    publishedDataSetConfig.publishedDataSetType = UA_PUBSUB_DATASET_PUBLISHEDITEMS;
    publishedDataSetConfig.name = UA_STRING("Demo PDS");
    /* Create new PublishedDataSet based on the PublishedDataSetConfig. */
    UA_Server_addPublishedDataSet(server, &publishedDataSetConfig, &publishedDataSetIdent);
}

/**
 * **DataSetField handling**
 * The DataSetField (DSF) is part of the PDS and describes exactly one published field.
 */
static void
addDataSetField(UA_Server *server) {
    /* Add fields to the previous created PublishedDataSet */
    UA_DataSetFieldConfig dataSetFieldConfig;
    memset(&dataSetFieldConfig, 0, sizeof(UA_DataSetFieldConfig));
    dataSetFieldConfig.dataSetFieldType = UA_PUBSUB_DATASETFIELD_VARIABLE;

    dataSetFieldConfig.field.variable.fieldNameAlias = UA_STRING("Server localtime");
    dataSetFieldConfig.field.variable.promotedField = UA_FALSE;
    dataSetFieldConfig.field.variable.publishParameters.publishedVariable =
    UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_CURRENTTIME);
    dataSetFieldConfig.field.variable.publishParameters.attributeId = UA_ATTRIBUTEID_VALUE;
    UA_Server_addDataSetField(server, publishedDataSetIdent, &dataSetFieldConfig, NULL);

    UA_DataSetFieldConfig dataSetFieldConfigCounter;
    memset(&dataSetFieldConfigCounter, 0, sizeof(UA_DataSetFieldConfig));
    dataSetFieldConfigCounter.dataSetFieldType = UA_PUBSUB_DATASETFIELD_VARIABLE;

    dataSetFieldConfigCounter.field.variable.fieldNameAlias = UA_STRING("Counter variable");
    dataSetFieldConfigCounter.field.variable.promotedField = UA_FALSE;
    dataSetFieldConfigCounter.field.variable.publishParameters.publishedVariable = counterVariable;
    dataSetFieldConfigCounter.field.variable.publishParameters.attributeId = UA_ATTRIBUTEID_VALUE;
    UA_Server_addDataSetField(server, publishedDataSetIdent, &dataSetFieldConfigCounter, NULL);

}

/**
 * **WriterGroup handling**
 * The WriterGroup (WG) is part of the connection and contains the primary configuration
 * parameters for the message creation.
 */
static void
addWriterGroup(UA_Server *server, int interval) {
    /* Now we create a new WriterGroupConfig and add the group to the existing PubSubConnection. */
    UA_WriterGroupConfig writerGroupConfig;
    memset(&writerGroupConfig, 0, sizeof(UA_WriterGroupConfig));
    writerGroupConfig.name = UA_STRING("Demo WriterGroup");
    writerGroupConfig.publishingInterval = interval;
    writerGroupConfig.enabled = UA_FALSE;
    writerGroupConfig.writerGroupId = 100;

    /* decide whether to use JSON or UADP encoding*/
#ifdef UA_ENABLE_JSON_ENCODING
    if(useJson)
        writerGroupConfig.encodingMimeType = UA_PUBSUB_ENCODING_JSON;
    else
#endif
        writerGroupConfig.encodingMimeType = UA_PUBSUB_ENCODING_UADP;

    /* configure the mqtt publish topic */
    UA_BrokerWriterGroupTransportDataType brokerTransportSettings;
    memset(&brokerTransportSettings, 0, sizeof(UA_BrokerWriterGroupTransportDataType));
    brokerTransportSettings.queueName = UA_STRING(mqttTopic);
    brokerTransportSettings.resourceUri = UA_STRING_NULL;
    brokerTransportSettings.authenticationProfileUri = UA_STRING_NULL;

    /* Choose the QOS Level for MQTT */
    brokerTransportSettings.requestedDeliveryGuarantee = UA_BROKERTRANSPORTQUALITYOFSERVICE_BESTEFFORT;

    /* Encapsulate config in transportSettings */
    UA_ExtensionObject transportSettings;
    memset(&transportSettings, 0, sizeof(UA_ExtensionObject));
    transportSettings.encoding = UA_EXTENSIONOBJECT_DECODED;
    transportSettings.content.decoded.type = &UA_TYPES[UA_TYPES_BROKERWRITERGROUPTRANSPORTDATATYPE];
    transportSettings.content.decoded.data = &brokerTransportSettings;

    writerGroupConfig.transportSettings = transportSettings;
    UA_Server_addWriterGroup(server, connectionIdent, &writerGroupConfig, &writerGroupIdent);
}

/**
 * **DataSetWriter handling**
 * A DataSetWriter (DSW) is the glue between the WG and the PDS. The DSW is linked to exactly one
 * PDS and contains additional informations for the message generation.
 */
static void
addDataSetWriter(UA_Server *server) {
    /* We need now a DataSetWriter within the WriterGroup. This means we must
     * create a new DataSetWriterConfig and add call the addWriterGroup function. */
    UA_NodeId dataSetWriterIdent;
    UA_DataSetWriterConfig dataSetWriterConfig;
    memset(&dataSetWriterConfig, 0, sizeof(UA_DataSetWriterConfig));
    dataSetWriterConfig.name = UA_STRING("Demo DataSetWriter");
    dataSetWriterConfig.dataSetWriterId = 62541;
    dataSetWriterConfig.keyFrameCount = 10;

#ifdef UA_ENABLE_JSON_ENCODING
    UA_JsonDataSetWriterMessageDataType jsonDswMd;
    UA_ExtensionObject messageSettings;
    if(useJson) {
        /* JSON config for the dataSetWriter */
        jsonDswMd.dataSetMessageContentMask = (UA_JsonDataSetMessageContentMask)
            (UA_JSONDATASETMESSAGECONTENTMASK_DATASETWRITERID |
             UA_JSONDATASETMESSAGECONTENTMASK_SEQUENCENUMBER |
             UA_JSONDATASETMESSAGECONTENTMASK_STATUS |
             UA_JSONDATASETMESSAGECONTENTMASK_METADATAVERSION |
             UA_JSONDATASETMESSAGECONTENTMASK_TIMESTAMP);

        messageSettings.encoding = UA_EXTENSIONOBJECT_DECODED;
        messageSettings.content.decoded.type = &UA_TYPES[UA_TYPES_JSONDATASETWRITERMESSAGEDATATYPE];
        messageSettings.content.decoded.data = &jsonDswMd;

        dataSetWriterConfig.messageSettings = messageSettings;
    }
#endif

    UA_Server_addDataSetWriter(server, writerGroupIdent, publishedDataSetIdent,
                               &dataSetWriterConfig, &dataSetWriterIdent);
}

static void publishCallback(UA_Server *server, void *data) {
    UA_Variant_setScalar(&variablePointer, &counterValue, &UA_TYPES[UA_TYPES_INT32]);
    UA_Server_writeValue(server, currentNodeId, variablePointer);
    counterValue++;
}

/**
 * That's it! You're now publishing the selected fields.
 * Open a packet inspection tool of trust e.g. wireshark and take a look on the outgoing packages.
 * The following graphic figures out the packages created by this tutorial.
 *
 * .. figure:: ua-wireshark-pubsub.png
 *     :figwidth: 100 %
 *     :alt: OPC UA PubSub communication in wireshark
 *
 * The open62541 subscriber API will be released later. If you want to process the the datagrams,
 * take a look on the ua_network_pubsub_networkmessage.c which already contains the decoding code for UADP messages.
 *
 * It follows the main server code, making use of the above definitions. */
UA_Boolean running = true;
static void stopHandler(int sign) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "received ctrl-c");
    running = false;
}

static void usage(void) {
    printf("Usage: tutorial_pubsub_mqtt_publish [--url <opc.mqtt://hostname:port>] "
           "[--topic <mqttTopic>] "
           "[--freq <frequency in ms> "
           "[--json]\n"
           "  Defaults are:\n"
           "  - Url: opc.mqtt://127.0.0.1:1883\n"
           "  - Topic: customTopic\n"
           "  - Frequency: 500\n"
           "  - JSON: Off\n");
}

int main(int argc, char **argv) {
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    char *addressUrl = "opc.mqtt://127.0.0.1:1883";
    char *topic = "customTopic";
    int interval = 500;

    /* Parse arguments */
    for(int argpos = 1; argpos < argc; argpos++) {
        if(strcmp(argv[argpos], "--help") == 0) {
            usage();
            return 0;
        }

        if(strcmp(argv[argpos], "--json") == 0) {
            useJson = true;
            continue;
        }

        if(strcmp(argv[argpos], "--url") == 0) {
            if(argpos + 1 == argc) {
                usage();
                return -1;
            }
            argpos++;
            addressUrl = argv[argpos];
            continue;
        }

        if(strcmp(argv[argpos], "--topic") == 0) {
            if(argpos + 1 == argc) {
                usage();
                return -1;
            }
            argpos++;
            topic = argv[argpos];
            continue;
        }

        if(strcmp(argv[argpos], "--freq") == 0) {
            if(argpos + 1 == argc) {
                usage();
                return -1;
            }
            argpos++;
            topic = argv[argpos];
            if(sscanf(argv[argpos], "%d", &interval) != 1) {
                usage();
                return -1;
            }
            if(interval <= 10) {
                UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                               "Publication interval too small");
                return -1;
            }
            continue;
        }

        usage();
        return -1;
    }

    /* Allocate the server and get the server config */
    UA_Server *server = UA_Server_new();
    UA_ServerConfig *config = UA_Server_getConfig(server);
    /* Details about the connection configuration and handling are located in the pubsub connection tutorial */
    UA_ServerConfig_setDefault(config);
     config->pubsubTransportLayers = (UA_PubSubTransportLayer *) UA_malloc(1 * sizeof(UA_PubSubTransportLayer));
    if(!config->pubsubTransportLayers) {
        return -1;
    }
    config->pubsubTransportLayers[0] = UA_PubSubTransportLayerMQTT();
    config->pubsubTransportLayersSize++;

   /* Add an object and attributes of the object into the information model */
    UA_NodeId counterId;
    UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
    oAttr.displayName         = UA_LOCALIZEDTEXT("en-US", "Counter Variable");
    UA_Server_addObjectNode(server, UA_NODEID_NULL,
                            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                            UA_QUALIFIEDNAME(1, "Counter Variable"), UA_NODEID_NULL,
                            oAttr, NULL, &counterId);

    UA_VariableAttributes v1Attr = UA_VariableAttributes_default;
    v1Attr.displayName = UA_LOCALIZEDTEXT("en-US", "Counter Variable");
    v1Attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    currentNodeId      = UA_NODEID_STRING(1, "Counter Variable");
    UA_Server_addVariableNode(server, currentNodeId, counterId,
                              UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                              UA_QUALIFIEDNAME(1, "Counter Variable"),
                              UA_NODEID_NULL, v1Attr, NULL, &counterVariable);

    addPubSubConnection(server, addressUrl);

    /* call back function iteration for every 500ms interval */
    UA_Server_addRepeatedCallback(server, publishCallback, NULL, interval, NULL);
    /* Assign the mqtt Topic to the publisher */
    mqttTopic = topic;
    addPublishedDataSet(server);
    addDataSetField(server);
    addWriterGroup(server, interval);
    addDataSetWriter(server);

    UA_PubSubConnection *connection = UA_PubSubConnection_findConnectionbyId(server, connectionIdent);

    if(!connection) {
        UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                       "Could not create a PubSubConnection");
        UA_Server_delete(server);
        return -1;
    }

    UA_Server_run(server, &running);
    UA_Server_delete(server);
    return 0;
}

