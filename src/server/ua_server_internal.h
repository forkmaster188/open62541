/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *    Copyright 2014-2018 (c) Fraunhofer IOSB (Author: Julius Pfrommer)
 *    Copyright 2014, 2017 (c) Florian Palm
 *    Copyright 2015-2016 (c) Sten Grüner
 *    Copyright 2015 (c) Chris Iatrou
 *    Copyright 2015-2016 (c) Oleksiy Vasylyev
 *    Copyright 2016-2017 (c) Stefan Profanter, fortiss GmbH
 *    Copyright 2017 (c) Julian Grothoff
 */

#ifndef UA_SERVER_INTERNAL_H_
#define UA_SERVER_INTERNAL_H_

#include "ua_util_internal.h"
#include "ua_server.h"
#include "ua_server_config.h"
#include "ua_timer.h"
#include "ua_connection_internal.h"
#include "ua_session_manager.h"
#include "ua_securechannel_manager.h"
#include "ua_workqueue.h"

_UA_BEGIN_DECLS

#ifdef UA_ENABLE_PUBSUB
#include "ua_pubsub_manager.h"
#endif

#ifdef UA_ENABLE_DISCOVERY
#include "ua_discovery_manager.h"
#endif

#ifdef UA_ENABLE_GDS
#include "ua_registration_manager.h"
#ifdef UA_ENABLE_GDS_CM
#include "ua_certificate_manager.h"
#endif
#endif

#ifdef UA_ENABLE_SERVER_PUSH
#include "ua_ca_gnutls.h"
#include <gnutls/x509.h>
#endif

#ifdef UA_ENABLE_SUBSCRIPTIONS
#include "ua_subscription.h"

typedef struct {
    UA_MonitoredItem monitoredItem;
    void *context;
    union {
        UA_Server_DataChangeNotificationCallback dataChangeCallback;
        /* UA_Server_EventNotificationCallback eventCallback; */
    } callback;
} UA_LocalMonitoredItem;

#endif

struct UA_Server {
    /* Config */
    UA_ServerConfig config;
    UA_DateTime startTime;

    /* Security */
    UA_SecureChannelManager secureChannelManager;
    UA_SessionManager sessionManager;
    UA_Session adminSession; /* Local access to the services (for startup and
                              * maintenance) uses this Session with all possible
                              * access rights (Session Id: 1) */

    /* Namespaces */
    size_t namespacesSize;
    UA_String *namespaces;

    /* Callbacks with a repetition interval */
    UA_Timer timer;

    /* WorkQueue and worker threads */
    UA_WorkQueue workQueue;

    /* For bootstrapping, omit some consistency checks, creating a reference to
     * the parent and member instantiation */
    UA_Boolean bootstrapNS0;

#ifdef UA_ENABLE_GDS
    LIST_HEAD(gds_list, gds_registeredServer_entry) gds_registeredServers_list;
    size_t gds_registeredServersSize;
#ifdef UA_ENABLE_GDS_CM
    UA_GDS_CertificateManager certificateManager;
#endif
#endif

    /* Discovery */
#ifdef UA_ENABLE_DISCOVERY
    UA_DiscoveryManager discoveryManager;
#endif

    /* Local MonitoredItems */
#ifdef UA_ENABLE_SUBSCRIPTIONS
    /* To be cast to UA_LocalMonitoredItem to get the callback and context */
    LIST_HEAD(LocalMonitoredItems, UA_MonitoredItem) localMonitoredItems;
    UA_UInt32 lastLocalMonitoredItemId;
#endif

    /* Publish/Subscribe */
#ifdef UA_ENABLE_PUBSUB
    UA_PubSubManager pubSubManager;
#endif
};

/*****************/
/* Node Handling */
/*****************/

#define UA_Nodestore_get(SERVER, NODEID)                                \
    (SERVER)->config.nodestore.getNode((SERVER)->config.nodestore.context, NODEID)

#define UA_Nodestore_release(SERVER, NODEID)                            \
    (SERVER)->config.nodestore.releaseNode((SERVER)->config.nodestore.context, NODEID)

#define UA_Nodestore_new(SERVER, NODECLASS)                               \
    (SERVER)->config.nodestore.newNode((SERVER)->config.nodestore.context, NODECLASS)

#define UA_Nodestore_getCopy(SERVER, NODEID, OUTNODE)                   \
    (SERVER)->config.nodestore.getNodeCopy((SERVER)->config.nodestore.context, NODEID, OUTNODE)

#define UA_Nodestore_insert(SERVER, NODE, OUTNODEID)                    \
    (SERVER)->config.nodestore.insertNode((SERVER)->config.nodestore.context, NODE, OUTNODEID)

