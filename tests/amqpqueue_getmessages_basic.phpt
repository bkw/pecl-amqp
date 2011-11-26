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

echo __FILE__ . ':' . __LINE__ . "\n";
// Bind it on the exchange to routing.key
$q->bind($ex->getName(), 'routing.*');
echo __FILE__ . ':' . __LINE__ . "\n";
// Publish a message to the exchange with a routing key
$ex->publish('message', 'routing.1');
echo __FILE__ . ':' . __LINE__ . "\n";
$ex->publish('message2', 'routing.2');
echo __FILE__ . ':' . __LINE__ . "\n";
$ex->publish('message3', 'routing.3');
echo __FILE__ . ':' . __LINE__ . "\n";

// Read from the queue
echo __FILE__ . ':' . __LINE__ . "\n";
$msgs = $q->getMessages(1, 3, AMQP_AUTOACK);
echo __FILE__ . ':' . __LINE__ . "\n";

foreach ($msgs as $msg) {
    echo $msg["message_body"] . "\n";
}

$ex->delete();
?>
--EXPECT--
message
message2
message3

