/*
  +---------------------------------------------------------------------+
  | PHP Version 5														|
  +---------------------------------------------------------------------+
  | Copyright (c) 1997-2007 The PHP Group								|
  +---------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,		|
  | that is bundled with this package in the file LICENSE, and is		|
  | available through the world-wide-web at the following url:			|
  | http://www.php.net/license/3_01.txt									|
  | If you did not receive a copy of the PHP license and are unable to	|
  | obtain it through the world-wide-web, please send a note to			|
  | license@php.net so we can mail you a copy immediately.				|
  |																		|
  | This source uses the librabbitmq under the MPL. For the MPL, please |
  | see LICENSE-MPL-RabbitMQ											|
  +---------------------------------------------------------------------+
  | Author: Alexandre Kalendarev akalend@mail.ru Copyright (c) 2009-2010|
  | Maintainer: Pieter de Zwart pdezwart@php.net						|
  | Contributers:														|
  | - Andrey Hristov													|
  | - Brad Rodriguez brodriguez@php.net									|
  +---------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "zend_exceptions.h"

#include <stdint.h>
#include <signal.h>
#include <amqp.h>
#include <amqp_framing.h>

#include <unistd.h>

#include "php_amqp.h"

void amqp_exchange_dtor(void *object TSRMLS_DC)
{
	amqp_exchange_object *ob = (amqp_exchange_object*)object;

	/* Destroy the connection object */
	if (ob->cnn) {
		zval_ptr_dtor(&ob->cnn);
	}

	efree(object);
}

zend_object_value amqp_exchange_ctor(zend_class_entry *ce TSRMLS_DC)
{
	zend_object_value new_value;
	amqp_queue_object* obj = (amqp_queue_object*)emalloc(sizeof(amqp_queue_object));

	memset(obj, 0, sizeof(amqp_exchange_object));

	zend_object_std_init(&obj->zo, ce TSRMLS_CC);

	new_value.handle = zend_objects_store_put(obj, (zend_objects_store_dtor_t)zend_objects_destroy_object,
		(zend_objects_free_object_storage_t)amqp_exchange_dtor, NULL TSRMLS_CC);
	new_value.handlers = zend_get_std_object_handlers();

	return new_value;
}

/* {{{ proto AMQPEexchange( AMQPConnection cnn, [string name]);
declare Exchange   */
PHP_METHOD(amqp_exchange_class, __construct)
{
	zval *id;
	zval *cnnOb;
	amqp_exchange_object *ctx;
	amqp_connection_object *ctx_cnn;

	char *name;
	int name_len = 0;
	amqp_rpc_reply_t res;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oo|s", &id, amqp_exchange_class_entry, &cnnOb, &name, &name_len) == FAILURE) {
		RETURN_FALSE;
	}
	
	if (!instanceof_function(Z_OBJCE_P(cnnOb), amqp_connection_class_entry TSRMLS_CC)) {
		zend_throw_exception(amqp_exchange_exception_class_entry, "The first parameter must be and instance of AMQPConnection.", 0 TSRMLS_CC);
		return;
	}

	ctx = (amqp_exchange_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	ctx->cnn = cnnOb;
	/* Increment the ref count */
	Z_ADDREF_P(cnnOb);
	
	ctx_cnn = (amqp_connection_object *) zend_object_store_get_object(ctx->cnn TSRMLS_CC);

	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (ctx_cnn->is_connected != '\1') {
		zend_throw_exception(amqp_exchange_exception_class_entry, "Could not create exchange. No connection available.", 0 TSRMLS_CC);
		return;
	}

	/* Make sure we have a legit connection object */
	if(!cnnOb) {
		zend_throw_exception(amqp_exchange_exception_class_entry, "The given AMQPConnection object is null.", 0 TSRMLS_CC);
		return;
	}

	if (name_len) {
		AMQP_SET_NAME(ctx, name);
	}

	/* We have a valid connection: */
	ctx->is_connected = '\1';

}
/* }}} */


