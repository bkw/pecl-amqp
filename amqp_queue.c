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
  | - Jonathan Tansavatdi jtansavatdi@php.net							|
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


static char *stringify_bytes(amqp_bytes_t bytes)
{
/* We will need up to 4 chars per byte, plus the terminating 0 */
	char *res = malloc(bytes.len * 4 + 1);
	uint8_t *data = bytes.bytes;
	char *p = res;
	size_t i;

	for (i = 0; i < bytes.len; i++) {
		if (data[i] >= 32 && data[i] != 127) {
			*p++ = data[i];
		} else {
			*p++ = '\\';
			*p++ = '0' + (data[i] >> 6);
			*p++ = '0' + (data[i] >> 3 & 0x7);
			*p++ = '0' + (data[i] & 0x7);
		}
	}

	*p = 0;
	return res;
}

void amqp_queue_dtor(void *object TSRMLS_DC)
{
	amqp_queue_object *queue = (amqp_queue_object*)object;
	
	/* Destroy the connection object */
	if (queue->cnn) {
		zval_ptr_dtor(&queue->cnn);
	}
	
	/* Destroy the arguments storage */
	if (&queue->arguments) {
		amqp_table_t table = queue->arguments;
		if (table.num_entries) {
			efree(table.entries);
		}
	}
	
	/* Destroy this object */
	efree(object);
}

zend_object_value amqp_queue_ctor(zend_class_entry *ce TSRMLS_DC)
{
	zend_object_value new_value;
	amqp_queue_object* queue = (amqp_queue_object*)emalloc(sizeof(amqp_queue_object));

	memset(queue, 0, sizeof(amqp_queue_object));

	zend_object_std_init(&queue->zo, ce TSRMLS_CC);
	
	new_value.handle = zend_objects_store_put(
		queue,
		(zend_objects_store_dtor_t)zend_objects_destroy_object,
		(zend_objects_free_object_storage_t)amqp_queue_dtor,
		NULL TSRMLS_CC
	);
	new_value.handlers = zend_get_std_object_handlers();

	return new_value;
}

/* {{{ proto AMQPQueue::__construct(AMQPConnection cnn)
AMQPQueue constructor
*/
PHP_METHOD(amqp_queue_class, __construct)
{
	zval *id;
	zval* cnnOb = NULL;
	amqp_queue_object *queue;
	amqp_connection_object *queue_cnn;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oo", &id, amqp_queue_class_entry, &cnnOb) == FAILURE) {
		RETURN_FALSE;
	}

	if (!instanceof_function(Z_OBJCE_P(cnnOb), amqp_connection_class_entry TSRMLS_CC)) {
		zend_throw_exception(amqp_queue_exception_class_entry, "The first parameter must be and instance of AMQPConnection.", 0 TSRMLS_CC);
		return;
	}

	/* Store the connection object for later */
	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	queue->cnn = cnnOb;
	
	/* Increment the ref count */
	Z_ADDREF_P(cnnOb);

	queue_cnn = (amqp_connection_object *) zend_object_store_get_object(queue->cnn TSRMLS_CC);

	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue_cnn->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not create queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	/* We have a valid connection: */
	queue->is_connected = '\1';

	/* By default, the auto_delete flag should be set */
	queue->auto_delete = 1;

	queue_cnn = (amqp_connection_object *)zend_object_store_get_object(cnnOb TSRMLS_CC);
}
/* }}} */


/* {{{ proto amqp_queue::getName()
Get the queue name */
PHP_METHOD(amqp_queue_class, getName)
{
	zval *id;
	amqp_queue_object *queue;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_queue_class_entry) == FAILURE) {
		RETURN_FALSE;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);

	// Check if there is a name to be had:
	if (queue->name_len) {
		RETURN_STRING(queue->name, 1);
	} else {
		RETURN_FALSE;
	}
}
/* }}} */


/* {{{ proto amqp_queue::setName(string name)
Set the queue name */
PHP_METHOD(amqp_queue_class, setName)
{
	zval *id;
	amqp_queue_object *queue;
	char *name = NULL;
	int name_len = 0;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os", &id, amqp_queue_class_entry, &name, &name_len) == FAILURE) {
		RETURN_FALSE;
	}

	/* Pull the queue off the object store */
	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Verify that the name is not null and not an empty string */
	if (name_len < 1 || name_len > 64) {
		zend_throw_exception(amqp_queue_exception_class_entry, "Invalid queue name given, must be between 1 and 64 characters long.", 0 TSRMLS_CC);
		return;
	}
	
	/* Set the queue name */
	AMQP_SET_NAME(queue, name);
}
/* }}} */



/* {{{ proto amqp_queue::getParameters()
Get the queue parameters */
PHP_METHOD(amqp_queue_class, getParameters)
{
	zval *id;
	amqp_queue_object *queue;
	long parameterBitmask = 0;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_queue_class_entry) == FAILURE) {
		RETURN_FALSE;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Set the bitmask based on what is set in the queue */
	parameterBitmask |= (queue->passive ? AMQP_PASSIVE : 0);
	parameterBitmask |= (queue->durable ? AMQP_DURABLE : 0);
	parameterBitmask |= (queue->exclusive ? AMQP_EXCLUSIVE : 0);
	parameterBitmask |= (queue->auto_delete ? AMQP_AUTODELETE : 0);
	
	RETURN_LONG(parameterBitmask);
}
/* }}} */


