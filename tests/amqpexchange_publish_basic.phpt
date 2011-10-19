--TEST--
AMQPExchange
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$cnn = new AMQPConnection();
$cnn->connect();

$ch = new AMQPChannel($cnn);

$ex = new AMQPExchange($ch);
$ex->declare('exchange-' . time());
echo $ex->publish('message', 'routing.key') ? 'true' : 'false';
?>
--EXPECT--
true