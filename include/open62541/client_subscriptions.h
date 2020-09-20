/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef UA_CLIENT_SUBSCRIPTIONS_H_
#define UA_CLIENT_SUBSCRIPTIONS_H_

#include <open62541/client.h>

_UA_BEGIN_DECLS

#ifdef UA_ENABLE_SUBSCRIPTIONS

/**
 * .. _client-subscriptions:
 *
 * Subscriptions
 * -------------
 *
 * Subscriptions in OPC UA are asynchronous. That is, the client sends several
 * PublishRequests to the server. The server returns PublishResponses with
 * notifications. But only when a notification has been generated. The client
 * does not wait for the responses and continues normal operations.
 *
 * Note the difference between Subscriptions and MonitoredItems. Subscriptions
 * are used to report back notifications. MonitoredItems are used to generate
 * notifications. Every MonitoredItem is attached to exactly one Subscription.
 * And a Subscription can contain many MonitoredItems.
 *
 * The client automatically processes PublishResponses (with a callback) in the
 * background and keeps enough PublishRequests in transit. The PublishResponses
 * may be recieved during a synchronous service call or in
 * ``UA_Client_run_iterate``. See more about
 * :ref:`asynchronicity<client-async-services>`.
 */

/* Callbacks defined for Subscriptions */
typedef void (*UA_Client_DeleteSubscriptionCallback)
    (UA_Client *client, UA_UInt32 subId, void *subContext);

typedef void (*UA_Client_StatusChangeNotificationCallback)
    (UA_Client *client, UA_UInt32 subId, void *subContext,
     UA_StatusChangeNotification *notification);

/* Provides default values for a new subscription.
 *
 * RequestedPublishingInterval:  500.0 [ms]
 * RequestedLifetimeCount: 10000
 * RequestedMaxKeepAliveCount: 10
 * MaxNotificationsPerPublish: 0 (unlimited)
 * PublishingEnabled: true
 * Priority: 0 */
static UA_INLINE UA_CreateSubscriptionRequest
UA_CreateSubscriptionRequest_default(void) {
    UA_CreateSubscriptionRequest request;
    UA_CreateSubscriptionRequest_init(&request);

    request.requestedPublishingInterval = 500.0;
    request.requestedLifetimeCount = 10000;
    request.requestedMaxKeepAliveCount = 10;
    request.maxNotificationsPerPublish = 0;
    request.publishingEnabled = true;
    request.priority = 0;
    return request;
}

UA_CreateSubscriptionResponse UA_EXPORT
UA_Client_Subscriptions_create(UA_Client *client,
    const UA_CreateSubscriptionRequest request,
    void *subscriptionContext,
    UA_Client_StatusChangeNotificationCallback statusChangeCallback,
    UA_Client_DeleteSubscriptionCallback deleteCallback);

UA_StatusCode UA_EXPORT
UA_Client_Subscriptions_create_async(UA_Client *client,
    const UA_CreateSubscriptionRequest request,
    void *subscriptionContext,
    UA_Client_StatusChangeNotificationCallback statusChangeCallback,
    UA_Client_DeleteSubscriptionCallback deleteCallback,
    UA_ClientAsyncServiceCallback callback,
    void *userdata, UA_UInt32 *requestId);

UA_ModifySubscriptionResponse UA_EXPORT
UA_Client_Subscriptions_modify(UA_Client *client,
    const UA_ModifySubscriptionRequest request);

UA_StatusCode UA_EXPORT
UA_Client_Subscriptions_modify_async(UA_Client *client,
    const UA_ModifySubscriptionRequest request,
    UA_ClientAsyncServiceCallback callback,
    void *userdata, UA_UInt32 *requestId);

UA_DeleteSubscriptionsResponse UA_EXPORT
UA_Client_Subscriptions_delete(UA_Client *client,
    const UA_DeleteSubscriptionsRequest request);

UA_StatusCode UA_EXPORT
UA_Client_Subscriptions_delete_async(UA_Client *client,
    const UA_DeleteSubscriptionsRequest request,
    UA_ClientAsyncServiceCallback callback,
    void *userdata, UA_UInt32 *requestId);

