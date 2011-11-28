/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2007 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Alexandre Kalendarev akalend@mail.ru Copyright (c) 2009-2010 |
  | Lead:                                                                |
  | - Pieter de Zwart                                                    |
  | Maintainers:                                                         |
  | - Brad Rodriguez                                                     |
  | - Jonathan Tansavatdi                                                |
  +----------------------------------------------------------------------+
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


/* Used in ctor, so must be declated first */
void amqp_queue_dtor(void *object TSRMLS_DC)
{
	amqp_queue_object *queue = (amqp_queue_object*)object;

	/* Destroy the connection object */
	if (queue->channel) {
		zval_ptr_dtor(&queue->channel);
	}

	/* Destroy the arguments storage */
	if (queue->arguments) {
		zval_ptr_dtor(&queue->arguments);
	}

	zend_object_std_dtor(&queue->zo TSRMLS_CC);

	/* Destroy this object */
	efree(object);
}

zend_object_value amqp_queue_ctor(zend_class_entry *ce TSRMLS_DC)
{
	zend_object_value new_value;
	amqp_queue_object* queue = (amqp_queue_object*)emalloc(sizeof(amqp_queue_object));

	memset(queue, 0, sizeof(amqp_queue_object));

	zend_object_std_init(&queue->zo, ce TSRMLS_CC);

	// Initialize the arguments array:
	MAKE_STD_ZVAL(queue->arguments);
	array_init(queue->arguments);

	new_value.handle = zend_objects_store_put(
		queue,
		(zend_objects_store_dtor_t)zend_objects_destroy_object,
		(zend_objects_free_object_storage_t)amqp_queue_dtor,
		NULL TSRMLS_CC
	);
	new_value.handlers = zend_get_std_object_handlers();

	return new_value;
}

