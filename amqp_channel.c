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

/* $Id: amqp_channel.c 318036 2011-10-11 20:30:46Z pdezwart $ */

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


void amqp_channel_dtor(void *object TSRMLS_DC)
{
	amqp_channel_object *ob = (amqp_channel_object*)object;

	zend_object_std_dtor(&ob->zo TSRMLS_CC);

	efree(object);

}

zend_object_value amqp_channel_ctor(zend_class_entry *ce TSRMLS_DC)
{
	zend_object_value new_value;
	amqp_channel_object* obj = (amqp_channel_object*)emalloc(sizeof(amqp_channel_object));

	memset(obj, 0, sizeof(amqp_channel_object));

	zend_object_std_init(&obj->zo, ce TSRMLS_CC);

	new_value.handle = zend_objects_store_put(obj, (zend_objects_store_dtor_t)zend_objects_destroy_object, (zend_objects_free_object_storage_t)amqp_channel_dtor, NULL TSRMLS_CC);
	new_value.handlers = zend_get_std_object_handlers();

	return new_value;
}


/* {{{ proto AMQPChannel::__construct(AMQPConnection obj)
 */
PHP_METHOD(amqp_channel_class, __construct)
{
	zval *id;
	zval *connObj = NULL;
	
	amqp_channel_object *channel;
	amqp_connection_object *connection;
	
	amqp_rpc_reply_t res;

	/* Parse out the method parameters */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oo", &id, amqp_channel_class_entry, &connObj) == FAILURE) {
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "parse parameter error", 0 TSRMLS_CC);
		return;
	}

	channel = (amqp_channel_object *)zend_object_store_get_object(id TSRMLS_CC);
	channel->connection = connObj; 

	channel->channel_id = 1;

	Z_ADDREF_P(connObj);

	/* Set the prefetch count */
	channel->prefetch_count = INI_INT("amqp.prefetch_count");

	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, amqp_channel_exception_class_entry, "Could not create channel.");

	amqp_channel_open(connection->connection_state, channel->channel_id);

	res = (amqp_rpc_reply_t)amqp_get_rpc_reply(connection->connection_state);
	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char ** pstr = (char **) &str;
		amqp_error(res, pstr);
		zend_throw_exception(amqp_channel_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	channel->is_connected = '\1';

	/* Set the prefetch count: */
	amqp_basic_qos_ok_t *r = amqp_basic_qos(
		connection->connection_state,
		channel->channel_id,
		0,
		channel->prefetch_count,
		0
	);
}
/* }}} */


/* {{{ proto amqp::isConnected()
check amqp channel */
PHP_METHOD(amqp_channel_class, isConnected)
{
	zval *id;
	amqp_channel_object *channel;

	/* Try to pull amqp object out of method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_channel_class_entry) == FAILURE) {
		RETURN_FALSE;
	}

	/* Get the channel object out of the store */
	channel = (amqp_channel_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* If the channel_connect is 1, we have a channel */
	if (channel->is_connected == '\1') {
		RETURN_TRUE;
	}

	/* We have no channel */
	RETURN_FALSE;
}
/* }}} */


/* {{{ proto amqp::setPrefetchCount(long count)
set the number of prefetches */
PHP_METHOD(amqp_channel_class, setPrefetchCount)
{
	zval *id;
	amqp_channel_object *channel;
	amqp_connection_object *connection;
	long prefetch_count;

	/* Get the vhost from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Ol", &id, amqp_channel_class_entry, &prefetch_count) == FAILURE) {
		RETURN_FALSE;
	}

	/* Get the channel object out of the store */
	channel = (amqp_channel_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Set the prefetch count - the implication is to disable the size */
	channel->prefetch_count = prefetch_count;
	channel->prefetch_size = 0;
		
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, amqp_channel_exception_class_entry, "Could not set prefetch count.");
	
	/* If we are already connected, set the new prefetch count */
	if (channel->is_connected) {
		amqp_basic_qos_ok_t *r = amqp_basic_qos(
			connection->connection_state,
			channel->channel_id,
			channel->prefetch_size,
			channel->prefetch_count,
			0
		);
	}
		
	RETURN_TRUE;
}
/* }}} */


/* {{{ proto amqp::setPrefetchSize(long size)
set the number of prefetches */
PHP_METHOD(amqp_channel_class, setPrefetchSize)
{
	zval *id;
	amqp_channel_object *channel;
	amqp_connection_object *connection;
	long prefetch_size;

	/* Get the vhost from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Ol", &id, amqp_channel_class_entry, &prefetch_size) == FAILURE) {
		RETURN_FALSE;
	}

	/* Get the channel object out of the store */
	channel = (amqp_channel_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Set the prefetch size - the implication is to disable the count */
	channel->prefetch_count = 0;
	channel->prefetch_size = prefetch_size;
	
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, amqp_channel_exception_class_entry, "Could not set prefetch size.");
	
	/* If we are already connected, set the new prefetch count */
	if (channel->is_connected) {
		amqp_basic_qos_ok_t *r = amqp_basic_qos(
			connection->connection_state,
			channel->channel_id,
			channel->prefetch_size,
			channel->prefetch_count,
			0
		);
	}
		
	RETURN_TRUE;
}
/* }}} */


/* {{{ proto amqp::qos(long size, long count)
set the number of prefetches */
PHP_METHOD(amqp_channel_class, qos)
{
	zval *id;
	amqp_channel_object *channel;
	amqp_connection_object *connection;
	long prefetch_size;
	long prefetch_count;

	/* Get the vhost from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oll", &id, amqp_channel_class_entry, &prefetch_size, &prefetch_count) == FAILURE) {
		RETURN_FALSE;
	}

	/* Get the channel object out of the store */
	channel = (amqp_channel_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Set the prefetch size - the implication is to disable the count */
	channel->prefetch_size = prefetch_size;
	channel->prefetch_count = prefetch_count;
	
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, amqp_channel_exception_class_entry, "Could not set qos parameters.");
	
	/* If we are already connected, set the new prefetch count */
	if (channel->is_connected) {
		amqp_basic_qos_ok_t *r = amqp_basic_qos(
			connection->connection_state,
			channel->channel_id,
			channel->prefetch_size,
			channel->prefetch_count,
			0
		);
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
