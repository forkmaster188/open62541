/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. 
 *
 *    Copyright 2014-2018 (c) Fraunhofer IOSB (Author: Julius Pfrommer)
 *    Copyright 2014-2017 (c) Florian Palm
 *    Copyright 2015-2016 (c) Sten Grüner
 *    Copyright 2015-2016 (c) Chris Iatrou
 *    Copyright 2015 (c) LEvertz
 *    Copyright 2015-2016 (c) Oleksiy Vasylyev
 *    Copyright 2016 (c) Julian Grothoff
 *    Copyright 2016-2017 (c) Stefan Profanter, fortiss GmbH
 *    Copyright 2016 (c) Lorenz Haas
 *    Copyright 2017 (c) frax2222
 *    Copyright 2017 (c) Mark Giraud, Fraunhofer IOSB
 *    Copyright 2018 (c) Hilscher Gesellschaft für Systemautomation mbH (Author: Martin Lang)
 */

#include "ua_server_internal.h"
#include "ua_securitypolicies.h"

#ifdef UA_ENABLE_SERVER_PUSH
#include "ua_ca_gnutls.h"
#include <gnutls/x509.h>
#endif

#ifdef UA_ENABLE_PUBSUB_INFORMATIONMODEL
#include "ua_pubsub_ns0.h"
#endif

#ifdef UA_ENABLE_SUBSCRIPTIONS
#include "ua_subscription.h"
#endif

#ifdef UA_ENABLE_GDS_CM
#include "ua_certificate_manager.h"
#endif

#ifdef UA_ENABLE_VALGRIND_INTERACTIVE
#include <valgrind/memcheck.h>
#endif

/**********************/
/* Namespace Handling */
/**********************/

UA_UInt16 addNamespace(UA_Server *server, const UA_String name) {
    /* Check if the namespace already exists in the server's namespace array */
    for(UA_UInt16 i = 0; i < server->namespacesSize; ++i) {
        if(UA_String_equal(&name, &server->namespaces[i]))
            return i;
    }

    /* Make the array bigger */
    UA_String *newNS = (UA_String*)UA_realloc(server->namespaces,
                                              sizeof(UA_String) * (server->namespacesSize + 1));
    if(!newNS)
        return 0;
    server->namespaces = newNS;

    /* Copy the namespace string */
    UA_StatusCode retval = UA_String_copy(&name, &server->namespaces[server->namespacesSize]);
    if(retval != UA_STATUSCODE_GOOD)
        return 0;

    /* Announce the change (otherwise, the array appears unchanged) */
    ++server->namespacesSize;
    return (UA_UInt16)(server->namespacesSize - 1);
}

UA_UInt16 UA_Server_addNamespace(UA_Server *server, const char* name) {
    /* Override const attribute to get string (dirty hack) */
    UA_String nameString;
    nameString.length = strlen(name);
    nameString.data = (UA_Byte*)(uintptr_t)name;
    return addNamespace(server, nameString);
}

UA_ServerConfig*
UA_Server_getConfig(UA_Server *server)
{
  if(!server)
    return NULL;
  else
    return &server->config;
}

UA_StatusCode
UA_Server_getNamespaceByName(UA_Server *server, const UA_String namespaceUri,
                             size_t* foundIndex) {
  for(size_t idx = 0; idx < server->namespacesSize; idx++)
  {
    if(UA_String_equal(&server->namespaces[idx], &namespaceUri) == true)
    {
      (*foundIndex) = idx;
      return UA_STATUSCODE_GOOD;
    }
  }

  return UA_STATUSCODE_BADNOTFOUND;
}