/*
Read a message that is pending on a channel. A call to basic.get or basic.consume must preceed this call.
*/
int read_message_from_channel(amqp_connection_state_t connection, zval *message) {
	size_t body_received = 0;
	size_t body_target = 0;
	char *message_body_buffer = NULL;
	amqp_frame_t frame;
	
	/* The "standard" for this is to continuously loop over frames in the pipeline until an entire message is read */
	while (1) {
		/* Release pending buffers for ingestion */
		amqp_maybe_release_buffers(connection);
		int result = amqp_simple_wait_frame(connection, &frame);
		
		/* Check that the basic read from frame did not fail */
		if (result < 0) {
			return AMQP_READ_ERROR;
		}
		
		if (frame.payload.method.id == AMQP_CHANNEL_CLOSE_OK_METHOD) {
			amqp_channel_close_t *err = (amqp_channel_close_t *)frame.payload.method.decoded;
			char str[256];
			char **pstr = (char **)&str;
			spprintf(pstr, 0, "Server error: %d", (int)err->reply_code);
			zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
			return AMQP_READ_ERROR;
		}
		
		if (frame.frame_type == AMQP_FRAME_METHOD) {
			if (frame.payload.method.id == AMQP_BASIC_GET_OK_METHOD) {
				/* get message metadata */
				amqp_basic_get_ok_t *delivery = (amqp_basic_get_ok_t *) frame.payload.method.decoded;
				
				add_assoc_stringl_ex(	message, 	"routing_key", 	12, delivery->routing_key.bytes, delivery->routing_key.len, 1);
				add_assoc_long_ex(		message,	"delivery_tag", 13, delivery->delivery_tag);
				add_assoc_bool_ex(		message,	"redelivered", 	12, delivery->redelivered);
				add_assoc_stringl_ex(	message,	"exchange", 	9, 	delivery->exchange.bytes, delivery->exchange.len, 1);
			} else if (frame.payload.method.id == AMQP_BASIC_DELIVER_METHOD) {
				/* get message metadata */
				amqp_basic_deliver_t *delivery = (amqp_basic_deliver_t *) frame.payload.method.decoded;
				
				add_assoc_stringl_ex(	message, 	"routing_key", 	12, delivery->routing_key.bytes, delivery->routing_key.len, 1);
				add_assoc_long_ex(		message,	"delivery_tag", 13, delivery->delivery_tag);
				add_assoc_bool_ex(		message,	"redelivered", 	12, delivery->redelivered);
				add_assoc_stringl_ex(	message,	"exchange", 	9, 	delivery->exchange.bytes, delivery->exchange.len, 1);
			}
			
			if (frame.payload.method.id == AMQP_BASIC_GET_EMPTY_METHOD) {
				return AMQP_READ_NO_MESSAGES;
			}
			
			/* This frame has been consumed, read in the next frame */
			continue;
		}
		
		if (frame.frame_type == AMQP_FRAME_HEADER) {
			body_target = frame.payload.properties.body_size;
			
			message_body_buffer = (char *) emalloc(body_target);
			memset(message_body_buffer, 0, body_target);
			
			amqp_basic_properties_t * p = (amqp_basic_properties_t *) frame.payload.properties.decoded;

			if (p->_flags & AMQP_BASIC_CONTENT_TYPE_FLAG) {
				add_assoc_stringl_ex(message, "Content-type", sizeof("Content-type"),
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
							add_assoc_stringl_ex(headers, entry->key.bytes, entry->key.len + 1, entry->value.value.bytes.bytes, entry->value.value.bytes.len, 1);
							break;
						case AMQP_FIELD_KIND_BYTES:
							add_assoc_stringl_ex(headers, entry->key.bytes, entry->key.len, entry->value.value.bytes.bytes, entry->value.value.bytes.len, 1);
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
			
			/* Check if we are going to even get a body */
			if (frame.payload.properties.body_size == 0) {
				/* No point in reading the next frame. Bail! */
				break;
			}
			
			/* This frame has been consumed, read in the next frame */
			continue;
		}
		
		if (frame.frame_type == AMQP_FRAME_BODY) {
			memcpy(message_body_buffer + body_received, frame.payload.body_fragment.bytes, frame.payload.body_fragment.len);
			body_received += frame.payload.body_fragment.len;
			
			/* Check if we have read in the entire body, ergo we have processed the entire message */
			if (body_received >= body_target) {
				break;
			}
		}
	}
	
	/* Put the final touches into the zval */
	add_assoc_stringl_ex(message, "message_body", sizeof("message_body"), message_body_buffer, body_target, 1);
	
	/* Free memory allocation */
	efree(message_body_buffer);
	
	return AMQP_READ_SUCCESS;
}

/* {{{ proto AMQPQueue::__construct(AMQPChannel channel)
AMQPQueue constructor
*/
PHP_METHOD(amqp_queue_class, __construct)
{
	zval *id;
	zval *channelObj = NULL;
	amqp_queue_object *queue;
	amqp_channel_object *channel;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "OO", &id, amqp_queue_class_entry, &channelObj, amqp_channel_class_entry) == FAILURE) {
		return;
	}

	/* Store the connection object for later */
	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Store the channel object */
	queue->channel = channelObj;
	
	/* Increment the ref count */
	Z_ADDREF_P(channelObj);

	/* Pull the channel out */
	channel = AMQP_GET_CHANNEL(queue);

	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	AMQP_VERIFY_CHANNEL(channel, "Could not construct queue.");

	/* We have a valid connection: */
	queue->is_connected = '\1';

	/* By default, the auto_delete flag should be set */
	queue->auto_delete = 1;
}
/* }}} */


/* {{{ proto AMQPQueue::getName()
Get the queue name */
PHP_METHOD(amqp_queue_class, getName)
{
	zval *id;
	amqp_queue_object *queue;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_queue_class_entry) == FAILURE) {
		return;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Check if there is a name to be had: */
	if (queue->name_len) {
		RETURN_STRING(queue->name, 1);
	} else {
		RETURN_FALSE;
	}
}
/* }}} */


/* {{{ proto AMQPQueue::setName(string name)
Set the queue name */
PHP_METHOD(amqp_queue_class, setName)
{
	zval *id;
	amqp_queue_object *queue;
	char *name = NULL;
	int name_len = 0;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os", &id, amqp_queue_class_entry, &name, &name_len) == FAILURE) {
		return;
	}

	/* Pull the queue off the object store */
	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Verify that the name is not null and not an empty string */
	if (name_len < 1 || name_len > 255) {
		zend_throw_exception(amqp_queue_exception_class_entry, "Invalid queue name given, must be between 1 and 255 characters long.", 0 TSRMLS_CC);
		return;
	}
	
	/* Set the queue name */
	AMQP_SET_NAME(queue, name);
}
/* }}} */



