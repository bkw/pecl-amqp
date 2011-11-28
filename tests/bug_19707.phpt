--TEST--
AMQPQueue::get() doesn't return the message
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$cnn = new AMQPConnection();
$cnn->connect();

$ch = new AMQPChannel($cnn);

$ex = new AMQPExchange($ch);
$ex->setName("exchange11");
$ex->setType(AMQP_EX_TYPE_FANOUT);
$ex->declare();

$q = new AMQPQueue($ch);
$q->setName('queue5');
$q->setFlags(AMQP_DURABLE);
$q->declare();

$q->bind($ex->getName(), 'routing.key');

$ex->publish('message', 'routing.key');

$msg = $q->getMessages(0, 1);

echo "message received from get: " . print_r($msg, true) . "\n";

$q->delete();
$ex->delete();
?>
--EXPECTF--
message received from get: Array
(
    [routing_key] => routing.key
    [exchange] => exchange11
    [delivery_tag] => 1
    [Content-type] => text/plain
    [count] => 0
    [msg] => message
)
