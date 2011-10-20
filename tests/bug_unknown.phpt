--TEST--
Connection Exception
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$conn = new AMQPConnection();
if (!$conn->connect()) {
    echo "Cannot connect to the broker \n";
}

$ch = new AMQPChannel($conn);

$publisher = new AMQPExchange($ch);
$publisher->setName("exchange-" . time());
$publisher->setType(AMQP_EX_TYPE_FANOUT);
$publisher->setFlags(AMQP_DURABLE);
$publisher->declare();

$message = md5("Time: " . rand(0,time()));
$key = "routing.key";
$params = 0;
$attributes = array('delivery_mode' => AMQP_DURABLE);

$consumer = new AMQPQueue($ch);
$consumer->setName("queue-" . time());
$consumer->setFlags(AMQP_DURABLE);
$consumer->declare();

$consumer->bind($publisher->getName(), $key);

for ($i = 0; $i < 10; $i++ ) {
    $published = $publisher->publish($message, $key, $params, $attributes);
    if (!$published) {
        echo "message publishing failed\n";
    }
}

// $data = $consumer->consume(1, 5, true);
echo "Success\n";

$consumer->delete();
$publisher->delete();
?>
--EXPECT--
Success