/* {{{ proto AMQPQueue::getFlags()
Get the queue parameters */
PHP_METHOD(amqp_queue_class, getFlags)
{
	zval *id;
	amqp_queue_object *queue;
	long flagBitmask = 0;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_queue_class_entry) == FAILURE) {
		return;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Set the bitmask based on what is set in the queue */
	flagBitmask |= (queue->passive ? AMQP_PASSIVE : 0);
	flagBitmask |= (queue->durable ? AMQP_DURABLE : 0);
	flagBitmask |= (queue->exclusive ? AMQP_EXCLUSIVE : 0);
	flagBitmask |= (queue->auto_delete ? AMQP_AUTODELETE : 0);
	
	RETURN_LONG(flagBitmask);
}
/* }}} */


/* {{{ proto AMQPQueue::setFlags(long bitmask)
Set the queue parameters */
PHP_METHOD(amqp_queue_class, setFlags)
{
	zval *id;
	amqp_queue_object *queue;
	long flagBitmask;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Ol", &id, amqp_queue_class_entry, &flagBitmask) == FAILURE) {
		return;
	}

	/* Pull the queue off the object store */
	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Set the flags based on the bitmask we were given */
	queue->passive = IS_PASSIVE(flagBitmask);
	queue->durable = IS_DURABLE(flagBitmask);
	queue->exclusive = IS_EXCLUSIVE(flagBitmask);
	queue->auto_delete = IS_AUTODELETE(flagBitmask);
	
	RETURN_TRUE;
}
/* }}} */


/* {{{ proto AMQPQueue::getArgument(string key)
Get the queue argument referenced by key */
PHP_METHOD(amqp_queue_class, getArgument)
{
	zval *id;
	zval **tmp;
	amqp_queue_object *queue;
	char *key;
	int key_len;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os", &id, amqp_queue_class_entry, &key, &key_len) == FAILURE) {
		return;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	if (zend_hash_find(Z_ARRVAL_P(queue->arguments), key, key_len + 1, (void **)&tmp) == FAILURE) {
		RETURN_FALSE;
	}
	
	*return_value = **tmp;
	zval_copy_ctor(return_value);
	
	Z_ADDREF_P(return_value);
	
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto AMQPQueue::getArguments
Get the queue arguments */
PHP_METHOD(amqp_queue_class, getArguments)
{
	zval *id;
	amqp_queue_object *queue;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_queue_class_entry) == FAILURE) {
		return;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	*return_value = *queue->arguments;
	zval_copy_ctor(return_value);

	/* Increment the ref count */
	Z_ADDREF_P(queue->arguments);

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto AMQPQueue::setArguments(array args)
Overwrite all queue arguments with given args */
PHP_METHOD(amqp_queue_class, setArguments)
{
	zval *id, *zvalArguments;
	amqp_queue_object *queue;
		
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oa", &id, amqp_queue_class_entry, &zvalArguments) == FAILURE) {
		return;
	}

	/* Pull the queue off the object store */
	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Destroy the arguments storage */
	if (queue->arguments) {
		zval_ptr_dtor(&queue->arguments);
	}
	
	queue->arguments = zvalArguments;
	
	/* Increment the ref count */
	Z_ADDREF_P(queue->arguments);

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto AMQPQueue::setArgument(key, value)
Get the queue name */
PHP_METHOD(amqp_queue_class, setArgument)
{
	zval *id, *value;
	amqp_queue_object *queue;
	char *key;
	int key_len;
	
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Osz", &id, amqp_queue_class_entry, &key, &key_len, &value) == FAILURE) {
		return;
	}

	/* Pull the queue off the object store */
	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);

	switch (Z_TYPE_P(value)) {
		case IS_NULL:
			zend_hash_del_key_or_index(Z_ARRVAL_P(queue->arguments), key, key_len + 1, 0, HASH_DEL_KEY);
			break;
		case IS_BOOL:
		case IS_LONG:
		case IS_DOUBLE:
		case IS_STRING:
			add_assoc_zval(queue->arguments, key, value);
			Z_ADDREF_P(value);
			break;
		default:
			zend_throw_exception(amqp_queue_exception_class_entry, "The value parameter must be of type NULL, int, double or string.", 0 TSRMLS_CC);
			return;
	}
	
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
	amqp_channel_object *channel;
	amqp_connection_object *connection;

	amqp_rpc_reply_t res;

	amqp_queue_declare_ok_t *r;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_queue_class_entry) == FAILURE) {
		zend_throw_exception(zend_exception_get_default(TSRMLS_C), "Error parsing parameters." ,0 TSRMLS_CC);
		return;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Make sure we have a queue name: */
	if (queue->name_len < 1) {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not declare queue. Queues must have names.", 0 TSRMLS_CC);
		return;
	}

	channel = AMQP_GET_CHANNEL(queue);
	AMQP_VERIFY_CHANNEL(channel, "Could not declare queue.");
	
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, "Could not declare queue.");
	
	amqp_table_t *arguments = convert_zval_to_arguments(queue->arguments);
	
	r = amqp_queue_declare(
		connection->connection_resource->connection_state,
		channel->channel_id,
		amqp_cstring_bytes(queue->name),
		queue->passive,
		queue->durable,
		queue->exclusive,
		queue->auto_delete,
		*arguments
	);
	
	res = (amqp_rpc_reply_t)amqp_get_rpc_reply(connection->connection_resource->connection_state); 
	
	AMQP_EFREE_ARGUMENTS(arguments);
	
	/* handle any errors that occured outside of signals */
	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char ** pstr = (char **) &str;
		amqp_error(res, pstr);
		channel->is_connected = '\0';
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}
	
	long message_count = r->message_count;
	
	RETURN_LONG(message_count);
}
/* }}} */