/* Delete a single subscription */
UA_StatusCode UA_EXPORT
UA_Client_Subscriptions_deleteSingle(UA_Client *client, UA_UInt32 subscriptionId);

static UA_INLINE UA_SetPublishingModeResponse
UA_Client_Subscriptions_setPublishingMode(UA_Client *client,
    const UA_SetPublishingModeRequest request) {
    UA_SetPublishingModeResponse response;
    __UA_Client_Service(client,
        &request, &UA_TYPES[UA_TYPES_SETPUBLISHINGMODEREQUEST],
        &response, &UA_TYPES[UA_TYPES_SETPUBLISHINGMODERESPONSE]);
    return response;
}

/**
 * MonitoredItems
 * --------------
 *
 * MonitoredItems for Events indicate the ``EventNotifier`` attribute. This
 * indicates to the server not to monitor changes of the attribute, but to
 * forward Event notifications from that node.
 *
 * During the creation of a MonitoredItem, the server may return changed
 * adjusted parameters. Check the returned ``UA_CreateMonitoredItemsResponse``
 * to get the current parameters. */

/* Provides default values for a new monitored item. */
static UA_INLINE UA_MonitoredItemCreateRequest
UA_MonitoredItemCreateRequest_default(UA_NodeId nodeId) {
    UA_MonitoredItemCreateRequest request;
    UA_MonitoredItemCreateRequest_init(&request);
    request.itemToMonitor.nodeId = nodeId;
    request.itemToMonitor.attributeId = UA_ATTRIBUTEID_VALUE;
    request.monitoringMode = UA_MONITORINGMODE_REPORTING;
    request.requestedParameters.samplingInterval = 250;
    request.requestedParameters.discardOldest = true;
    request.requestedParameters.queueSize = 1;
    return request;
}

/**
 * The clientHandle parameter can't be set by the user, any value will be replaced
 * by the client before sending the request to the server. */

/* Callback for the deletion of a MonitoredItem */
typedef void (*UA_Client_DeleteMonitoredItemCallback)
    (UA_Client *client, UA_UInt32 subId, void *subContext,
     UA_UInt32 monId, void *monContext);

/* Callback for DataChange notifications */
typedef void (*UA_Client_DataChangeNotificationCallback)
    (UA_Client *client, UA_UInt32 subId, void *subContext,
     UA_UInt32 monId, void *monContext,
     UA_DataValue *value);

/* Callback for Event notifications */
typedef void (*UA_Client_EventNotificationCallback)
    (UA_Client *client, UA_UInt32 subId, void *subContext,
     UA_UInt32 monId, void *monContext,
     size_t nEventFields, UA_Variant *eventFields);

/* Don't use to monitor the EventNotifier attribute */
UA_CreateMonitoredItemsResponse UA_EXPORT
UA_Client_MonitoredItems_createDataChanges(UA_Client *client,
    const UA_CreateMonitoredItemsRequest request, void **contexts,
    UA_Client_DataChangeNotificationCallback *callbacks,
    UA_Client_DeleteMonitoredItemCallback *deleteCallbacks);

UA_StatusCode UA_EXPORT
UA_Client_MonitoredItems_createDataChanges_async(UA_Client *client,
    const UA_CreateMonitoredItemsRequest request, void **contexts,
    UA_Client_DataChangeNotificationCallback *callbacks,
    UA_Client_DeleteMonitoredItemCallback *deleteCallbacks,
    UA_ClientAsyncServiceCallback createCallback,
    void *userdata, UA_UInt32 *requestId);

UA_MonitoredItemCreateResult UA_EXPORT
UA_Client_MonitoredItems_createDataChange(UA_Client *client,
    UA_UInt32 subscriptionId,
    UA_TimestampsToReturn timestampsToReturn,
    const UA_MonitoredItemCreateRequest item,
    void *context, UA_Client_DataChangeNotificationCallback callback,
    UA_Client_DeleteMonitoredItemCallback deleteCallback);

/* Monitor the EventNotifier attribute only */
UA_CreateMonitoredItemsResponse UA_EXPORT
UA_Client_MonitoredItems_createEvents(UA_Client *client,
    const UA_CreateMonitoredItemsRequest request, void **contexts,
    UA_Client_EventNotificationCallback *callback,
    UA_Client_DeleteMonitoredItemCallback *deleteCallback);

