--TEST--
AMQPQueue::get() doesn't return the message
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$cnn = new AMQPConnection();
$cnn->connect();
$ex = new AMQPExchange($cnn);
$ex->declare('exchange1', AMQP_EX_TYPE_FANOUT);
$q = new AMQPQueue($cnn, 'queue1');
$q->declare();
$ex->bind('queue1', 'routing.key');
$ex->publish('message', 'routing.key');
echo "Number of messages before get: " .  $q->declare() . "\n";
$msg = $q->get();
echo "Number of messages after get: " . $q->declare() . "\n";
echo "message received from get: " . print_r($msg, true) . "\n";
?>
--EXPECTF--
Number of messages before get: %s
Number of messages after get: %s
message received from get: Array
(
    [routing_key] => routing.key
    [exchange] => exchange1
    [delivery_tag] => 1
    [Content-type] => text/plain
    [count] => 0
    [msg] => message
)