/* {{{ proto int AMQPQueue::bind(string exchangeName, string routingKey);
bind queue to exchange by routing key
*/
PHP_METHOD(amqp_queue_class, bind)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_channel_object *channel;
	amqp_connection_object *connection;
	char *exchange_name;
	int exchange_name_len;
	char *keyname;
	int keyname_len;

	amqp_rpc_reply_t res;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oss", &id, amqp_queue_class_entry, &exchange_name, &exchange_name_len, &keyname, &keyname_len) == FAILURE) {
		return;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not bind queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	channel = AMQP_GET_CHANNEL(queue);
	AMQP_VERIFY_CHANNEL(channel, "Could not bind queue.");
	
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, "Could not bind queue.");

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
		connection->connection_resource->connection_state,
		channel->channel_id,
		AMQP_QUEUE_BIND_METHOD,
		&bind_ok,
		&s
	);

	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char **pstr = (char **)&str;
		amqp_error(res, pstr);

		channel->is_connected = 0;
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto int AMQPQueue::get([bit flags=AMQP_NOPARAM]);
read messages from queue
return array (messages)
*/
PHP_METHOD(amqp_queue_class, get)
{
	zval *id;
    amqp_queue_object *queue;
	amqp_channel_object *channel;
	amqp_connection_object *connection;
	
	amqp_rpc_reply_t res;
	long flags = INI_INT("amqp.auto_ack") ? AMQP_AUTOACK : AMQP_NOPARAM;
	
	char *pbuf;

	int buf_max = FRAME_MAX;

	/* Parse out the method parameters */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O|l", &id, amqp_queue_class_entry, &flags) == FAILURE) {
		return;
	}
	
	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not get messages from queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	channel = AMQP_GET_CHANNEL(queue);
	AMQP_VERIFY_CHANNEL(channel, "Could not get messages from queue.");
	
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, "Could not get messages from queue.");
	
	/* Set the QOS for this channel to only pull off one message at a time */
	amqp_basic_qos(
		connection->connection_resource->connection_state,
		channel->channel_id,
		0,						/* prefetch window size */
		1,						/* prefetch message count */
		0						/* global flag */
	);
		
	/* Build basic.get request */
	amqp_basic_get_t s;
	
	s.ticket = 0,
	s.queue = amqp_cstring_bytes(queue->name);
	s.no_ack = (AMQP_AUTOACK & flags) ? 1 : 0;

	/* Get the next message using basic.get */
	int status = amqp_send_method(
		connection->connection_resource->connection_state,
		channel->channel_id,
		AMQP_BASIC_GET_METHOD,
		&s
	);
	
	/* We need to make an array out of the return value */
	array_init(return_value);
	
	/* Read the message off of the channel */
	int read = read_message_from_channel(connection->connection_resource->connection_state, return_value);
	
	/* Set the QOS back to what the user requested at the beginning */
	amqp_basic_qos(
		connection->connection_resource->connection_state,
		channel->channel_id,
		channel->prefetch_size,		/* prefetch window size */
		channel->prefetch_count,	/* prefetch message count */
		0							/* global flag */
	);

	if (read != AMQP_READ_SUCCESS) {
		/* Bad things happened. Bail */
		RETURN_FALSE;
	}	
}
/* }}} */