/* {{{ proto amqp_queue::setParameters(mixed bitmask)
Set the queue parameters */
PHP_METHOD(amqp_queue_class, setParameters)
{
	zval *id;
	amqp_queue_object *queue;
	zval *zvalParameterBitmask;
	long parameterBitmask;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oz", &id, amqp_queue_class_entry, &zvalParameterBitmask) == FAILURE) {
		RETURN_FALSE;
	}

	/* Pull the queue off the object store */
	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Parse out the port*/
	switch (Z_TYPE_P(zvalParameterBitmask)) {
		case IS_DOUBLE:
			parameterBitmask = (int)Z_DVAL_P(zvalParameterBitmask);
			break;
		case IS_LONG:
			parameterBitmask = (int)Z_LVAL_P(zvalParameterBitmask);
			break;
		case IS_STRING:
			convert_to_long(zvalParameterBitmask);
			parameterBitmask = (int)Z_LVAL_P(zvalParameterBitmask);
			break;
		default:
			parameterBitmask = 0;
	}
	
	/* Set the flags based on the bitmask we were given */
	queue->passive = IS_PASSIVE(parameterBitmask);
	queue->durable = IS_DURABLE(parameterBitmask);
	queue->exclusive = IS_EXCLUSIVE(parameterBitmask);
	queue->auto_delete = IS_AUTODELETE(parameterBitmask);
}
/* }}} */


/* {{{ proto amqp_queue::getArgument(string key)
Get the queue argument referenced by key */
PHP_METHOD(amqp_queue_class, getArgument)
{
	zval *id;
	amqp_queue_object *queue;
	char *key;
	int key_len;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os", &id, amqp_queue_class_entry, &key, &key_len) == FAILURE) {
		RETURN_FALSE;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	RETURN_NULL();
}
/* }}} */



/* {{{ proto amqp_queue::getArguments
Get the queue arguments */
PHP_METHOD(amqp_queue_class, getArguments)
{
	zval *id, *arguments;
	amqp_queue_object *queue;
	long index = 0;
	int i;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_queue_class_entry) == FAILURE) {
		RETURN_FALSE;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	MAKE_STD_ZVAL(arguments);
	array_init(arguments);
	for (i = 0; i < queue->arguments.num_entries; i++) {
		amqp_table_entry_t *entry = &(queue->arguments.entries[i]);

		switch (entry->value.kind) {
			case AMQP_FIELD_KIND_BOOLEAN:
				add_assoc_bool_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.boolean);
				break;
			case AMQP_FIELD_KIND_I8:
				add_assoc_long_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.i8);
				break;
			case AMQP_FIELD_KIND_U8:
				add_assoc_long_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.u8);
				break;
			case AMQP_FIELD_KIND_I16:
				add_assoc_long_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.i16);
				break;
			case AMQP_FIELD_KIND_U16:
				add_assoc_long_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.u16);
				break;
			case AMQP_FIELD_KIND_I32:
				add_assoc_long_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.i32);
				break;
			case AMQP_FIELD_KIND_U32:
				add_assoc_long_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.u32);
				break;
			case AMQP_FIELD_KIND_I64:
				printf("Setting %s to %d\n", stringify_bytes(entry->key), entry->value.value.i64);
				add_assoc_long_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.i64);
				break;
			case AMQP_FIELD_KIND_U64:
				add_assoc_long_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.u64);
				break;
			case AMQP_FIELD_KIND_F32:
				add_assoc_double_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.f32);
				break;
			case AMQP_FIELD_KIND_F64:
				add_assoc_double_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.f64);
				break;
			case AMQP_FIELD_KIND_UTF8:
				add_assoc_stringl_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.bytes.bytes, entry->value.value.bytes.len, 1);
				break;
			case AMQP_FIELD_KIND_BYTES:
				add_assoc_stringl_ex(arguments, entry->key.bytes, entry->key.len + 1, entry->value.value.bytes.bytes, entry->value.value.bytes.len, 1);
				break;

			case AMQP_FIELD_KIND_ARRAY:
			case AMQP_FIELD_KIND_TIMESTAMP:
			case AMQP_FIELD_KIND_TABLE:
			case AMQP_FIELD_KIND_VOID:
			case AMQP_FIELD_KIND_DECIMAL:
			break;
		}
	}

	*return_value = *arguments;
	
	zval_copy_ctor(return_value);
}
/* }}} */


