--TEST--
AMQPQueue var_dump
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$cnn = new AMQPConnection();
$cnn->connect();
$ch = new AMQPChannel($cnn);
// Declare a new exchange
$ex = new AMQPExchange($ch);
$ex->setName('exchange1');
$ex->setType(AMQP_EX_TYPE_FANOUT);
$ex->declare();
// Create a new queue
$q = new AMQPQueue($ch);
$q->setName('queue_var_dump');
$q->declare();
var_dump($q);
?>
--EXPECT--
object(AMQPQueue)#4 (7) {
  ["queue_name"]=>
  string(14) "queue_var_dump"
  ["consumer_tag"]=>
  string(0) ""
  ["passive"]=>
  bool(false)
  ["durable"]=>
  bool(false)
  ["exclusive"]=>
  bool(false)
  ["auto_delete"]=>
  bool(true)
  ["arguments"]=>
  NULL
}