/* {{{ proto array AMQPQueue::consume(callback, [flags = <bitmask>]);
consume the message
return array messages
*/
PHP_METHOD(amqp_queue_class, consume)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_channel_object *channel;
	amqp_connection_object *connection;

	zend_fcall_info fci;
	zend_fcall_info_cache fci_cache;
	zval *retval_ptr = NULL;
	int function_call_succeeded = 1;

	char str[256];
	char **pstr = (char **)&str;
	long flags = INI_INT("amqp.auto_ack") ? AMQP_AUTOACK : AMQP_NOPARAM;

	amqp_basic_get_ok_t *get_ok;
	amqp_channel_close_t *err;

	int result;

	amqp_frame_t frame;

	size_t len = 0;
	char *tmp = NULL;
	char *old_tmp = NULL;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Of|l", &id, amqp_queue_class_entry, &fci, &fci_cache, &flags) == FAILURE) {
		return;
	}
	/* Initialize the return value pointer */
	fci.retval_ptr_ptr = &retval_ptr;

	/* Pull the queue out */
	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);

	channel = AMQP_GET_CHANNEL(queue);
	AMQP_VERIFY_CHANNEL(channel, "Could not get queue.");
	
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, "Could not get queue.");

	/* Setup the consume */
	amqp_table_t *arguments = convert_zval_to_arguments(queue->arguments);

	amqp_basic_consume(
		connection->connection_resource->connection_state,
		channel->channel_id,
		amqp_cstring_bytes(queue->name),
		AMQP_EMPTY_BYTES,					/* Consumer tag */
		(AMQP_NOLOCAL & flags) ? 1 : 0, 	/* No local */
		(AMQP_AUTOACK & flags) ? 1 : 0,		/* no_ack, aka AUTOACK */
		queue->exclusive,
		*arguments
	);

	AMQP_EFREE_ARGUMENTS(arguments);

	do {
		/* Initialize the message */
		zval *message;
		MAKE_STD_ZVAL(message);
		array_init(message);

		/* Read the message */
		int read = read_message_from_channel(connection->connection_resource->connection_state, message);

		/* Make the callback */
		if (read == AMQP_READ_SUCCESS) {
			/* Build the parameter array */
			zval *params;
			MAKE_STD_ZVAL(params);
			array_init(params);
			
			/* Dump it into the params array */
			add_index_zval(params, 0, message);
			
			/* Add a pointer to the queue: */
			add_index_zval(params, 1, id);
			
			/* Convert everything to be callable */
			zend_fcall_info_args(&fci, params TSRMLS_CC);
 
			/* Call the function, and track the return value */
 			if (zend_call_function(&fci, &fci_cache TSRMLS_CC) == SUCCESS && fci.retval_ptr_ptr && *fci.retval_ptr_ptr) {
 				COPY_PZVAL_TO_ZVAL(*return_value, *fci.retval_ptr_ptr);
			}
			
			/* Check if user land function wants to bail */
			if (Z_TYPE_P(return_value) == IS_BOOL && !Z_BVAL_P(return_value)) {
				function_call_succeeded = 0;
			}
			
			/* Clean up our mess */
			zend_fcall_info_args_clear(&fci, 1);
		}

	} while (read != AMQP_READ_ERROR && function_call_succeeded == 1);
}
/* }}} */


