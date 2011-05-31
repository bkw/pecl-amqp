--TEST--
AMQPQueue
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$cnn = new AMQPConnection();
$cnn->connect();

// Declare a new exchange
$ex = new AMQPExchange($cnn);
$ex->declare('exchange1', AMQP_EX_TYPE_FANOUT);

// Create a new queue
$q = new AMQPQueue($cnn);
$queueName = 'queue1' . time();
$q->declare($queueName);

// Bind it on the exchange to routing.key
$ex->bind($queueName, 'routing.*');

// Publish a message to the exchange with a routing key
$ex->publish('message', 'routing.1');
$ex->publish('message2', 'routing.2');
$ex->publish('message3', 'routing.3');

// Read from the queue
$options = array(
 'min' => 1,
 'max' => 10,
 'ack' => false
);
$msg = $q->consume($options);
var_dump($msg);
?>
--EXPECT--
array(3) {
  [0]=>
  array(7) {
    ["consumer_tag"]=>
    string(33) "amq.ctag-B7pSjCczI4Q3aJMmYSz03Q=="
    ["delivery_tag"]=>
    int(1)
    ["redelivered"]=>
    bool(false)
    ["routing_key"]=>
    string(9) "routing.1"
    ["exchange"]=>
    string(9) "exchange1"
    ["Content-type"]=>
    string(10) "text/plain"
    ["message_body"]=>
    string(7) "message"
  }
  [1]=>
  array(7) {
    ["consumer_tag"]=>
    string(33) "amq.ctag-B7pSjCczI4Q3aJMmYSz03Q=="
    ["delivery_tag"]=>
    int(2)
    ["redelivered"]=>
    bool(false)
    ["routing_key"]=>
    string(9) "routing.2"
    ["exchange"]=>
    string(9) "exchange1"
    ["Content-type"]=>
    string(10) "text/plain"
    ["message_body"]=>
    string(8) "message2"
  }
  [2]=>
  array(7) {
    ["consumer_tag"]=>
    string(33) "amq.ctag-B7pSjCczI4Q3aJMmYSz03Q=="
    ["delivery_tag"]=>
    int(3)
    ["redelivered"]=>
    bool(false)
    ["routing_key"]=>
    string(9) "routing.3"
    ["exchange"]=>
    string(9) "exchange1"
    ["Content-type"]=>
    string(10) "text/plain"
    ["message_body"]=>
    string(8) "message3"
  }
}