UA_StatusCode
UA_Server_forEachChildNodeCall(UA_Server *server, UA_NodeId parentNodeId,
                               UA_NodeIteratorCallback callback, void *handle) {
    const UA_Node *parent =
        server->config.nodestore.getNode(server->config.nodestore.context,
                                         &parentNodeId);
    if(!parent)
        return UA_STATUSCODE_BADNODEIDINVALID;

    /* TODO: We need to do an ugly copy of the references array since users may
     * delete references from within the callback. In single-threaded mode this
     * changes the same node we point at here. In multi-threaded mode, this
     * creates a new copy as nodes are truly immutable.
     * The callback could remove a node via the regular public API.
     * This can remove a member of the nodes-array we iterate over...
     * */
    UA_Node *parentCopy = UA_Node_copy_alloc(parent);
    if(!parentCopy) {
        server->config.nodestore.releaseNode(server->config.nodestore.context, parent);
        return UA_STATUSCODE_BADUNEXPECTEDERROR;
    }

    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    for(size_t i = parentCopy->referencesSize; i > 0; --i) {
        UA_NodeReferenceKind *ref = &parentCopy->references[i - 1];
        for(size_t j = 0; j<ref->targetIdsSize; j++) {
            retval = callback(ref->targetIds[j].nodeId, ref->isInverse,
                              ref->referenceTypeId, handle);
            if(retval != UA_STATUSCODE_GOOD)
                goto cleanup;
        }
    }

cleanup:
    UA_Node_deleteMembers(parentCopy);
    UA_free(parentCopy);

    server->config.nodestore.releaseNode(server->config.nodestore.context, parent);
    return retval;
}

/********************/
/* Server Lifecycle */
/********************/

/* The server needs to be stopped before it can be deleted */
void UA_Server_delete(UA_Server *server) {
    /* Delete all internal data */
    UA_SecureChannelManager_deleteMembers(&server->secureChannelManager);
    UA_SessionManager_deleteMembers(&server->sessionManager);
    UA_Array_delete(server->namespaces, server->namespacesSize, &UA_TYPES[UA_TYPES_STRING]);

#ifdef UA_ENABLE_SUBSCRIPTIONS
    UA_MonitoredItem *mon, *mon_tmp;
    LIST_FOREACH_SAFE(mon, &server->localMonitoredItems, listEntry, mon_tmp) {
        LIST_REMOVE(mon, listEntry);
        UA_MonitoredItem_delete(server, mon);
    }
#endif

#ifdef UA_ENABLE_PUBSUB
    UA_PubSubManager_delete(server, &server->pubSubManager);
#endif

#ifdef UA_ENABLE_GDS
    UA_GDS_RegistrationManager_close(server);
#ifdef UA_ENABLE_GDS_CM
    UA_GDS_CertificateManager_close(server);
#endif
    UA_GDS_deinitNS(server);
#endif

#ifdef UA_ENABLE_DISCOVERY
    UA_DiscoveryManager_deleteMembers(&server->discoveryManager, server);
#endif

    /* Clean up the Admin Session */
    UA_Session_deleteMembersCleanup(&server->adminSession, server);

    /* Clean up the work queue */
    UA_WorkQueue_cleanup(&server->workQueue);

    /* Delete the timed work */
    UA_Timer_deleteMembers(&server->timer);

    /* Delete the server itself */
    UA_free(server);
}

#ifdef UA_ENABLE_SERVER_PUSH
UA_StatusCode copy_private_key_gnu_struc(gnutls_datum_t *data_privkey, UA_ByteString *privkey_copy) {
    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    data_privkey->data = (unsigned char *)UA_malloc(privkey_copy->length + 1);
    if (data_privkey->data == NULL)
        return UA_STATUSCODE_BADOUTOFMEMORY;

    data_privkey->size = (unsigned int)(privkey_copy->length + 1);

    memcpy(data_privkey->data, privkey_copy->data, privkey_copy->length);
    data_privkey->data[privkey_copy->length] = '\0';
    data_privkey->size--;

    return retval;
}