/* {{{ proto int AMQPQueue::ack(long deliveryTag, [bit flags=AMQP_NOPARAM]);
	acknowledge the message
*/
PHP_METHOD(amqp_queue_class, ack)
{
	zval *id;
	amqp_queue_object *queue;	
	amqp_channel_object *channel;
	amqp_connection_object *connection;
	
	long deliveryTag = 0;
	long flags = AMQP_NOPARAM;

	amqp_basic_ack_t s;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Ol|l", &id, amqp_queue_class_entry, &deliveryTag, &flags ) == FAILURE) {
		return;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not ack message. No connection available.", 0 TSRMLS_CC);
		return;
	}

	channel = AMQP_GET_CHANNEL(queue);
	AMQP_VERIFY_CHANNEL(channel, "Could not ack message.");
	
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, "Could not ack message.");
	
	s.delivery_tag = deliveryTag;
	s.multiple = (AMQP_MULTIPLE & flags) ? 1 : 0;

	int res = amqp_send_method(
		connection->connection_resource->connection_state,
		channel->channel_id,
		AMQP_BASIC_ACK_METHOD,
		&s
	);

	if (res) {
		channel->is_connected = 0;
		zend_throw_exception_ex(amqp_queue_exception_class_entry, 0 TSRMLS_CC, "Could not ack message, error code=%d", res);
		return;
	}

	RETURN_TRUE;
}
/* }}} */




/* {{{ proto int AMQPQueue::nack(long deliveryTag, [bit flags=AMQP_NOPARAM]);
	acknowledge the message
*/
PHP_METHOD(amqp_queue_class, nack)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_channel_object *channel;
	amqp_connection_object *connection;
	
	long deliveryTag = 0;
	long flags = AMQP_NOPARAM;

	amqp_basic_ack_t s;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Ol|l", &id, amqp_queue_class_entry, &deliveryTag, &flags ) == FAILURE) {
		return;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not nack message. No connection available.", 0 TSRMLS_CC);
		return;
	}

	channel = AMQP_GET_CHANNEL(queue);
	AMQP_VERIFY_CHANNEL(channel, "Could not ack message.");
	
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, "Could not ack message.");
	
	s.delivery_tag = deliveryTag;
	s.multiple = (AMQP_MULTIPLE & flags) ? 1 : 0;

	int res = amqp_send_method(
		connection->connection_resource->connection_state,
		channel->channel_id,
		AMQP_BASIC_NACK_METHOD,
		&s
	);

	if (res) {
		channel->is_connected = 0;
		zend_throw_exception_ex(amqp_queue_exception_class_entry, 0 TSRMLS_CC, "Could not nack message, error code=%d", res);
		return;
	}

	RETURN_TRUE;
}
/* }}} */



