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

/**
 * 	php_amqp_connect
 *	handles connecting to amqp
 *	called by connect() and reconnect()
 */
void php_amqp_connect(amqp_connection_object *connection TSRMLS_DC)
{
	char str[256];
	char ** pstr = (char **) &str;
	void * old_handler;

	/* create the connection */
	connection->connection_state = amqp_new_connection();

	connection->fd = amqp_open_socket(connection->host, connection->port);

	if (connection->fd < 1) {
		/* Start ignoring SIGPIPE */
		old_handler = signal(SIGPIPE, SIG_IGN);

		amqp_destroy_connection(connection->connection_state);

		/* End ignoring of SIGPIPEs */
		signal(SIGPIPE, old_handler);

		zend_throw_exception(amqp_connection_exception_class_entry, "Socket error: could not connect to host.", 0 TSRMLS_CC);
		return;
	}
	connection->is_connected = '\1';

	amqp_set_sockfd(connection->connection_state, connection->fd);

	amqp_rpc_reply_t x = amqp_login(connection->connection_state, connection->vhost, 0, FRAME_MAX, AMQP_HEARTBEAT, AMQP_SASL_METHOD_PLAIN, connection->login, connection->password);

	if (x.reply_type != AMQP_RESPONSE_NORMAL) {
		amqp_error(x, pstr);
		zend_throw_exception(amqp_connection_exception_class_entry, *pstr, 0 TSRMLS_CC);
		return;
	}

	connection->is_connected = '\1';
}

/* 	php_amqp_disconnect
	handles disconnecting from amqp
	called by disconnect(), reconnect(), and d_tor
 */
void php_amqp_disconnect(amqp_connection_object *connection)
{
	void * old_handler;
	/*
	If we are trying to close the connection and the connection already closed, it will throw
	SIGPIPE, which is fine, so ignore all SIGPIPES
	*/

	/* Start ignoring SIGPIPE */
	old_handler = signal(SIGPIPE, SIG_IGN);
	
	if (connection->is_connected == '\1') {
		/* TODO: disconnect from all channels */
		// amqp_channel_close(connection->connection_state, channel->channel_id, AMQP_REPLY_SUCCESS);
	}
	connection->is_connected = '\0';

	if (connection->connection_state && connection->is_connected == '\1') {
		amqp_connection_close(connection->connection_state, AMQP_REPLY_SUCCESS);
		amqp_destroy_connection(connection->connection_state);
	}
	
	connection->is_connected = '\0';
	if (connection->fd) {
		close(connection->fd);
	}

	/* End ignoring of SIGPIPEs */
	signal(SIGPIPE, old_handler);
	
	return;
}

void amqp_connection_dtor(void *object TSRMLS_DC)
{
	amqp_connection_object *ob = (amqp_connection_object*)object;

	php_amqp_disconnect(ob);

	/* Clean up all the strings */
	if (ob->host) {
		efree(ob->host);
	}
	
	if (ob->vhost) {
		efree(ob->vhost);
	}
	
	if (ob->login) {
		efree(ob->login);
	}
	
	if (ob->password) {
		efree(ob->password);
	}

	zend_object_std_dtor(&ob->zo TSRMLS_CC);

	efree(object);

}

zend_object_value amqp_connection_ctor(zend_class_entry *ce TSRMLS_DC)
{
	zend_object_value new_value;
	amqp_connection_object* obj = (amqp_connection_object*)emalloc(sizeof(amqp_connection_object));

	memset(obj, 0, sizeof(amqp_connection_object));

	zend_object_std_init(&obj->zo, ce TSRMLS_CC);

	new_value.handle = zend_objects_store_put(obj, (zend_objects_store_dtor_t)zend_objects_destroy_object, (zend_objects_free_object_storage_t)amqp_connection_dtor, NULL TSRMLS_CC);
	new_value.handlers = zend_get_std_object_handlers();

	return new_value;
}


