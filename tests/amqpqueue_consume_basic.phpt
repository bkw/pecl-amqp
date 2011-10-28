--TEST--
AMQPQueue
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$cnn = new AMQPConnection();
$cnn->connect();

$ch = new AMQPChannel($cnn);

// Declare a new exchange
$ex = new AMQPExchange($ch);
$ex->setName('exchange-' . time());
$ex->setType(AMQP_EX_TYPE_FANOUT);
$ex->declare();

// Create a new queue
$q = new AMQPQueue($ch);
$q->setName('queue-' . time());
$q->declare();

// Bind it on the exchange to routing.key
$q->bind($ex->getName(), 'routing.*');

// Publish a message to the exchange with a routing key
$ex->publish('message', 'routing.1');
$ex->publish('message2', 'routing.2');
$ex->publish('message3', 'routing.3');

// Read from the queue
$msg = $q->consume();
var_dump($msg);

$msg = $q->consume();
var_dump($msg);

$ex->delete();
?>
--EXPECT--
array(2) {
  ["Content-type"]=>
  string(10) "text/plain"
  ["msg"]=>
  string(7) "message"
}
array(2) {
  ["Content-type"]=>
  string(10) "text/plain"
  ["msg"]=>
  string(8) "message2"
}