/* {{{ proto int AMQPQueue::purge();
purge queue
*/
PHP_METHOD(amqp_queue_class, purge)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_channel_object *channel;
	amqp_connection_object *connection;
	
	amqp_rpc_reply_t res;
	amqp_rpc_reply_t result;
	amqp_queue_purge_t s;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_queue_class_entry) == FAILURE) {
		return;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry,	"Could not purge queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	channel = AMQP_GET_CHANNEL(queue);
	AMQP_VERIFY_CHANNEL(channel, "Could not purge queue.");
	
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, "Could not purge queue.");

	s.ticket		= 0;
	s.queue.len		= queue->name_len;
	s.queue.bytes	= queue->name;
	s.nowait		= 0;

	amqp_method_number_t method_ok = AMQP_QUEUE_PURGE_OK_METHOD;
	result = amqp_simple_rpc(
		connection->connection_resource->connection_state,
		channel->channel_id,
		AMQP_QUEUE_PURGE_METHOD,
		&method_ok,
		&s
	);

	res = (amqp_rpc_reply_t) result;

	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char **pstr = (char **)&str;
		amqp_error(res, pstr);
		channel->is_connected = 0;
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto int AMQPQueue::cancel([string consumer_tag]);
cancel queue to consumer
*/
PHP_METHOD(amqp_queue_class, cancel)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_channel_object *channel;
	amqp_connection_object *connection;
	
	char *consumer_tag = NULL;
	int consumer_tag_len=0;
	amqp_rpc_reply_t res;
	amqp_rpc_reply_t result;
	amqp_basic_cancel_t s;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O|s", &id, amqp_queue_class_entry, &consumer_tag, &consumer_tag_len) == FAILURE) {
		return;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not cancel queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	channel = AMQP_GET_CHANNEL(queue);
	AMQP_VERIFY_CHANNEL(channel, "Could not cancel queue.");
	
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, "Could not cancel queue.");

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

	result = amqp_simple_rpc(
		connection->connection_resource->connection_state,
		channel->channel_id,
		AMQP_BASIC_CANCEL_METHOD,
		&method_ok,
		&s
	);

	res = (amqp_rpc_reply_t)result;

	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char **pstr = (char **)&str;
		amqp_error(res, pstr);
		channel->is_connected = 0;
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto int AMQPQueue::unbind(string exchangeName, string routingKey);
unbind queue from exchange
*/
PHP_METHOD(amqp_queue_class, unbind)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_channel_object *channel;
	amqp_connection_object *connection;
	
	char *exchange_name;
	int exchange_name_len;
	char *keyname;
	int keyname_len;

	amqp_rpc_reply_t res;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oss", &id, amqp_queue_class_entry, &exchange_name, &exchange_name_len, &keyname, &keyname_len) == FAILURE) {
		return;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not unbind queue. No connection available.", 0 TSRMLS_CC);
		return;
	}

	channel = AMQP_GET_CHANNEL(queue);
	AMQP_VERIFY_CHANNEL(channel, "Could not unbind queue.");
	
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, "Could not unbind queue.");

	amqp_queue_unbind_t s;
	s.ticket				= 0;
	s.queue.len				= queue->name_len;
	s.queue.bytes			= queue->name;
	s.exchange.len			= exchange_name_len;
	s.exchange.bytes		= exchange_name;
	s.routing_key.len		= keyname_len;
	s.routing_key.bytes		= keyname;
	s.arguments.num_entries = 0;
	s.arguments.entries		= NULL;

	amqp_method_number_t method_ok = AMQP_QUEUE_UNBIND_OK_METHOD;

	res = (amqp_rpc_reply_t) amqp_simple_rpc(
		connection->connection_resource->connection_state,
		channel->channel_id,
		AMQP_QUEUE_UNBIND_METHOD,
		&method_ok,
		&s);

	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char ** pstr = (char **) &str;
		amqp_error(res, pstr);

		channel->is_connected = 0;
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto int AMQPQueue::delete([long flags = AMQP_NOPARAM]]);
delete queue
*/
PHP_METHOD(amqp_queue_class, delete)
{
	zval *id;
	amqp_queue_object *queue;
	amqp_channel_object *channel;
	amqp_connection_object *connection;
	
	long flags = AMQP_NOPARAM;

	amqp_rpc_reply_t res;
	amqp_rpc_reply_t result;
	amqp_queue_delete_t s;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O|l", &id, amqp_queue_class_entry, &flags) == FAILURE) {
		return;
	}

	queue = (amqp_queue_object *)zend_object_store_get_object(id TSRMLS_CC);
	/* Check that the given connection has a channel, before trying to pull the connection off the stack */
	if (queue->is_connected != '\1') {
		zend_throw_exception(amqp_queue_exception_class_entry, "Could not delete queue. No connection available.", 0 TSRMLS_CC);
		return;
	}
	
	channel = AMQP_GET_CHANNEL(queue);
	AMQP_VERIFY_CHANNEL(channel, "Could not delete queue.");
	
	connection = AMQP_GET_CONNECTION(channel);
	AMQP_VERIFY_CONNECTION(connection, "Could not delete queue.");
	
	s.queue.len		= queue->name_len;
	s.queue.bytes	= queue->name;
	s.ticket		= 0;
	s.if_unused		= (AMQP_IFUNUSED & flags) ? 1 : 0;
	s.if_empty		= (AMQP_IFEMPTY & flags) ? 1 : 0;
	s.nowait		= 0;

	amqp_method_number_t method_ok = AMQP_QUEUE_DELETE_OK_METHOD;

	result = amqp_simple_rpc(
		connection->connection_resource->connection_state,
		channel->channel_id,
		AMQP_QUEUE_DELETE_METHOD,
		&method_ok,
		&s
	);

	res = (amqp_rpc_reply_t) result;

	if (res.reply_type != AMQP_RESPONSE_NORMAL) {
		char str[256];
		char **pstr = (char **)&str;
		amqp_error(res, pstr);
		channel->is_connected = 0;
		zend_throw_exception(amqp_queue_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	RETURN_TRUE;
}
/* }}} */


/*
*Local variables:
*tab-width: 4
*tabstop: 4
*c-basic-offset: 4
*End:
*vim600: noet sw=4 ts=4 fdm=marker
*vim<600: noet sw=4 ts=4
*/
