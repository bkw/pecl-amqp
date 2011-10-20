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
$ex->setName('exchange1');
$ex->setType(AMQP_EX_TYPE_FANOUT);
$ex->declare();

// Create a new queue
$q = new AMQPQueue($ch);
$q->setName('queue1' . time());
$q->declare();

// Bind it on the exchange to routing.key
$ex->bind($q->getName(), 'routing.*');

// Publish a message to the exchange with a routing key
$ex->publish('message', 'routing.1');
$ex->publish('message2', 'routing.2');
$ex->publish('message3', 'routing.3');

// Read from the queue
$msgs = $q->consume(1, 3, AMQP_AUTOACK);

foreach ($msgs as $msg) {
    echo $msg["message_body"] . "\n";
}

$ex->delete();
$q->delete();
?>
--EXPECT--
message
message2
message3

