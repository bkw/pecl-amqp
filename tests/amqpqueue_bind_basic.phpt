--TEST--
AMQPQueue
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$cnn = new AMQPConnection();
$cnn->connect();

$ch = new AMQPChannel($cnn);

$ex = new AMQPExchange($ch);
$exName = 'exchange-' . time();
$qName = 'queue-' . time();
$ex->declare($exName, AMQP_EX_TYPE_DIRECT);

$queue = new AMQPQueue($ch);
$queue->declare($qName);
var_dump($queue->bind($exName, 'routing.key'));
?>
--EXPECT--
bool(true)