/* {{{ proto AMQPConnection::__construct([array optional])
 * The array can contain 'host', 'port', 'login', 'password', 'vhost' indexes
 */
PHP_METHOD(amqp_connection_class, __construct)
{
	zval *id;
	amqp_connection_object *connection;

	zval* iniArr = NULL;
	zval** zdata;

	/* Parse out the method parameters */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O|a", &id, amqp_connection_class_entry, &iniArr) == FAILURE) {
		return;
	}

	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Pull the login out of the $params array */
	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "login", sizeof("login"), (void*)&zdata)) {
		convert_to_string(*zdata);
	}
	/* Validate the given login */
	if (zdata && Z_STRLEN_PP(zdata) > 0) {
		if (Z_STRLEN_PP(zdata) < 32) {
			connection->login = estrndup(Z_STRVAL_PP(zdata), Z_STRLEN_PP(zdata));
		} else {
			zend_throw_exception(amqp_connection_exception_class_entry, "Parameter 'login' exceeds 32 character limit.", 0 TSRMLS_CC);
			return;
		}
	} else {
		connection->login = estrndup(INI_STR("amqp.login"), strlen(INI_STR("amqp.login")) > 32 ? 32 : strlen(INI_STR("amqp.login")));
	}
	/* @TODO: write a macro to reduce code duplication */

	/* Pull the password out of the $params array */
	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF(iniArr), "password", sizeof("password"), (void*)&zdata)) {
		convert_to_string(*zdata);
	}
	/* Validate the given password */
	if (zdata && Z_STRLEN_PP(zdata) > 0) {
		if (Z_STRLEN_PP(zdata) < 32) {
			connection->password = estrndup(Z_STRVAL_PP(zdata), Z_STRLEN_PP(zdata));
		} else {
			zend_throw_exception(amqp_connection_exception_class_entry, "Parameter 'password' exceeds 32 character limit.", 0 TSRMLS_CC);
			return;
		}
	} else {
		connection->password = estrndup(INI_STR("amqp.password"), strlen(INI_STR("amqp.password")) > 32 ? 32 : strlen(INI_STR("amqp.password")));
	}

	/* Pull the host out of the $params array */
	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF(iniArr), "host", sizeof("host"), (void *)&zdata)) {
		convert_to_string(*zdata);
	}
	/* Validate the given host */
	if (zdata && Z_STRLEN_PP(zdata) > 0) {
		if (Z_STRLEN_PP(zdata) < 32) {
			connection->host = estrndup(Z_STRVAL_PP(zdata), Z_STRLEN_PP(zdata));
		} else {
			zend_throw_exception(amqp_connection_exception_class_entry, "Parameter 'host' exceeds 32 character limit.", 0 TSRMLS_CC);
			return;
		}
	} else {
		connection->host = estrndup(INI_STR("amqp.host"), strlen(INI_STR("amqp.host")) > 32 ? 32 : strlen(INI_STR("amqp.host")));
	}

	/* Pull the vhost out of the $params array */
	zdata = NULL;
	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "vhost", sizeof("vhost"), (void*)&zdata)) {
		convert_to_string(*zdata);
	}
	/* Validate the given vhost */
	if (zdata && Z_STRLEN_PP(zdata) > 0) {
		if (Z_STRLEN_PP(zdata) < 32) {
			connection->vhost = estrndup(Z_STRVAL_PP(zdata), Z_STRLEN_PP(zdata));
		} else {
			zend_throw_exception(amqp_connection_exception_class_entry, "Parameter 'vhost' exceeds 32 character limit.", 0 TSRMLS_CC);
			return;
		}
	} else {
		connection->vhost = estrndup(INI_STR("amqp.vhost"), strlen(INI_STR("amqp.vhost")) > 32 ? 32 : strlen(INI_STR("amqp.vhost")));
	}

	connection->port = INI_INT("amqp.port");

	if (iniArr && SUCCESS == zend_hash_find(HASH_OF (iniArr), "port", sizeof("port"), (void*)&zdata)) {
		convert_to_long(*zdata);
		connection->port = (size_t)Z_LVAL_PP(zdata);
	}
}
/* }}} */