/* {{{ proto AMQPExchange::declare( [string name], [string type=direct], [ bit params ]);
declare Exchange
*/
PHP_METHOD(amqp_exchange_class, declare)
{
	zval *id;
	zval *cnnOb;

	amqp_exchange_object *ctx;
	amqp_connection_object *ctx_cnn;

	char *name;
	int name_len = 0;
	char *type;
	int type_len = 0;
	char *verified_type;
	long parms = 0;

	amqp_rpc_reply_t res;
	amqp_exchange_declare_t s;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O|ssl", &id, amqp_exchange_class_entry, &name, &name_len, &type, &type_len, &parms) == FAILURE) {
		RETURN_FALSE;
	}

	ctx = (amqp_exchange_object *)zend_object_store_get_object(id TSRMLS_CC);

	if (!type_len) {
		type = AMQP_EX_TYPE_DIRECT; /* default - direct */
		type_len = strlen(AMQP_EX_TYPE_DIRECT);
	}

	/* Check that the given connection is active before declaring the exchange */
	if (ctx->is_connected != '\1') {
		zend_throw_exception(amqp_exchange_exception_class_entry, "Could not declare exchange. No connection available.", 0 TSRMLS_CC);
		return;
	}

	ctx_cnn = (amqp_connection_object *) zend_object_store_get_object(ctx->cnn TSRMLS_CC);

	if (!ctx_cnn) {
		zend_throw_exception(amqp_exchange_exception_class_entry, "The given AMQPConnection object is null.", 0 TSRMLS_CC);
		return;
	}
	
	amqp_bytes_t amqp_name;
	if (name_len) {
		AMQP_SET_NAME(ctx, name);
		amqp_name.len = name_len;
		amqp_name.bytes = name;
	} else {
		amqp_name.len = ctx->name_len;
		amqp_name.bytes = ctx->name;
	}
	
	AMQP_NULLARGS
	AMQP_PASSIVE_D
	AMQP_DURABLE_D
	AMQP_AUTODELETE_D
	
	amqp_exchange_declare(ctx_cnn->conn, AMQP_CHANNEL, amqp_cstring_bytes(amqp_name.bytes), amqp_cstring_bytes(type), passive, durable, arguments);
	res = (amqp_rpc_reply_t)amqp_get_rpc_reply(ctx_cnn->conn); 
	
	/* handle any errors that occured outside of signals */
	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char ** pstr = (char **) &str;
		amqp_error(res, pstr);
		ctx_cnn->is_connected = '\0';
		zend_throw_exception(amqp_exchange_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}
	

	RETURN_TRUE;
}
/* }}} */



/* {{{ proto AMQPEexchange::delete([string name]);
delete Exchange
*/
PHP_METHOD(amqp_exchange_class, delete)
{
	zval *id;
	zval *cnnOb;

	amqp_exchange_object *ctx;
	amqp_connection_object *ctx_cnn;

	char *name;
	int name_len = 0;
	long parms = 0;

	amqp_rpc_reply_t res;
	amqp_exchange_delete_t s;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O|sl", &id, amqp_exchange_class_entry, &name, &name_len, &parms) == FAILURE) {
		RETURN_FALSE;
	}

	ctx = (amqp_exchange_object *)zend_object_store_get_object(id TSRMLS_CC);

	if (name_len) {
		AMQP_SET_NAME(ctx, name);
		s.ticket = 0;
		s.exchange.len = name_len;
		s.exchange.bytes = name;
		s.if_unused = (AMQP_IFUNUSED & parms) ? 1 : 0;
		s.nowait = 0;
	} else {
		s.ticket = 0,
		s.exchange.len = ctx->name_len;
		s.exchange.bytes = ctx->name;
		s.if_unused = (AMQP_IFUNUSED & parms) ? 1 : 0;
		s.nowait = 0;
	}

