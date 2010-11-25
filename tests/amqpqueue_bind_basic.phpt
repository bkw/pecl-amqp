--TEST--
AMQPQueue
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$cnn = new AMQPConnection();
$cnn->connect();
$ex = new AMQPExchange($cnn);
$ex->declare('exchange1', AMQP_EX_TYPE_DIRECT);
$queue = new AMQPQueue($cnn);
$queue->declare('queue1');
var_dump($queue->bind('exchange1', 'routing.key'));
?>
--EXPECT--
true