#define UA_Nodestore_delete(SERVER, NODE)                               \
    (SERVER)->config.nodestore.deleteNode((SERVER)->config.nodestore.context, NODE)

#define UA_Nodestore_remove(SERVER, NODEID)                             \
    (SERVER)->config.nodestore.removeNode((SERVER)->config.nodestore.context, NODEID)

/* Deletes references from the node which are not matching any type in the given
 * array. Could be used to e.g. delete all the references, except
 * 'HASMODELINGRULE' */
void UA_Node_deleteReferencesSubset(UA_Node *node, size_t referencesSkipSize,
                                    UA_NodeId* referencesSkip);

/* Calls the callback with the node retrieved from the nodestore on top of the
 * stack. Either a copy or the original node for in-situ editing. Depends on
 * multithreading and the nodestore.*/
typedef UA_StatusCode (*UA_EditNodeCallback)(UA_Server*, UA_Session*,
                                             UA_Node *node, void*);
UA_StatusCode UA_Server_editNode(UA_Server *server, UA_Session *session,
                                 const UA_NodeId *nodeId,
                                 UA_EditNodeCallback callback,
                                 void *data);

/*********************/
/* Utility Functions */
/*********************/

/* A few global NodeId definitions */
extern const UA_NodeId subtypeId;
extern const UA_NodeId hierarchicalReferences;

UA_UInt16 addNamespace(UA_Server *server, const UA_String name);

UA_Boolean
UA_Node_hasSubTypeOrInstances(const UA_Node *node);

/* Recursively searches "upwards" in the tree following specific reference types */
UA_Boolean
isNodeInTree(UA_Nodestore *ns, const UA_NodeId *leafNode,
             const UA_NodeId *nodeToFind, const UA_NodeId *referenceTypeIds,
             size_t referenceTypeIdsSize);

/* Returns an array with the hierarchy of type nodes. The returned array starts
 * at the leaf and continues "upwards" or "downwards" in the hierarchy based on the
 * ``hasSubType`` references. Since multiple-inheritance is possible in general,
 * duplicate entries are removed.
 * The parameter `walkDownwards` indicates the direction of search.
 * If set to TRUE it will get all the subtypes of the given
 * leafType (including leafType).
 * If set to FALSE it will get all the parent types of the given
 * leafType (including leafType)*/
UA_StatusCode
getTypeHierarchy(UA_Nodestore *ns, const UA_NodeId *leafType,
                 UA_NodeId **typeHierarchy, size_t *typeHierarchySize,
                 UA_Boolean walkDownwards);

/* Same as getTypeHierarchy but takes multiple leafTypes as parameter and returns
 * an combined list of all the found types for all the leaf types */
UA_StatusCode
getTypesHierarchy(UA_Nodestore *ns, const UA_NodeId *leafType, size_t leafTypeSize,
                 UA_NodeId **typeHierarchy, size_t *typeHierarchySize,
                 UA_Boolean walkDownwards);

/* Returns the type node from the node on the stack top. The type node is pushed
 * on the stack and returned. */
const UA_Node * getNodeType(UA_Server *server, const UA_Node *node);

/* Write a node attribute with a defined session */
UA_StatusCode
UA_Server_writeWithSession(UA_Server *server, UA_Session *session,
                           const UA_WriteValue *value);


/* Many services come as an array of operations. This function generalizes the
 * processing of the operations. */
typedef void (*UA_ServiceOperation)(UA_Server *server, UA_Session *session,
                                    void *context,
                                    const void *requestOperation,
                                    void *responseOperation);

UA_StatusCode
UA_Server_processServiceOperations(UA_Server *server, UA_Session *session,
                                   UA_ServiceOperation operationCallback,
                                   void *context,
                                   const size_t *requestOperations,
                                   const UA_DataType *requestOperationsType,
                                   size_t *responseOperations,
                                   const UA_DataType *responseOperationsType)
    UA_FUNC_ATTR_WARN_UNUSED_RESULT;

/***************************************/
/* Check Information Model Consistency */
/***************************************/

/* Read a node attribute in the context of a "checked-out" node. So the
 * attribute will not be copied when possible. The variant then points into the
 * node and has UA_VARIANT_DATA_NODELETE set. */
void
ReadWithNode(const UA_Node *node, UA_Server *server, UA_Session *session,
             UA_TimestampsToReturn timestampsToReturn,
             const UA_ReadValueId *id, UA_DataValue *v);

UA_StatusCode
readValueAttribute(UA_Server *server, UA_Session *session,
                   const UA_VariableNode *vn, UA_DataValue *v);