/* To Do: Need to move it to the plugin file*/
/* Creation of Certificate Signing Request */
UA_StatusCode create_csr(UA_Server *server, UA_String *subjectName,
                         UA_Boolean *regeneratePrivateKey,
                         UA_ByteString *certificateRequest) {

    gnutls_x509_crq_t crq;
    gnutls_x509_privkey_t private_key;
    UA_String subjectName_nullTerminated;
    unsigned char buffer[10 * 1024];
    size_t buffer_size   = sizeof(buffer);
    UA_StatusCode retval = UA_STATUSCODE_GOOD;

    /* Initialize an empty certificate request */
    int gnuErr = gnutls_x509_crq_init(&crq);
    if (gnuErr < 0) {
        gnutls_x509_crq_deinit(crq);
        return UA_STATUSCODE_BADOUTOFMEMORY;
    }

    /* Initialize an empty private key */
    gnutls_x509_privkey_init(&private_key);
    /* UA_GNUTLS_ERRORHANDLING_RETURN(UA_STATUSCODE_BADOUTOFMEMORY); */

    if (*regeneratePrivateKey == 1) {
        unsigned int security_bits;

        /* To-do: Nonce the additional entropy functionality */

        /* Generate an RSA key of high security */
        security_bits = gnutls_sec_param_to_pk_bits(GNUTLS_PK_RSA,
                                                    GNUTLS_SEC_PARAM_HIGH);

        /* Create a private key */
        gnutls_x509_privkey_generate(private_key, GNUTLS_PK_RSA, security_bits, 0);
        /* UA_GNUTLS_ERRORHANDLING_RETURN(UA_STATUSCODE_BADINTERNALERROR); */

        /* To-do: Private key storage and upload while calling UpdateCertificate method */
    }
    else {
        gnutls_datum_t data_privkey;
        gnutls_datum_t test_key;
        UA_ByteString privkey_copy;

        UA_SecurityPolicy *securityPolicy = &server->config.endpoints[1].securityPolicy;
        retval = private_key_abstraction(securityPolicy, &privkey_copy);
        if(retval != UA_STATUSCODE_GOOD)
            return retval;

        retval = copy_private_key_gnu_struc(&data_privkey, &privkey_copy);
/*---------------------------------------------------------------------------------------------------*/
    /*********** To do: Should change to key obtained from ****************
     ******** mbedtls struc instead of loading the private key ************
     **********************************************************************/
/*---------------------------------------------------------------------------------------------------*/
        gnuErr = gnutls_load_file("<private key path>/<private_key>.der", &test_key);
        if(gnuErr < 0) {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                        "Loading file was unsuccessful, verify your private key path\n");
            return UA_STATUSCODE_BADINTERNALERROR;
        }

        gnuErr = gnutls_x509_privkey_import2(private_key, &test_key,
                                             GNUTLS_X509_FMT_DER, NULL, 0);
        if (gnuErr < 0) {
            return UA_STATUSCODE_BADINTERNALERROR;
        }

    }

    //gnutls_x509_crt_set_dn requires null terminated string
    subjectName_nullTerminated.length = subjectName->length + 1;
    subjectName_nullTerminated.data = (UA_Byte *)
            UA_calloc(subjectName_nullTerminated.length, sizeof(UA_Byte));
    memcpy(subjectName_nullTerminated.data, subjectName->data, subjectName->length);
    subjectName_nullTerminated.length--;

    /* Add subject name to the distinguished name */
    gnuErr = gnutls_x509_crq_set_dn(crq, (char *) subjectName_nullTerminated.data, NULL);
    /* UA_GNUTLS_ERRORHANDLING_RETURN(UA_STATUSCODE_BADSECURITYCHECKSFAILED); */

    /* Set the request version to 3 */
    gnuErr = gnutls_x509_crq_set_version(crq, 3);
    /* UA_GNUTLS_ERRORHANDLING_RETURN(UA_STATUSCODE_BADSECURITYCHECKSFAILED); */

    /* Associate the request with the private key */
    gnuErr = gnutls_x509_crq_set_key(crq, private_key);
    /* UA_GNUTLS_ERRORHANDLING_RETURN(UA_STATUSCODE_BADSECURITYCHECKSFAILED); */

    /* Self sign the certificate request */
    gnuErr = gnutls_x509_crq_sign2(crq, private_key, GNUTLS_DIG_SHA1, 0);
    /* UA_GNUTLS_ERRORHANDLING_RETURN(UA_STATUSCODE_BADSECURITYCHECKSFAILED); */

    /* Export the PEM encoded certificate request, and display it */
    gnuErr = gnutls_x509_crq_export(crq, GNUTLS_X509_FMT_DER, buffer,
                           &buffer_size);
    /* UA_GNUTLS_ERRORHANDLING_RETURN(UA_STATUSCODE_BADSECURITYCHECKSFAILED); */

    /* Allocate the output buffer */
    retval = UA_ByteString_allocBuffer(certificateRequest, buffer_size);
    if(retval != UA_STATUSCODE_GOOD)
        return retval;

    /* Copy the output to the certificate */
    certificateRequest->length = buffer_size;
    //UA_GDS_CM_CHECK_ALLOC(ret);
    memcpy(certificateRequest->data, buffer, buffer_size);

    return retval;

}

