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


void amqp_dtor(void *object TSRMLS_DC);
zend_object_value amqp_ctor(zend_class_entry *ce TSRMLS_DC);

void php_amqp_connect(amqp_connection_object *amqp_connection);
void php_amqp_disconnect(amqp_connection_object *amqp_connection);

PHP_METHOD(amqp_connection_class, __construct);
PHP_METHOD(amqp_connection_class, isConnected);
PHP_METHOD(amqp_connection_class, connect);
PHP_METHOD(amqp_connection_class, disconnect);
PHP_METHOD(amqp_connection_class, reconnect);
PHP_METHOD(amqp_connection_class, setLogin);
PHP_METHOD(amqp_connection_class, setPassword);
PHP_METHOD(amqp_connection_class, setHost);
PHP_METHOD(amqp_connection_class, setPort);
PHP_METHOD(amqp_connection_class, setVhost);


/*
*Local variables:
*tab-width: 4
*c-basic-offset: 4
*End:
*vim600: noet sw=4 ts=4 fdm=marker
*vim<600: noet sw=4 ts=4
*/