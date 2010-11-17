--TEST--
AMQPQueue
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$cnn = new AMQPConnection();
$cnn->connect();
$queue = new AMQPQueue($cnn);
echo $queue->bind('exchange', 'routing.key') ? 'true' : 'false';
?>
--EXPECT--
true