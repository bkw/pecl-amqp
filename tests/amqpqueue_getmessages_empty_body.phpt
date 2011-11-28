--TEST--
AMQPQueue::getMessages empty body
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$cnn = new AMQPConnection();
$cnn->connect();

$ch = new AMQPChannel($cnn);

// Declare a new exchange
$ex = new AMQPExchange($ch);
$ex->setName('exchange' . time());
$ex->setType(AMQP_EX_TYPE_FANOUT);
$ex->declare();

// Create a new queue
$q = new AMQPQueue($ch);
$q->setName('queue1' . time());
$q->declare();

// Bind it on the exchange to routing.key
$q->bind($ex->getName(), 'routing.*');

// Publish a message to the exchange with a routing key
$ex->publish('', 'routing.1');

// Read from the queue
$msgs = $q->getMessages(0, 2, AMQP_AUTOACK);

foreach ($msgs as $msg) {
    echo "'" . $msg["message_body"] . "'\n";
}

$ex->delete();
$q->delete();
?>
--EXPECT--
''
