--TEST--
Segfault when publishing to non existent exchange
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$c = new AMQPConnection();
$c->connect();
$ex = new AMQPExchange($c, "foo");
try {
    $ex->publish("data", "bar");
    echo "Success?\n";
} catch (Exception $e) {
    var_dump($e->getMessage());
}
?>
--EXPECT--
Could not publish to exchange. Exchange does not exist.