/* Test whether the value matches a variable definition given by
 * - datatype
 * - valueranke
 * - array dimensions.
 * Sometimes it can be necessary to transform the content of the value, e.g.
 * byte array to bytestring or uint32 to some enum. If editableValue is non-NULL,
 * we try to create a matching variant that points to the original data. */
UA_Boolean
compatibleValue(UA_Server *server, UA_Session *session, const UA_NodeId *targetDataTypeId,
                UA_Int32 targetValueRank, size_t targetArrayDimensionsSize,
                const UA_UInt32 *targetArrayDimensions, const UA_Variant *value,
                const UA_NumericRange *range);

UA_Boolean
compatibleArrayDimensions(size_t constraintArrayDimensionsSize,
                          const UA_UInt32 *constraintArrayDimensions,
                          size_t testArrayDimensionsSize,
                          const UA_UInt32 *testArrayDimensions);

UA_Boolean
compatibleValueArrayDimensions(const UA_Variant *value, size_t targetArrayDimensionsSize,
                               const UA_UInt32 *targetArrayDimensions);

UA_Boolean
compatibleValueRankArrayDimensions(UA_Server *server, UA_Session *session,
                                   UA_Int32 valueRank, size_t arrayDimensionsSize);

UA_Boolean
compatibleDataType(UA_Server *server, const UA_NodeId *dataType,
                   const UA_NodeId *constraintDataType, UA_Boolean isValue);

UA_Boolean
compatibleValueRanks(UA_Int32 valueRank, UA_Int32 constraintValueRank);

void
Operation_Browse(UA_Server *server, UA_Session *session, UA_UInt32 *maxrefs,
                 const UA_BrowseDescription *descr, UA_BrowseResult *result);

UA_DataValue
UA_Server_readWithSession(UA_Server *server, UA_Session *session,
                          const UA_ReadValueId *item,
                          UA_TimestampsToReturn timestampsToReturn);

/*****************************/
/* AddNodes Begin and Finish */
/*****************************/

/* Creates a new node in the nodestore. */
UA_StatusCode
AddNode_raw(UA_Server *server, UA_Session *session, void *nodeContext,
            const UA_AddNodesItem *item, UA_NodeId *outNewNodeId);

/* Check the reference to the parent node; Add references. */
UA_StatusCode
AddNode_addRefs(UA_Server *server, UA_Session *session, const UA_NodeId *nodeId,
                const UA_NodeId *parentNodeId, const UA_NodeId *referenceTypeId,
                const UA_NodeId *typeDefinitionId);

/* Type-check type-definition; Run the constructors */
UA_StatusCode
AddNode_finish(UA_Server *server, UA_Session *session, const UA_NodeId *nodeId);

/**********************/
/* Create Namespace 0 */
/**********************/

UA_StatusCode UA_Server_initNS0(UA_Server *server);

/**********************/
/* Create Namespace for GDS */
/**********************/
#ifdef UA_ENABLE_GDS
UA_StatusCode UA_GDS_initNS(UA_Server *server);
UA_StatusCode UA_GDS_deinitNS(UA_Server *server);
#endif

#ifdef UA_ENABLE_SERVER_PUSH
UA_StatusCode copy_private_key_gnu_struc(gnutls_datum_t *data_privkey,
                                         UA_ByteString *privkey_copy);

UA_StatusCode create_csr(UA_Server *server, UA_String *subjectName,
                         UA_ByteString *certificateRequest);

UA_StatusCode server_update_certificate(UA_Server *server, UA_ByteString *certificate,
                                        UA_Boolean *applyChangesRequired);
UA_StatusCode
UA_GDS_CreateSigningRequest(UA_Server *server,
                            UA_NodeId *certificateGroupId,
                            UA_NodeId *certificateTypeId,
                            UA_String *subjectName,
                            UA_Boolean *regeneratePrivateKey,
                            UA_ByteString * nonce,
                            UA_ByteString *certificateRequest);
UA_StatusCode
UA_GDS_UpdateCertificate(UA_Server *server,
                         const UA_NodeId *certificateGroupId,
                         const UA_NodeId *certificateTypeId,
                         UA_ByteString *certificate,
                         UA_ByteString *issuerCertificates,
                         UA_String *privateKeyFormat,
                         UA_ByteString *privateKey,
                         UA_Boolean *applyChangesRequired);
UA_StatusCode UA_SERVER_initpushmanager(UA_Server *server);
#endif
_UA_END_DECLS

#endif /* UA_SERVER_INTERNAL_H_ */