UA_StatusCode
UA_GDS_CreateSigningRequest(UA_Server *server,
                            UA_NodeId *certificateGroupId,
                            UA_NodeId *certificateTypeId,
                            UA_String *subjectName,
                            UA_Boolean *regeneratePrivateKey,
                            UA_ByteString *nonce,
                            UA_ByteString *certificateRequest){

    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    UA_ByteString output;

    if (subjectName == NULL) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    /* Create csr for requesting the certificate */
    retval = create_csr(server, subjectName, regeneratePrivateKey, &output);

    /* Allocate the output buffer */
    retval = UA_ByteString_allocBuffer(certificateRequest, output.length);
    if(retval != UA_STATUSCODE_GOOD)
        return retval;

    /* Copy the output to the certificate */
    certificateRequest->length = output.length;
    memcpy(certificateRequest->data, output.data, output.length);

    return retval;
}

/* Callbacks for server push management */
static UA_StatusCode
createSigningRequestMethodCallback (UA_Server *server,
                    const UA_NodeId *sessionId, void *sessionHandle,
                    const UA_NodeId *methodId, void *methodContext,
                    const UA_NodeId *objectId, void *objectContext,
                    size_t inputSize, const UA_Variant *input,
                    size_t outputSize, UA_Variant *output) {
    UA_ByteString certrequest ;
    UA_StatusCode retval = UA_GDS_CreateSigningRequest(server,
                                                       (UA_NodeId *) input[0].data,
                                                       (UA_NodeId *) input[1].data,
                                                       (UA_String *) input[2].data,
                                                       (UA_Boolean *) input[3].data,
                                                       (UA_ByteString *) input[4].data,
                                                       &certrequest);

    if (retval == UA_STATUSCODE_GOOD)
        UA_Variant_setScalarCopy(output, &certrequest, &UA_TYPES[UA_TYPES_BYTESTRING]);

    return retval;
}
#endif

/* Recurring cleanup. Removing unused and timed-out channels and sessions */
static void
UA_Server_cleanup(UA_Server *server, void *_) {
    UA_DateTime nowMonotonic = UA_DateTime_nowMonotonic();
    UA_SessionManager_cleanupTimedOut(&server->sessionManager, nowMonotonic);
    UA_SecureChannelManager_cleanupTimedOut(&server->secureChannelManager, nowMonotonic);
#ifdef UA_ENABLE_DISCOVERY
    UA_Discovery_cleanupTimedOut(server, nowMonotonic);
#endif
}

#ifdef UA_ENABLE_SERVER_PUSH
UA_StatusCode UA_SERVER_initpushmanager(UA_Server *server) {
    UA_Server_setMethodNode_callback(server, UA_NODEID_NUMERIC(0, 12737), &createSigningRequestMethodCallback);
    return UA_STATUSCODE_GOOD;
}
#endif

/********************/
/* Server Lifecycle */
/********************/