/* Monitor the EventNotifier attribute only */
UA_StatusCode UA_EXPORT
UA_Client_MonitoredItems_createEvents_async(UA_Client *client,
    const UA_CreateMonitoredItemsRequest request, void **contexts,
    UA_Client_EventNotificationCallback *callbacks,
    UA_Client_DeleteMonitoredItemCallback *deleteCallbacks,
    UA_ClientAsyncServiceCallback createCallback,
    void *userdata, UA_UInt32 *requestId);

UA_MonitoredItemCreateResult UA_EXPORT
UA_Client_MonitoredItems_createEvent(UA_Client *client,
    UA_UInt32 subscriptionId,
    UA_TimestampsToReturn timestampsToReturn,
    const UA_MonitoredItemCreateRequest item,
    void *context, UA_Client_EventNotificationCallback callback,
    UA_Client_DeleteMonitoredItemCallback deleteCallback);

UA_DeleteMonitoredItemsResponse UA_EXPORT
UA_Client_MonitoredItems_delete(UA_Client *client,
    const UA_DeleteMonitoredItemsRequest);

UA_StatusCode UA_EXPORT
UA_Client_MonitoredItems_delete_async(UA_Client *client,
    const UA_DeleteMonitoredItemsRequest request,
    UA_ClientAsyncServiceCallback callback,
    void *userdata, UA_UInt32 *requestId);

UA_StatusCode UA_EXPORT
UA_Client_MonitoredItems_deleteSingle(UA_Client *client,
    UA_UInt32 subscriptionId, UA_UInt32 monitoredItemId);

/* The clientHandle parameter will be filled automatically */
UA_ModifyMonitoredItemsResponse UA_EXPORT
UA_Client_MonitoredItems_modify(UA_Client *client,
    const UA_ModifyMonitoredItemsRequest request);

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

/* Parse the alarms and condition notification */
void handler_events_alarms_condition(UA_Client *client, UA_UInt32 subId,
                                     void *subContext, UA_UInt32 monId,
                                     void *monContext, size_t nEventFields,
                                     UA_Variant *eventFields);

/* Set select clause operand for alarms and condition notification */
UA_SimpleAttributeOperand *setupSelectClausesAlarmCondition(void);

/* Call Enable Method of the condition instance
 *
 * @param client to use
 * @param objectId nodeId of the condition instance to enable
 * @param subId Subscription Id of the subscription to be enabled
 * @return Indicates whether the operation succeeded or returns an error code 
 */
UA_StatusCode UA_EXPORT callEnableMethod(UA_Client *client, UA_NodeId objectId, UA_UInt32 subId);

/* Call Disable Method of the condition instance
 *
 * @param client to use
 * @param objectId nodeId of the condition instance to disable
 * @param subId  Subscription Id of the subscription to be disabled
 * @return Indicates whether the operation succeeded or returns an error code 
 */
UA_StatusCode UA_EXPORT callDisableMethod(UA_Client *client, UA_NodeId objectId, UA_UInt32 subId);

/* Call Acknowledge Method for the active alarm
 *
 * @param client to use
 * @param objectId nodeId of the condition instance to acknowledge
 * @param eventId Unique 16 byte length Id of the event notification
 * @param comment comment to be added for the event acknowledgement
 * @return Indicates whether the operation succeeded or returns an error code 
 */
UA_StatusCode UA_EXPORT callAcknowledgeMethod(UA_Client *client, UA_NodeId objectId, UA_ByteString eventId,
                                            UA_LocalizedText comment);

/* Call Confirm Method for the active alarm
 *
 * @param client to use
 * @param objectId nodeId of the condition instance to confirm
 * @param eventId Unique 16 byte length Id of the event notification
 * @param comment comment to be added for the event to confirm
 * @return Indicates whether the operation succeeded or returns an error code 
 */
UA_StatusCode UA_EXPORT callConfirmMethod(UA_Client *client, UA_NodeId objectId, UA_ByteString eventId,
                                            UA_LocalizedText comment);