/* {{{ proto amqp::isConnected()
check amqp connection */
PHP_METHOD(amqp_connection_class, isConnected)
{
	zval *id;
	amqp_connection_object *connection;

	/* Try to pull amqp object out of method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_connection_class_entry) == FAILURE) {
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* If the channel_connect is 1, we have a connection */
	if (connection->is_connected == '\1') {
		RETURN_TRUE;
	}

	/* We have no connection */
	RETURN_FALSE;
}
/* }}} */


/* {{{ proto amqp::connect()
create amqp connection */
PHP_METHOD(amqp_connection_class, connect)
{
	zval *id;
	amqp_connection_object *connection;

	/* Try to pull amqp object out of method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_connection_class_entry) == FAILURE) {
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	php_amqp_connect(connection TSRMLS_CC);
	
	/* @TODO: return connection success or failure */
	RETURN_TRUE;
}

/* }}} */

/* {{{ proto amqp::disconnect()
destroy amqp connection */
PHP_METHOD(amqp_connection_class, disconnect)
{
	zval *id;
	amqp_connection_object *connection;

	/* Try to pull amqp object out of method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_connection_class_entry) == FAILURE) {
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	php_amqp_disconnect(connection);

	RETURN_TRUE;
}

/* }}} */

/* {{{ proto amqp::reconnect()
recreate amqp connection */
PHP_METHOD(amqp_connection_class, reconnect)
{
	zval *id;
	amqp_connection_object *connection;

	/* Try to pull amqp object out of method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_connection_class_entry) == FAILURE) {
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	if (connection->is_connected) {
		php_amqp_disconnect(connection);
	}
	
	php_amqp_connect(connection TSRMLS_CC);
	
	/* @TODO: return the success or failure of connect */
	RETURN_TRUE;
}
/* }}} */


/* {{{ proto amqp::getLogin()
get the login */
PHP_METHOD(amqp_connection_class, getLogin)
{
	zval *id;
	amqp_connection_object *connection;

	/* Get the login from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_connection_class_entry) == FAILURE) {
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Copy the login to the amqp object */
	RETURN_STRING(connection->login, 1);
}
/* }}} */


/* {{{ proto amqp::setLogin(string login)
set the login */
PHP_METHOD(amqp_connection_class, setLogin)
{
	zval *id;
	amqp_connection_object *connection;
	char *login;
	int login_len;

	/* @TODO: use macro when one is created for constructor */
	/* Get the login from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os", &id, amqp_connection_class_entry, &login, &login_len) == FAILURE) {
		return;
	}

	/* Validate login length */
	if (login_len > 32) {
		zend_throw_exception(amqp_connection_exception_class_entry, "Invalid 'login' given, exceeds 32 characters limit.", 0 TSRMLS_CC);
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Copy the login to the amqp object */
	connection->login = estrndup(login, login_len);

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto amqp::getPassword()
get the password */
PHP_METHOD(amqp_connection_class, getPassword)
{
	zval *id;
	amqp_connection_object *connection;

	/* Get the password from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_connection_class_entry) == FAILURE) {
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Copy the password to the amqp object */
	RETURN_STRING(connection->password, 1);
}
/* }}} */


/* {{{ proto amqp::setPassword(string password)
set the password */
PHP_METHOD(amqp_connection_class, setPassword)
{
	zval *id;
	amqp_connection_object *connection;
	char *password;
	int password_len;

	/* Get the password from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os", &id, amqp_connection_class_entry, &password, &password_len) == FAILURE) {
		return;
	}

	/* Validate password length */
	if (password_len > 32) {
		zend_throw_exception(amqp_connection_exception_class_entry, "Invalid 'password' given, exceeds 32 characters limit.", 0 TSRMLS_CC);
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Copy the password to the amqp object */
	connection->password = estrndup(password, password_len);

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto amqp::getHost()
get the host */
PHP_METHOD(amqp_connection_class, getHost)
{
	zval *id;
	amqp_connection_object *connection;

	/* Get the host from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_connection_class_entry) == FAILURE) {
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Copy the host to the amqp object */
	RETURN_STRING(connection->host, 1);
}
/* }}} */


/* {{{ proto amqp::setHost(string host)
set the host */
PHP_METHOD(amqp_connection_class, setHost)
{
	zval *id;
	amqp_connection_object *connection;
	char *host;
	int host_len;

	/* Get the host from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os", &id, amqp_connection_class_entry, &host, &host_len) == FAILURE) {
		return;
	}

	/* Validate host length */
	if (host_len > 1024) {
		zend_throw_exception(amqp_connection_exception_class_entry, "Invalid 'host' given, exceeds 1024 character limit.", 0 TSRMLS_CC);
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Copy the host to the amqp object */
	connection->host = estrndup(host, host_len);

	RETURN_TRUE;
}
/* }}} */


/* {{{ proto amqp::getPort()
get the port */
PHP_METHOD(amqp_connection_class, getPort)
{
	zval *id;
	amqp_connection_object *connection;

	/* Get the port from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_connection_class_entry) == FAILURE) {
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Copy the port to the amqp object */
	RETURN_LONG(connection->port);
}
/* }}} */


/* {{{ proto amqp::setPort(mixed port)
set the port */
PHP_METHOD(amqp_connection_class, setPort)
{
	zval *id;
	amqp_connection_object *connection;
	zval *zvalPort;
	int port;

	/* Get the port from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oz", &id, amqp_connection_class_entry, &zvalPort) == FAILURE) {
		return;
	}

	/* Parse out the port*/
	switch (Z_TYPE_P(zvalPort)) {
		case IS_DOUBLE:
			port = (int)Z_DVAL_P(zvalPort);
			break;
		case IS_LONG:
			port = (int)Z_LVAL_P(zvalPort);
			break;
		case IS_STRING:
			convert_to_long(zvalPort);
			port = (int)Z_LVAL_P(zvalPort);
			break;
		default:
			port = 0;
	}

	/* Check the port value */
	if (port <= 0 || port > 65535) {
		zend_throw_exception(amqp_connection_exception_class_entry, "Invalid port given. Value must be between 1 and 65535.", 0 TSRMLS_CC);
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Copy the port to the amqp object */
	connection->port = port;

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto amqp::getVhost()
get the vhost */
PHP_METHOD(amqp_connection_class, getVhost)
{
	zval *id;
	amqp_connection_object *connection;

	/* Get the vhost from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &id, amqp_connection_class_entry) == FAILURE) {
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Copy the vhost to the amqp object */
	RETURN_STRING(connection->vhost, 1);
}
/* }}} */


/* {{{ proto amqp::setVhost(string vhost)
set the vhost */
PHP_METHOD(amqp_connection_class, setVhost)
{
	zval *id;
	amqp_connection_object *connection;
	char *vhost;
	int vhost_len;

	/* Get the vhost from the method params */
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os", &id, amqp_connection_class_entry, &vhost, &vhost_len) == FAILURE) {
		return;
	}

	/* Validate vhost length */
	if (vhost_len > 32) {
		zend_throw_exception(amqp_connection_exception_class_entry, "Parameter 'vhost' exceeds 32 characters limit.", 0 TSRMLS_CC);
		return;
	}

	/* Get the connection object out of the store */
	connection = (amqp_connection_object *)zend_object_store_get_object(id TSRMLS_CC);

	/* Copy the vhost to the amqp object */
	connection->vhost = estrndup(vhost, vhost_len);

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