UA_Server *
UA_Server_new(const UA_ServerConfig *config) {
    /* A config is required */
    if(!config)
        return NULL;

    /* Allocate the server */
    UA_Server *server = (UA_Server *)UA_calloc(1, sizeof(UA_Server));
    if(!server)
        return NULL;

    /* Set the config */
    server->config = *config;

    /* Init start time to zero, the actual start time will be sampled in
     * UA_Server_run_startup() */
    server->startTime = 0;

    /* Set a seed for non-cyptographic randomness */
#ifndef UA_ENABLE_DETERMINISTIC_RNG
    UA_random_seed((UA_UInt64)UA_DateTime_now());
#endif

    /* Initialize the handling of repeated callbacks */
    UA_Timer_init(&server->timer);

    UA_WorkQueue_init(&server->workQueue);

    /* Initialize the adminSession */
    UA_Session_init(&server->adminSession);
    server->adminSession.sessionId.identifierType = UA_NODEIDTYPE_GUID;
    server->adminSession.sessionId.identifier.guid.data1 = 1;
    server->adminSession.validTill = UA_INT64_MAX;

    /* Create Namespaces 0 and 1 */
    server->namespaces = (UA_String *)UA_Array_new(2, &UA_TYPES[UA_TYPES_STRING]);
    server->namespaces[0] = UA_STRING_ALLOC("http://opcfoundation.org/UA/");
    UA_String_copy(&server->config.applicationDescription.applicationUri, &server->namespaces[1]);
    server->namespacesSize = 2;

    /* Initialized SecureChannel and Session managers */
    UA_SecureChannelManager_init(&server->secureChannelManager, server);
    UA_SessionManager_init(&server->sessionManager, server);

    /* Add a regular callback for cleanup and maintenance. With a 10s interval. */
    UA_Server_addRepeatedCallback(server, (UA_ServerCallback)UA_Server_cleanup, NULL,
                                  10000.0, NULL);

    /* Initialized discovery */
#ifdef UA_ENABLE_DISCOVERY
    UA_DiscoveryManager_init(&server->discoveryManager, server);
#endif

    /* Initialize namespace 0*/
    UA_StatusCode retVal = UA_Server_initNS0(server);
    if(retVal != UA_STATUSCODE_GOOD) {
        UA_LOG_ERROR(&config->logger, UA_LOGCATEGORY_SERVER,
                     "Namespace 0 could not be bootstrapped with error %s. "
                     "Shutting down the server.",
                     UA_StatusCode_name(retVal));
        UA_Server_delete(server);
        return NULL;
    }

    /* Build PubSub information model */
#ifdef UA_ENABLE_PUBSUB_INFORMATIONMODEL
    UA_Server_initPubSubNS0(server);
#endif

#ifdef UA_ENABLE_GDS
    UA_GDS_initNS(server);
    UA_GDS_RegistrationManager_init(server);
#ifdef UA_ENABLE_GDS_CM
    UA_GDS_CertificateManager_init(server);
#endif
#endif

#ifdef UA_ENABLE_SERVER_PUSH
    UA_SERVER_initpushmanager(server);
#endif

    return server;
}

/*****************/
/* Repeated Jobs */
/*****************/

UA_StatusCode
UA_Server_addRepeatedCallback(UA_Server *server, UA_ServerCallback callback,
                              void *data, UA_Double interval_ms,
                              UA_UInt64 *callbackId) {
    return UA_Timer_addRepeatedCallback(&server->timer,
                                        (UA_ApplicationCallback)callback,
                                        server, data, interval_ms, callbackId);
}

UA_StatusCode
UA_Server_changeRepeatedCallbackInterval(UA_Server *server, UA_UInt64 callbackId,
                                         UA_Double interval_ms) {
    return UA_Timer_changeRepeatedCallbackInterval(&server->timer, callbackId,
                                                   interval_ms);
}

UA_StatusCode
UA_Server_removeRepeatedCallback(UA_Server *server, UA_UInt64 callbackId) {
    return UA_Timer_removeRepeatedCallback(&server->timer, callbackId);
}

