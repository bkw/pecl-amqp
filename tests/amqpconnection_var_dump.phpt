--TEST--
AMQPConnection var_dump
--SKIPIF--
<?php if (!extension_loaded("amqp")) print "skip"; ?>
--FILE--
<?php
$cnn = new AMQPConnection();
var_dump($cnn);
?>
--EXPECT--
object(AMQPConnection)#1 (5) {
  ["login"]=>
  string(4) "revv"
  ["password"]=>
  string(4) "revv"
  ["host"]=>
  string(9) "localhost"
  ["vhost"]=>
  string(1) "/"
  ["port"]=>
  int(5672)
}