/* {{{ proto amqp_queue::setArguments(array args)
Overwrite all queue arguments with given args */
PHP_METHOD(amqp_queue_class, setArguments)
{
	zval *id, *zvalArguments, **value, **data;
	amqp_queue_object *queue;
	HashTable *argumentHash;
	HashPosition pos;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oa", &id, amqp_queue_class_entry, &zvalArguments) == FAILURE) {
		RETURN_FALSE;
	}

	/* Pull the queue off the object store */
	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Pull the array out into something we can parse */
	argumentHash = Z_ARRVAL_P(zvalArguments);

	/* Clean up any old arguments: */
	if (&queue->arguments) {
		amqp_table_t table = queue->arguments;
		if (table.num_entries) {
			efree(table.entries);
		}
	}
	
	/* In setArguments, we are overwriting all the existing values */
	amqp_table_t arguments = EMPTY_ARGUMENTS;
	
	/* Allocate all the memory necessary for storing the arguments */
	arguments.entries = (amqp_table_entry_t *)emalloc(zend_hash_num_elements(argumentHash) * sizeof(amqp_table_entry_t));
	arguments.num_entries = 0;

	for (zend_hash_internal_pointer_reset_ex(argumentHash, &pos);
		zend_hash_get_current_data_ex(argumentHash, (void**) &data, &pos) == SUCCESS;
		zend_hash_move_forward_ex(argumentHash, &pos)) {
		
		/* Make a copy of the value: */
		zval value;
		value = **data;
		zval_copy_ctor(&value);
		
		/* Now pull the key */
		char *key;
		int key_len;
		long index;
		
		if (zend_hash_get_current_key_ex(argumentHash, &key, &key_len, &index, 0, &pos) != HASH_KEY_IS_STRING) {
			/* Skip things that are not strings */
			continue;
		}
		
		/* Build the value */
		amqp_table_entry_t *table = &arguments.entries[arguments.num_entries++];
		amqp_field_value_t *field = &table->value;
		table->key = amqp_cstring_bytes(estrndup(key, key_len));
		
		switch (Z_TYPE_P(&value)) {
			case IS_BOOL:
				field->kind = AMQP_FIELD_KIND_BOOLEAN;
				field->value.boolean = (amqp_boolean_t)Z_LVAL_P(&value);
				break;
			case IS_DOUBLE:
				field->kind = AMQP_FIELD_KIND_F64;
				field->value.f64 = Z_DVAL_P(&value);
				break;
			case IS_LONG:
				field->kind = AMQP_FIELD_KIND_I64;
				field->value.i64 = Z_LVAL_P(&value);
				break;
			case IS_STRING:
				field->kind = AMQP_FIELD_KIND_BYTES;
				field->value.bytes = amqp_cstring_bytes(estrndup(Z_STRVAL_P(&value), Z_STRLEN_P(&value)));
				break;
			default:
				continue;
		}
				
		/* Clean up the zval */
		zval_dtor(&value);
	}

	/* Store the arguments in the queue for later */
	queue->arguments = arguments;
	
	RETURN_TRUE;
}
/* }}} */


/* {{{ proto amqp_queue::setArgument(key, value)
Get the queue name */
PHP_METHOD(amqp_queue_class, setArgument)
{
	zval *id;
	amqp_queue_object *queue;
	char *newKey, *newValue;
	int key_len, value_len;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oss", &id, amqp_queue_class_entry, &newKey, key_len, &newValue, value_len) == FAILURE) {
		RETURN_FALSE;
	}

	/* Pull the queue off the object store */
	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);	

	

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto int AMQPQueue::declare();
declare queue
*/
PHP_METHOD(amqp_queue_class, declare)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_connection_object *queue_cnn;
	char *name;
	int name_len = 0;
	long parms = 0;
	amqp_queue_declare_t s;

	amqp_rpc_reply_t res;

	amqp_queue_declare_ok_t *r;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_queue_class_entry, &name, &name_len, &parms) == FAILURE) {
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Error parsing parameters." ,0 TSRMLS_CC);
		RETURN_FALSE;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not declare queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	amqp_bytes_t amqp_name = (amqp_bytes_t) {queue->name_len, queue->name};
	amqp_table_t arguments = EMPTY_ARGUMENTS;
		
	queue_cnn = (amqp_connection_object *) zend_object_store_get_object(queue->cnn TSRMLS_CC);
	
	amqp_queue_declare(queue_cnn->conn, AMQP_CHANNEL, amqp_name, queue->passive, queue->durable, queue->exclusive, queue->auto_delete, arguments);
	res = (amqp_rpc_reply_t)amqp_get_rpc_reply(queue_cnn->conn); 
	
	/* handle any errors that occured outside of signals */
	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char ** pstr = (char **) &str;
		amqp_error(res, pstr);
		queue_cnn->is_connected = '\0';
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	r = (amqp_queue_declare_ok_t *) res.reply.decoded;

	/* Pull the name out of the request so that we can store it */
	strncpy(queue->name, stringify_bytes(amqp_bytes_malloc_dup(r->queue)), 254);
	/* Null terminate */
	queue->name[254] = 0;
	/* Set the name len as well */
	queue->name_len = strlen(queue->name);

	RETURN_LONG(r->message_count);
}
/* }}} */