/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (ctx->is_connected != '\1') {
		zend_throw_exception(amqp_exchange_exception_class_entry, "Could not delete exchange. No connection available.", 0 TSRMLS_CC);
		return;
	}

	ctx_cnn = (amqp_connection_object *) zend_object_store_get_object(ctx->cnn TSRMLS_CC);
	if(!ctx_cnn) {
		zend_throw_exception(amqp_exchange_exception_class_entry, "The given AMQPConnection object is null.", 0 TSRMLS_CC);
		return;
	}

	amqp_method_number_t method_ok = AMQP_EXCHANGE_DELETE_OK_METHOD;

	res = amqp_simple_rpc(ctx_cnn->conn, AMQP_CHANNEL,
		AMQP_EXCHANGE_DELETE_METHOD,
		&method_ok, &s
	);

	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char ** pstr = (char **) &str;
		amqp_error(res, pstr);
		zend_throw_exception(amqp_exchange_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto AMQPEexchange::publish(string msg, string key, [params, attributes]);
publish into Exchange
*/
PHP_METHOD(amqp_exchange_class, publish)
{
	zval *id;
	zval *cnnOb;
	zval *iniArr = NULL;
	zval** zdata;
	amqp_exchange_object *ctx;
	amqp_connection_object *ctx_cnn;

	char *key_name = NULL;
	int key_len = 0;

	char *msg;
	int msg_len= 0;

	long parms = 0;

	/* Storage for previous signal handler during SIGPIPE override */
	void * old_handler;

	amqp_rpc_reply_t res;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oss|la",
	&id, amqp_exchange_class_entry, &msg, &msg_len, &key_name, &key_len, &parms, &iniArr) == FAILURE) {
		RETURN_FALSE;
	}

	ctx = (amqp_exchange_object *)zend_object_store_get_object(id TSRMLS_CC);

	if (!ctx->name_len) {
		zend_throw_exception(amqp_exchange_exception_class_entry, "Could not publish to exchange. Exchange name not set.", 0 TSRMLS_CC);
		return;
	}
	
	if (!key_len) {
		zend_throw_exception(amqp_exchange_exception_class_entry, "Could not publish to exchange. No routing key given.", 0 TSRMLS_CC);
		return;
	}

	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (ctx->is_connected != '\1') {
		zend_throw_exception(amqp_exchange_exception_class_entry, "Could not publish to exchange. No connection available.", 0 TSRMLS_CC);
		return;
	}

	ctx_cnn = (amqp_connection_object *) zend_object_store_get_object(ctx->cnn TSRMLS_CC);

	if(!ctx_cnn) {
		zend_throw_exception(amqp_exchange_exception_class_entry, "The given AMQPConnection object is null.", 0 TSRMLS_CC);
		return;
	}

	amqp_basic_properties_t props;

	props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG;

	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "Content-type", sizeof("Content-type"), (void*)&zdata)) {
		convert_to_string(*zdata);
	}
	if (zdata && strlen(Z_STRVAL_PP(zdata)) > 0) {
		props.content_type = amqp_cstring_bytes((char*)Z_STRVAL_PP(zdata));
	} else {
		props.content_type = amqp_cstring_bytes("text/plain");
	}

	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "Content-encoding", sizeof("Content-encoding"), (void*)&zdata)) {
		convert_to_string(*zdata);
	}
	if (zdata && strlen(Z_STRVAL_PP(zdata)) > 0) {
		props.content_encoding = amqp_cstring_bytes((char*)Z_STRVAL_PP(zdata));
		props._flags += AMQP_BASIC_CONTENT_ENCODING_FLAG;
	}

	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "message_id", sizeof("message_id"), (void*)&zdata)) {
		convert_to_string(*zdata);
	}
	if (zdata && strlen(Z_STRVAL_PP(zdata)) > 0) {
		props.message_id = amqp_cstring_bytes((char*)Z_STRVAL_PP(zdata));
		props._flags += AMQP_BASIC_MESSAGE_ID_FLAG;
	}

	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "user_id", sizeof("user_id"), (void*)&zdata)) {
		convert_to_string(*zdata);
	}
	if (zdata && strlen(Z_STRVAL_PP(zdata)) > 0) {
		props.user_id = amqp_cstring_bytes((char*)Z_STRVAL_PP(zdata));
		props._flags += AMQP_BASIC_USER_ID_FLAG;
	}

	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "app_id", sizeof("app_id"), (void*)&zdata)) {
		convert_to_string(*zdata);
	}
	if (zdata && strlen(Z_STRVAL_PP(zdata)) > 0) {
		props.app_id = amqp_cstring_bytes((char*)Z_STRVAL_PP(zdata));
		props._flags += AMQP_BASIC_APP_ID_FLAG;
	}

	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "delivery_mode", sizeof("delivery_mode"), (void*)&zdata)) {
		convert_to_long(*zdata);
	}
	if (zdata) {
		props.delivery_mode = (uint8_t)Z_LVAL_PP(zdata);
		props._flags += AMQP_BASIC_DELIVERY_MODE_FLAG;
	}


	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "priority", sizeof("priority"), (void*)&zdata)) {
		convert_to_long(*zdata);
	}
	if (zdata) {
		props.priority = (uint8_t)Z_LVAL_PP(zdata);
		props._flags += AMQP_BASIC_PRIORITY_FLAG;
	}

	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "timestamp", sizeof("timestamp"), (void*)&zdata)) {
		convert_to_long(*zdata);
	}
	if (zdata) {
		props.timestamp = (uint64_t)Z_LVAL_PP(zdata);
		props._flags += AMQP_BASIC_TIMESTAMP_FLAG;
	}

	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "expiration", sizeof("expiration"), (void*)&zdata)) {
		convert_to_string(*zdata);
	}
	if (zdata && strlen(Z_STRVAL_PP(zdata)) > 0) {
		props.expiration =	amqp_cstring_bytes((char*)Z_STRVAL_PP(zdata));
		props._flags += AMQP_BASIC_EXPIRATION_FLAG;
	}

	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "type", sizeof("type"), (void*)&zdata)) {
		convert_to_string(*zdata);
	}
	if (zdata && strlen(Z_STRVAL_PP(zdata)) > 0) {
		props.type =  amqp_cstring_bytes((char*)Z_STRVAL_PP(zdata));
		props._flags += AMQP_BASIC_TYPE_FLAG;
	}

	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "reply_to", sizeof("reply_to"), (void*)&zdata)) {
		convert_to_string(*zdata);
	}
	if (zdata && strlen(Z_STRVAL_PP(zdata)) > 0) {
		props.reply_to = amqp_cstring_bytes((char*)Z_STRVAL_PP(zdata));
		props._flags += AMQP_BASIC_REPLY_TO_FLAG;
	}
	
	if (ctx->is_connected != '\1') {
		zend_throw_exception(amqp_exchange_exception_class_entry, "Could not publish. No connection available.", 0 TSRMLS_CC);
		return;
	}

	ctx_cnn = (amqp_connection_object *) zend_object_store_get_object(ctx->cnn TSRMLS_CC);
	if(!ctx_cnn) {
		zend_throw_exception(amqp_exchange_exception_class_entry, "The given AMQPConnection object is null.", 0 TSRMLS_CC);
		return;
	}
	
	if (ctx_cnn->fd < 0) {
		zend_throw_exception(amqp_exchange_exception_class_entry, "Could not publish. Connetion socket is closed.", 0 TSRMLS_CC);
		return;
	}

	/* Start ignoring SIGPIPE */
	old_handler = signal(SIGPIPE, SIG_IGN);

	int r = amqp_basic_publish(
		ctx_cnn->conn,
		AMQP_CHANNEL,
		(amqp_bytes_t) {ctx->name_len, ctx->name},
		(amqp_bytes_t) {key_len, key_name },
		(AMQP_MANDATORY & parms) ? 1 : 0, /* mandatory */
		(AMQP_IMMEDIATE & parms) ? 1 : 0, /* immediate */
		&props,
		(amqp_bytes_t) {msg_len, msg }
	);
	
	/* End ignoring of SIGPIPEs */
	signal(SIGPIPE, old_handler);

	/* handle any errors that occured outside of signals */
	if (r) {
		char str[256];
		char ** pstr = (char **) &str;
		res = (amqp_rpc_reply_t)amqp_get_rpc_reply(ctx_cnn->conn); 
		ctx_cnn->is_connected = '\0';
		amqp_error(res, pstr);
		zend_throw_exception(amqp_exchange_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}
	
	RETURN_TRUE;
}
/* }}} */


