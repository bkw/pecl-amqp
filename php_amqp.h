/*
  +----------------------------------------------------------------------+
  | PHP Version 5														|
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2007 The PHP Group								|
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,	  |
  | that is bundled with this package in the file LICENSE, and is		|
  | available through the world-wide-web at the following url:		   |
  | http://www.php.net/license/3_01.txt								  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to		  |
  | license@php.net so we can mail you a copy immediately.			   |
  +----------------------------------------------------------------------+
  | Author: Alexandre Kalendarev akalend@mail.ru Copyright (c) 2009-2010 |
  | Contributor: 														 |
  | - Pieter de Zwart pdezwart@php.net									 |
  | - Brad Rodriguez brodrigu@gmail.com									 |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifndef PHP_AMQP_H
#define PHP_AMQP_H

extern zend_module_entry amqp_module_entry;
#define phpext_amqp_ptr &amqp_module_entry

#ifdef PHP_WIN32
#define PHP_AMQP_API __declspec(dllexport)
#else
#define PHP_AMQP_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

#define AMQP_NOPARM			1

#define AMQP_DURABLE		2
#define AMQP_PASSIVE		4
#define AMQP_EXCLUSIVE		8
#define AMQP_AUTODELETE		16
#define AMQP_INTERNAL		32
#define AMQP_NOLOCAL		64
#define AMQP_NOACK			128
#define AMQP_IFEMPTY		256
#define AMQP_IFUNUSED		528
#define AMQP_MANDATORY		1024
#define AMQP_IMMEDIATE		2048
#define AMQP_MULTIPLE	   4096

#define AMQP_EX_TYPE_DIRECT	 "direct"
#define AMQP_EX_TYPE_FANOUT	 "fanout"
#define AMQP_EX_TYPE_TOPIC	  "topic"
#define AMQP_EX_TYPE_HEADER	 "header"

PHP_MINIT_FUNCTION(amqp);
PHP_MSHUTDOWN_FUNCTION(amqp);
PHP_MINFO_FUNCTION(amqp);

void amqp_error(amqp_rpc_reply_t x, char ** pstr);

/* True global resources - no need for thread safety here */
extern zend_class_entry *amqp_connection_class_entry;
extern zend_class_entry *amqp_queue_class_entry;
extern zend_class_entry *amqp_exchange_class_entry;
extern zend_class_entry *amqp_exception_class_entry,
				 *amqp_connection_exception_class_entry,
				 *amqp_exchange_exception_class_entry,
				 *amqp_queue_exception_class_entry;


#define FRAME_MAX				131072	/* max length (size) of frame */
#define HEADER_FOOTER_SIZE		8	   /*  7 bytes up front, then payload, then 1 byte footer */
#define DEFAULT_PORT			5672	/* default AMQP port */
#define DEFAULT_PORT_STR		"5672"
#define DEFAULT_HOST			"localhost"
#define DEFAULT_VHOST		   "/"
#define DEFAULT_LOGIN			"guest"
#define DEFAULT_PASSWORD		"guest"
#define DEFAULT_ACK				"1"
#define DEFAULT_MIN_CONSUME		"0"
#define DEFAULT_MAX_CONSUME		"1"
#define AMQP_CHANNEL			1	   /* default channel number */
#define AMQP_HEARTBEAT			0	   /* heartbeat */

#define AMQP_NULLARGS			amqp_table_t arguments = {0, NULL};
#define AMQP_PASSIVE_D			short passive = (AMQP_PASSIVE & parms) ? 1 : 0;
#define AMQP_DURABLE_D			short durable = (AMQP_DURABLE & parms) ? 1 : 0;
#define AMQP_AUTODELETE_D		short auto_delete = (AMQP_AUTODELETE & parms) ? 1 : 0;
#define AMQP_EXCLUSIVE_D		short exclusive = (AMQP_EXCLUSIVE & parms) ? 1 : 0;

#define AMQP_SET_NAME(ctx, str) (ctx)->name_len = strlen(str) >= sizeof((ctx)->name) ? sizeof((ctx)->name) - 1 : strlen(str); \
			 strncpy((ctx)->name, name, (ctx)->name_len); \
				 (ctx)->name[(ctx)->name_len] = '\0';

/* If you declare any globals in php_amqp.h uncomment this:
 ZEND_DECLARE_MODULE_GLOBALS(amqp)
*/

typedef struct _amqp_connection_object {
	zend_object zo;
	char is_connected;
	char is_channel_connected;
	char *login;
	int char_len;
	char *password;
	int password_len;
	char *host;
	int host_len;
	char *vhost;
	int vhost_len;
	int port;
	int fd;
	amqp_connection_state_t conn;
} amqp_connection_object;

typedef struct _amqp_queue_object {
	zend_object zo;
	zval *cnn;
	char is_connected;
	char name[64];
	int name_len;
	char consumer_tag[64];
	int consumer_tag_len;
	int passive; /* @TODO: consider making these bit fields */
	int durable;
	int exclusive;
	int auto_delete; /* end @TODO */
} amqp_queue_object;


typedef struct _amqp_exchange_object {
	zend_object zo;
	zval *cnn;
	char is_connected;
	char name[64];
	int name_len;
} amqp_exchange_object;

#ifdef ZTS
#define AMQP_G(v) TSRMG(amqp_globals_id, zend_amqp_globals *, v)
#else
#define AMQP_G(v) (amqp_globals.v)
#endif

#endif	/* PHP_AMQP_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