/* {{{ proto int queue::bind(string exchangeName, string routingKey);
bind queue to exchange by routing key
*/
PHP_METHOD(amqp_queue_class, bind)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_connection_object *queue_cnn;
	char *name;
	int name_len;
	char *exchange_name;
	int exchange_name_len;
	char *keyname;
	int keyname_len;

	amqp_rpc_reply_t res;
	amqp_rpc_reply_t result;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oss", &id,
	amqp_queue_class_entry, &exchange_name, &exchange_name_len, &keyname, &keyname_len) == FAILURE) {
		RETURN_FALSE;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not bind queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	amqp_connection_object *cnn = (amqp_connection_object *) zend_object_store_get_object(queue->cnn TSRMLS_CC);

	amqp_queue_bind_t s;
	s.ticket 				= 0;
	s.queue.len				= queue->name_len;
	s.queue.bytes			= queue->name;
	s.exchange.len			= exchange_name_len;
	s.exchange.bytes		= exchange_name;
	s.routing_key.len		= keyname_len;
	s.routing_key.bytes		= keyname;
	s.nowait				= 0;
	s.arguments.num_entries = 0;
	s.arguments.entries	 	= NULL;

	amqp_method_number_t bind_ok = AMQP_QUEUE_BIND_OK_METHOD;

	res = (amqp_rpc_reply_t) amqp_simple_rpc(
		cnn->conn,
		AMQP_CHANNEL,
		AMQP_QUEUE_BIND_METHOD,
		&bind_ok,
		&s
	);

	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char **pstr = (char **)&str;
		amqp_error(res, pstr);

		cnn->is_channel_connected = 0;
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto int queue::get([ bit params=AMQP_NOACK ]);
read message from queue
return array (count_in_queue, message)
*/
PHP_METHOD(amqp_queue_class, get)
{
	zval *id;
	amqp_queue_object *queue;
	char *type=NULL;
	int type_len;
	amqp_rpc_reply_t res;

	char str[256];
	char **pstr = (char **)&str;
	long parms = AMQP_NOACK;

	zval content;

	amqp_basic_get_ok_t *get_ok;
	amqp_channel_close_t *err;

	int result;

	int count = 0;
	amqp_frame_t frame;

	size_t len = 0;
	char *tmp = NULL;
	char *old_tmp = NULL;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O|l", &id, amqp_queue_class_entry, &parms) == FAILURE) {
		RETURN_FALSE;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not get from queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	amqp_connection_object *cnn = (amqp_connection_object *) zend_object_store_get_object(queue->cnn TSRMLS_CC);

	amqp_basic_get_t s;
	s.ticket = 0,
	s.queue.len = queue->name_len;
	s.queue.bytes = queue->name;
	s.no_ack = (AMQP_NOACK & parms) ? 1 : 0;

	int status = amqp_send_method(cnn->conn,
		AMQP_CHANNEL,
		AMQP_BASIC_GET_METHOD,
		&s
	);
	
	array_init(return_value);

	while (1) { /* receive	frames:	 */

		amqp_maybe_release_buffers(cnn->conn);
		result = amqp_simple_wait_frame(cnn->conn, &frame);

		if (result < 0) {
			RETURN_FALSE;
		}

		if (frame.frame_type == AMQP_FRAME_METHOD) {

			if (AMQP_BASIC_GET_OK_METHOD == frame.payload.method.id) {

				get_ok = (amqp_basic_get_ok_t *) frame.payload.method.decoded;
				count = get_ok->message_count;

				add_assoc_stringl_ex(
					return_value,
					"routing_key",
					12,
					get_ok->routing_key.bytes,
					get_ok->routing_key.len,
					1
				);

				add_assoc_stringl_ex(
					return_value,
					"exchange",
					9,
					get_ok->exchange.bytes,
					get_ok->exchange.len,
					1
				);

				add_assoc_long_ex(
					return_value,
					"delivery_tag",
					13,
					get_ok->delivery_tag
				);
			}

			if (AMQP_CHANNEL_CLOSE_OK_METHOD == frame.payload.method.id) {
				err = (amqp_channel_close_t *)frame.payload.method.decoded;
				spprintf(pstr, 0, "Server error: %d", (int)err->reply_code);
				zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
				return;
			}

			if (AMQP_BASIC_GET_EMPTY_METHOD == frame.payload.method.id ) {
				count = -1;
				break;
			}

			continue;

		} /* ------ end GET_OK */

		if (frame.frame_type == AMQP_FRAME_HEADER) {

			amqp_basic_properties_t *p = (amqp_basic_properties_t *) frame.payload.properties.decoded;

			if (p->_flags & AMQP_BASIC_CONTENT_TYPE_FLAG) {
				add_assoc_stringl_ex(return_value,
					"Content-type",
					13,
					p->content_type.bytes,
					p->content_type.len,
					1
				);
			}

			if (p->_flags & AMQP_BASIC_CONTENT_ENCODING_FLAG) {
				add_assoc_stringl_ex(return_value,
					"Content-encoding",
					sizeof("Content-encoding"),
					p->content_encoding.bytes,
					p->content_encoding.len,
					1
				);
			}

			if (p->_flags & AMQP_BASIC_TYPE_FLAG) {
				add_assoc_stringl_ex(return_value,
					"type",
					sizeof("type"),
					p->type.bytes,
					p->type.len,
					1
				);
			}

			if (p->_flags & AMQP_BASIC_TIMESTAMP_FLAG) {
				add_assoc_long(return_value, "timestamp", p->timestamp);
			}

			if (p->_flags & AMQP_BASIC_PRIORITY_FLAG) {
				add_assoc_long(return_value, "priority", p->priority);
			}

			if (p->_flags & AMQP_BASIC_EXPIRATION_FLAG) {
				add_assoc_stringl_ex(return_value,
					"expiration",
					sizeof("expiration"),
					p->expiration.bytes,
					p->expiration.len,
					1
				);
			}

			if (p->_flags & AMQP_BASIC_USER_ID_FLAG) {
				add_assoc_stringl_ex(return_value,
					"user_id",
					sizeof("user_id"),
					p->user_id.bytes,
					p->user_id.len,
					1
				);
			}

			if (p->_flags & AMQP_BASIC_APP_ID_FLAG) {
				add_assoc_stringl_ex(return_value,
					"app_id",
					sizeof("app_id"),
					p->app_id.bytes,
					p->app_id.len,
					1
				);
			}

			if (p->_flags & AMQP_BASIC_MESSAGE_ID_FLAG) {
				add_assoc_stringl_ex(return_value,
					"message_id",
					sizeof("message_id"),
					p->message_id.bytes,
					p->message_id.len,
					1
				);
			}

			if (p->_flags & AMQP_BASIC_REPLY_TO_FLAG) {
				add_assoc_stringl_ex(return_value,
					"Reply-to",
					sizeof("Reply-to"),
					p->reply_to.bytes,
					p->reply_to.len,
					1
				);
			}

			if (p->_flags & AMQP_BASIC_CORRELATION_ID_FLAG) {
				add_assoc_stringl_ex(return_value,
					"correlation_id",
					sizeof("correlation_id"),
					p->correlation_id.bytes,
					p->correlation_id.len,
					1
				);
			}

			if (frame.payload.properties.body_size==0) {
				break;
			}
			continue;
		}

		if (frame.frame_type == AMQP_FRAME_BODY) {

			uint frame_len = frame.payload.body_fragment.len;
			size_t old_len = len;
			len += frame_len;

			if (tmp) {
				old_tmp = tmp;
				tmp = (char *)emalloc(len);
				memcpy(tmp, old_tmp, old_len);
				efree(old_tmp);
				memcpy(tmp + old_len,frame.payload.body_fragment.bytes, frame_len);
			} else { /* the first allocate */
				tmp = (char *)estrdup(frame.payload.body_fragment.bytes);
			}

			if (frame_len < FRAME_MAX - HEADER_FOOTER_SIZE) {
				break;
			}

			continue;
		}

	} /* end while */

	add_assoc_long(return_value, "count",count);

	if (count > -1) {
		add_assoc_stringl_ex(return_value,
			"msg",
			4,
			tmp,
			len,
			1
		);
		efree(tmp);
	}
}
/* }}} */


/* {{{ proto array queue::consume(array('ack' => true, 'min' => 1, 'max' => 5));
consume the message
return array messages
*/
PHP_METHOD(amqp_queue_class, consume)
{
	zval *id;
	amqp_queue_object *queue;
	int queue_len;
	amqp_rpc_reply_t res;
	
	int ack;
	long min_consume;
	long max_consume;
	zval* iniArr = NULL;
	zval** zdata;

	char *pbuf;
	long parms = 0;

	zval content;
	int buf_max = FRAME_MAX;

	/* Parse out the method parameters */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O|a", &id, amqp_queue_class_entry, &iniArr) == FAILURE) {
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "parse parameter error", 0 TSRMLS_CC);
		return;
	}
	
	/* Pull the minimum consume settings out of the config array */
	min_consume = INI_INT("amqp.min_consume");
	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "min", sizeof("min"), (void*)&zdata)) {
		convert_to_long(*zdata);
		min_consume = (size_t)Z_LVAL_PP(zdata);
	}
	
	/* Pull the minimum consume settings out of the config array */
	max_consume = INI_INT("amqp.max_consume");
	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "max", sizeof("max"), (void*)&zdata)) {
		convert_to_long(*zdata);
		max_consume = (size_t)Z_LVAL_PP(zdata);
	}

	if (min_consume > max_consume) {
		zend_throw_exception(amqp_queue_exception_class_entry, "'min' cannot be more then 'max' consume", 0 TSRMLS_CC);
	}

	/* Pull the auto ack settings out of the config array */
	ack = INI_INT("amqp.ack");
	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "ack", sizeof("ack"), (void*)&zdata)) {
		convert_to_long(*zdata);
		ack = (size_t)Z_LVAL_PP(zdata);
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not consume from queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	amqp_connection_object *cnn = (amqp_connection_object *)zend_object_store_get_object(queue->cnn TSRMLS_CC);

	amqp_basic_consume(cnn->conn, AMQP_CHANNEL, amqp_cstring_bytes(queue->name), AMQP_EMPTY_BYTES, 0, 0, 0, AMQP_EMPTY_TABLE);
	
	/* verify there are no errors before grabbing the messages */
	res = (amqp_rpc_reply_t)amqp_get_rpc_reply(cnn->conn);	
	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		cnn->is_channel_connected = 0;
		char str[256];
		char ** pstr = (char **) &str;
		amqp_error(res, pstr);
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}
	
	amqp_basic_consume_ok_t *r = (amqp_basic_consume_ok_t *) res.reply.decoded;

	memcpy(queue->consumer_tag, r->consumer_tag.bytes, r->consumer_tag.len);
	queue->consumer_tag_len = r->consumer_tag.len;

	int received = 0;
	int previous_received = 0;

	amqp_frame_t frame;
	int result;
	size_t body_received;
	size_t body_target;
	int i;
	array_init(return_value);
	char *buf = NULL;
	
	amqp_boolean_t messages_left;

	for (i = 0; i < max_consume; i++) {

		amqp_maybe_release_buffers(cnn->conn);
		
		/* if we have met the minimum number of messages, check to see if there are messages left */
		if (i >= min_consume) {
			/* see if there are messages in the queue */ 
			amqp_bytes_t amqp_name;
			amqp_name = (amqp_bytes_t) {queue->name_len, queue->name};
			amqp_queue_declare_ok_t *r = amqp_queue_declare(cnn->conn,
				AMQP_CHANNEL,
				amqp_name,
				queue->passive,
				queue->durable,
				queue->exclusive,
				queue->auto_delete,
				AMQP_EMPTY_TABLE);
			int messages_in_queue = r->message_count;
								
			/* see if there are frames enqueued */
			amqp_boolean_t frames = amqp_frames_enqueued(cnn->conn);
			
			/* see if there is any unread data in the buffer */
			amqp_boolean_t buffer = amqp_data_in_buffer(cnn->conn);
			
			if (!messages_in_queue && !frames && !buffer) {
				break;
			}
		}

		/* get next frame from the queue (blocks) */
		result = amqp_simple_wait_frame(cnn->conn, &frame);
		
		/* check frame validity */
		if (result < 0) {
			return;
		}
		if (frame.frame_type != AMQP_FRAME_METHOD) {
			continue;
		}
		if (frame.payload.method.id != AMQP_BASIC_DELIVER_METHOD) {
			continue;
		}

		/* initialize message array */
		zval *message;
		MAKE_STD_ZVAL(message);
		array_init(message);

		/* get message metadata */
		amqp_basic_deliver_t * delivery = (amqp_basic_deliver_t *) frame.payload.method.decoded;
		add_assoc_stringl_ex(message, "consumer_tag", 13,
			delivery->consumer_tag.bytes,
			delivery->consumer_tag.len, 1);

		add_assoc_long_ex(message, "delivery_tag", 13,
			delivery->delivery_tag);

		add_assoc_bool_ex(message, "redelivered", 12,
			delivery->redelivered);

		add_assoc_stringl_ex(message, "routing_key", 12,
			delivery->routing_key.bytes,
			delivery->routing_key.len, 1 );

		add_assoc_stringl_ex(message, "exchange", 9,
			delivery->exchange.bytes,
			delivery->exchange.len, 1);			
		
		/* get header frame (blocks) */
		result = amqp_simple_wait_frame(cnn->conn, &frame);
		if (result < 0) {
			zend_throw_exception(amqp_queue_exception_class_entry, "The returned read frame is invalid.", 0 TSRMLS_CC);
			return;
		}

		if (frame.frame_type != AMQP_FRAME_HEADER) {
			zend_throw_exception(amqp_queue_exception_class_entry, "The returned frame type is invalid.", 0 TSRMLS_CC);
			return;
		}
		
		amqp_basic_properties_t * p = (amqp_basic_properties_t *) frame.payload.properties.decoded;

		if (p->_flags & AMQP_BASIC_CONTENT_TYPE_FLAG) {
			add_assoc_stringl_ex(message, "Content-type", 13,
				p->content_type.bytes,
				p->content_type.len, 1);
			}
			
		if (p->_flags & AMQP_BASIC_CONTENT_ENCODING_FLAG) {
			add_assoc_stringl_ex(message, "Content-encoding", sizeof("Content-encoding"),
				p->content_encoding.bytes,
				p->content_encoding.len, 1);
		}
		
		if (p->_flags & AMQP_BASIC_TYPE_FLAG) {
			add_assoc_stringl_ex(message, "type", sizeof("type"),
				p->type.bytes,
				p->type.len, 1);
		}
	
		if (p->_flags & AMQP_BASIC_TIMESTAMP_FLAG) {
			add_assoc_long(message, "timestamp", p->timestamp);
		}

		if (p->_flags & AMQP_BASIC_PRIORITY_FLAG) {
			add_assoc_long(message, "priority", p->priority);
		}
			
		if (p->_flags & AMQP_BASIC_EXPIRATION_FLAG) {
			add_assoc_stringl_ex(message, "expiration", sizeof("expiration"),
				p->expiration.bytes,
				p->expiration.len, 1);
		}
			
		if (p->_flags & AMQP_BASIC_USER_ID_FLAG) {
			add_assoc_stringl_ex(message, "user_id", sizeof("user_id"),
				p->user_id.bytes,
				p->user_id.len, 1);
		}
			
		if (p->_flags & AMQP_BASIC_APP_ID_FLAG) {
			add_assoc_stringl_ex(message, "app_id", sizeof("app_id"),
				p->app_id.bytes,
				p->app_id.len, 1 );
		}

		if (p->_flags & AMQP_BASIC_MESSAGE_ID_FLAG) {
			add_assoc_stringl_ex(message, "message_id", sizeof("message_id"),
				p->message_id.bytes,
				p->message_id.len, 1);
		}

		if (p->_flags & AMQP_BASIC_REPLY_TO_FLAG) {
			add_assoc_stringl_ex(message, "Reply-to", sizeof("Reply-to"),
				p->reply_to.bytes,
				p->reply_to.len, 1);
		}

		if (p->_flags & AMQP_BASIC_CORRELATION_ID_FLAG) {
			add_assoc_stringl_ex(message, "correlation_id", sizeof("correlation_id"),
				p->correlation_id.bytes,
				p->correlation_id.len, 1);
		}

		if (p->_flags & AMQP_BASIC_HEADERS_FLAG) {
			zval *headers;
			int   i;

			MAKE_STD_ZVAL(headers);
			array_init(headers);
			for (i = 0; i < p->headers.num_entries; i++) {
				amqp_table_entry_t *entry = &(p->headers.entries[i]);

				switch (entry->value.kind) {
					case AMQP_FIELD_KIND_BOOLEAN:
						add_assoc_bool_ex(headers, entry->key.bytes, entry->key.len + 1, entry->value.value.boolean);
						break;
					case AMQP_FIELD_KIND_I8:
						add_assoc_long_ex(headers, entry->key.bytes, entry->key.len + 1, entry->value.value.i8);
						break;
					case AMQP_FIELD_KIND_U8:
						add_assoc_long_ex(headers, entry->key.bytes, entry->key.len + 1, entry->value.value.u8);
						break;
					case AMQP_FIELD_KIND_I16:
						add_assoc_long_ex(headers, entry->key.bytes, entry->key.len + 1, entry->value.value.i16);
						break;
					case AMQP_FIELD_KIND_U16:
						add_assoc_long_ex(headers, entry->key.bytes, entry->key.len + 1, entry->value.value.u16);
						break;
					case AMQP_FIELD_KIND_I32:
						add_assoc_long_ex(headers, entry->key.bytes, entry->key.len + 1, entry->value.value.i32);
						break;
					case AMQP_FIELD_KIND_U32:
						add_assoc_long_ex(headers, entry->key.bytes, entry->key.len + 1, entry->value.value.u32);
						break;
					case AMQP_FIELD_KIND_I64:
						add_assoc_long_ex(headers, entry->key.bytes, entry->key.len + 1, entry->value.value.i64);
						break;
					case AMQP_FIELD_KIND_U64:
						add_assoc_long_ex(headers, entry->key.bytes, entry->key.len + 1, entry->value.value.u64);
						break;
					case AMQP_FIELD_KIND_F32:
						add_assoc_double_ex(headers, entry->key.bytes, entry->key.len + 1, entry->value.value.f32);
						break;
					case AMQP_FIELD_KIND_F64:
						add_assoc_double_ex(headers, entry->key.bytes, entry->key.len + 1, entry->value.value.f64);
						break;
					case AMQP_FIELD_KIND_UTF8:
						add_assoc_stringl_ex(headers, entry->key.bytes, entry->key.len + 1,
													entry->value.value.bytes.bytes, entry->value.value.bytes.len, 1);
						break;
					case AMQP_FIELD_KIND_BYTES:
						add_assoc_stringl_ex(headers, entry->key.bytes, entry->key.len,
													entry->value.value.bytes.bytes, entry->value.value.bytes.len, 1);
					break;

					case AMQP_FIELD_KIND_ARRAY:
					case AMQP_FIELD_KIND_TIMESTAMP:
					case AMQP_FIELD_KIND_TABLE:
					case AMQP_FIELD_KIND_VOID:
					case AMQP_FIELD_KIND_DECIMAL:
					break;
				}
			}

			add_assoc_zval_ex(message, "headers", sizeof("headers"), headers);
			
		}

		body_target = frame.payload.properties.body_size;
		body_received = 0;
		
		buf = (char*) emalloc(FRAME_MAX);
		if (!buf) {
			zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Out of memory (malloc)" ,0 TSRMLS_CC);   
		}

		/* resize buffer if necessary */
		if (body_target > buf_max) {
			int count_buf = body_target / FRAME_MAX +1;
			int resize = count_buf * FRAME_MAX;
			buf_max = resize;
			pbuf = erealloc(buf, resize);
			if (!pbuf) {
				efree(buf);
				zend_throw_exception(zend_exception_get_default(TSRMLS_C), "The memory is out (realloc)", 0 TSRMLS_CC);
			}
			buf = pbuf; 
		}

		pbuf = buf;
		while (body_received < body_target) {
			result = amqp_simple_wait_frame(cnn->conn, &frame);
			if (result < 0) {
				break;
			}

			if (frame.frame_type != AMQP_FRAME_BODY) {
				zend_throw_exception(amqp_queue_exception_class_entry, "The returned frame has no body.", 0 TSRMLS_CC);
				return;
			}

			memcpy(pbuf, frame.payload.body_fragment.bytes, frame.payload.body_fragment.len);
			body_received += frame.payload.body_fragment.len;
			pbuf += frame.payload.body_fragment.len;

		} /* end while	*/

		/* add message body to message */
		add_assoc_stringl_ex(message, "message_body", sizeof("message_body"), buf, body_target, 1);
		/* add message to return value */
		add_index_zval(return_value, i, message);
		
		/* if we have chosen to ack, do so */
		if (ack) {
			amqp_basic_ack(cnn->conn, 1, delivery->delivery_tag, 0);
		}
		
		efree(buf);
	}

}
/* }}} */