/* {{{ proto int exchange::bind(string queueName, string routingKey);
bind exchange to queue by routing key
*/
PHP_METHOD(amqp_exchange_class, bind)
{
	zval *id;
	amqp_exchange_object *ctx;
	amqp_connection_object *ctx_cnn;
	char *name;
	int name_len;
	char *queue_name;
	int queue_name_len;
	char *keyname;
	int keyname_len;

	amqp_rpc_reply_t res;
	amqp_rpc_reply_t result;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oss", &id, amqp_exchange_class_entry, &queue_name, &queue_name_len, &keyname, &keyname_len) == FAILURE) {
		RETURN_FALSE;
	}

	ctx = (amqp_exchange_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (ctx->is_connected != '\1') {
		zend_throw_exception(amqp_exchange_exception_class_entry, "Could not bind exchange. No connection available.", 0 TSRMLS_CC);
		return;
	}

	amqp_connection_object *cnn = (amqp_connection_object *) zend_object_store_get_object(ctx->cnn TSRMLS_CC);

	amqp_queue_bind_t s;
	s.ticket				= 0;
	s.queue.len				= queue_name_len;
	s.queue.bytes			= queue_name;
	s.exchange.len			= ctx->name_len;
	s.exchange.bytes		= ctx->name;
	s.routing_key.len		= keyname_len;
	s.routing_key.bytes		= keyname;
	s.nowait				= 0;
	s.arguments.num_entries = 0;
	s.arguments.entries		= NULL;

	amqp_method_number_t method_ok = AMQP_QUEUE_BIND_OK_METHOD;
	result = amqp_simple_rpc(cnn->conn,
		AMQP_CHANNEL,
		AMQP_QUEUE_BIND_METHOD,
		&method_ok,
		&s);

	res = (amqp_rpc_reply_t)result;

	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char ** pstr = (char **) &str;
		amqp_error(res, pstr);
		cnn->is_channel_connected = 0;
		zend_throw_exception(amqp_exchange_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	RETURN_TRUE;
}
/* }}} */


/*
*Local variables:
*tab-width: 4
*c-basic-offset: 4
*End:
*vim600: noet sw=4 ts=4 fdm=marker
*vim<600: noet sw=4 ts=4
*/