/* Call comment Method to add comment for the condition instance
 *
 * @param client to use
 * @param objectId nodeId of the condition instance to comment
 * @param eventId A Unique 16 byte length Id of the latest event
 * @param comment comment to be added for the condition Instance
 * @return Indicates whether the operation succeeded or returns an error code 
 */
UA_StatusCode UA_EXPORT callAddCommentMethod(UA_Client *client, UA_NodeId objectId, UA_ByteString eventId,
                                            UA_LocalizedText comment);

/* Call refresh Method to fetch the list of enabled alarms in the server
 *
 * @param client to use
 * @param subId The identifier of the Subscription containing the MonitoredItem to be refreshed
 * @return Indicates whether the operation succeeded or returns an error code 
 */
UA_StatusCode UA_EXPORT callConditionRefresh(UA_Client *client, UA_UInt32 subId);

/* Call refresh2 Method to fetch the list of enabled alarms confined the respective monitoredItemId
 *
 * @param client to use
 * @param subId The identifier of the Subscription containing the MonitoredItem to be refreshed
 * @param monId value of the monitoredItemId
 * @return Indicates whether the operation succeeded or returns an error code 
 */
UA_StatusCode UA_EXPORT callConditionRefresh2(UA_Client *client, UA_UInt32 subId, UA_UInt32 monId);

#endif
/**
 * The following service calls go directly to the server. The MonitoredItem
 * settings are not stored in the client. */

static UA_INLINE UA_SetMonitoringModeResponse
UA_Client_MonitoredItems_setMonitoringMode(UA_Client *client,
    const UA_SetMonitoringModeRequest request) {
    UA_SetMonitoringModeResponse response;
    __UA_Client_Service(client,
        &request, &UA_TYPES[UA_TYPES_SETMONITORINGMODEREQUEST],
        &response, &UA_TYPES[UA_TYPES_SETMONITORINGMODERESPONSE]);
    return response;
}

static UA_INLINE UA_SetTriggeringResponse
UA_Client_MonitoredItems_setTriggering(UA_Client *client,
    const UA_SetTriggeringRequest request) {
    UA_SetTriggeringResponse response;
    __UA_Client_Service(client,
        &request, &UA_TYPES[UA_TYPES_SETTRIGGERINGREQUEST],
        &response, &UA_TYPES[UA_TYPES_SETTRIGGERINGRESPONSE]);
    return response;
}

static UA_INLINE UA_StatusCode
UA_Client_MonitoredItems_modify_async(UA_Client *client,
    const UA_ModifyMonitoredItemsRequest request,
    UA_ClientAsyncServiceCallback callback,
    void *userdata, UA_UInt32 *requestId) {
    return __UA_Client_AsyncService(client, &request,
        &UA_TYPES[UA_TYPES_MODIFYMONITOREDITEMSREQUEST], callback,
        &UA_TYPES[UA_TYPES_MODIFYMONITOREDITEMSRESPONSE],
        userdata, requestId);
}

static UA_INLINE UA_StatusCode
UA_Client_MonitoredItems_setMonitoringMode_async(UA_Client *client,
    const UA_SetMonitoringModeRequest request,
    UA_ClientAsyncServiceCallback callback,
    void *userdata, UA_UInt32 *requestId) {
    return __UA_Client_AsyncService(client, &request,
        &UA_TYPES[UA_TYPES_SETMONITORINGMODEREQUEST], callback,
        &UA_TYPES[UA_TYPES_SETMONITORINGMODERESPONSE],
        userdata, requestId);
}

static UA_INLINE UA_StatusCode
UA_Client_MonitoredItems_setTriggering_async(UA_Client *client,
    const UA_SetTriggeringRequest request,
    UA_ClientAsyncServiceCallback callback,
    void *userdata, UA_UInt32 *requestId) {
    return __UA_Client_AsyncService(client, &request,
        &UA_TYPES[UA_TYPES_SETTRIGGERINGREQUEST], callback,
        &UA_TYPES[UA_TYPES_SETTRIGGERINGRESPONSE],
        userdata, requestId);
}

#endif

_UA_END_DECLS

#endif /* UA_CLIENT_SUBSCRIPTIONS_H_ */