UA_StatusCode UA_EXPORT
UA_Server_updateCertificate(UA_Server *server,
                            const UA_ByteString *oldCertificate,
                            const UA_ByteString *newCertificate,
                            const UA_ByteString *newPrivateKey,
                            UA_Boolean closeSessions,
                            UA_Boolean closeSecureChannels) {

    if (server == NULL || oldCertificate == NULL
        || newCertificate == NULL || newPrivateKey == NULL) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    if (closeSessions) {
        UA_SessionManager *sm = &server->sessionManager;
        session_list_entry *current;
        LIST_FOREACH(current, &sm->sessions, pointers) {
            if (UA_ByteString_equal(oldCertificate,
                                    &current->session.header.channel->securityPolicy->localCertificate)) {
                UA_SessionManager_removeSession(sm, &current->session.header.authenticationToken);
            }
        }

    }

    if (closeSecureChannels) {
        UA_SecureChannelManager *cm = &server->secureChannelManager;
        channel_entry *entry;
        TAILQ_FOREACH(entry, &cm->channels, pointers) {
            if(UA_ByteString_equal(&entry->channel.securityPolicy->localCertificate, oldCertificate)){
                UA_SecureChannelManager_close(cm, entry->channel.securityToken.channelId);
            }
        }
    }

    size_t i = 0;
    while (i < server->config.endpointsSize) {
        UA_EndpointDescription *ed = &server->config.endpoints[i].endpointDescription;
        if (UA_ByteString_equal(&ed->serverCertificate, oldCertificate)) {
            UA_String_deleteMembers(&ed->serverCertificate);
            UA_String_copy(newCertificate, &ed->serverCertificate);
            UA_SecurityPolicy *sp = &server->config.endpoints[i].securityPolicy;
            sp->updateCertificateAndPrivateKey(sp, *newCertificate, *newPrivateKey);
        }
        i++;
    }

    return UA_STATUSCODE_GOOD;
}

/********************/
/* Main Server Loop */
/********************/

#define UA_MAXTIMEOUT 50 /* Max timeout in ms between main-loop iterations */

/* Start: Spin up the workers and the network layer and sample the server's
 *        start time.
 * Iterate: Process repeated callbacks and events in the network layer. This
 *          part can be driven from an external main-loop in an event-driven
 *          single-threaded architecture.
 * Stop: Stop workers, finish all callbacks, stop the network layer, clean up */

UA_StatusCode
UA_Server_run_startup(UA_Server *server) {
    UA_Variant var;
    UA_StatusCode result = UA_STATUSCODE_GOOD;
	
	/* At least one endpoint has to be configured */
    if(server->config.endpointsSize == 0) {
        UA_LOG_WARNING(&server->config.logger, UA_LOGCATEGORY_SERVER,
                       "There has to be at least one endpoint.");
    }

    /* Sample the start time and set it to the Server object */
    server->startTime = UA_DateTime_now();
    UA_Variant_init(&var);
    UA_Variant_setScalar(&var, &server->startTime, &UA_TYPES[UA_TYPES_DATETIME]);
    UA_Server_writeValue(server,
                         UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_STARTTIME),
                         var);

    /* Start the networklayers */
    for(size_t i = 0; i < server->config.networkLayersSize; ++i) {
        UA_ServerNetworkLayer *nl = &server->config.networkLayers[i];
        result |= nl->start(nl, &server->config.customHostname);
    }

    /* Spin up the worker threads */
#ifdef UA_ENABLE_MULTITHREADING
    UA_LOG_INFO(&server->config.logger, UA_LOGCATEGORY_SERVER,
                "Spinning up %u worker thread(s)", server->config.nThreads);
    UA_WorkQueue_start(&server->workQueue, server->config.nThreads);
#endif

    /* Start the multicast discovery server */
#ifdef UA_ENABLE_DISCOVERY_MULTICAST
    if(server->config.applicationDescription.applicationType ==
       UA_APPLICATIONTYPE_DISCOVERYSERVER)
        startMulticastDiscoveryServer(server);
#endif

    return result;
}

static void
serverExecuteRepeatedCallback(UA_Server *server, UA_ApplicationCallback cb,
                        void *callbackApplication, void *data) {
#ifndef UA_ENABLE_MULTITHREADING
    cb(callbackApplication, data);
#else
    UA_WorkQueue_enqueue(&server->workQueue, cb, callbackApplication, data);
#endif
}