/* {{{ proto int queue::ack(long deliveryTag, [bit params=AMQP_NONE]);
	acknowledge the message
*/
PHP_METHOD(amqp_queue_class, ack)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_connection_object *queue_cnn;
	long deliveryTag = 0;
	long parms = 0;

	amqp_connection_object * cnn;
	amqp_basic_ack_t s;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Ol|l", &id, amqp_queue_class_entry, &deliveryTag, &parms ) == FAILURE) {
		RETURN_FALSE;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not ack message. No connection available.", 0 TSRMLS_CC);
		return;
	}

	cnn = (amqp_connection_object *)zend_object_store_get_object(queue->cnn TSRMLS_CC);

	s.delivery_tag = deliveryTag;
	s.multiple = ( AMQP_MULTIPLE & parms ) ? 1 : 0;

	int res = amqp_send_method(cnn->conn,
				AMQP_CHANNEL,
				AMQP_BASIC_ACK_METHOD,
				&s);

	if (res) {
		cnn->is_channel_connected = 0;
		zend_throw_exception_ex(amqp_queue_exception_class_entry, 0 TSRMLS_CC, "Ack error; code=%d", res);
		return;
	}

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto int queue::purge(name);
purge queue
*/
PHP_METHOD(amqp_queue_class, purge)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_connection_object *queue_cnn;
	char *name;
	int name_len=0;

	amqp_rpc_reply_t res;
	amqp_rpc_reply_t result;
	amqp_queue_purge_ok_t *r;
	amqp_queue_purge_t s;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os", &id,
	amqp_queue_class_entry, &name, &name_len) == FAILURE) {
		RETURN_FALSE;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry,	"Could not purge queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	amqp_connection_object *cnn = (amqp_connection_object *) zend_object_store_get_object(queue->cnn TSRMLS_CC);

	if (name_len) {
		s.ticket		= 0;
		s.queue.len		= name_len;
		s.queue.bytes	= name;
		s.nowait		= 0;
	} else {
		s.ticket		= 0;
		s.queue.len		= queue->name_len;
		s.queue.bytes	= queue->name;
		s.nowait		= 0;
	}

	amqp_method_number_t method_ok = AMQP_QUEUE_PURGE_OK_METHOD;
	result = amqp_simple_rpc(
		cnn->conn,
		AMQP_CHANNEL,
		AMQP_QUEUE_PURGE_METHOD,
		&method_ok,
		&s
	);

	res = (amqp_rpc_reply_t) result;

	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char **pstr = (char **)&str;
		amqp_error(res, pstr);
		cnn->is_channel_connected = 0;
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto int queue::cancel(consumer_tag);
cancel queue to consumer
*/
PHP_METHOD(amqp_queue_class, cancel)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_connection_object *queue_cnn;
	char *consumer_tag = NULL;
	int consumer_tag_len=0;
	amqp_rpc_reply_t res;
	amqp_rpc_reply_t result;
	amqp_basic_cancel_t s;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O|s", &id, amqp_queue_class_entry, &consumer_tag, &consumer_tag_len) == FAILURE) {
		RETURN_FALSE;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not cancel queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	amqp_connection_object *cnn = (amqp_connection_object *)zend_object_store_get_object(queue->cnn TSRMLS_CC);

	if (consumer_tag_len) {
		s.consumer_tag.len = consumer_tag_len;
		s.consumer_tag.bytes = consumer_tag;
		s.nowait = 0;
	} else {
		s.consumer_tag.len = queue->consumer_tag_len;
		s.consumer_tag.bytes = queue->consumer_tag;
		s.nowait = 0;
	}

	amqp_method_number_t method_ok = AMQP_BASIC_CANCEL_OK_METHOD;

	result = amqp_simple_rpc(cnn->conn,
		AMQP_CHANNEL,
		AMQP_BASIC_CANCEL_METHOD,
		&method_ok,
		&s
	);

	res = (amqp_rpc_reply_t)result;

	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char **pstr = (char **)&str;
		amqp_error(res, pstr);
		cnn->is_channel_connected = 0;
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto int queue::ubind(string exchangeName, string routingKey);
unbind queue from exchange
*/
PHP_METHOD(amqp_queue_class, unbind)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_connection_object *queue_cnn;
	char *name;
	int name_len;
	char *exchange_name;
	int exchange_name_len;
	char *keyname;
	int keyname_len;

	amqp_rpc_reply_t res;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oss", &id,
	amqp_queue_class_entry, &exchange_name, &exchange_name_len, &keyname, &keyname_len) == FAILURE) {
		RETURN_FALSE;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not unbind queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	amqp_connection_object *cnn = (amqp_connection_object *) zend_object_store_get_object(queue->cnn TSRMLS_CC);

	amqp_queue_unbind_t s;
	s.ticket				= 0,
	s.queue.len			 = queue->name_len;
	s.queue.bytes		   = queue->name;
	s.exchange.len		  = exchange_name_len;
	s.exchange.bytes		= exchange_name;
	s.routing_key.len	   = keyname_len;
	s.routing_key.bytes	 = keyname;
	s.arguments.num_entries = 0;
	s.arguments.entries	 = NULL;

	amqp_method_number_t method_ok = AMQP_QUEUE_UNBIND_OK_METHOD;

	res = (amqp_rpc_reply_t) amqp_simple_rpc(cnn->conn,
		AMQP_CHANNEL,
		AMQP_QUEUE_UNBIND_METHOD,
		&method_ok,
		&s);

	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char ** pstr = (char **) &str;
		amqp_error(res, pstr);

		cnn->is_channel_connected = 0;
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto int queue::delete(name);
delete queue
*/
PHP_METHOD(amqp_queue_class, delete)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_connection_object *queue_cnn;
	char *name;
	int name_len = 0;
	long parms = 0;

	amqp_rpc_reply_t res;
	amqp_rpc_reply_t result;
	amqp_queue_delete_ok_t *r;
	amqp_queue_delete_t s;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O|s", &id, amqp_queue_class_entry, &name, &name_len, &parms) == FAILURE) {
		RETURN_FALSE;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not delete queue. No connection available.", 0 TSRMLS_CC);
		return;
	}
	amqp_connection_object *cnn = (amqp_connection_object *) zend_object_store_get_object(queue->cnn TSRMLS_CC);

	if (!cnn || !cnn->conn) {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not delete queue. The connection is closed.", 0 TSRMLS_CC);
		return;
	}

	if (name_len) {
		s.ticket		= 0;
		s.queue.len		= name_len;
		s.queue.bytes	= name;
		s.if_unused		= (AMQP_IFUNUSED & parms)? 1:0;
		s.if_empty		= (AMQP_IFEMPTY & parms)? 1:0;
		s.nowait		= 0;
	} else {
		s.ticket		= 0;
		s.queue.len		= queue->name_len;
		s.queue.bytes	= queue->name;
		s.if_unused		= (AMQP_IFUNUSED & parms) ? 1 : 0;
		s.if_empty		= (AMQP_IFEMPTY & parms) ? 1 : 0;
		s.nowait		= 0;
	}

	amqp_method_number_t method_ok = AMQP_QUEUE_DELETE_OK_METHOD;

	result = amqp_simple_rpc(
		cnn->conn,
		AMQP_CHANNEL,
		AMQP_QUEUE_DELETE_METHOD,
		&method_ok,
		&s
	);

	res = (amqp_rpc_reply_t) result;

	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char **pstr = (char **)&str;
		amqp_error(res, pstr);
		cnn->is_channel_connected = 0;
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
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