UA_UInt16
UA_Server_run_iterate(UA_Server *server, UA_Boolean waitInternal) {
    /* Process repeated work */
    UA_DateTime now = UA_DateTime_nowMonotonic();
    UA_DateTime nextRepeated = UA_Timer_process(&server->timer, now,
                     (UA_TimerExecutionCallback)serverExecuteRepeatedCallback, server);
    UA_DateTime latest = now + (UA_MAXTIMEOUT * UA_DATETIME_MSEC);
    if(nextRepeated > latest)
        nextRepeated = latest;

    UA_UInt16 timeout = 0;

    /* round always to upper value to avoid timeout to be set to 0
    * if(nextRepeated - now) < (UA_DATETIME_MSEC/2) */
    if(waitInternal)
        timeout = (UA_UInt16)(((nextRepeated - now) + (UA_DATETIME_MSEC - 1)) / UA_DATETIME_MSEC);

    /* Listen on the networklayer */
    for(size_t i = 0; i < server->config.networkLayersSize; ++i) {
        UA_ServerNetworkLayer *nl = &server->config.networkLayers[i];
        nl->listen(nl, server, timeout);
    }

#if defined(UA_ENABLE_DISCOVERY_MULTICAST) && !defined(UA_ENABLE_MULTITHREADING)
    if(server->config.applicationDescription.applicationType ==
       UA_APPLICATIONTYPE_DISCOVERYSERVER) {
        // TODO multicastNextRepeat does not consider new input data (requests)
        // on the socket. It will be handled on the next call. if needed, we
        // need to use select with timeout on the multicast socket
        // server->mdnsSocket (see example in mdnsd library) on higher level.
        UA_DateTime multicastNextRepeat = 0;
        UA_StatusCode hasNext =
            iterateMulticastDiscoveryServer(server, &multicastNextRepeat, true);
        if(hasNext == UA_STATUSCODE_GOOD && multicastNextRepeat < nextRepeated)
            nextRepeated = multicastNextRepeat;
    }
#endif

#ifndef UA_ENABLE_MULTITHREADING
    UA_WorkQueue_manuallyProcessDelayed(&server->workQueue);
#endif

    now = UA_DateTime_nowMonotonic();
    timeout = 0;
    if(nextRepeated > now)
        timeout = (UA_UInt16)((nextRepeated - now) / UA_DATETIME_MSEC);
    return timeout;
}

UA_StatusCode
UA_Server_run_shutdown(UA_Server *server) {
    /* Stop the netowrk layer */
    for(size_t i = 0; i < server->config.networkLayersSize; ++i) {
        UA_ServerNetworkLayer *nl = &server->config.networkLayers[i];
        nl->stop(nl, server);
    }

#ifdef UA_ENABLE_MULTITHREADING
    /* Shut down the workers */
    UA_LOG_INFO(&server->config.logger, UA_LOGCATEGORY_SERVER,
                "Shutting down %u worker thread(s)",
                (UA_UInt32)server->workQueue.workersSize);
    UA_WorkQueue_stop(&server->workQueue);
#endif

#ifdef UA_ENABLE_DISCOVERY_MULTICAST
    /* Stop multicast discovery */
    if(server->config.applicationDescription.applicationType ==
       UA_APPLICATIONTYPE_DISCOVERYSERVER)
        stopMulticastDiscoveryServer(server);
#endif

    /* Execute all delayed callbacks */
    UA_WorkQueue_cleanup(&server->workQueue);

    return UA_STATUSCODE_GOOD;
}

UA_StatusCode
UA_Server_run(UA_Server *server, volatile UA_Boolean *running) {
    UA_StatusCode retval = UA_Server_run_startup(server);
    if(retval != UA_STATUSCODE_GOOD)
        return retval;
#ifdef UA_ENABLE_VALGRIND_INTERACTIVE
    size_t loopCount = 0;
#endif
    while(*running) {
#ifdef UA_ENABLE_VALGRIND_INTERACTIVE
        if(loopCount == 0) {
            VALGRIND_DO_LEAK_CHECK;
        }
        ++loopCount;
        loopCount %= UA_VALGRIND_INTERACTIVE_INTERVAL;
#endif
        UA_Server_run_iterate(server, true);
    }
    return UA_Server_run_shutdown(server);
}
